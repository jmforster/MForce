#include "mforce/core/denormals.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imnodes.h"

#include <GLFW/glfw3.h>

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <shobjidl.h>   // IFileOpenDialog (FOS_PICKFOLDERS) — real folder picker
#pragma comment(lib, "ole32.lib")
#include <DbgHelp.h>
#pragma comment(lib, "DbgHelp.lib")
#include <typeinfo>
#include <nlohmann/json.hpp>
#include <complex>
#include <mutex>
#include <atomic>
#include <climits>
#include <string_view>
#include "RtAudio.h"
#include "RtMidi.h"
#include <cstring>
#include "mforce/render/patch_loader.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/core/source_registry.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/envelope.h"      // needed for Envelope::make_adsr
#include "mforce/core/envelope_json.h" // shared preset-form dispatch (both loaders)
#include "mforce/core/envelope_presets.h" // ADSREnvelope for NT_ENVELOPE nodes
#include "mforce/core/var_source.h"     // needed for VarSource constructor
#include "mforce/core/range_source.h"   // needed for RangeSource constructor
#include "mforce/core/curve_node.h"     // CurveNode knots/interp are modeled (P2b)
#include "mforce/source/shaper_source.h" // shape editor Shaper client probe
#include "mforce/core/smoothness_interpolator.h"  // SegmentSource shape preview
#include "mforce/source/additive/formant.h" // needed for FormantSpectrum inline table
#include "mforce/source/additive/partials.h" // for Partials live array access in strip draw
#include "mforce/render/instrument.h"
#include "mforce/render/mixer.h"
#include "mforce/render/limiter.h"  // soft_clip for the live polyphonic mix
#include "mforce/render/wav_reader.h"  // audition: load WAVs from --explore sweep folders
#include "mforce/util/fft.h"        // shared FFT (also used by mforce_cli --explore)
#include "mforce/render/wav_writer.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/structure.h"
#include "mforce/music/conductor.h"

using namespace mforce;

#include <tuple>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include "build_stamp.h"

// ===========================================================================
// ID generation
// ===========================================================================
static int s_nextId = 1;
static int next_id() { return s_nextId++; }

// ===========================================================================
// Graph mode
// ===========================================================================
enum class GraphMode { NodeGraph, PatchGraph };
static GraphMode s_graphMode = GraphMode::PatchGraph;

// ===========================================================================
// Pin / Node / Link
// ===========================================================================

enum class PinKind { Input, Output };

struct Pin {
    int id;
    std::string name;
    PinKind kind;
    float defaultValue{0.0f};
    bool inputOnly{false};  // true for input_descriptors pins (no editable value)
    // True when the loaded JSON supplied a scalar CONSTANT in this slot
    // (e.g. CombinedSource "source2": 0.3836). inputOnly pins have no
    // editable value in the UI, so without this flag save paths dropped
    // the constant and every gain stage became a pass-through — the
    // 2026-08-13 edit-then-play volume-drop bug.
    bool hasConstant{false};
    bool multi{false};      // true = accepts multiple connections
    // Tap output pin (feedback_loop_design.md §4.1): wires from it read the
    // node's PREVIOUS sample (guarded RefSource) instead of pulling — the
    // only legal way to close a feedback cycle.
    bool isTap{false};
    std::string hint;       // optional advisory tag (e.g. "hz", "0-1"); empty = none
    std::shared_ptr<ConstantSource> constantSrc;  // holds editable value for unconnected pins

    Pin(const std::string& n, PinKind k, float def = 0.0f, bool inOnly = false, bool isMulti = false,
        const char* hintStr = nullptr)
        : id(next_id()), name(n), kind(k), defaultValue(def), inputOnly(inOnly), multi(isMulti)
        , hint(hintStr ? hintStr : "")
        , constantSrc(std::make_shared<ConstantSource>(def)) {}
};

// Special UI-only type name constants (not in the registry)
static constexpr const char* NT_SOUND_CHANNEL = "SoundChannel";
static constexpr const char* NT_STEREO_MIXER  = "StereoMixer";
static constexpr const char* NT_PATCH_OUTPUT  = "PatchOutput";
static constexpr const char* NT_PARAMETER     = "Parameter";
static constexpr const char* NT_ENVELOPE      = "Envelope";
// PerformNode reports one field of the note being played. It has no engine
// object in the editor: the real PerformOut adapter belongs to a VOICE, and
// the editor is not a voice. create_dsp backs it with a ConstantSource holding
// a plausible preview value so waveform previews and Listen taps show
// something rather than silence — that value is a fiction of the canvas, never
// of a render. Patch mode only; the engine throws "no voice context" otherwise.
static constexpr const char* NT_PERFORM       = "PerformNode";

// PerformNode fields (frequency/velocity/wheel/pressure) are carried by the
// Note face's OUTPUT PIN NAMES — see build_pins — and one file node per
// field at save. No index vocabulary needed since 2026-08-20.

static bool is_special_ui_type(const std::string& typeName) {
    return typeName == NT_SOUND_CHANNEL || typeName == NT_STEREO_MIXER
        || typeName == NT_PATCH_OUTPUT  || typeName == NT_PARAMETER;
}

static std::string node_display_name(const std::string& typeName) {
    if (typeName == NT_SOUND_CHANNEL) return "Channel";
    if (typeName == NT_STEREO_MIXER)  return "Mixer";
    if (typeName == NT_PATCH_OUTPUT)  return "Output";
    if (typeName == NT_PARAMETER)     return "Parameter";
    if (typeName == NT_PERFORM)       return "Note";
    if (typeName == "CurveNode")      return "Curve";
    // Strip "Source" suffix for cleaner display
    std::string name = typeName;
    if (name.size() > 6 && name.substr(name.size() - 6) == "Source")
        name = name.substr(0, name.size() - 6);
    return name;
}

// Check if a type name is a noise source
static bool is_noise_type(const std::string& t) {
    return t.find("Noise") != std::string::npos
        || t == "SegmentSource";
}

// Check if a type name is a wavetable/evolution source
static bool is_wavetable_type(const std::string& t) {
    return t == "WavetableSource"
        || t.find("Evolution") != std::string::npos;
}

// Gold = "set once at note-on, frozen for the note" — the performance
// cadence. Shared by the Note node's title, the Settings-pane promotion UI
// and the node-face dynamic pins so the meaning reads as one color.
static constexpr ImU32 kDynPinGold = IM_COL32(205, 170, 60, 255);

static ImU32 node_title_color(const std::string& typeName) {
    if (typeName == NT_SOUND_CHANNEL || typeName == NT_STEREO_MIXER)
        return IM_COL32(120, 130, 145, 255);    // Blue grey — Output
    if (typeName == NT_PATCH_OUTPUT)
        return IM_COL32(120, 130, 145, 255);    // Blue grey — Output
    if (typeName == NT_PARAMETER)
        return IM_COL32(190, 140, 170, 255);    // Pink — Parameter
    // The performance family, deliberately its own colour: these are the only
    // nodes whose value comes from the NOTE rather than from the graph.
    if (typeName == NT_PERFORM)
        return IM_COL32(205, 170, 60, 255);     // Gold — Note (performance)
    if (typeName == "CurveNode")
        return IM_COL32(170, 145, 75, 255);     // Dark gold — Curve (transfer)

    auto& reg = SourceRegistry::instance();
    if (!reg.has(typeName)) return IM_COL32(128, 128, 128, 255);

    // Noise and Wavetable override category-based coloring
    if (is_noise_type(typeName))
        return IM_COL32(185, 95, 85, 255);      // Reddish orange — Noise
    if (is_wavetable_type(typeName))
        return IM_COL32(155, 150, 110, 255);    // Olive-y tan — Wavetable

    switch (reg.get_category(typeName)) {
        case SourceCategory::Additive:  return IM_COL32(195, 145, 90, 255);  // Orange — Additive
        case SourceCategory::Envelope:  return IM_COL32(200, 185, 110, 255); // Pale gold — Envelope
        case SourceCategory::Oscillator:return IM_COL32(95, 165, 150, 255);  // Greenish teal — Generator
        case SourceCategory::Generator: return IM_COL32(95, 165, 150, 255);  // Greenish teal — Generator
        case SourceCategory::Modulator: return IM_COL32(95, 165, 150, 255);  // Greenish teal — Generator
        case SourceCategory::Filter:    return IM_COL32(145, 130, 175, 255); // Dark lavender — Filter
        case SourceCategory::Combiner:  return IM_COL32(95, 165, 150, 255);  // Greenish teal — Generator
        case SourceCategory::Utility:   return IM_COL32(128, 128, 128, 255);
    }
    return IM_COL32(128, 128, 128, 255);
}

// Create a pale/desaturated version of a color for node backgrounds
static ImU32 node_bg_color(ImU32 titleColor) {
    int r = (titleColor >>  0) & 0xFF;
    int g = (titleColor >>  8) & 0xFF;
    int b = (titleColor >> 16) & 0xFF;
    // Blend 35% of title color with dark gray base (45,45,48)
    r = 45 + (r - 45) * 35 / 100;
    g = 45 + (g - 45) * 35 / 100;
    b = 48 + (b - 48) * 35 / 100;
    return IM_COL32(r, g, b, 255);
}

// Lighten toward white by pct (0-100). Selected vs hovered node cues
// (Matt, 2026-08-15): hover = subtle background lift; selected = strong
// lift + brightened title, so a ctrl-click deselect is visible while the
// pointer is still on the node.
static ImU32 lighten(ImU32 col, int pct) {
    int r = (col >>  0) & 0xFF;
    int g = (col >>  8) & 0xFF;
    int b = (col >> 16) & 0xFF;
    r = r + (255 - r) * pct / 100;
    g = g + (255 - g) * pct / 100;
    b = b + (255 - b) * pct / 100;
    return IM_COL32(r, g, b, 255);
}

static constexpr int DSP_SAMPLE_RATE = 48000;

// Row data for inline formant table (FormantSpectrum node)
struct FormantRow {
    float frequency{1000.0f}, gain{1.0f}, width{500.0f}, power{2.0f};
    // Original JSON id of the consumed Formant child ("f1"). Save re-emits
    // the child under this id — paramMap curves target formants by id
    // (vowel patches: "f1.frequency"), so synthesizing "<spec>__fN" names
    // orphaned those mappings and made the saved patch unrenderable
    // (backlog 3n, node:f1 species). Empty = UI-created row, gets __fN.
    std::string srcId;
};

static std::string unique_node_label(const std::string& typeName);

struct GraphNode {
    int id;
    std::string typeName;
    std::string label;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    // Live DSP object
    std::shared_ptr<ValueSource> dspSource;

    // Config values (non-connectable params like holdCycles, absolute, etc.)
    std::vector<std::pair<SettingDescriptor, float>> settingValues;

    // Array values (user-editable vectors — gains, partial multipliers, etc.)
    // Preserves descriptor order; inspector groups consecutive entries with
    // the same groupName into a parallel-columns table.
    std::vector<std::pair<ArrayDescriptor, std::vector<float>>> arrayValues;

    // Inline table data (FormantSpectrum)
    std::vector<FormantRow> formantRows;

    // PatchOutput-specific
    int polyphony{4};

    // Verbatim "seed" param from the loaded JSON. The UI does not model seeds
    // as pins/configs, but the engine loader uses them for reproducible
    // randomness ("seeds stored in JSON" rule) — carry the value through
    // save_patch_graph / save_node_graph so Save and the playback temp JSON
    // don't silently drop it (dropping it changes the loader's RNG stream).
    // -1 = no seed present in the source JSON.
    long long jsonSeed{-1};

    // Loaded params the UI does not model (ExplicitPartials expandRule,
    // loader-lambda keys like WavetableSource "evolution"/"gap", …),
    // carried verbatim so save cannot silently drop them (backlog 3n).
    // On save these OVERWRITE the modeled emission for the same key; a UI
    // edit of a pin/config with the same name erases its extras entry so
    // the edit wins instead.
    nlohmann::json jsonExtras = nlohmann::json::object();

    // Offline-rendered waveform samples for display
    std::vector<float> waveformData;

    // Canvas position, app-tracked: imnodes DESTROYS the pool entry of any
    // node not submitted in a frame, so a node hidden by group drill-in
    // loses its position inside imnodes. We keep it here (updated every
    // visible frame) and re-apply on the hidden->visible transition; save
    // also reads it so saving while drilled-in keeps hidden nodes' spots.
    ImVec2 gridPos{0.0f, 0.0f};
    bool gridPosKnown{false};
    bool wasVisible{false};

    // Parameter-specific
    std::string paramName;
    char paramNameBuf[32]{};

    // True when this node's id came from the file already carrying the "__"
    // synthesized-node prefix. Set at load; keeps sanitize_unique_id from
    // stripping it back out on save. A human still cannot type "__" —
    // rename_node refuses it.
    bool synthesizedId{false};

    // PerformNode: which field of the note this reports. 0 = frequency,
    // 1 = velocity. Matches PerformOut::Field and the JSON "field" string.
    // (Legacy — the editor face now carries one output pin per field; kept
    // because create-menu construction predates pins being built.)
    int performField{0};

    // Note FACE only: field -> serialized per-field node id. The editor
    // shows one Note node with an output pin per field; the FILE keeps one
    // PerformNode per field (a ref names a node, and a node is one value
    // stream). Load consumes the file nodes into this map; save re-emits
    // them under the SAME ids so round-trips stay id-stable. A wired field
    // with no entry gets a fresh __perf_* id at save.
    std::map<std::string, std::string> perfFieldIds;

    // CurveNode: knots and interp, edited in the node's Properties pane.
    // interp: 0 = linear, 1 = logx, 2 = loglog — matches CurveNode::CurveInterp.
    std::vector<std::pair<float, float>> curveKnots;
    int curveInterp{0};
    // Expressions mode (curve_node.h header comment): knot values are
    // Linear a*x+b / Power a*x^p rows instead of literals.
    bool curveExprMode{false};
    std::vector<CurveNode::ExprKnot> curveExprKnots;

    // Dynamic pins: the SETTINGS this node instance drives once per note
    // (pin_model_design.md §3). {"sustainLevel": {"ref": "__curve_sus"}}.
    // A sibling of "params", not a member of it, because node construction
    // reads params before any descriptor is known. Carried through save with
    // its refs remapped; Task 4 makes it editable via promotion.
    nlohmann::json dynamicPins = nlohmann::json::object();

    // imnodes ids for the gold pins/wires that render dynamicPins on the
    // node face. Session-only (never serialized — dynamicPins is the truth);
    // stable across frames because imnodes tracks pin positions by id.
    // Entries for demoted pins linger harmlessly.
    std::unordered_map<std::string, int> dynPinAttrIds;
    std::unordered_map<std::string, int> dynLinkIds;
    int dyn_attr_id(const std::string& setting) {
        auto it = dynPinAttrIds.find(setting);
        if (it == dynPinAttrIds.end())
            it = dynPinAttrIds.emplace(setting, next_id()).first;
        return it->second;
    }
    int dyn_link_id(const std::string& setting) {
        auto it = dynLinkIds.find(setting);
        if (it == dynLinkIds.end())
            it = dynLinkIds.emplace(setting, next_id()).first;
        return it->second;
    }

    GraphNode(const std::string& type) : id(next_id()), typeName(type), label(unique_node_label(type)) {
        build_pins();
        create_dsp();
        init_config();
        init_arrays();
    }

    GraphNode(const std::string& type, const std::string& name) : GraphNode(type) {
        if (typeName == NT_PARAMETER) {
            paramName = name;
            // label stays "Parameter" — paramName displays inside node body
            snprintf(paramNameBuf, sizeof(paramNameBuf), "%s", name.c_str());
        }
    }

    // Create the DSP object and wire pins' ConstantSources as defaults
    void create_dsp() {
        if (typeName == NT_PATCH_OUTPUT || typeName == NT_SOUND_CHANNEL || typeName == NT_STEREO_MIXER)
            return;

        if (typeName == NT_PARAMETER) {
            // Parameter node's DSP is just its constantSrc from the "default" pin
            if (auto* p = find_input("default"))
                dspSource = p->constantSrc;
            return;
        }

        if (typeName == NT_PERFORM) {
            // STAND-INS, not the real thing. The engine resolves a PerformNode
            // to the voice's shared PerformOut adapters at load; the editor
            // has no voice, so each OUTPUT PIN's constantSrc is that field's
            // preview value (frequency 440, velocity 0.8, wheel/pressure 0 —
            // an untouched controller). update_node_dsp wires consumers to
            // the PIN's constant, not to dspSource; dspSource is kept as the
            // frequency stand-in for node-level uses (waveform preview).
            dspSource = find_output("frequency")
                            ? find_output("frequency")->constantSrc
                            : std::make_shared<ConstantSource>(440.0f);
            return;
        }

        if (typeName == NT_ENVELOPE) {
            dspSource = std::make_shared<Envelope>(Envelope::make_adsr(DSP_SAMPLE_RATE,
                0.05f, 0.1f, 0.7f, 0.2f));
            for (auto& pin : inputs)
                if (pin.constantSrc) dspSource->set_param(pin.name, pin.constantSrc);
            return;
        }

        if (typeName == "VarSource") {
            auto* val = find_input("val");
            auto* var = find_input("var");
            auto* pct = find_input("varPct");
            auto s = std::make_shared<VarSource>(
                val ? val->constantSrc : std::make_shared<ConstantSource>(1.0f),
                var ? var->constantSrc : std::make_shared<ConstantSource>(0.0f),
                pct ? pct->constantSrc : std::make_shared<ConstantSource>(0.0f));
            dspSource = s;
            return;
        }

        if (typeName == "RangeSource") {
            auto* mn = find_input("min");
            auto* mx = find_input("max");
            auto* vr = find_input("var");
            auto s = std::make_shared<RangeSource>(
                mn ? mn->constantSrc : std::make_shared<ConstantSource>(0.0f),
                mx ? mx->constantSrc : std::make_shared<ConstantSource>(1.0f),
                vr ? vr->constantSrc : std::make_shared<ConstantSource>(0.5f));
            dspSource = s;
            return;
        }

        // FormantSpectrum: inline table of formant rows
        if (typeName == "FormantSpectrum") {
            dspSource = std::make_shared<FormantSpectrum>();
            if (formantRows.empty()) {
                // Default: classic vowel-like formant set
                formantRows = {
                    {220.0f, 0.7f, 400.0f, 1.0f},
                    {850.0f, 1.0f, 900.0f, 2.0f},
                    {1850.0f, 0.6f, 1200.0f, 1.0f},
                    {3100.0f, 0.65f, 1000.0f, 1.5f},
                };
            }
            rebuild_formant_spectrum();
            return;
        }

        // Generic: create via registry, wire all pin defaults
        auto& reg = SourceRegistry::instance();
        if (!reg.has(typeName)) return;

        dspSource = reg.create(typeName, DSP_SAMPLE_RATE);
        for (auto& pin : inputs) {
            if (pin.constantSrc)
                dspSource->set_param(pin.name, pin.constantSrc);
        }
    }

    // Rewire a specific input pin's DSP connection
    void wire_pin(const std::string& pinName, std::shared_ptr<ValueSource> src) {
        if (!src || !dspSource) return;
        dspSource->set_param(pinName, src);
    }

    Pin* find_input(const std::string& name) {
        for (auto& p : inputs) if (p.name == name) return &p;
        return nullptr;
    }

    Pin* find_output(const std::string& name) {
        for (auto& p : outputs) if (p.name == name) return &p;
        return nullptr;
    }

    void init_config() {
        if (!dspSource) return;
        auto descs = dspSource->setting_descriptors();
        settingValues.clear();
        for (const auto& desc : descs)
            settingValues.push_back({desc, desc.default_value});
    }

    // Populate editable array cache from the live DSP object. Called once on
    // construction; UI edits push back via dspSource->set_array(...).
    void init_arrays() {
        if (!dspSource) return;
        auto descs = dspSource->array_descriptors();
        arrayValues.clear();
        for (const auto& desc : descs)
            arrayValues.push_back({desc, dspSource->get_array(desc.name)});
    }

    // Push a single array's UI state back to the DSP object.
    void push_array(const char* name) {
        if (!dspSource) return;
        for (auto& [d, v] : arrayValues) {
            if (std::string_view(d.name) == name) {
                dspSource->set_array(name, v);
                return;
            }
        }
    }

    // Rebuild FormantSpectrum DSP from inline table rows
    void rebuild_formant_spectrum() {
        auto spec = std::dynamic_pointer_cast<FormantSpectrum>(dspSource);
        if (!spec) return;
        spec->formants.clear();
        for (auto& row : formantRows) {
            auto f = std::make_shared<Formant>();
            f->set_frequency(std::make_shared<ConstantSource>(row.frequency));
            f->set_gain(std::make_shared<ConstantSource>(row.gain));
            f->set_width(std::make_shared<ConstantSource>(row.width));
            f->set_power(std::make_shared<ConstantSource>(row.power));
            spec->formants.push_back(f);
        }
    }

    void apply_config() {
        if (!dspSource) return;
        for (auto& [desc, val] : settingValues)
            dspSource->set_setting(desc.name, val);
    }

    void build_pins() {
        if (is_special_ui_type(typeName)) {
            // Special UI-only nodes with hand-crafted pins
            if (typeName == NT_SOUND_CHANNEL) {
                inputs.emplace_back("source", PinKind::Input, 0.0f);
                inputs.emplace_back("volume", PinKind::Input, 1.0f);
                inputs.emplace_back("pan",    PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
            } else if (typeName == NT_STEREO_MIXER) {
                inputs.emplace_back("ch 1", PinKind::Input, 0.0f);
                inputs.emplace_back("gainL", PinKind::Input, 1.0f);
                inputs.emplace_back("gainR", PinKind::Input, 1.0f);
                outputs.emplace_back("outL", PinKind::Output);
                outputs.emplace_back("outR", PinKind::Output);
            } else if (typeName == NT_PATCH_OUTPUT) {
                inputs.emplace_back("source", PinKind::Input, 0.0f);
            } else if (typeName == NT_PARAMETER) {
                inputs.emplace_back("default", PinKind::Input, 440.0f);
                outputs.emplace_back("out", PinKind::Output);
            }
            return;
        }

        // Envelope: stage shape lives in settings/stages (Properties), but the
        // PULLABLE range params (minValue/maxValue, PerformSource P1) are real
        // pins — read off the engine's own descriptors so the list cannot
        // drift. Before 2026-08-22 this branch emitted only "out", so the
        // P1 params were unreachable from the editor on the stage-form type
        // (Matt: "I don't see maxValue anywhere in the properties").
        if (typeName == NT_ENVELOPE) {
            Envelope tmp(DSP_SAMPLE_RATE);
            for (const auto& desc : tmp.param_descriptors())
                inputs.emplace_back(desc.name, PinKind::Input, desc.default_value,
                                    false, false, desc.hint);
            outputs.emplace_back("out", PinKind::Output);
            outputs.emplace_back("tap", PinKind::Output);
            outputs.back().isTap = true;
            return;
        }

        // PerformNode: a leaf with one output PER FIELD — the node Matt's
        // design discussions always described (perform_source_design.md
        // §2.1/§2.3 "all instances render the same outputS"); the P2a/P2b
        // one-field-per-node face was an editor-side conflation of the FILE
        // shape (which stays one node per field, invisible here). Each
        // output pin's constantSrc is that field's preview stand-in.
        if (typeName == NT_PERFORM) {
            outputs.emplace_back("frequency", PinKind::Output, 440.0f);
            outputs.emplace_back("velocity",  PinKind::Output, 0.8f);
            outputs.emplace_back("wheel",     PinKind::Output, 0.0f);
            outputs.emplace_back("pressure",  PinKind::Output, 0.0f);
            return;
        }

        // Generic: create a temporary instance to read descriptors
        auto& reg = SourceRegistry::instance();
        if (!reg.has(typeName)) {
            // Unknown type (e.g. a patch authored against an engine branch
            // this build doesn't have — gs_chaotic.json's GrayScottSource).
            // Still emit one output attribute: an imnodes node with ZERO
            // attributes makes ImGui 1.92's error-recovery fire every frame
            // ("SetCursorPos() to extend parent boundaries" tooltip in the
            // node-editor scrolling region). The node stays inert (no
            // dspSource), but draws cleanly and keeps its place in the graph.
            outputs.emplace_back("out", PinKind::Output);
            return;
        }

        auto tmp = reg.create(typeName, DSP_SAMPLE_RATE);
        for (const auto& desc : tmp->input_descriptors())
            inputs.emplace_back(desc.name, PinKind::Input, 0.0f, true, desc.multi, desc.hint);
        for (const auto& desc : tmp->param_descriptors())
            inputs.emplace_back(desc.name, PinKind::Input, desc.default_value, false, false, desc.hint);
        outputs.emplace_back("out", PinKind::Output);
        outputs.emplace_back("tap", PinKind::Output);
        outputs.back().isTap = true;
    }

    void add_channel_input() {
        if (typeName != NT_STEREO_MIXER) return;
        int chNum = 0;
        for (auto& p : inputs)
            if (p.name.substr(0, 2) == "ch") chNum++;
        char name[16];
        snprintf(name, sizeof(name), "ch %d", chNum + 1);
        auto pos = inputs.end() - 2;
        inputs.insert(pos, Pin(name, PinKind::Input, 0.0f));
    }
};

struct Link {
    int id;
    int startPinId;
    int endPinId;
    Link(int start, int end) : id(next_id()), startPinId(start), endPinId(end) {}
};

// ===========================================================================
// Graph state
// ===========================================================================
static std::vector<GraphNode> s_nodes;
static std::vector<Link> s_links;

// ===========================================================================
// Groups (spec §3): UI-level structure over the FLAT graph. The engine never
// sees them — the patch JSON keeps every node top-level and a root "groups"
// section records membership. Members are node labels or other group names
// (nesting). editorId/pinIds are synthetic imnodes ids for the collapsed
// node; pinIds[0..n-1] = input pins (one per boundary target), last = output.
// ===========================================================================
struct NodeGroup {
    std::string name;
    std::vector<std::string> members;
    ImVec2 pos{0.0f, 0.0f};
    bool posApplied{false};       // SetNodeGridSpacePos once after load
    int editorId{-1};
    std::vector<int> inPinIds;    // grown on demand per boundary input
    int outPinId{-1};
};
static std::vector<NodeGroup> s_groups;
// Drill-in path: empty = top level; back() = the group whose interior the
// Node Editor currently shows.
static std::vector<std::string> s_groupPath;
// Listen tap (spec §3): the node whose output is routed to the ears.
// -1 = the patch output. In-context semantics — the whole patch keeps
// running; only the monitored signal moves. Never persisted by a save.
static int s_listenTapNode = -1;
// Per-group session memory for the breadcrumb Patch|Group toggle
// (default true: drill-in listens to the group).
static std::unordered_map<std::string, bool> s_groupListen;

static NodeGroup* group_by_name(const std::string& name) {
    for (auto& g : s_groups) if (g.name == name) return &g;
    return nullptr;
}

// Innermost group that lists `label` (node label or group name) as a member.
static NodeGroup* group_of(const std::string& label) {
    for (auto& g : s_groups)
        for (auto& m : g.members)
            if (m == label) return &g;
    return nullptr;
}

// A label is visible when its containing group IS the current path target
// (nullptr target = top level). Collapsed child groups are drawn separately.
static bool visible_at_path(const std::string& label) {
    NodeGroup* container = group_of(label);
    if (s_groupPath.empty()) return container == nullptr;
    return container && container->name == s_groupPath.back();
}

// Node labels ARE the serialized ids (2026-08-14 spec §1: stable
// identity), so every node needs a unique one from birth. Load overwrites
// with the JSON id afterward; this covers UI-created nodes.
static std::string unique_node_label(const std::string& typeName) {
    std::string base = node_display_name(typeName);
    auto taken = [&](const std::string& s) {
        for (auto& n : s_nodes) if (n.label == s) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::string cand = base + std::to_string(i);
        if (!taken(cand)) return cand;
    }
}
// Set by the --roundtrip headless path so load/save skip ImNodes node-position
// calls (those need a live editor/frame the headless path doesn't set up).
static bool s_headless = false;
// paramMap RESIDUE — the entries convert_parammap_to_wiring could NOT turn
// into graph nodes. No longer the model (P2b, 2026-08-19): load converts every
// convertible entry into real PerformNode/CurveNode/CombinedSource nodes, and
// the curve editors and Mappings dialog derive from the graph, not from here.
//
// What is left, and why: a paramMap target pointing INTO a Formant child owned
// by a FormantSpectrum. The UI consumes those children into the spectrum's row
// table, and a row holds literal floats — it cannot represent a driven param.
// Writing a ref into one makes the pre-scan stop consuming it, so the spectrum
// re-emits synthesized rows and orphans the originals. Those entries stay here
// and keep working exactly as they always have.
//
// So this is expected to be EMPTY for most patches and small for the rest.
// If it is ever empty for all of them, it can go — that needs the formant row
// model to carry driven params. Reset on new/clear/load.
static nlohmann::json s_loadedParamMap = nlohmann::json::object();

// Verbatim "score" array and "seconds" value from the loaded patch JSON.
// save_patch_graph used to overwrite these with a hardcoded default note
// (60 / 0.8 / 2.0s), so Save (and the playback temp JSON) silently destroyed
// the patch's score — and a CLI render of the saved file played a different
// note than the original. Carried through verbatim instead; the default is
// only emitted when the loaded patch had no score at all.
static nlohmann::json s_loadedScore   = nlohmann::json();
// True when the loaded file had NO score key at all. Save must preserve the
// absence — injecting the default note changes what mforce_cli renders
// (backlog 3n root:score class, 7 library patches). Fresh graphs (New) keep
// the default injection so a new patch renders a note out of the box.
static bool s_loadedScoreAbsent = false;
// Instrument-block fields the UI has no widgets for (release, volume, future
// keys). Preserved verbatim from load to save so UI round-trips don't strip
// them (the 2026-08-10 live-audition damper/gain loss).
static nlohmann::json s_loadedInstrumentExtras = nlohmann::json::object();
static nlohmann::json s_loadedSeconds = nlohmann::json();

// Convert-graph stash (Edit > Convert to <type> graph). Converting a Patch
// graph to a Node graph strips the instrument-level data the node graph
// cannot model (paramMap incl. curve entries, score, seconds, polyphony);
// it is parked here so converting back within the session restores the
// originals verbatim. Cleared on New and on any file load — the stash
// belongs to the graph it was stripped from, not to the session at large.
static nlohmann::json s_convParamMap  = nlohmann::json::object();
static nlohmann::json s_convScore     = nlohmann::json();
static nlohmann::json s_convSeconds   = nlohmann::json();
static int  s_convPolyphony  = 1;
static bool s_convStashValid = false;

static void conv_stash_clear() {
    s_convParamMap  = nlohmann::json::object();
    s_convScore     = nlohmann::json();
    s_convSeconds   = nlohmann::json();
    s_convPolyphony = 1;
    s_convStashValid = false;
}

// Defined after g_transport/g_keyboard (declaration-order constraint) —
// seeds the transport and keyboard defaults from a loaded patch's score.
static void apply_score_defaults(const nlohmann::json& score);
static std::string s_currentFilePath;
// True when the UI graph has been edited since last save/load. Playback paths
// sync to a temp file before loading the instrument so MultiplexSource (and
// anything else that bakes state at patch-load time) picks up current edits.
static bool s_graphDirty = false;
// Monotonic count of graph-content changes (edits, loads, new graphs, listen
// taps). The live-playback instrument cache keys on it: notes replay the
// cached instrument until this moves, then one rebuild picks up the change
// (BACKLOG dsp 17, 2026-08-18). Unlike s_graphDirty it never resets — it
// only answers "did anything change since the cache was built?".
static uint64_t g_graphEditCounter = 0;
static void mark_graph_dirty() {
    s_graphDirty = true;
    ++g_graphEditCounter;
}
// For graph-content changes that must invalidate the playback cache but do
// NOT count as unsaved edits (load, new graph, listen-tap toggles).
static void invalidate_instrument_cache() { ++g_graphEditCounter; }
// Save-prompt machinery: the close callback sets s_closeRequested; the main
// loop checks it each frame, runs the dirty-check, and either pops the modal
// (s_showCloseConfirm) or lets the close proceed. s_closeApproved short-
// circuits the dirty check after the user picks Save / Don't Save.
static bool s_closeRequested = false;
static bool s_showCloseConfirm = false;
static bool s_closeApproved = false;

static Pin* find_pin(int pinId) {
    for (auto& node : s_nodes) {
        for (auto& pin : node.inputs)  if (pin.id == pinId) return &pin;
        for (auto& pin : node.outputs) if (pin.id == pinId) return &pin;
    }
    return nullptr;
}

static GraphNode* find_node_for_pin(int pinId) {
    for (auto& node : s_nodes) {
        for (auto& pin : node.inputs)  if (pin.id == pinId) return &node;
        for (auto& pin : node.outputs) if (pin.id == pinId) return &node;
    }
    return nullptr;
}

// Resolve a gold dynamic-pin attribute (or its wire) back to the owning node
// and the setting it drives. Only ids for CURRENTLY promoted settings count —
// stale ids from demoted pins resolve to nothing.
static GraphNode* find_node_for_dyn_attr(int attrId, std::string* settingOut) {
    for (auto& node : s_nodes) {
        for (auto& [setting, id] : node.dynPinAttrIds) {
            if (id == attrId && node.dynamicPins.contains(setting)) {
                if (settingOut) *settingOut = setting;
                return &node;
            }
        }
    }
    return nullptr;
}
static GraphNode* find_node_for_dyn_link(int linkId, std::string* settingOut) {
    for (auto& node : s_nodes) {
        for (auto& [setting, id] : node.dynLinkIds) {
            if (id == linkId && node.dynamicPins.contains(setting)) {
                if (settingOut) *settingOut = setting;
                return &node;
            }
        }
    }
    return nullptr;
}

// Structural pin compatibility. Returns a user-readable error message if the
// source's type can't satisfy the target pin's interface requirement, or
// nullptr if the link is fine. Keeps "lying wires" out of the UI: before
// this check, the loader silently dropped mismatched refs (non-IFormant
// wired to FormantSpectrum.formants, non-IPartials wired to partials pins,
// etc.) and the UI showed the link anyway.
static const char* pin_type_compat_error(const std::string& targetType,
                                         const std::string& targetPin,
                                         const std::string& sourceType) {
    auto is_iformant = [](const std::string& t) {
        return t == "Formant" || t == "FormantSpectrum" ||
               t == "FixedSpectrum" || t == "BandSpectrum" ||
               t == "FormantSequence";
    };
    auto is_ipartials = [](const std::string& t) {
        return t == "FullPartials" || t == "SequencePartials" ||
               t == "ExplicitPartials" || t == "CompositePartials";
    };

    // IFormant-requiring pins
    bool needsFormant =
        (targetType == "FormantSpectrum" && targetPin == "formants") ||
        (targetType == "FormantSequence" && targetPin == "spectra") ||
        ((targetType == "AdditiveSource"   || targetType == "AdditiveSource2" ||
          targetType == "BasicAdditiveSource") && targetPin == "formant");
    if (needsFormant && !is_iformant(sourceType)) {
        return "Pin needs a Formant-type source (Formant, FormantSpectrum, FixedSpectrum, BandSpectrum, or FormantSequence)";
    }

    // IPartials-requiring pins
    bool needsPartials =
        (targetType == "CompositePartials" && targetPin == "partials") ||
        ((targetType == "AdditiveSource"   || targetType == "AdditiveSource2" ||
          targetType == "BasicAdditiveSource") && targetPin == "partials");
    if (needsPartials && !is_ipartials(sourceType)) {
        return "Pin needs a Partials-type source (FullPartials, SequencePartials, ExplicitPartials, or CompositePartials)";
    }

    return nullptr;
}

// Rewire DSP after a link is created or destroyed
static void dsp_rewire_link(int outputPinId, int inputPinId, bool connect) {
    GraphNode* srcNode = find_node_for_pin(outputPinId);
    GraphNode* dstNode = find_node_for_pin(inputPinId);
    Pin* dstPin = find_pin(inputPinId);

    if (!srcNode || !dstNode || !dstPin) return;
    if (dstPin->kind != PinKind::Input) return;

    if (connect && srcNode->dspSource) {
        dstNode->wire_pin(dstPin->name, srcNode->dspSource);
    } else {
        // Disconnect: wire back to the pin's ConstantSource
        dstNode->wire_pin(dstPin->name, dstPin->constantSrc);
    }
}

// UpdateNode: re-apply ALL sources to a node's DSP object.
// For each input pin: if connected, use the source node's dspSource; else use constantSrc.
// Serializes the audio callback's graph reads against UI-thread rewiring.
// Declared here (ahead of the RtAudio block that documents it) because
// update_node_dsp/update_all_dsp mutate live DSP shared_ptr edges that the
// stream taps dereference per sample — an unlocked rewire during streaming
// is a torn pointer read on the audio thread (backlog 3k).
static std::mutex g_audioMutex;

static void update_node_dsp_unlocked(GraphNode& node) {
    if (!node.dspSource) return;

    // First pass: wire all pins to their ConstantSource defaults, clear multi pins
    for (auto& pin : node.inputs) {
        if (pin.multi && node.dspSource) {
            node.dspSource->clear_param(pin.name);
        } else if (pin.kind == PinKind::Input && pin.constantSrc) {
            node.wire_pin(pin.name, pin.constantSrc);
        }
    }

    // Second pass: override with connected sources (multi pins use add_param)
    for (auto& pin : node.inputs) {
        for (auto& link : s_links) {
            int otherPinId = -1;
            if (link.endPinId == pin.id) otherPinId = link.startPinId;
            if (link.startPinId == pin.id) otherPinId = link.endPinId;
            if (otherPinId < 0) continue;

            GraphNode* srcNode = find_node_for_pin(otherPinId);
            Pin* otherPin = find_pin(otherPinId);
            if (srcNode && otherPin && otherPin->kind == PinKind::Output && srcNode->dspSource) {
                // A Note face has one output PER FIELD — the source is the
                // PIN's stand-in constant, not the node-level dspSource
                // (which is just the frequency pin's constant).
                std::shared_ptr<ValueSource> src =
                    (srcNode->typeName == NT_PERFORM && otherPin->constantSrc)
                        ? std::static_pointer_cast<ValueSource>(otherPin->constantSrc)
                        : srcNode->dspSource;
                // Tap wire: previous-sample read through a guarded RefSource
                // (feedback_loop_design.md §4.1) — never the raw source.
                if (otherPin->isTap)
                    src = std::make_shared<RefSource>(src, true);
                if (pin.multi && node.dspSource)
                    node.dspSource->add_param(pin.name, src);
                else
                    node.wire_pin(pin.name, src);
            }
        }
    }

    // Envelope: apply config values (ADSREnvelope rebuilds internally)
    if (node.typeName == NT_ENVELOPE) {
        node.apply_config();
    }
}

static void update_node_dsp(GraphNode& node) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    update_node_dsp_unlocked(node);
}

// Cycle legality (feedback_loop_design.md §2): a NORMAL wire src->dst is
// refused when a normal-edge path dst -> ... -> src already exists — the
// pull would recurse forever. Tap edges are invisible here (they read the
// previous sample, breaking the recursion), so loops close through taps.
static bool would_close_normal_cycle(GraphNode* srcNode, GraphNode* dstNode) {
    if (!srcNode || !dstNode) return false;
    if (srcNode == dstNode) return true;   // direct self-wire
    std::vector<GraphNode*> stack{dstNode};
    std::unordered_set<GraphNode*> visited;
    while (!stack.empty()) {
        GraphNode* n = stack.back();
        stack.pop_back();
        if (!visited.insert(n).second) continue;
        if (n == srcNode) return true;
        // Follow n's outputs to their consumers along non-tap links.
        for (auto& out : n->outputs) {
            if (out.isTap) continue;
            for (auto& link : s_links) {
                int other = -1;
                if (link.startPinId == out.id) other = link.endPinId;
                else if (link.endPinId == out.id) other = link.startPinId;
                if (other < 0) continue;
                Pin* op = find_pin(other);
                if (!op || op->kind != PinKind::Input) continue;
                if (GraphNode* consumer = find_node_for_pin(other))
                    stack.push_back(consumer);
            }
        }
    }
    return false;
}

// Update ALL nodes' DSP (call after link changes)
// Automatically wraps shared sources in RefSource for secondary consumers.
static void update_all_dsp() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    try {
        // First: run standard per-node wiring (primary sources)
        for (auto& node : s_nodes)
            update_node_dsp_unlocked(node);

        // Second: find sources with multiple consumers and wrap secondaries in RefSource.
        // Build map: source node output pin → list of (dest node, dest pin name)
        struct Consumer { GraphNode* node; std::string pinName; };
        std::unordered_map<int, std::vector<Consumer>> consumers; // output pin id → consumers

        for (auto& link : s_links) {
            // Find which is output and which is input
            Pin* startPin = find_pin(link.startPinId);
            Pin* endPin = find_pin(link.endPinId);
            if (!startPin || !endPin) continue;

            int outPinId = -1;
            int inPinId = -1;
            if (startPin->kind == PinKind::Output && endPin->kind == PinKind::Input) {
                outPinId = link.startPinId; inPinId = link.endPinId;
            } else if (endPin->kind == PinKind::Output && startPin->kind == PinKind::Input) {
                outPinId = link.endPinId; inPinId = link.startPinId;
            }
            if (outPinId < 0) continue;

            // Tap wires never join the advancer bookkeeping: each tap
            // consumer already holds its own guarded RefSource (idempotent
            // previous-sample read — any number is safe), and wrapping here
            // would replace it with an UNguarded live read.
            Pin* outPinDesc = find_pin(outPinId);
            if (outPinDesc && outPinDesc->isTap) continue;

            GraphNode* dstNode = find_node_for_pin(inPinId);
            Pin* dstPin = find_pin(inPinId);
            if (dstNode && dstPin)
                consumers[outPinId].push_back({dstNode, dstPin->name});
        }

        // For each output with >1 consumer, first consumer keeps real source,
        // rest get RefSource wrappers
        for (auto& [outPinId, consList] : consumers) {
            if (consList.size() <= 1) continue;

            GraphNode* srcNode = find_node_for_pin(outPinId);
            if (!srcNode || !srcNode->dspSource) continue;
            // Note-face pins are ConstantSources — idempotent, any number of
            // direct consumers is safe, and a RefSource here would wrap the
            // WRONG source (node-level dspSource = the frequency pin).
            if (srcNode->typeName == NT_PERFORM) continue;

            // First consumer already has the real source wired (from update_node_dsp).
            // Wrap for subsequent consumers.
            for (size_t i = 1; i < consList.size(); ++i) {
                auto ref = std::make_shared<RefSource>(srcNode->dspSource);
                consList[i].node->wire_pin(consList[i].pinName, ref);
            }
        }
    } catch (...) {
        // Don't crash the UI on DSP wiring errors
    }
}

static bool is_pin_connected(int pinId) {
    for (auto& link : s_links)
        if (link.startPinId == pinId || link.endPinId == pinId) return true;
    return false;
}

// ===========================================================================
// Selected node tracking
// ===========================================================================
static int g_selectedNodeId = -1;  // -1 = nothing selected

static GraphNode* find_selected_node() {
    for (auto& n : s_nodes)
        if (n.id == g_selectedNodeId) return &n;
    return nullptr;
}

// Defined with the transport helpers — stops only the continuous streams
// (patch mono stream + node-graph mixer stream), which hold raw pointers
// into node dspSources and would dangle when a node is destroyed. Voices
// own their data (per-voice InstrumentPatch shared_ptrs) and survive
// structural edits. Buffer playback does NOT own its data — it points at
// g_outputWaveform.data(), so any resize/clear of that vector must call
// buffer_playback_detach() first (backlog 3k: a reallocation mid-playback
// was an audio-thread read of freed memory).
static void stop_streams();
static void buffer_playback_detach();

static void delete_node(int nodeId) {
    stop_streams();
    mark_graph_dirty();
    if (g_selectedNodeId == nodeId) g_selectedNodeId = -1;
    for (auto& node : s_nodes) {
        if (node.id != nodeId) continue;
        s_links.erase(
            std::remove_if(s_links.begin(), s_links.end(),
                [&node](const Link& l) {
                    for (auto& p : node.inputs)
                        if (l.startPinId == p.id || l.endPinId == p.id) return true;
                    for (auto& p : node.outputs)
                        if (l.startPinId == p.id || l.endPinId == p.id) return true;
                    return false;
                }),
            s_links.end());
        break;
    }
    // Group bookkeeping: drop the deleted node from any membership list and
    // release a tap pointing at it.
    for (auto& node : s_nodes) {
        if (node.id != nodeId) continue;
        for (auto& g : s_groups)
            g.members.erase(std::remove(g.members.begin(), g.members.end(),
                                        node.label),
                            g.members.end());
        // Any dynamic pin driven by the deleted node is DEMOTED — the entry
        // is dropped and the setting's scalar (never overwritten by
        // promotion) takes over again. Without this, save emitted a ref to a
        // node that no longer exists and the CLI loader refused the whole
        // patch ("refs unknown node"). Demotion, not error, is the right
        // response: it is exactly what the demote button does, applied
        // because the driver went away.
        for (auto& other : s_nodes) {
            if (other.dynamicPins.empty()) continue;
            for (auto it = other.dynamicPins.begin();
                 it != other.dynamicPins.end(); ) {
                if (it.value().is_object() &&
                    it.value().value("ref", std::string()) == node.label) {
                    std::fprintf(stderr,
                        "[ui] %s.%s unwired: its driver '%s' was deleted\n",
                        other.label.c_str(), it.key().c_str(),
                        node.label.c_str());
                    it.value() = nullptr;  // pin stays promoted, unwired
                    ++it;
                } else {
                    ++it;
                }
            }
        }
        break;
    }
    if (s_listenTapNode == nodeId) s_listenTapNode = -1;
    s_nodes.erase(
        std::remove_if(s_nodes.begin(), s_nodes.end(),
            [nodeId](const GraphNode& n) { return n.id == nodeId; }),
        s_nodes.end());
}

static void delete_link(int linkId) {
    mark_graph_dirty();
    s_links.erase(
        std::remove_if(s_links.begin(), s_links.end(),
            [linkId](const Link& l) { return l.id == linkId; }),
        s_links.end());
    update_all_dsp();
}

// Set true when nodes need an automatic grid layout (boot/New default,
// or a loaded patch with no saved positions). Cleared by load when
// positions ARE present, so reload preserves user-placed positions.
static bool s_needsLayout = false;

// Defined with the transport helpers below. Called before graph teardown:
// the continuous-stream paths (g_streamSource / g_streamChannels) hold raw
// pointers into s_nodes' dspSources, so clearing the graph while the audio
// callback is pulling them is a use-after-free.
static void stop_playback();

static void new_graph(GraphMode mode) {
    stop_playback();
    s_currentFilePath.clear();
    s_nodes.clear();
    s_links.clear();
    s_loadedParamMap = nlohmann::json::object();
    s_loadedScore    = nlohmann::json();
    s_loadedInstrumentExtras = nlohmann::json::object();
    s_loadedSeconds  = nlohmann::json();
    s_loadedScoreAbsent = false;  // fresh graphs get the default score
    s_groups.clear();
    s_groupPath.clear();
    s_groupListen.clear();
    s_listenTapNode = -1;
    invalidate_instrument_cache();
    conv_stash_clear();
    s_graphMode = mode;
    s_nextId = 1;
    g_selectedNodeId = -1;
    // Default boot/New nodes need a grid layout — load_graph_from_path
    // will reset this to false if the loaded patch has saved positions.
    s_needsLayout = true;

    if (mode == GraphMode::PatchGraph) {
        // No Parameter node (spec §2): a new patch starts with no mappings;
        // bindings are added via the Parameter-mapping dialog.
        s_nodes.emplace_back("SineSource");
        s_nodes.emplace_back(std::string(NT_ENVELOPE));
        s_nodes.emplace_back(std::string(NT_PATCH_OUTPUT));
    } else {
        s_nodes.emplace_back("SineSource");
        s_nodes.emplace_back(std::string(NT_ENVELOPE));
        s_nodes.emplace_back(std::string(NT_SOUND_CHANNEL));
        s_nodes.emplace_back(std::string(NT_STEREO_MIXER));
    }
}

// ===========================================================================
// Save file dialog
// ===========================================================================

// Per-feature folder memory (slots live in UiSettings below; declared here
// because the dialogs precede the settings block).
static std::string* settings_dir_slot(const char* feature);
static void settings_save();

static void remember_feature_dir(const char* feature, const std::string& pickedPath) {
    std::string* slot = settings_dir_slot(feature);
    if (!slot) return;
    std::error_code ec;
    auto parent = std::filesystem::path(pickedPath).parent_path();
    if (!parent.empty() && std::filesystem::exists(parent, ec)) {
        *slot = parent.string();
        settings_save();
    }
}

static const char* feature_initial_dir(const char* feature) {
    std::string* slot = settings_dir_slot(feature);
    if (!slot || slot->empty()) return nullptr;
    std::error_code ec;
    if (!std::filesystem::exists(*slot, ec)) return nullptr;
    return slot->c_str();
}

static std::string save_file_dialog() {
    char filename[MAX_PATH] = "patch.json";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = feature_initial_dir("save");
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "json";
    if (GetSaveFileNameA(&ofn)) {
        remember_feature_dir("save", filename);
        return filename;
    }
    return "";
}

static std::string open_file_dialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrInitialDir = feature_initial_dir("open");
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) {
        remember_feature_dir("open", filename);
        return filename;
    }
    return "";
}

// ===========================================================================
// JSON import
// ===========================================================================

// json_type_to_node removed — typeName strings are used directly

// Envelope stage-list (de)serialization — the params.stages form shared by
// save_patch_graph, save_node_graph, load_graph_from_path, and the node
// clipboard. Factored so all four stay byte-identical.
static nlohmann::json envelope_stages_to_json(const Envelope& env) {
    nlohmann::json stages = nlohmann::json::array();
    for (int i = 0; i < env.stage_count(); ++i) {
        const auto& s = env.stage(i);
        const char* t = (s.ramp.type == RampType::Expo)        ? "Expo"
                      : (s.ramp.type == RampType::InverseExpo) ? "InverseExpo"
                      : (s.ramp.type == RampType::Sine)        ? "Sine"
                      : (s.ramp.type == RampType::Hold)        ? "Hold"
                                                                : "Linear";
        nlohmann::json sj = {
            {"percent",  s.percent},
            {"startVal", s.ramp.startVal},
            {"endVal",   s.ramp.endVal},
            {"type",     t},
            {"power",    s.ramp.power},
            {"minSec",   s.minSec},
            {"maxSec",   s.maxSec},
        };
        // Stage.nominal — the stage's authored length for LIVE playback, added
        // by P1 and never taught to this serializer. Omitted at its default so
        // no existing patch's bytes move; emitted when set, so a re-save stops
        // silently discarding it. That was the standing "don't hand-author
        // these fields before P2b" warning in BACKLOG 20.
        if (s.nominal != 0.0f) sj["nominal"] = s.nominal;
        stages.push_back(std::move(sj));
    }
    return stages;
}

// Replaces env's stage list with the one described by `stages`.
static void envelope_stages_from_json(Envelope& env, const nlohmann::json& stages) {
    env = Envelope(DSP_SAMPLE_RATE);
    for (const auto& sj : stages) {
        Envelope::Stage s;
        s.ramp.startVal = sj.value("startVal", 0.0f);
        s.ramp.endVal   = sj.value("endVal",   0.0f);
        s.ramp.power    = sj.value("power",    0.0f);
        std::string t   = sj.value("type", std::string("Linear"));
        s.ramp.type = (t == "Expo")        ? RampType::Expo
                    : (t == "InverseExpo") ? RampType::InverseExpo
                    : (t == "Sine")        ? RampType::Sine
                    : (t == "Hold")        ? RampType::Hold
                                            : RampType::Linear;
        s.percent = sj.value("percent", 0.0f);
        s.minSec  = sj.value("minSec",  0.0f);
        s.maxSec  = sj.value("maxSec",  0.0f);
        s.nominal = sj.value("nominal", 0.0f);   // P1 live-playback stage length
        env.add_stage(s);
    }
}

// Defined with the transport helpers below — loads report unknown node
// types (patch authored against an engine branch this build lacks) in the
// transport status line instead of silently creating inert nodes.
static void transport_set_status(const char* msg, bool isError);

// ---------------------------------------------------------------------------
// Legacy paramMap -> wiring format, in JSON, before the graph is built.
//
// Deliberately done in JSON space rather than by constructing GraphNodes and
// Links directly: it is the SAME algorithm on the SAME representation as
// tools/parammap_to_wiring.py, which is gated at 196/196, so the two cannot
// drift into disagreement. The existing loader then builds the graph from the
// converted document and needs no knowledge of paramMap at all.
//
// Mirrors build_bindings' matrix (engine/src/patch_loader.cpp):
//   bare "node.pin"         -> pin refs __perf_freq
//   {target, curve}         -> CurveNode(logx|loglog) reading __perf_freq
//   {target, vcurve}        -> multiply of the freq leg and a linear CurveNode
//                              reading __perf_vel
//   target with no '.'      -> pin defaults to "frequency"
//   name != "frequency"     -> dropped; the engine ignored these too
// A target lands in dynamicPins when it names a SETTING on that node's type,
// asked of the registry directly.
// ---------------------------------------------------------------------------
static bool convert_parammap_to_wiring(nlohmann::json& root) {
    using json = nlohmann::json;
    if (!root.contains("instrument") || !root["instrument"].contains("paramMap"))
        return false;
    json pm = root["instrument"]["paramMap"];
    if (!pm.is_object() || pm.empty()) {
        root["instrument"].erase("paramMap");
        return false;
    }
    json* container = nullptr;
    if (root.contains("graph") && root["graph"].contains("nodes"))
        container = &root["graph"];
    else if (root.contains("nodes"))
        container = &root;
    if (!container) return false;

    auto& nodes = (*container)["nodes"];
    std::unordered_map<std::string, json*> byId;
    for (auto& n : nodes) byId[n["id"].get<std::string>()] = &n;

    // Formant children owned by a FormantSpectrum are NOT convertible. The
    // UI consumes them into the spectrum's row table, and a row holds literal
    // floats — it cannot represent a driven param. Writing a ref into one
    // makes the pre-scan stop consuming it, so the spectrum re-emits
    // synthesized rows and the original children survive as orphans (caught
    // by the round-trip gate on 5 voice patches: spec.formants went
    // [f1..f5] -> [spec__f0..spec__f3] with f1..f5 left loose).
    // Those entries stay in the paramMap, which still round-trips as it always
    // has. Converting them needs the row model to carry driven params — real
    // work, and not this task's.
    std::unordered_set<std::string> ownedFormants;
    for (auto& n : nodes) {
        if (!n.contains("params") || !n["params"].contains("formants")) continue;
        const auto& arr = n["params"]["formants"];
        if (!arr.is_array()) continue;
        for (const auto& f : arr)
            if (f.is_object() && f.contains("ref")) {
                const std::string rid = f["ref"].get<std::string>();
                auto it = byId.find(rid);
                if (it != byId.end() && (*it->second)["type"] == "Formant")
                    ownedFormants.insert(rid);
            }
    }

    std::vector<json> synth;
    std::unordered_map<std::string, bool> havePerf;
    int counter = 0;

    auto need_perf = [&](const char* field) -> std::string {
        std::string nid = std::string("__perf_") + (std::string(field) == "velocity" ? "vel" : "freq");
        if (!havePerf[nid]) {
            synth.push_back({{"id", nid}, {"type", "PerformNode"},
                             {"params", {{"field", field}}}});
            havePerf[nid] = true;
        }
        return nid;
    };
    auto add_curve = [&](const json& knots, const char* interp,
                         const std::string& srcId) -> std::string {
        std::string nid = "__curve_" + std::to_string(++counter);
        synth.push_back({{"id", nid}, {"type", "CurveNode"},
                         {"params", {{"interp", interp}, {"knots", knots},
                                     {"source", {{"ref", srcId}}}}}});
        return nid;
    };
    auto add_mul = [&](const std::string& a, const std::string& b) -> std::string {
        std::string nid = "__mul_" + std::to_string(++counter);
        synth.push_back({{"id", nid}, {"type", "CombinedSource"},
                         {"params", {{"source1", {{"ref", a}}},
                                     {"source2", {{"ref", b}}},
                                     {"operation", "multiply"}, {"gainAdj", 0.0}}}});
        return nid;
    };

    auto& reg = SourceRegistry::instance();
    auto is_setting = [&](const std::string& type, const std::string& key) {
        if (!reg.has(type)) return false;
        auto probe = reg.create(type, DSP_SAMPLE_RATE);
        if (!probe) return false;
        for (const auto& d : probe->setting_descriptors())
            if (key == d.name) return true;
        return false;
    };

    int converted = 0, kept = 0;
    json leftover = json::object();
    auto keep = [&](const std::string& name, const json& e) {
        if (!leftover.contains(name)) leftover[name] = json::array();
        leftover[name].push_back(e);
        ++kept;
    };

    for (auto& [name, entries] : pm.items()) {
        json list = entries.is_array() ? entries : json::array({entries});
        if (name != "frequency") {   // the engine ignored these too; keep as-is
            for (const auto& e : list) keep(name, e);
            continue;
        }
        for (const auto& e : list) {
            std::string target;
            if (e.is_string())                              target = e.get<std::string>();
            else if (e.is_object() && e.contains("target")) target = e["target"].get<std::string>();
            else { keep(name, e); continue; }

            auto dot = target.find('.');
            std::string nodeId = dot == std::string::npos ? target : target.substr(0, dot);
            std::string pin    = dot == std::string::npos ? "frequency" : target.substr(dot + 1);
            auto it = byId.find(nodeId);
            if (it == byId.end() || ownedFormants.count(nodeId)) { keep(name, e); continue; }

            // Build the chain only once the target is known convertible, so a
            // kept entry leaves no orphan synthesized nodes behind.
            std::string head = need_perf("frequency");
            if (e.is_object()) {
                if (e.contains("curve"))
                    head = add_curve(e["curve"],
                                     e.value("interp", std::string()) == "loglog"
                                         ? "loglog" : "logx", head);
                if (e.contains("vcurve"))
                    head = add_mul(head, add_curve(e["vcurve"], "linear",
                                                   need_perf("velocity")));
            }
            json& tgt = *it->second;
            const std::string ttype = tgt["type"].get<std::string>();
            const char* slot = is_setting(ttype, pin) ? "dynamicPins" : "params";
            if (!tgt.contains(slot)) tgt[slot] = json::object();
            tgt[slot][pin] = {{"ref", head}};
            ++converted;
        }
    }

    // Prepend in dependency order — refs must resolve against earlier nodes.
    json merged = json::array();
    for (auto& n : synth) merged.push_back(n);
    for (auto& n : nodes) merged.push_back(n);
    nodes = merged;
    // Unconvertible entries stay in the paramMap and keep working exactly as
    // before; the stash path is unchanged for them. Only a fully converted
    // patch loses its paramMap.
    if (kept) root["instrument"]["paramMap"] = leftover;
    else      root["instrument"].erase("paramMap");
    if (converted || kept)
        std::fprintf(stderr, "[ui] paramMap: %d converted to wiring, %d kept "
                     "(owned formant children are not convertible)\n",
                     converted, kept);
    return converted > 0;
}

static void load_graph_from_path(const std::string& path) {
    using json = nlohmann::json;

    if (path.empty()) return;

    std::ifstream f(path);
    if (!f) return;
    json root = json::parse(f);
    // Legacy paramMap becomes real nodes before anything else looks at the
    // document. After this the stash is dead: nothing downstream reads it.
    convert_parammap_to_wiring(root);
    stop_playback();  // stream paths hold raw pointers into s_nodes' DSP
    s_currentFilePath = path;
    s_graphDirty = false;

    s_nodes.clear();
    s_links.clear();
    s_groups.clear();
    s_groupPath.clear();
    s_groupListen.clear();
    s_listenTapNode = -1;
    invalidate_instrument_cache();
    s_loadedParamMap = nlohmann::json::object();
    s_loadedScore    = nlohmann::json();
    s_loadedInstrumentExtras = nlohmann::json::object();
    s_loadedSeconds  = nlohmann::json();
    conv_stash_clear();
    s_nextId = 1;

    bool hasInstrument = root.contains("instrument");
    s_graphMode = hasInstrument ? GraphMode::PatchGraph : GraphMode::NodeGraph;

    // Preserve the patch's score/seconds verbatim for save, and seed the
    // transport + keyboard defaults from the score so UI playback (Generate,
    // keyboard) matches what `mforce_cli <patch> out.wav` renders. Percent-
    // based envelopes make note DURATION part of the sound, so playing the
    // transport's old hardcoded 2.0s/C4 against a 3.0s/A2 score was audibly
    // a different patch.
    if (root.contains("score")) s_loadedScore = root["score"];
    s_loadedScoreAbsent = !root.contains("score");
    if (root.contains("instrument")) {
        s_loadedInstrumentExtras = root["instrument"];
        s_loadedInstrumentExtras.erase("polyphony");
        s_loadedInstrumentExtras.erase("paramMap");
    }
    if (root.contains("seconds")) s_loadedSeconds = root["seconds"];
    apply_score_defaults(s_loadedScore);

    const auto& nodes = root["graph"]["nodes"];
    std::string outputId = root["graph"]["output"].get<std::string>();

    // Map JSON node IDs to created GraphNodes
    std::unordered_map<std::string, GraphNode*> nodeMap;
    // Map "nodeId.paramName" → pin ID for wiring refs
    std::unordered_map<std::string, int> inputPinMap;
    // Map nodeId → output pin ID
    std::unordered_map<std::string, int> outputPinMap;
    // Map nodeId → tap pin ID ({"tap": id} wires reload onto this pin)
    std::unordered_map<std::string, int> tapPinMap;

    // Pre-scan: count ref occurrences across the whole graph so we can
    // detect Formant nodes whose sole referrer is a FormantSpectrum.
    std::unordered_map<std::string, int> refCounts;
    std::function<void(const json&)> countRefs = [&](const json& val) {
        if (val.is_object()) {
            if (val.contains("ref") && val["ref"].is_string()) {
                refCounts[val["ref"].get<std::string>()]++;
                return;
            }
            for (auto it = val.begin(); it != val.end(); ++it) countRefs(it.value());
        } else if (val.is_array()) {
            for (const auto& item : val) countRefs(item);
        }
    };
    for (const auto& jnode : nodes) {
        if (jnode.contains("params")) countRefs(jnode["params"]);
    }

    // Locate FormantSpectrum nodes whose params.formants is a ref-array of
    // bare-constant, single-use Formant nodes. Those formants get consumed
    // into the spectrum's inline row table (and skipped as graph nodes).
    std::unordered_map<std::string, std::vector<FormantRow>> specRows;
    std::unordered_map<std::string, bool> ownedFormants;
    auto find_json_node = [&](const std::string& id) -> const json* {
        for (const auto& jn : nodes)
            if (jn.contains("id") && jn["id"].get<std::string>() == id) return &jn;
        return nullptr;
    };
    for (const auto& jnode : nodes) {
        if (!jnode.contains("type") || jnode["type"].get<std::string>() != "FormantSpectrum") continue;
        if (!jnode.contains("params") || !jnode["params"].contains("formants")) continue;
        const auto& arr = jnode["params"]["formants"];
        if (!arr.is_array() || arr.empty()) continue;
        if (!arr[0].is_object() || !arr[0].contains("ref")) continue;  // legacy inline form, skip

        std::vector<FormantRow> rows;
        std::vector<std::string> ids;
        bool canConsume = true;
        for (const auto& item : arr) {
            if (!item.is_object() || !item.contains("ref")) { canConsume = false; break; }
            std::string fid = item["ref"].get<std::string>();
            if (refCounts[fid] != 1) { canConsume = false; break; }
            const json* fnode = find_json_node(fid);
            if (!fnode || !fnode->contains("type") ||
                (*fnode)["type"].get<std::string>() != "Formant" ||
                !fnode->contains("params")) { canConsume = false; break; }
            const auto& fp = (*fnode)["params"];
            if (!fp.contains("frequency") || !fp["frequency"].is_number() ||
                !fp.contains("gain")      || !fp["gain"].is_number() ||
                !fp.contains("width")     || !fp["width"].is_number() ||
                !fp.contains("power")     || !fp["power"].is_number()) {
                canConsume = false; break;
            }
            FormantRow row;
            row.frequency = fp["frequency"].get<float>();
            row.gain      = fp["gain"].get<float>();
            row.width     = fp["width"].get<float>();
            row.power     = fp["power"].get<float>();
            row.srcId     = fid;
            rows.push_back(row);
            ids.push_back(fid);
        }
        if (canConsume) {
            specRows[jnode["id"].get<std::string>()] = std::move(rows);
            for (auto& fid : ids) ownedFormants[fid] = true;
        }
    }

    // PerformNode file nodes are CONSUMED into Note faces: the file keeps
    // one node per field (refs name nodes; a node is one value stream), the
    // editor shows one Note node with an output pin per field — the shape
    // every design discussion described (Matt 2026-08-20; the per-field
    // editor face was a P2a/P2b conflation of file shape with face shape).
    // ui.noteFaces records face identity; without it (converted/legacy
    // files) all perf nodes merge into a single face.
    std::unordered_map<std::string, std::string> perfFileField;  // id -> field
    for (const auto& jnode : nodes) {
        if (jnode.value("type", std::string()) != NT_PERFORM) continue;
        std::string f = "frequency";
        if (jnode.contains("params"))
            f = jnode["params"].value("field", std::string("frequency"));
        perfFileField[jnode["id"].get<std::string>()] = f;
    }

    // First pass: create all nodes
    std::vector<std::string> unknownTypes;
    for (const auto& jnode : nodes) {
        std::string id = jnode["id"].get<std::string>();
        std::string type = jnode["type"].get<std::string>();

        // Skip Formants owned by a FormantSpectrum — they'll live inside its row table.
        if (ownedFormants.count(id)) continue;
        // Skip consumed PerformNode file nodes — faces are built after this
        // loop and their pins registered under these ids.
        if (perfFileField.count(id)) continue;

        // Track types this build's engine doesn't know. The node is still
        // created (inert, no dspSource, one bare "out" pin) so the graph
        // shape survives a load→save, but params/links through it are lost —
        // surface that instead of failing silently.
        if (!is_special_ui_type(type) && type != NT_ENVELOPE &&
            type != NT_PERFORM &&   // UI-special like Parameter/Output: the
                                    // engine resolves it per voice, the editor
                                    // has its own branch — NOT unknown. The
                                    // exemption list predates PerformNode and
                                    // fired a false "loaded inert" on every
                                    // converted patch's first human load
                                    // (Matt, 2026-08-20).
            type != "FormantSpectrum" && !SourceRegistry::instance().has(type) &&
            std::find(unknownTypes.begin(), unknownTypes.end(), type) == unknownTypes.end())
            unknownTypes.push_back(type);

        s_nodes.emplace_back(type);
        GraphNode& gn = s_nodes.back();
        gn.label = id;  // use JSON id as label
        // A "__" id in the FILE is a legitimately synthesized node — the
        // wiring format's __perf_freq / __curve_N / __mul_N, or the UI's own
        // __output / __param_* / __fN. sanitize_unique_id strips that prefix
        // to stop a human LABEL claiming the reserved namespace; without this
        // flag it also renames the converter's nodes on every round trip
        // (__perf_freq -> _perf_freq). Renders are unaffected, but silently
        // renaming nodes the converter created makes later diffs unreadable.
        gn.synthesizedId = (id.rfind("__", 0) == 0);

        // Dynamic pins ride ALONGSIDE params, not inside them — read here
        // rather than in the params block, since a node can carry dynamicPins
        // and no params at all.
        if (jnode.contains("dynamicPins") && jnode["dynamicPins"].is_object())
            gn.dynamicPins = jnode["dynamicPins"];

        nodeMap[id] = &gn;

        // Map output pin (and the tap pin, for {"tap": id} wire reload)
        if (!gn.outputs.empty())
            outputPinMap[id] = gn.outputs[0].id;
        for (auto& op : gn.outputs)
            if (op.isTap) { tapPinMap[id] = op.id; break; }

        // Map input pins and set default values
        for (auto& pin : gn.inputs) {
            inputPinMap[id + "." + pin.name] = pin.id;
        }

        // Set default values from params
        if (jnode.contains("params")) {
            const auto& params = jnode["params"];

            // Seed is engine-side state, not a UI pin/config — stash it
            // verbatim so save paths re-emit it (see GraphNode::jsonSeed).
            if (params.contains("seed") && params["seed"].is_number())
                gn.jsonSeed = params["seed"].get<long long>();

            for (auto& pin : gn.inputs) {
                if (!params.contains(pin.name)) continue;
                const auto& val = params[pin.name];
                if (val.is_number()) {
                    pin.defaultValue = val.get<float>();
                    pin.hasConstant = true;
                    if (pin.constantSrc) pin.constantSrc->set(pin.defaultValue);
                }
                // refs handled in second pass
            }

            if (gn.typeName == "CurveNode") {
                gn.curveKnots.clear();
                if (params.contains("knots") && params["knots"].is_array())
                    for (const auto& k : params["knots"])
                        if (k.is_array() && k.size() >= 2)
                            gn.curveKnots.emplace_back(k[0].get<float>(), k[1].get<float>());
                const std::string in = params.value("interp", std::string("linear"));
                gn.curveInterp = (in == "loglog") ? 2 : (in == "logx") ? 1 : 0;
                gn.curveExprMode =
                    params.value("mode", std::string("points")) == "expressions";
                gn.curveExprKnots.clear();
                if (params.contains("exprKnots") && params["exprKnots"].is_array())
                    for (const auto& k : params["exprKnots"]) {
                        CurveNode::ExprKnot ek;
                        ek.x = k.value("x", 100.0f);
                        ek.form = k.value("form", std::string("linear")) == "power"
                                  ? CurveNode::KnotForm::Power
                                  : CurveNode::KnotForm::Linear;
                        ek.a = k.value("a", 0.0f);
                        ek.b = k.value("b", 0.0f);
                        gn.curveExprKnots.push_back(ek);
                    }
                if (auto* cn = dynamic_cast<CurveNode*>(gn.dspSource.get())) {
                    cn->knots = gn.curveKnots;
                    cn->interp = static_cast<CurveNode::CurveInterp>(gn.curveInterp);
                    cn->exprMode = gn.curveExprMode;
                    cn->exprKnots = gn.curveExprKnots;
                }
            }


            // Restore formant table. New canonical form: params.formants is a
            // ref-array of owned Formant nodes, pulled into rows via the
            // pre-scan. Legacy form: inline struct array.
            if (auto sit = specRows.find(id); sit != specRows.end()) {
                gn.formantRows = sit->second;
                gn.rebuild_formant_spectrum();
            } else if (params.contains("formants") && params["formants"].is_array() &&
                       !params["formants"].empty() && params["formants"][0].is_object() &&
                       !params["formants"][0].contains("ref")) {
                gn.formantRows.clear();
                for (const auto& fj : params["formants"]) {
                    FormantRow row;
                    row.frequency = fj.value("frequency", 1000.0f);
                    row.gain      = fj.value("gain", 1.0f);
                    row.width     = fj.value("width", 500.0f);
                    row.power     = fj.value("power", 2.0f);
                    gn.formantRows.push_back(row);
                }
                gn.rebuild_formant_spectrum();
            }

            // Restore preset-form Envelopes (params.preset + attack/decay/…).
            // Previously the preset params were IGNORED — the node kept its
            // constructor default make_adsr(0.05, 0.1, 0.7, 0.2) and only
            // sustainLevel was restored via config — so Save / the playback
            // temp JSON silently replaced e.g. adsr(0.03, 0.05, 0.9, 0.0)
            // with the default shape. Release=0 is especially destructive:
            // the last 0%-stage is the Envelope's expand stage, so an adsr
            // with release 0 is really "slow release fills the sustain
            // region" — swapping in release 0.2 changes the entire decay.
            if (gn.typeName == NT_ENVELOPE && !params.contains("stages") &&
                params.contains("preset")) {
                if (auto* env = dynamic_cast<Envelope*>(gn.dspSource.get())) {
                    // Shared preset dispatch (envelope_json.h) — same
                    // function the engine loader uses, so the two cannot
                    // drift. (The hand-copied dispatch this replaces
                    // silently kept the default ADSR for unknown presets
                    // — the 2026-08-13 damper-fires-on-attack bug — and
                    // dropped the fraction-adsr min/max jitter args.)
                    *env = envelope_from_preset_json(params, DSP_SAMPLE_RATE);
                }
            }

            // Restore Envelope stages. For NT_ENVELOPE nodes saved with
            // params.stages, replace the live Envelope's stage list.
            if (gn.typeName == NT_ENVELOPE && params.contains("stages")) {
                if (auto* env = dynamic_cast<Envelope*>(gn.dspSource.get())) {
                    envelope_stages_from_json(*env, params["stages"]);
                    env->absolute_time =
                        params.value("timeMode", std::string("fraction")) == "seconds";
                    // Sync the cached sustainLevel to the stages' actual slot
                    // value: apply_config below pushes EVERY cached config,
                    // and the 0.7 descriptor default would rewrite the four
                    // sustain slots now that set_setting recognizes the adsr
                    // shape (it was a silent no-op before — 3n).
                    for (auto& [desc, val] : gn.settingValues)
                        if (std::string_view(desc.name) == "sustainLevel")
                            val = env->get_setting("sustainLevel");
                }
            }

            // Restore config values
            for (auto& [desc, val] : gn.settingValues) {
                if (!params.contains(desc.name)) continue;
                const auto& jval = params[desc.name];
                if (jval.is_boolean()) val = jval.get<bool>() ? 1.0f : 0.0f;
                else if (jval.is_number()) val = jval.get<float>();
                else if (jval.is_string() && desc.enum_labels) {
                    // Enum configs arrive as strings in hand-written patches
                    // ("operation": "multiply"). Match display labels
                    // case-insensitively; a miss keeps the default LOUDLY —
                    // skipping strings silently is how every CombinedSource
                    // loaded as Mix (the 2026-08-13 edit-then-play
                    // volume-drop bug).
                    std::string s = jval.get<std::string>();
                    bool matched = false;
                    for (int li = 0; desc.enum_labels[li]; ++li) {
                        if (_stricmp(s.c_str(), desc.enum_labels[li]) == 0) {
                            val = float(li);
                            matched = true;
                            break;
                        }
                    }
                    if (!matched)
                        std::fprintf(stderr,
                            "[load] %s.%s: enum string '%s' matches no label; "
                            "keeping default\n", id.c_str(), desc.name, s.c_str());
                }
            }
            gn.apply_config();

            // Restore user-editable arrays (ExplicitPartials mult/ampl,
            // spectrum gains, …). Must run AFTER apply_config: configs not
            // present in the JSON get pushed at their descriptor defaults
            // (e.g. maxPartials=16), which rebuilds the DSP arrays — the
            // JSON arrays then overwrite that. Previously arrays were never
            // restored at all, so any ExplicitPartials patch loaded into the
            // UI silently played 16 default harmonics.
            for (auto& [desc, vec] : gn.arrayValues) {
                if (!params.contains(desc.name)) continue;
                const auto& jv = params[desc.name];
                if (!jv.is_array()) continue;
                vec = jv.get<std::vector<float>>();
                gn.dspSource->set_array(desc.name, vec);
            }
            // Arrays can change derived configs (set_array updates
            // maxPartials from array length) — sync the UI table back from
            // the DSP object so the inspector shows live values, not stale
            // descriptor defaults.
            for (auto& [desc, val] : gn.settingValues)
                val = gn.dspSource->get_setting(desc.name);

            // Capture every params key the passes above did NOT consume —
            // carried verbatim through save (GraphNode::jsonExtras, 3n).
            {
                auto is_pin = [&](const std::string& k) {
                    for (auto& pin : gn.inputs) if (pin.name == k) return true;
                    return false;
                };
                auto is_config = [&](const std::string& k) {
                    for (auto& [desc, v] : gn.settingValues)
                        if (k == desc.name) return true;
                    return false;
                };
                auto is_float_array_key = [&](const std::string& k, const nlohmann::json& v) {
                    if (!v.is_array()) return false;
                    for (auto& [desc, vec] : gn.arrayValues)
                        if (k == desc.name) return true;
                    return false;
                };
                static const std::unordered_set<std::string> kEnvelopeKeys = {
                    "stages", "timeMode", "preset", "attack", "decay",
                    "sustainLevel", "release", "attackMin", "attackMax",
                    "decayMin", "decayMax", "releaseMin", "releaseMax"};
                for (auto& [k, v] : params.items()) {
                    if (k == "seed") continue;
                    if (k == "formants") continue;  // rows / ref-pins, both modeled
                    if (gn.typeName == NT_ENVELOPE && kEnvelopeKeys.count(k)) continue;
                    // CurveNode knots/interp are modeled (curveKnots /
                    // curveInterp) so the Properties curve editor can edit them. They
                    // rode jsonExtras verbatim until P2b; a verbatim copy
                    // would win over an edit on save.
                    if (gn.typeName == "CurveNode" &&
                        (k == "knots" || k == "interp" ||
                         k == "mode" || k == "exprKnots"))
                        continue;
                    if (gn.typeName == NT_PERFORM && k == "field") continue;
                    bool pinConsumed = is_pin(k) &&
                        (v.is_number() ||
                         (v.is_object() && v.contains("ref")) ||
                         v.is_array());
                    if (pinConsumed) continue;
                    if (is_config(k)) continue;
                    if (is_float_array_key(k, v)) continue;
                    gn.jsonExtras[k] = v;
                }
            }
        }

        // Mixer: add extra channel inputs if needed
        if (gn.typeName == NT_STEREO_MIXER && jnode.contains("inputs") &&
            jnode["inputs"].contains("channels")) {
            int numCh = (int)jnode["inputs"]["channels"].size();
            for (int c = 1; c < numCh; ++c)
                gn.add_channel_input();
            // Re-map input pins after adding channels
            for (auto& pin : gn.inputs)
                inputPinMap[id + "." + pin.name] = pin.id;
        }
    }

    // Build Note faces from the consumed PerformNode file nodes, and
    // register each file id under the matching face PIN so the ref pass
    // below wires straight to it. ui.noteFaces preserves face identity
    // across UI round-trips; a file without it (converted/legacy) merges
    // every perf node into one face.
    if (!perfFileField.empty()) {
        std::vector<std::pair<std::string, std::map<std::string, std::string>>> plan;
        if (root.contains("ui") && root["ui"].contains("noteFaces") &&
            root["ui"]["noteFaces"].is_array()) {
            for (const auto& fj : root["ui"]["noteFaces"]) {
                std::map<std::string, std::string> fields;
                if (fj.contains("fields") && fj["fields"].is_object())
                    for (auto& [f, fid] : fj["fields"].items())
                        if (fid.is_string() &&
                            perfFileField.count(fid.get<std::string>()))
                            fields[f] = fid.get<std::string>();
                plan.emplace_back(fj.value("label", std::string("Note")),
                                  std::move(fields));
            }
        }
        if (plan.empty()) {
            std::map<std::string, std::string> fields;
            for (auto& [fid, f] : perfFileField)
                if (!fields.count(f)) fields[f] = fid;  // first id per field
            plan.emplace_back(unique_node_label(NT_PERFORM), std::move(fields));
        }
        for (auto& [label, fields] : plan) {
            s_nodes.emplace_back(std::string(NT_PERFORM));
            GraphNode& face = s_nodes.back();
            face.label = label;
            face.perfFieldIds = fields;
            for (auto& [f, fid] : fields)
                if (Pin* p = face.find_output(f))
                    outputPinMap[fid] = p->id;
        }
        // Stragglers (duplicate same-field file nodes, or a noteFaces list
        // that missed one): wire through the first face's matching pin.
        for (auto& [fid, f] : perfFileField) {
            if (outputPinMap.count(fid)) continue;
            for (auto& n : s_nodes) {
                if (n.typeName != NT_PERFORM) continue;
                if (Pin* p = n.find_output(f)) {
                    outputPinMap[fid] = p->id;
                    if (!n.perfFieldIds.count(f)) n.perfFieldIds[f] = fid;
                }
                break;
            }
        }
    }

    // Second pass: create links from refs
    for (const auto& jnode : nodes) {
        std::string id = jnode["id"].get<std::string>();
        if (!jnode.contains("params")) continue;

        for (auto& [paramName, val] : jnode["params"].items()) {
            // Multi-input: array of refs
            if (val.is_array()) {
                auto inIt = inputPinMap.find(id + "." + paramName);
                if (inIt == inputPinMap.end()) continue;
                for (const auto& refObj : val) {
                    if (!refObj.is_object()) continue;
                    if (refObj.contains("ref")) {
                        std::string refId = refObj["ref"].get<std::string>();
                        auto outIt = outputPinMap.find(refId);
                        if (outIt != outputPinMap.end())
                            s_links.emplace_back(outIt->second, inIt->second);
                    } else if (refObj.contains("tap")) {
                        std::string refId = refObj["tap"].get<std::string>();
                        auto tapIt = tapPinMap.find(refId);
                        if (tapIt != tapPinMap.end())
                            s_links.emplace_back(tapIt->second, inIt->second);
                    }
                }
                continue;
            }

            if (!val.is_object()) continue;
            if (val.contains("tap")) {
                // Tap wire: reload onto the source node's TAP pin so the
                // previous-sample semantics survive the round trip.
                std::string refId = val["tap"].get<std::string>();
                auto tapIt = tapPinMap.find(refId);
                auto inIt = inputPinMap.find(id + "." + paramName);
                if (tapIt != tapPinMap.end() && inIt != inputPinMap.end())
                    s_links.emplace_back(tapIt->second, inIt->second);
                continue;
            }
            if (!val.contains("ref")) continue;
            std::string refId = val["ref"].get<std::string>();

            auto outIt = outputPinMap.find(refId);
            auto inIt = inputPinMap.find(id + "." + paramName);
            if (outIt != outputPinMap.end() && inIt != inputPinMap.end()) {
                s_links.emplace_back(outIt->second, inIt->second);
            }
        }

        // SoundChannel source input
        if (jnode.contains("inputs") && jnode["inputs"].contains("source")) {
            std::string srcId = jnode["inputs"]["source"].get<std::string>();
            auto outIt = outputPinMap.find(srcId);
            auto inIt = inputPinMap.find(id + ".source");
            if (outIt != outputPinMap.end() && inIt != inputPinMap.end())
                s_links.emplace_back(outIt->second, inIt->second);
        }

        // StereoMixer channel inputs
        if (jnode.contains("inputs") && jnode["inputs"].contains("channels")) {
            int chIdx = 0;
            for (const auto& chId : jnode["inputs"]["channels"]) {
                std::string srcId = chId.get<std::string>();
                char chName[16];
                snprintf(chName, sizeof(chName), "ch %d", chIdx + 1);
                auto outIt = outputPinMap.find(srcId);
                auto inIt = inputPinMap.find(id + "." + std::string(chName));
                if (outIt != outputPinMap.end() && inIt != inputPinMap.end())
                    s_links.emplace_back(outIt->second, inIt->second);
                chIdx++;
            }
        }
    }

    // Patch Graph: create Output node and Parameter nodes
    if (hasInstrument) {
        // Output node
        s_nodes.emplace_back(NT_PATCH_OUTPUT);
        GraphNode& outNode = s_nodes.back();
        if (root["instrument"].contains("polyphony"))
            outNode.polyphony = root["instrument"]["polyphony"].get<int>();

        // Wire output node's source to the graph output
        auto outIt = outputPinMap.find(outputId);
        if (outIt != outputPinMap.end())
            s_links.emplace_back(outIt->second, outNode.inputs[0].id);

        // paramMap: the stash IS the model (2026-08-14 spec §2). No Parameter
        // nodes are materialized in patch mode — bindings live as data, are
        // edited in the Mappings dialog / Curve Properties, show as green badges on
        // Properties rows, and are applied per note by apply_param_map.
        if (root["instrument"].contains("paramMap"))
            s_loadedParamMap = root["instrument"]["paramMap"];
    }

    // Groups (spec §3): root-level section, engine-blind. Two passes so a
    // member may reference a group defined later in the array.
    if (root.contains("groups") && root["groups"].is_array()) {
        for (const auto& jg : root["groups"]) {
            if (!jg.contains("name") || !jg["name"].is_string()) continue;
            NodeGroup g;
            g.name = jg["name"].get<std::string>();
            if (jg.contains("pos") && jg["pos"].is_array() && jg["pos"].size() == 2) {
                g.pos = ImVec2(jg["pos"][0].get<float>(), jg["pos"][1].get<float>());
                g.posApplied = false;
            }
            g.editorId = next_id();
            g.outPinId = next_id();
            for (const auto& m : jg.value("members", nlohmann::json::array()))
                if (m.is_string()) g.members.push_back(m.get<std::string>());
            s_groups.push_back(std::move(g));
        }
        // Validate membership against live labels + group names; drop dead
        // entries loudly rather than carrying ghosts.
        for (auto& g : s_groups) {
            auto known = [&](const std::string& m) {
                if (group_by_name(m)) return true;
                for (auto& n : s_nodes) if (n.label == m) return true;
                return false;
            };
            for (auto it = g.members.begin(); it != g.members.end();) {
                if (!known(*it)) {
                    std::fprintf(stderr, "[load] group '%s': member '%s' "
                                 "not in graph — dropped\n",
                                 g.name.c_str(), it->c_str());
                    it = g.members.erase(it);
                } else ++it;
            }
        }
    }

    // Restore positions from UI metadata, or fall back to grid layout
    if (root.contains("ui") && root["ui"].contains("positions")) {
        const auto& positions = root["ui"]["positions"];

        for (auto& node : s_nodes) {
            std::string key;
            if (node.typeName == NT_PATCH_OUTPUT)
                key = "__output";
            else
                key = node.label;  // label was set to the JSON id

            if (positions.contains(key)) {
                float x = positions[key][0].get<float>();
                float y = positions[key][1].get<float>();
                // App-tracked too: a node hidden inside a group at load is
                // never submitted to imnodes until drill-in, and imnodes
                // forgets unsubmitted nodes — gridPos is the durable copy.
                node.gridPos = ImVec2(x, y);
                node.gridPosKnown = true;
                if (!s_headless)
                    ImNodes::SetNodeGridSpacePos(node.id, ImVec2(x, y));
            }
        }
        // Nodes with NO saved position are the ones paramMap conversion just
        // synthesized (__perf_freq, __curve_N, __mul_N) — the file's ui block
        // predates them. The first placement pass (2026-08-19) put each one
        // at consumer.x - 280 with no collision awareness, which on a dense
        // saved canvas scattered them straight into occupied space — Matt's
        // 2026-08-20 verdict: still "a pile of nodes". So: a CONTROL STRIP.
        // The whole synthesized set lays out in clean dependency columns
        // (perform -> curve -> mul) in empty canvas below everything the
        // file positioned, rows sorted by the y of what they feed so wires
        // don't cross more than they must. Guaranteed collision-free with
        // the saved layout because it starts past the canvas extent.
        {
            // Extent of everything already placed — nodes and group faces.
            float minX = 1e9f, maxY = -1e9f;
            bool any = false;
            for (const auto& c : s_nodes)
                if (c.gridPosKnown) {
                    minX = std::min(minX, c.gridPos.x);
                    maxY = std::max(maxY, c.gridPos.y);
                    any = true;
                }
            for (const auto& g : s_groups) {
                minX = std::min(minX, g.pos.x);
                maxY = std::max(maxY, g.pos.y);
                any = true;
            }
            if (!any) { minX = 0.0f; maxY = 0.0f; }

            // Dependency depth among the UNPLACED set, via links only
            // (dynamicPins point at placed targets, never between synthesized
            // nodes). Iterate to fixpoint; chains are perf->curve->mul, so
            // this settles in 2-3 passes.
            std::unordered_map<int, int> depth;   // node id -> column
            for (const auto& n : s_nodes)
                if (!n.gridPosKnown) depth[n.id] = 0;
            for (bool moved = true; moved; ) {
                moved = false;
                for (const auto& l : s_links) {
                    const GraphNode* src = find_node_for_pin(l.startPinId);
                    const GraphNode* dst = find_node_for_pin(l.endPinId);
                    if (!src || !dst) continue;
                    auto si = depth.find(src->id), di = depth.find(dst->id);
                    if (si == depth.end() || di == depth.end()) continue;
                    if (di->second < si->second + 1) {
                        di->second = si->second + 1;
                        moved = true;
                    }
                }
            }

            // Row order: the y of the placed node each one ultimately feeds
            // (via dynamicPins or links), so a curve lands roughly opposite
            // its target. Unknown feeds sort last.
            auto feed_y = [&](const GraphNode& n) -> float {
                for (const auto& c : s_nodes) {
                    if (!c.gridPosKnown) continue;
                    for (auto& [k, v] : c.dynamicPins.items())
                        if (v.is_object() &&
                            v.value("ref", std::string()) == n.label)
                            return c.gridPos.y;
                }
                for (const auto& l : s_links) {
                    bool fromN = false;
                    for (const auto& p : n.outputs)
                        if (l.startPinId == p.id) fromN = true;
                    if (!fromN) continue;
                    const GraphNode* dst = find_node_for_pin(l.endPinId);
                    if (dst && dst->gridPosKnown) return dst->gridPos.y;
                }
                return 1e9f;
            };

            // Estimated face height from pin count — imnodes only knows real
            // sizes after a frame, and this runs at load. Generous row gaps
            // beat exact numbers.
            auto est_h = [](const GraphNode& n) {
                return 40.0f + 24.0f * float(n.inputs.size() + n.outputs.size());
            };

            std::vector<GraphNode*> strip;
            for (auto& n : s_nodes)
                if (!n.gridPosKnown) strip.push_back(&n);
            std::stable_sort(strip.begin(), strip.end(),
                [&](GraphNode* a, GraphNode* b) {
                    if (depth[a->id] != depth[b->id])
                        return depth[a->id] < depth[b->id];
                    return feed_y(*a) < feed_y(*b);
                });

            const float stripTop = maxY + 260.0f;   // clear of the tallest saved face
            const float colW = 240.0f;
            std::unordered_map<int, float> colY;    // column -> next free y
            for (auto* n : strip) {
                int col = depth[n->id];
                if (!colY.count(col)) colY[col] = stripTop;
                n->gridPos = ImVec2(minX + colW * float(col), colY[col]);
                colY[col] += est_h(*n) + 40.0f;
                n->gridPosKnown = true;
                if (!s_headless)
                    ImNodes::SetNodeGridSpacePos(n->id, n->gridPos);
            }
        }

        // Positions restored — suppress the first-frame grid auto-layout that
        // would otherwise scatter the just-loaded nodes.
        s_needsLayout = false;
    } else {
        s_needsLayout = true;
    }

    // Restore editor pan (so the saved view-of-the-graph reappears).
    if (root.contains("ui") && root["ui"].contains("panning") &&
        root["ui"]["panning"].is_array() && root["ui"]["panning"].size() == 2)
    {
        float px = root["ui"]["panning"][0].get<float>();
        float py = root["ui"]["panning"][1].get<float>();
        ImNodes::EditorContextResetPanning(ImVec2(px, py));
    }

    // Wire all DSP connections (including RefSource for shared sources)
    update_all_dsp();

    if (!unknownTypes.empty()) {
        std::string msg = "Unknown node type(s) not in this build's engine:";
        for (const auto& t : unknownTypes) msg += " " + t;
        msg += " — node(s) loaded inert (no params, no audio)";
        transport_set_status(msg.c_str(), true);
    }
}

// Dialog-driven wrapper for load_graph_from_path.
// Recent-files list: most-recent-first, capped, persisted to mforce_recents.json
// in the current working directory (same neighborhood as imgui.ini).
static constexpr int RECENTS_MAX = 12;
static std::vector<std::string> g_recentFiles;
static const char* RECENTS_PATH = "mforce_recents.json";

static void recents_save() {
    nlohmann::json j = nlohmann::json::array();
    for (const auto& p : g_recentFiles) j.push_back(p);
    std::ofstream f(RECENTS_PATH);
    if (f) f << j.dump(2);
}

// Normalize path so different-looking strings for the same file dedupe
// correctly. Uses weakly_canonical (works for non-existent paths too) and
// forward-slash separators for stable cross-format matching.
static std::string normalize_path(const std::string& p) {
    if (p.empty()) return p;
    try {
        std::filesystem::path fp = std::filesystem::weakly_canonical(p);
        std::string s = fp.string();
        for (char& c : s) if (c == '\\') c = '/';
        return s;
    } catch (...) {
        return p;
    }
}

static void recents_load() {
    g_recentFiles.clear();
    std::ifstream f(RECENTS_PATH);
    if (!f) return;
    try {
        nlohmann::json j; f >> j;
        if (!j.is_array()) return;
        for (const auto& v : j) {
            if (!v.is_string()) continue;
            std::string p = normalize_path(v.get<std::string>());
            // Dedupe loaded list (legacy entries may collide post-normalize).
            if (std::find(g_recentFiles.begin(), g_recentFiles.end(), p) == g_recentFiles.end())
                g_recentFiles.push_back(std::move(p));
        }
        if ((int)g_recentFiles.size() > RECENTS_MAX) g_recentFiles.resize(RECENTS_MAX);
    } catch (...) {}
}

static void recents_push(const std::string& rawPath) {
    if (rawPath.empty()) return;
    std::string path = normalize_path(rawPath);
    // Move-to-front: dedupe by normalized-path equality.
    auto it = std::find(g_recentFiles.begin(), g_recentFiles.end(), path);
    if (it != g_recentFiles.end()) g_recentFiles.erase(it);
    g_recentFiles.insert(g_recentFiles.begin(), path);
    if ((int)g_recentFiles.size() > RECENTS_MAX) g_recentFiles.resize(RECENTS_MAX);
    recents_save();
}

static void load_graph() {
    std::string path = open_file_dialog();
    if (path.empty()) return;
    load_graph_from_path(path);
    recents_push(path);
}

// ===========================================================================
// Persisted UI settings (audition curated-folder path, etc.)
// Tiny JSON next to the exe; read at startup, written when changed.
// ===========================================================================

struct UiSettings {
    std::string curatedFolder = "patches/curated";
    // Per-feature folder memory: each picker remembers ITS last folder
    // (Matt 2026-08-12 — a shared memory made Open/Save/Audition/SaveWAV
    // fight over the location).
    std::string lastOpenDir;
    std::string lastSaveDir;
    std::string lastWavDir;
    std::string lastAuditionDir;
};
static UiSettings g_settings;
static const char* SETTINGS_PATH = "mforce_ui_settings.json";

static void settings_load() {
    std::ifstream f(SETTINGS_PATH);
    if (!f) return;
    try {
        nlohmann::json j; f >> j;
        if (j.contains("curatedFolder") && j["curatedFolder"].is_string())
            g_settings.curatedFolder = j["curatedFolder"].get<std::string>();
        auto readDir = [&](const char* key, std::string& slot) {
            if (j.contains(key) && j[key].is_string()) slot = j[key].get<std::string>();
        };
        readDir("lastOpenDir",     g_settings.lastOpenDir);
        readDir("lastSaveDir",     g_settings.lastSaveDir);
        readDir("lastWavDir",      g_settings.lastWavDir);
        readDir("lastAuditionDir", g_settings.lastAuditionDir);
    } catch (...) {}
}

static std::string* settings_dir_slot(const char* feature) {
    std::string f(feature);
    if (f == "open")     return &g_settings.lastOpenDir;
    if (f == "save")     return &g_settings.lastSaveDir;
    if (f == "wav")      return &g_settings.lastWavDir;
    if (f == "audition") return &g_settings.lastAuditionDir;
    return nullptr;
}

static void settings_save() {
    nlohmann::json j;
    j["curatedFolder"]   = g_settings.curatedFolder;
    j["lastOpenDir"]     = g_settings.lastOpenDir;
    j["lastSaveDir"]     = g_settings.lastSaveDir;
    j["lastWavDir"]      = g_settings.lastWavDir;
    j["lastAuditionDir"] = g_settings.lastAuditionDir;
    std::ofstream f(SETTINGS_PATH);
    if (f) f << j.dump(2);
}

// ===========================================================================
// JSON export
// ===========================================================================

// node_type_to_json removed — typeName strings are used directly

// type_prefix removed — serialized ids are the node labels (stable
// identity, 2026-08-14 spec §1); see sanitize_unique_id.

// Topological sort: dependencies before dependents
static std::vector<GraphNode*> topo_sort() {
    // Build adjacency: for each node, which nodes does it depend on?
    std::unordered_map<int, std::vector<int>> deps; // nodeId → [dependency nodeIds]
    for (auto& node : s_nodes) {
        deps[node.id] = {};
        for (auto& pin : node.inputs) {
            for (auto& link : s_links) {
                int srcPinId = -1;
                if (link.endPinId == pin.id) srcPinId = link.startPinId;
                if (link.startPinId == pin.id) srcPinId = link.endPinId;
                if (srcPinId < 0) continue;

                for (auto& srcNode : s_nodes) {
                    for (auto& sp : srcNode.outputs) {
                        // Tap edges are NOT build-order dependencies: they
                        // may point forward (the loader binds them pass-2),
                        // and counting them here would rotate a loop's
                        // normal refs out of backward order.
                        if (sp.id == srcPinId && !sp.isTap)
                            deps[node.id].push_back(srcNode.id);
                    }
                }
            }
        }
    }

    std::vector<GraphNode*> sorted;
    std::unordered_set<int> visited;

    std::function<void(int)> visit = [&](int nodeId) {
        if (visited.count(nodeId)) return;
        visited.insert(nodeId);
        for (int depId : deps[nodeId])
            visit(depId);
        for (auto& n : s_nodes)
            if (n.id == nodeId) { sorted.push_back(&n); break; }
    };

    for (auto& node : s_nodes)
        visit(node.id);

    return sorted;
}

// Find which node's output is connected to a given input pin
static GraphNode* find_source_node(int inputPinId) {
    for (auto& link : s_links) {
        if (link.endPinId == inputPinId) {
            // startPinId is an output pin — find its node
            for (auto& node : s_nodes)
                for (auto& pin : node.outputs)
                    if (pin.id == link.startPinId) return &node;
        }
        if (link.startPinId == inputPinId) {
            for (auto& node : s_nodes)
                for (auto& pin : node.outputs)
                    if (pin.id == link.endPinId) return &node;
        }
    }
    return nullptr;
}

// Same walk, but returns the source OUTPUT PIN — needed wherever the source
// node alone is ambiguous (a Note face has one output per field).
static Pin* find_source_out_pin(int inputPinId) {
    for (auto& link : s_links) {
        int other = -1;
        if (link.endPinId == inputPinId)   other = link.startPinId;
        if (link.startPinId == inputPinId) other = link.endPinId;
        if (other < 0) continue;
        for (auto& node : s_nodes)
            for (auto& pin : node.outputs)
                if (pin.id == other) return &pin;
    }
    return nullptr;
}

// Serialized id = the node's label, made safe: ids embed in "node.pin"
// paramMap targets (no '.'), "__" is reserved for synthesized keys
// (__output, __param_*, FormantSpectrum "__fN" children), and duplicates
// from a hand-edited file must not collapse two nodes into one id.
static std::string sanitize_unique_id(const std::string& want,
                                      std::unordered_set<std::string>& used,
                                      const std::string& typeName,
                                      bool keepReservedPrefix = false) {
    std::string base = want;
    std::replace(base.begin(), base.end(), '.', '_');
    // The strip guards the reserved namespace against human LABELS. A node
    // whose id arrived from the file already synthesized keeps it.
    if (!keepReservedPrefix)
        while (base.rfind("__", 0) == 0) base.erase(0, 1);
    if (base.empty()) base = node_display_name(typeName);
    std::string name = base;
    for (int i = 2; used.count(name); ++i) name = base + std::to_string(i);
    used.insert(name);
    return name;
}

// All node labels inside a group, including nested groups' members.
static void collect_member_labels(const NodeGroup& g,
                                  std::unordered_set<std::string>& out) {
    for (const auto& m : g.members) {
        if (NodeGroup* child = group_by_name(m)) {
            if (out.insert(m).second) collect_member_labels(*child, out);
        } else {
            out.insert(m);
        }
    }
}

// One boundary-inbound crossing: an outside output pin feeding a member
// node's input pin. The projected group-input pin represents realInPin.
struct GroupBoundaryIn {
    int  outsideOutPin;
    int  realInPin;
    std::string label;  // "member.pin" — the projected pin's display name
};

// Derive the group's interface from the live wiring (spec §3: boundary
// edges ARE the interface, never authored).
static void group_boundary(const NodeGroup& g,
                           std::vector<GroupBoundaryIn>& ins,
                           std::vector<GraphNode*>& outSources) {
    std::unordered_set<std::string> inside;
    collect_member_labels(g, inside);
    auto is_inside = [&](GraphNode* n) {
        return n && inside.count(n->label) > 0;
    };
    outSources.clear();
    ins.clear();
    for (auto& link : s_links) {
        GraphNode* a = find_node_for_pin(link.startPinId);
        GraphNode* b = find_node_for_pin(link.endPinId);
        if (!a || !b) continue;
        Pin* pa = find_pin(link.startPinId);
        Pin* pb = find_pin(link.endPinId);
        if (!pa || !pb) continue;
        GraphNode* srcN = pa->kind == PinKind::Output ? a : b;
        GraphNode* dstN = pa->kind == PinKind::Output ? b : a;
        int srcPin = pa->kind == PinKind::Output ? link.startPinId : link.endPinId;
        int dstPin = pa->kind == PinKind::Output ? link.endPinId : link.startPinId;
        if (!is_inside(srcN) && is_inside(dstN)) {
            Pin* dp = find_pin(dstPin);
            ins.push_back({srcPin, dstPin,
                           dstN->label + "." + (dp ? dp->name : "?")});
        } else if (is_inside(srcN) && !is_inside(dstN)) {
            if (std::find(outSources.begin(), outSources.end(), srcN) ==
                outSources.end())
                outSources.push_back(srcN);
        }
    }
}

// The node whose output IS the group's output: the single member feeding
// outside; with no outward wire, the topologically last member with an
// output pin (Listen needs a tappable output).
static GraphNode* group_output_node(const NodeGroup& g) {
    std::vector<GroupBoundaryIn> ins;
    std::vector<GraphNode*> outs;
    group_boundary(g, ins, outs);
    if (outs.size() == 1) return outs[0];
    if (outs.size() > 1)  return nullptr;  // invalid state; creation refuses it
    std::unordered_set<std::string> inside;
    collect_member_labels(g, inside);
    GraphNode* last = nullptr;
    for (auto* n : topo_sort())
        if (inside.count(n->label) && !n->outputs.empty()) last = n;
    return last;
}

// Rename = identity change: the label is the serialized id, so the
// paramMap stash (which references ids as "node.pin" target strings) must
// be rewritten in the same breath. Graph wiring needs nothing — links are
// integer pin ids. ui.positions keys regenerate from labels on save.
static bool rename_node(GraphNode& node, const std::string& newName,
                        std::string& err) {
    if (newName.empty())              { err = "name is empty"; return false; }
    if (newName.find('.') != std::string::npos)
                                      { err = "'.' not allowed (ids embed in node.pin targets)"; return false; }
    if (newName.rfind("__", 0) == 0)  { err = "'__' prefix is reserved"; return false; }
    for (auto& n : s_nodes)
        if (&n != &node && n.label == newName)
                                      { err = "name already in use: " + newName; return false; }
    if (group_by_name(newName))       { err = "name already in use by a group: " + newName; return false; }
    const std::string oldName = node.label;
    node.label = newName;
    if (oldName != newName && s_loadedParamMap.is_object()) {
        std::string prefix = oldName + ".";
        std::function<void(nlohmann::json&)> fix = [&](nlohmann::json& e) {
            if (e.is_string()) {
                std::string s = e.get<std::string>();
                if (s.rfind(prefix, 0) == 0) e = newName + s.substr(oldName.size());
            } else if (e.is_object() && e.contains("target") && e["target"].is_string()) {
                std::string s = e["target"].get<std::string>();
                if (s.rfind(prefix, 0) == 0) e["target"] = newName + s.substr(oldName.size());
            } else if (e.is_array()) {
                for (auto& t : e) fix(t);
            }
        };
        for (auto& [k, v] : s_loadedParamMap.items()) fix(v);
    }
    if (oldName != newName) {
        // Verbatim-carried extras may embed refs ({"ref": "oldName"} inside
        // e.g. an expandRule) — rewrite them so the rename can't strand one.
        std::function<void(nlohmann::json&)> fixref = [&](nlohmann::json& j) {
            if (j.is_object()) {
                if (j.contains("ref") && j["ref"].is_string() &&
                    j["ref"].get<std::string>() == oldName)
                    j["ref"] = newName;
                for (auto& [k, v] : j.items()) fixref(v);
            } else if (j.is_array()) {
                for (auto& v : j) fixref(v);
            }
        };
        for (auto& n : s_nodes) fixref(n.jsonExtras);
        // Dynamic pins reference their driving CurveNode by label. Renaming
        // that curve without this left a stale ref that save would emit
        // verbatim — and the CLI loader then throws "refs unknown node", so
        // the saved patch stopped RENDERING. Same 2026-08-13 failure family
        // (UI action silently produces a broken file); found in the
        // 2026-08-19 day review, not by the gates, which never simulate a
        // user edit after promotion.
        for (auto& n : s_nodes) fixref(n.dynamicPins);
    }
    if (oldName != newName) {
        // Group membership stores labels — follow the rename.
        for (auto& g : s_groups)
            for (auto& m : g.members)
                if (m == oldName) m = newName;
    }
    mark_graph_dirty();
    return true;
}

// Rename a GROUP: shares the node-id namespace rules (a group name is the
// future reuse-library type name, so it can never collide with a node id).
static bool rename_group(NodeGroup& group, const std::string& newName,
                         std::string& err) {
    if (newName.empty())              { err = "name is empty"; return false; }
    if (newName.find('.') != std::string::npos)
                                      { err = "'.' not allowed"; return false; }
    if (newName.rfind("__", 0) == 0)  { err = "'__' prefix is reserved"; return false; }
    for (auto& n : s_nodes)
        if (n.label == newName)       { err = "name already in use by a node: " + newName; return false; }
    for (auto& g : s_groups)
        if (&g != &group && g.name == newName)
                                      { err = "name already in use: " + newName; return false; }
    const std::string oldName = group.name;
    group.name = newName;
    if (oldName != newName) {
        for (auto& g : s_groups)                 // nesting references
            for (auto& m : g.members)
                if (m == oldName) m = newName;
        for (auto& p : s_groupPath)              // live breadcrumb
            if (p == oldName) p = newName;
    }
    mark_graph_dirty();
    return true;
}

// tapOverride: emit the Listen-tap node as graph.output (playback TEMP file
// only — a user-facing Save must never persist the tap).
static void save_patch_graph(const std::string& path, bool tapOverride = false) {
    using json = nlohmann::json;

    // Assign string IDs to nodes — the label IS the id (stable identity).
    std::unordered_map<int, std::string> nodeIds;
    std::unordered_set<std::string> usedIds;
    for (auto& node : s_nodes) {
        if (node.typeName == NT_PATCH_OUTPUT || node.typeName == NT_PARAMETER)
            continue;
        nodeIds[node.id] = sanitize_unique_id(node.label, usedIds, node.typeName,
                                              node.synthesizedId);
    }

    // Note faces (editor-only): the FILE keeps one PerformNode per field.
    // Give each WIRED face pin its serialized per-field node id — the id it
    // arrived under (perfFieldIds), or a fresh __perf_<field> — and collect
    // those nodes for PREPENDING, so refs resolve against earlier nodes
    // exactly as the converter guarantees.
    std::unordered_map<int, std::string> perfPinRef;   // face pin id -> file id
    json perfNodes = json::array();
    for (auto& node : s_nodes) {
        if (node.typeName != NT_PERFORM) continue;
        for (auto& p : node.outputs) {
            if (!is_pin_connected(p.id)) continue;
            std::string fid;
            if (auto it = node.perfFieldIds.find(p.name);
                it != node.perfFieldIds.end() && !usedIds.count(it->second))
                fid = it->second;
            if (fid.empty()) {
                std::string base = "__perf_" + p.name;
                fid = base;
                for (int i = 2; usedIds.count(fid); ++i)
                    fid = base + std::to_string(i);
            }
            usedIds.insert(fid);
            node.perfFieldIds[p.name] = fid;   // stable on the next save
            perfPinRef[p.id] = fid;
            perfNodes.push_back({{"id", fid}, {"type", NT_PERFORM},
                                 {"params", {{"field", p.name}}}});
        }
    }

    // Find the Output node
    GraphNode* outputNode = nullptr;
    for (auto& node : s_nodes)
        if (node.typeName == NT_PATCH_OUTPUT) outputNode = &node;

    // Determine graph output: what's connected to Output's source pin
    std::string outputId;
    if (outputNode && !outputNode->inputs.empty()) {
        GraphNode* src = find_source_node(outputNode->inputs[0].id);
        if (src) outputId = nodeIds[src->id];
    }

    // Old-id → serialized-id map. Ids are labels now, so this is identity
    // except where sanitize_unique_id had to rename (collision / illegal
    // name) — exactly when the paramMap stash still needs the remap.
    std::unordered_map<std::string, std::string> oldToNew;
    for (auto& node : s_nodes) {
        auto it = nodeIds.find(node.id);
        if (it != nodeIds.end() && !node.label.empty())
            oldToNew[node.label] = it->second;
    }
    auto remap_target = [&](const std::string& tgt) -> std::string {
        auto dot = tgt.find('.');
        if (dot == std::string::npos) return tgt;
        auto it = oldToNew.find(tgt.substr(0, dot));
        return it == oldToNew.end() ? tgt : it->second + tgt.substr(dot);
    };
    std::function<json(const json&)> remap_entry = [&](const json& e) -> json {
        if (e.is_string()) return remap_target(e.get<std::string>());
        if (e.is_object()) {
            json o = e;
            if (o.contains("target") && o["target"].is_string())
                o["target"] = remap_target(o["target"].get<std::string>());
            return o;
        }
        if (e.is_array()) {
            json a = json::array();
            for (const auto& t : e) a.push_back(remap_entry(t));
            return a;
        }
        return e;
    };

    // paramMap: the stash is the model (spec §2) — emit it verbatim through
    // the collision remap. The old link-derived path (trace Parameter-node
    // wires) is gone with the Parameter nodes themselves.
    json paramMap = json::object();
    if (s_loadedParamMap.is_object())
        for (auto& [pname, entry] : s_loadedParamMap.items())
            paramMap[pname] = remap_entry(entry);

    // Build graph nodes array (topologically sorted)
    auto sorted = topo_sort();
    json nodes = json::array();
    for (auto* nodePtr : sorted) {
        auto& node = *nodePtr;
        if (node.typeName == NT_PATCH_OUTPUT || node.typeName == NT_PARAMETER)
            continue;
        // Note faces are editor-only: their file form is the per-field
        // PerformNode set in perfNodes, prepended below.
        if (node.typeName == NT_PERFORM)
            continue;

        json jnode;
        jnode["id"] = nodeIds[node.id];
        jnode["type"] = node.typeName;

        json params = json::object();

        // CurveNode knots/interp are modeled, not carried — emitted from the
        // node so an edit in the curve editor survives the save.
        if (node.typeName == "CurveNode") {
            json knots = json::array();
            for (const auto& [x, y] : node.curveKnots)
                knots.push_back(json::array({x, y}));
            params["knots"] = knots;
            params["interp"] = node.curveInterp == 2 ? "loglog"
                             : node.curveInterp == 1 ? "logx" : "linear";
            // Expressions mode rides its own keys; the typed coefficients
            // round-trip, never evaluated values (the 3n fidelity lesson).
            if (node.curveExprMode || !node.curveExprKnots.empty()) {
                params["mode"] = node.curveExprMode ? "expressions" : "points";
                json ek = json::array();
                for (const auto& k : node.curveExprKnots)
                    ek.push_back({{"x", k.x},
                                  {"form", k.form == CurveNode::KnotForm::Power
                                           ? "power" : "linear"},
                                  {"a", k.a}, {"b", k.b}});
                params["exprKnots"] = ek;
            }
        }

        for (auto& pin : node.inputs) {
            bool isChannelPin = (pin.name.substr(0, 3) == "ch ");
            if (isChannelPin) continue;

            if (pin.multi) {
                // Multi-input pin: collect all connected sources as array of refs
                json refs = json::array();
                for (auto& link : s_links) {
                    int outPinId = -1;
                    if (link.endPinId == pin.id) outPinId = link.startPinId;
                    if (link.startPinId == pin.id) outPinId = link.endPinId;
                    if (outPinId < 0) continue;
                    GraphNode* srcNode = find_node_for_pin(outPinId);
                    Pin* srcPin = find_pin(outPinId);
                    if (!srcNode || !srcPin || srcPin->kind != PinKind::Output)
                        continue;
                    if (srcNode->typeName == NT_PERFORM) {
                        if (auto pr = perfPinRef.find(srcPin->id);
                            pr != perfPinRef.end())
                            refs.push_back(json{{"ref", pr->second}});
                    } else if (nodeIds.count(srcNode->id)) {
                        // Tap wires save as {"tap": id} — previous-sample
                        // reads, not pulls (feedback_loop_design.md §4.1).
                        refs.push_back(json{{srcPin->isTap ? "tap" : "ref",
                                             nodeIds[srcNode->id]}});
                    }
                }
                if (!refs.empty()) params[pin.name] = refs;
            } else {
                GraphNode* src = find_source_node(pin.id);
                // A Note face's ref is PER PIN (one file node per field).
                auto ref_for = [&](GraphNode* s) -> json {
                    if (s->typeName == NT_PERFORM) {
                        if (Pin* op = find_source_out_pin(pin.id))
                            if (auto pr = perfPinRef.find(op->id);
                                pr != perfPinRef.end())
                                return json{{"ref", pr->second}};
                        return json();   // unwired face pin: fall to default
                    }
                    if (nodeIds.count(s->id)) {
                        // Tap wires save as {"tap": id} (previous-sample
                        // read); the feeding OUT pin knows which kind it is.
                        Pin* op = find_source_out_pin(pin.id);
                        return json{{op && op->isTap ? "tap" : "ref",
                                     nodeIds[s->id]}};
                    }
                    return json();
                };
                if (pin.inputOnly) {
                    json r = src ? ref_for(src) : json();
                    if (!r.is_null())
                        params[pin.name] = r;
                    else if (!src && pin.hasConstant)
                        params[pin.name] = pin.defaultValue;
                } else if (src) {
                    json r = (src->typeName == NT_PARAMETER) ? json() : ref_for(src);
                    if (!r.is_null()) params[pin.name] = r;
                    else              params[pin.name] = pin.defaultValue;
                } else {
                    params[pin.name] = pin.defaultValue;
                }
            }
        }
        if (!params.empty()) jnode["params"] = params;

        // Dynamic pins: the settings this node drives once per note. Their
        // refs name node ids, which sanitize_unique_id may have renamed, so
        // they go through the same oldToNew map the paramMap targets used.
        if (!node.dynamicPins.empty()) {
            json dp = json::object();
            for (auto& [key, val] : node.dynamicPins.items()) {
                json entry = val;
                if (entry.is_object() && entry.contains("ref") &&
                    entry["ref"].is_string()) {
                    auto it = oldToNew.find(entry["ref"].get<std::string>());
                    if (it != oldToNew.end()) entry["ref"] = it->second;
                }
                dp[key] = entry;
            }
            jnode["dynamicPins"] = dp;
        }

        // Envelope: emit multi-stage form
        if (node.typeName == NT_ENVELOPE) {
            if (auto* env = dynamic_cast<Envelope*>(node.dspSource.get())) {
                if (!jnode.contains("params")) jnode["params"] = json::object();
                jnode["params"]["stages"] = envelope_stages_to_json(*env);
                // Literal-seconds envelopes (make_adsr_abs) must round-trip:
                // without this key the reload treats percent as a fraction of
                // note duration (2 ms attack -> 0.2% of the note; the
                // 2026-08-10 live-audition click + stretched-tail bug).
                jnode["params"]["timeMode"] = env->absolute_time ? "seconds" : "fraction";
            }
        }

        // FormantSpectrum: synthesize a bare Formant graph node per row and
        // emit params.formants as a ref-array. On-disk format matches
        // hand-written patches; the synthesized nodes are hidden from the UI.
        if (node.typeName == "FormantSpectrum" && !node.formantRows.empty()) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            json refs = json::array();
            const std::string& specId = nodeIds[node.id];
            for (size_t i = 0; i < node.formantRows.size(); ++i) {
                const auto& row = node.formantRows[i];
                // Keep the consumed child's ORIGINAL id — paramMap curves
                // target formants by id (3n node:f1); __fN only for rows
                // created in the UI.
                std::string fid = row.srcId.empty()
                    ? specId + "__f" + std::to_string(i)
                    : sanitize_unique_id(row.srcId, usedIds, "Formant");
                json fnode;
                fnode["id"] = fid;
                fnode["type"] = "Formant";
                fnode["params"] = {
                    {"frequency", row.frequency}, {"gain", row.gain},
                    {"width", row.width}, {"power", row.power},
                };
                nodes.push_back(fnode);
                refs.push_back(json{{"ref", fid}});
            }
            jnode["params"]["formants"] = refs;
        }

        // Config values
        for (auto& [desc, val] : node.settingValues) {
            // Envelope sustain lives INSIDE the emitted stages (slot
            // values); writing the config too made reload rewrite the slots
            // through set_setting's [0,1] clamp, breaking >1.0 test
            // envelopes (_envacc_test) — and 0.0 artifacts of the old
            // get_setting broke three library patches (3n).
            if (node.typeName == NT_ENVELOPE &&
                std::string_view(desc.name) == "sustainLevel")
                continue;
            if (!jnode.contains("params")) jnode["params"] = json::object();
            if (desc.type == SettingType::Bool)
                jnode["params"][desc.name] = (val != 0.0f);
            else if (desc.type == SettingType::Int)
                jnode["params"][desc.name] = int(val);
            else
                jnode["params"][desc.name] = val;
        }

        // Array values (ExplicitPartials mult/ampl, Fixed/BandSpectrum gains, …)
        for (auto& [desc, vec] : node.arrayValues) {
            if (vec.empty()) continue;
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"][desc.name] = vec;
        }

        // Re-emit the loaded seed so the engine loader's RNG stream is
        // reproducible across UI save / playback-temp-JSON round trips.
        if (node.jsonSeed >= 0) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["seed"] = node.jsonSeed;
        }

        // Unmodeled params carried verbatim (jsonExtras, 3n). They win over
        // the modeled emission for the same key: extras only exist for keys
        // the UI never displayed, so the file's value is the truth; editing
        // a same-named pin/config erases the extras entry first.
        if (!node.jsonExtras.empty()) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            for (auto& [k, v] : node.jsonExtras.items())
                jnode["params"][k] = v;
        }

        nodes.push_back(jnode);
    }

    // Listen tap: the playback temp file monitors the tapped node instead
    // of the patch output. The full graph is still emitted, so everything
    // upstream keeps running exactly as wired (in-context semantics).
    if (tapOverride && s_listenTapNode >= 0 && nodeIds.count(s_listenTapNode))
        outputId = nodeIds[s_listenTapNode];

    // Build final JSON
    json root;
    root["sampleRate"] = 48000;
    // Per-field PerformNode file nodes PREPENDED — refs must resolve
    // against earlier nodes (same rule as the converter).
    if (!perfNodes.empty()) {
        json merged = perfNodes;
        for (auto& n : nodes) merged.push_back(std::move(n));
        nodes = std::move(merged);
    }
    root["graph"]["nodes"] = nodes;
    root["graph"]["output"] = outputId;

    if (outputNode) {
        // Fields the UI doesn't model (release, volume, ...) pass through
        // verbatim; UI-owned keys below overwrite.
        for (auto it = s_loadedInstrumentExtras.begin();
             it != s_loadedInstrumentExtras.end(); ++it)
            root["instrument"][it.key()] = it.value();
        // Listen tap: instrument volume is leveled for the PATCH OUTPUT (a
        // resonator's gain, an auto-leveled mix); a mid-chain monitor point
        // never went through that leveling, so the same volume renders raw
        // excitation taps 20-30 dB down — Matt heard them as "silent"
        // (backlog 32, the non-engine half). Monitor at unity instead.
        if (tapOverride && s_listenTapNode >= 0)
            root["instrument"]["volume"] = 1.0f;
        root["instrument"]["polyphony"] = outputNode->polyphony;
        if (!paramMap.empty())
            root["instrument"]["paramMap"] = paramMap;
    }

    // Score: preserve the loaded patch's score/seconds verbatim. Only fall
    // back to the default note when the loaded patch had none (new graphs) —
    // the old unconditional default silently destroyed hand-authored scores
    // on Save and made CLI renders of UI-saved patches play the wrong note.
    if (s_loadedScore.is_array() && !s_loadedScore.empty()) {
        root["score"] = s_loadedScore;
        if (s_loadedSeconds.is_number())
            root["seconds"] = s_loadedSeconds;
        else
            root["seconds"] = 3.0f;
    } else if (s_loadedScoreAbsent) {
        // The loaded file had no score: keep it that way (3n root:score).
        if (s_loadedSeconds.is_number())
            root["seconds"] = s_loadedSeconds;
    } else {
        root["seconds"] = 3.0f;
        root["score"] = json::array({
            json{
                {"note",     60},
                {"velocity", 0.8f},
                {"time",     0.0f},
                {"duration", 2.0f}
            }
        });
    }

    // Groups (spec §3): engine-blind root section, emitted verbatim from
    // the model. Names double as the future reuse-library type names.
    if (!s_groups.empty()) {
        json jgroups = json::array();
        for (const auto& g : s_groups) {
            json jg;
            jg["name"] = g.name;
            jg["members"] = g.members;
            jg["pos"] = {g.pos.x, g.pos.y};
            jgroups.push_back(std::move(jg));
        }
        root["groups"] = std::move(jgroups);
    }

    // Save UI layout (skip under headless round-trip — no live editor).
    if (!s_headless) {
        json positions = json::object();
        for (auto* nodePtr : sorted) {
            if (nodePtr->typeName == NT_PATCH_OUTPUT || nodePtr->typeName == NT_PARAMETER)
                continue;
            // gridPos is the durable copy — imnodes has already forgotten
            // any node currently hidden by group drill-in.
            ImVec2 pos = nodePtr->gridPosKnown
                ? nodePtr->gridPos
                : ImNodes::GetNodeGridSpacePos(nodePtr->id);
            positions[nodeIds[nodePtr->id]] = {pos.x, pos.y};
        }
        if (outputNode) {
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(outputNode->id);
            positions["__output"] = {pos.x, pos.y};
        }
        root["ui"]["positions"] = positions;
        ImVec2 pan = ImNodes::EditorContextGetPanning();
        root["ui"]["panning"] = {pan.x, pan.y};

        // Note-face identity: which per-field file nodes belong to which
        // editor face. UI-owned; the engine loader never reads it. Without
        // it a re-load merges all perf nodes into one face.
        json faces = json::array();
        for (auto& n : s_nodes) {
            if (n.typeName != NT_PERFORM || n.perfFieldIds.empty()) continue;
            json ff = json::object();
            for (auto& [f, fid] : n.perfFieldIds) ff[f] = fid;
            faces.push_back({{"label", nodeIds[n.id]}, {"fields", ff}});
        }
        if (!faces.empty()) root["ui"]["noteFaces"] = faces;
    }

    std::ofstream f(path);
    f << root.dump(2);
    f.close();
}

static void save_node_graph(const std::string& path) {
    using json = nlohmann::json;

    // Assign string IDs — label IS the id (stable identity).
    std::unordered_map<int, std::string> nodeIds;
    std::unordered_set<std::string> usedIds;
    for (auto& node : s_nodes)
        nodeIds[node.id] = sanitize_unique_id(node.label, usedIds, node.typeName,
                                              node.synthesizedId);

    // Find mixer for output
    std::string outputId;
    for (auto& node : s_nodes)
        if (node.typeName == NT_STEREO_MIXER)
            outputId = nodeIds[node.id];

    // Build nodes (topologically sorted)
    auto sorted = topo_sort();
    json nodes = json::array();
    for (auto* nodePtr : sorted) {
        auto& node = *nodePtr;
        json jnode;
        jnode["id"] = nodeIds[node.id];
        jnode["type"] = node.typeName;

        json params = json::object();
        for (auto& pin : node.inputs) {
            bool isChannelPin = (pin.name.substr(0, 3) == "ch ");

            GraphNode* src = find_source_node(pin.id);

            if (node.typeName == NT_SOUND_CHANNEL && pin.name == "source") {
                if (src) jnode["inputs"]["source"] = nodeIds[src->id];
            } else if (isChannelPin && node.typeName == NT_STEREO_MIXER) {
                if (src) {
                    if (!jnode.contains("inputs") || !jnode["inputs"].contains("channels"))
                        jnode["inputs"]["channels"] = json::array();
                    jnode["inputs"]["channels"].push_back(nodeIds[src->id]);
                }
            } else if (pin.multi) {
                json refs = json::array();
                for (auto& link : s_links) {
                    int outPinId = -1;
                    if (link.endPinId == pin.id) outPinId = link.startPinId;
                    if (link.startPinId == pin.id) outPinId = link.endPinId;
                    if (outPinId < 0) continue;
                    GraphNode* srcNode = find_node_for_pin(outPinId);
                    Pin* srcPin = find_pin(outPinId);
                    if (srcNode && srcPin && srcPin->kind == PinKind::Output)
                        refs.push_back(json{{srcPin->isTap ? "tap" : "ref",
                                             nodeIds[srcNode->id]}});
                }
                if (!refs.empty()) params[pin.name] = refs;
            } else if (pin.inputOnly) {
                Pin* op = find_source_out_pin(pin.id);
                if (src)
                    params[pin.name] = json{{op && op->isTap ? "tap" : "ref",
                                             nodeIds[src->id]}};
                else if (pin.hasConstant)
                    params[pin.name] = pin.defaultValue;
            } else {
                Pin* op = find_source_out_pin(pin.id);
                if (src)
                    params[pin.name] = json{{op && op->isTap ? "tap" : "ref",
                                             nodeIds[src->id]}};
                else
                    params[pin.name] = pin.defaultValue;
            }
        }
        if (!params.empty()) jnode["params"] = params;

        if (node.typeName == NT_ENVELOPE) {
            if (auto* env = dynamic_cast<Envelope*>(node.dspSource.get())) {
                if (!jnode.contains("params")) jnode["params"] = json::object();
                jnode["params"]["stages"] = envelope_stages_to_json(*env);
                // Literal-seconds envelopes (make_adsr_abs) must round-trip:
                // without this key the reload treats percent as a fraction of
                // note duration (2 ms attack -> 0.2% of the note; the
                // 2026-08-10 live-audition click + stretched-tail bug).
                jnode["params"]["timeMode"] = env->absolute_time ? "seconds" : "fraction";
            }
        }

        if (node.typeName == "FormantSpectrum" && !node.formantRows.empty()) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            json refs = json::array();
            const std::string& specId = nodeIds[node.id];
            for (size_t i = 0; i < node.formantRows.size(); ++i) {
                const auto& row = node.formantRows[i];
                // Keep the consumed child's ORIGINAL id — paramMap curves
                // target formants by id (3n node:f1); __fN only for rows
                // created in the UI.
                std::string fid = row.srcId.empty()
                    ? specId + "__f" + std::to_string(i)
                    : sanitize_unique_id(row.srcId, usedIds, "Formant");
                json fnode;
                fnode["id"] = fid;
                fnode["type"] = "Formant";
                fnode["params"] = {
                    {"frequency", row.frequency}, {"gain", row.gain},
                    {"width", row.width}, {"power", row.power},
                };
                nodes.push_back(fnode);
                refs.push_back(json{{"ref", fid}});
            }
            jnode["params"]["formants"] = refs;
        }

        for (auto& [desc, val] : node.settingValues) {
            // Envelope sustain lives INSIDE the emitted stages (slot
            // values); writing the config too made reload rewrite the slots
            // through set_setting's [0,1] clamp, breaking >1.0 test
            // envelopes (_envacc_test) — and 0.0 artifacts of the old
            // get_setting broke three library patches (3n).
            if (node.typeName == NT_ENVELOPE &&
                std::string_view(desc.name) == "sustainLevel")
                continue;
            if (!jnode.contains("params")) jnode["params"] = json::object();
            if (desc.type == SettingType::Bool)
                jnode["params"][desc.name] = (val != 0.0f);
            else if (desc.type == SettingType::Int)
                jnode["params"][desc.name] = int(val);
            else
                jnode["params"][desc.name] = val;
        }

        // Array values (ExplicitPartials mult/ampl, Fixed/BandSpectrum gains, …)
        for (auto& [desc, vec] : node.arrayValues) {
            if (vec.empty()) continue;
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"][desc.name] = vec;
        }

        // Re-emit the loaded seed (see save_patch_graph).
        if (node.jsonSeed >= 0) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["seed"] = node.jsonSeed;
        }

        // Unmodeled params carried verbatim (jsonExtras, 3n). They win over
        // the modeled emission for the same key: extras only exist for keys
        // the UI never displayed, so the file's value is the truth; editing
        // a same-named pin/config erases the extras entry first.
        if (!node.jsonExtras.empty()) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            for (auto& [k, v] : node.jsonExtras.items())
                jnode["params"][k] = v;
        }

        nodes.push_back(jnode);
    }

    json root;
    root["sampleRate"] = 48000;
    root["seconds"] = 5;
    root["graph"]["nodes"] = nodes;
    root["graph"]["output"] = outputId;

    // Groups — same engine-blind section as the patch save.
    if (!s_groups.empty()) {
        json jgroups = json::array();
        for (const auto& g : s_groups) {
            json jg;
            jg["name"] = g.name;
            jg["members"] = g.members;
            jg["pos"] = {g.pos.x, g.pos.y};
            jgroups.push_back(std::move(jg));
        }
        root["groups"] = std::move(jgroups);
    }

    // Save UI layout
    json positions = json::object();
    for (auto* nodePtr : sorted) {
        ImVec2 pos = nodePtr->gridPosKnown
            ? nodePtr->gridPos
            : ImNodes::GetNodeGridSpacePos(nodePtr->id);
        positions[nodeIds[nodePtr->id]] = {pos.x, pos.y};
    }
    root["ui"]["positions"] = positions;
    {
        ImVec2 pan = ImNodes::EditorContextGetPanning();
        root["ui"]["panning"] = {pan.x, pan.y};
    }

    std::ofstream f(path);
    f << root.dump(2);
    f.close();
}

static void save_to_path(const std::string& path) {
    if (s_graphMode == GraphMode::PatchGraph)
        save_patch_graph(path);
    else
        save_node_graph(path);
    s_currentFilePath = path;
    s_graphDirty = false;
    recents_push(path);  // saved patches are equally worth remembering
}

// Type predicates shared by render-time snapshot capture and the strip
// dispatch in draw_waveform_window.
static bool is_partials_type(const std::string& t) {
    return t == "FullPartials" || t == "SequencePartials" || t == "ExplicitPartials";
}
static bool is_formant_type(const std::string& t) {
    return t == "Formant" || t == "FormantSpectrum"
        || t == "FormantSequence" || t == "BandSpectrum";
}

// Per-render snapshot of envelope-driven inputs feeding evolving (Partials/
// Formant) nodes. Indexed by node id, then by input pin name (e.g. "multEnv",
// "amplEnv", "frequency"). Each timeline holds EVO_SNAP_COUNT samples evenly
// spaced across the rendered note duration. Empty until first render.
static constexpr int EVO_SNAP_COUNT = 100;
static std::unordered_map<int, std::unordered_map<std::string, std::vector<float>>>
    g_evoSnapshots;

// Scrubber position (0..1) — at 0, strip shows static endpoints only; at >0,
// strip overlays a third bar/curve at the snapshot value for the corresponding
// time fraction along the rendered note.
static float g_scrubberPos = 0.0f;

// Returns the path to a patch file reflecting current UI state — either
// s_currentFilePath (if no edits pending) or a temp file we sync-write to.
// Playback paths should use this instead of s_currentFilePath directly so
// MultiplexSource and other load-time-baked constructs see current edits.
static std::string get_playback_patch_path() {
    // A live Listen tap always goes through the temp file — the saved patch
    // must keep its real output, the tap only exists in the playback copy.
    if (s_graphMode == GraphMode::PatchGraph && s_listenTapNode >= 0) {
        std::string tmp = (std::filesystem::temp_directory_path() / "mforce_playback.json").string();
        save_patch_graph(tmp, true);
        return tmp;
    }
    // Use the saved file if one exists and the graph hasn't been edited.
    if (!s_graphDirty && !s_currentFilePath.empty()) return s_currentFilePath;
    // Otherwise (edited since last save, or never saved) write current state
    // to a temp file and return that path. User's explicit save file is
    // untouched until they explicitly Save.
    //
    // NOTE: s_graphDirty is deliberately NOT cleared here. It used to be, as a
    // "temp file is up to date" optimization, but that conflated two meanings:
    // clearing it made later playback/generate calls fall back to the on-disk
    // s_currentFilePath (silently discarding all edits since the last Save),
    // and it suppressed the unsaved-changes prompt on close. Re-serializing on
    // every playback call is a few ms of JSON write — correctness wins.
    std::string tmp = (std::filesystem::temp_directory_path() / "mforce_playback.json").string();
    if (s_graphMode == GraphMode::PatchGraph) save_patch_graph(tmp);
    else save_node_graph(tmp);
    return tmp;
}

static void save_graph_as() {
    std::string path = save_file_dialog();
    if (!path.empty())
        save_to_path(path);
}

static void save_graph() {
    if (s_currentFilePath.empty())
        save_graph_as();
    else
        save_to_path(s_currentFilePath);
}

// ===========================================================================
// Audio: poll-driven streaming via waveOut
// Buffers are filled from the main loop each frame — no callbacks,
// no threading issues, no deadlocks on quit.
// ===========================================================================

static constexpr int AUDIO_SAMPLE_RATE = 48000;
// RtAudio buffer size — smaller = lower latency but more callback overhead.
// 512 @ 48k = ~10.7ms. Matches typical WASAPI shared-mode period, giving
// the audio callback enough cushion for occasional jitter without underrun.
static constexpr unsigned int AUDIO_BUF_FRAMES = 512;

// ===========================================================================
// Offline waveform display buffers
// ===========================================================================
static std::vector<float> g_outputWaveform;  // final output waveform
static int g_waveformSamples = 0;            // number of samples in waveform buffers

static int g_waveZoom = 1;        // samples per pixel (1 = most zoomed in)
static int g_waveScrollPos = 0;   // starting sample offset into available data
static bool g_waveViewInited = false;  // fit-to-buffer has run at least once
static int g_waveColumns = 1;     // number of columns for waveform tiling

// Post-render view policy: auto-fit only the FIRST render — after that a
// Generate keeps the user's zoom and scroll so a zoomed-in region can be
// watched across regenerations (the draw loop clamps scroll to the new
// buffer length). The explicit "fit" button still refits any time.
static void wave_view_after_render(int samples) {
    if (g_waveViewInited) return;
    g_waveScrollPos = 0;
    g_waveZoom = std::max(1, samples / 800);
    g_waveViewInited = true;
}
static bool g_showEnvelopes = true;  // header toggle — hide envelope-category strips

// Pop-out waveform: one independent ImGui window per entry, each with its own
// zoom/scroll. nodeId == -1 means the main Output buffer; otherwise a GraphNode id.
struct WavePopout {
    int         nodeId;   // -1 for Output, else GraphNode::id
    std::string label;    // cached for window title; buffer re-resolved each frame
    bool        open{true};
    int         zoom{1};
    int         scroll{0};
};
static std::vector<WavePopout> g_wavePopouts;

// Spectrum view of the output buffer — recomputed after each render.
// Magnitudes are stored in dB (20*log10(|X[k]|/refRMS)); g_outputSpectrumN is
// the number of magnitude bins (= FFT size / 2). Empty when no render yet.
static std::vector<float> g_outputSpectrumDb;
static int                g_outputSpectrumN  = 0;
static int                g_outputSpectrumSR = 48000;
// Forward decl — defined alongside draw_spectrum_window below.
static void compute_output_spectrum();

// ===========================================================================
// Transport state
// ===========================================================================
enum class PlayMode { Note, Passage, Chords, Drums };

struct TransportState {
    PlayMode mode = PlayMode::Note;
    // Note mode
    char noteStr[16] = "C4";
    float velocity = 0.8f;
    float duration = 2.0f;
    // Passage mode
    char passageStr[256] = "";
    int octave = 4;
    float bpm = 120.0f;
    // Chords mode
    char chordsStr[256] = "";
    char defChordGrp[64] = "";
    char figure[64] = "";
    int inversion = 0;
    int spread = 0;
    float chordDelay = 0.0f;
    // Drums mode
    char pattern[256] = "";
    char drumMap[512] = "KK=patches/kick_drum.json;SN=patches/snare_drum.json";
    int repeats = 2;
    // Shared
    bool noteMode = false;
    // Status message (shown in transport panel)
    char statusMsg[256] = "";
    bool statusIsError = false;
};
static TransportState g_transport;

// Generate-button state machine. 3 phases so the "Generating..." label and
// the waveform clear are both visible BEFORE the blocking render runs:
//   0 = idle
//   1 = click captured, transition to 2 at next frame start
//   2 = draw "Generating..." + cleared waveform this frame; block after swap
// After generation: back to 0.
static int s_genState = 0;

// RtAudio: callback-based audio I/O. The audio thread is created and managed
// by RtAudio itself; our callback (audio_callback) is invoked on it whenever
// the device needs more samples. UI-thread mutation of g_voices / g_streamSource
// / g_bufferPlayback must be serialized with the callback's reads via
// g_audioMutex (declared up with update_node_dsp — graph rewiring holds it too).
static std::unique_ptr<RtAudio> g_audio;

// Diagnostic peak meters — sampled per-buffer by the audio callback, displayed
// by the UI. Pre-clip = peak the mixer produced BEFORE soft_clip; post-clip =
// what actually went out. If pre > 1 but post ≈ 1, soft_clip is shaping. If
// pre is small but distortion is audible, the distortion isn't amplitude-based.
static std::atomic<float> g_audioPeakPre{0.0f};
static std::atomic<float> g_audioPeakPost{0.0f};
static std::atomic<int>   g_audioActiveVoices{0};
// Incremented every audio_callback invocation. The UI-thread watchdog
// (audio_watchdog) reopens the stream when this flatlines — WASAPI streams
// die silently when another app grabs the device or changes its sample rate
// (2026-08-10: playing a video in the browser killed audio until restart).
static std::atomic<uint64_t> g_audioHeartbeat{0};
// Focus-regain restart request (main loop sets, watchdog services): WASAPI
// streams can go ZOMBIE after another app reconfigures the device — writes
// succeed, callback keeps ticking, nothing renders. Undetectable from inside,
// so we proactively restart the stream when the window regains focus after
// being away (the exact switch-to-video-and-back moment; ~100 ms, and nothing
// is playing right then anyway).
static std::atomic<bool> g_audioRestartRequest{false};
// Last async error RtAudio reported (WASAPI thread death reasons land here).
static std::mutex g_audioErrMutex;
static std::string g_lastAudioError;
static void on_rtaudio_error(RtAudioErrorType /*type*/, const std::string& text) {
    std::lock_guard<std::mutex> lk(g_audioErrMutex);
    g_lastAudioError = text;
}

static ValueSource* g_streamSource = nullptr;
static int g_streamRemaining = 0;
static float g_streamVelocity = 0.5f;

// Node-graph stereo stream (Channel → Mixer graphs, no instrument block).
// Raw taps into the UI's in-memory DSP graph, pulled per-sample by the audio
// callback with engine StereoMixer::render semantics (volume, equal-power
// pan, gainL/gainR, soft_clip at the mix). Because these are the same live
// objects the Properties panel mutates (constantSrc->set / update_node_dsp),
// parameter tweaks during streaming are audible immediately — identical to
// the patch-mode g_streamSource path. Non-empty vector == stream active.
struct StreamChannel {
    ValueSource* source = nullptr;   // Channel's "source" input (required)
    ValueSource* volume = nullptr;   // Channel "volume" (node or pin constant)
    ValueSource* pan    = nullptr;   // Channel "pan"    (node or pin constant)
};
static std::vector<StreamChannel> g_streamChannels;
static ValueSource* g_streamGainL = nullptr;
static ValueSource* g_streamGainR = nullptr;

// Buffer playback: stream from a pre-rendered buffer (e.g. g_outputWaveform)
static const float* g_bufferPlayback = nullptr;
static int g_bufferPlaybackPos = 0;
static int g_bufferPlaybackLen = 0;

// Polyphonic voice pool: pre-rendered note buffers that mix together
static constexpr int MAX_VOICES = 16;
struct Voice {
    // Streaming voice: DSP graph is pulled per-sample in fill_audio_buffer
    // rather than pre-rendered into a buffer. patch keeps the instrument
    // (and thus the whole DSP graph) alive while source is in use.
    std::shared_ptr<InstrumentPatch> patch;
    std::shared_ptr<ValueSource>     source;
    int   samplesRemaining = 0;
    float gain = 1.0f;
    // Live-gated note (key held): sustains until key-up fires
    // gate_release() on its envelopes (note-contained sound, 2026-08-13 —
    // release is the final envelope stage, so no fade/reclaim plumbing).
    // envs are non-owning; the patch shared_ptr keeps the graph alive.
    bool  held = false;
    std::vector<mforce::Envelope*> envs;
    bool  active = false;
    int   midiNote = 0;  // for keyboard highlight
    // Which of patch->instrument's pool slots this voice occupies; -1 = not
    // pool-backed. Deactivation must release it (voice_deactivate_unlocked /
    // the audio callback's finish path) or the slot leaks.
    int   poolSlot = -1;
    // P3 sample clock: ticked once per sample by the audio callback before
    // source->next(), so bend and wheel/pressure move during the note. Null
    // for voices with no perform context.
    std::shared_ptr<mforce::PerformSource> performSource;
    // Tap-only loop tails (feedback_loop_design.md §3.3): ticked once per
    // sample AFTER source->next(), or tap-closed feedback loops fall silent.
    std::vector<std::shared_ptr<mforce::ValueSource>> advanceList;
};
static Voice g_voices[MAX_VOICES];

// Deactivate a voice and return its pool slot. Caller holds g_audioMutex.
// Flag writes only — shared_ptr destruction stays with voice_gc (UI thread).
static void voice_deactivate_unlocked(Voice& v) {
    if (v.active && v.poolSlot >= 0 && v.patch && v.patch->instrument)
        v.patch->instrument->release_voice(v.poolSlot);
    v.poolSlot = -1;
    v.active = false;
}

// Caller must hold g_audioMutex (the play paths prepare the voice and
// schedule it under one lock, since prepare mutates graph state the audio
// callback may be reading).
static void voice_schedule_unlocked(std::shared_ptr<InstrumentPatch> patch,
                                    std::shared_ptr<ValueSource> source,
                                    int totalSamples, float gain, int midiNote,
                                    bool held = false,
                                    std::vector<mforce::Envelope*> envs = {},
                                    int poolSlot = -1,
                                    std::shared_ptr<mforce::PerformSource>
                                        performSource = nullptr,
                                    std::vector<std::shared_ptr<mforce::ValueSource>>
                                        advanceList = {}) {
    // Same-source steal: a note whose pool slot's previous note is still
    // sounding must replace that voice outright (two active entries pulling
    // one source would double-render it).
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (g_voices[i].active && g_voices[i].source.get() == source.get()) {
            slot = i;
            break;
        }
    }
    // Otherwise a free voice, or steal the one closest to done
    if (slot < 0) {
        for (int i = 0; i < MAX_VOICES; ++i) {
            if (!g_voices[i].active) { slot = i; break; }
        }
    }
    if (slot < 0) {
        int best = 0;
        for (int i = 1; i < MAX_VOICES; ++i)
            if (g_voices[i].samplesRemaining < g_voices[best].samplesRemaining) best = i;
        slot = best;
    }
    // Whatever voice we're overwriting gives its pool slot back — unless it
    // IS this note's slot (same-source steal above: slot ownership just
    // transfers to the new note).
    if (g_voices[slot].active && g_voices[slot].poolSlot != poolSlot)
        voice_deactivate_unlocked(g_voices[slot]);
    g_voices[slot].patch = std::move(patch);
    g_voices[slot].source = std::move(source);
    g_voices[slot].samplesRemaining = totalSamples;
    g_voices[slot].gain = gain;
    g_voices[slot].held = held;
    g_voices[slot].envs = std::move(envs);
    g_voices[slot].midiNote = midiNote;
    g_voices[slot].poolSlot = poolSlot;
    g_voices[slot].performSource = std::move(performSource);
    g_voices[slot].advanceList = std::move(advanceList);
    g_voices[slot].active = true;
}

static void voice_schedule(std::shared_ptr<InstrumentPatch> patch,
                           std::shared_ptr<ValueSource> source,
                           int totalSamples, float gain, int midiNote,
                           bool held = false,
                           std::vector<mforce::Envelope*> envs = {}) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    voice_schedule_unlocked(std::move(patch), std::move(source), totalSamples,
                            gain, midiNote, held, std::move(envs));
}

// Acquire a pool slot for a live note. The pool's own free-list answers the
// common case; when every slot is sounding, steal the active voice of this
// instrument closest to done (held notes sit at INT_MAX/2 remaining, so
// they are stolen last) and take its slot. Caller must hold g_audioMutex.
// Returns -1 only for an instrument with no pool.
static int acquire_pool_slot(InstrumentPatch* patch) {
    auto* pitched = patch->instrument.get();
    int slot = pitched->acquire_voice();
    if (slot >= 0) return slot;
    int best = -1, bestRem = INT_MAX;
    for (int v = 0; v < MAX_VOICES; ++v) {
        auto& vc = g_voices[v];
        if (!vc.active || vc.patch.get() != patch || vc.poolSlot < 0) continue;
        if (vc.samplesRemaining < bestRem) { bestRem = vc.samplesRemaining; best = v; }
    }
    if (best >= 0) voice_deactivate_unlocked(g_voices[best]);
    else pitched->release_all_voices();  // slot leak (bug) — self-heal
    return pitched->acquire_voice();
}

static bool any_voice_active() {
    for (int i = 0; i < MAX_VOICES; ++i)
        if (g_voices[i].active) return true;
    return false;
}

static bool is_playing() {
    return g_streamSource != nullptr || !g_streamChannels.empty()
        || g_bufferPlayback != nullptr || any_voice_active();
}

// RtAudio callback — runs on RtAudio's audio thread, not the UI thread.
// Mixes all active streaming voices + buffer playback + live stream into
// interleaved stereo float32. The g_audioMutex guards the shared playback
// state against UI-thread mutation.
static int audio_callback(void* outputBuffer, void* /*inputBuffer*/,
                          unsigned int nFrames, double /*streamTime*/,
                          RtAudioStreamStatus /*status*/, void* /*userData*/) {
    float* out = static_cast<float*>(outputBuffer);
    // RtAudio's thread — cheap to set per callback, disastrous to forget.
    mforce::enable_flush_denormals();
    g_audioHeartbeat.fetch_add(1, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_audioMutex);

    // No polyphony scaling: each voice contributes at its full voice.gain.
    // Earlier attempts at 1/sqrt(N) and 1/N both had artifacts — sqrt(N) let
    // two in-phase sines distort, 1/N gave a clean baseline but pumped
    // audibly on note-end as the scale ramped 1/N → 1/(N-1). User-driven
    // levels turn out to be the cleanest option: voices add raw, soft_clip
    // is the only safety net. Dense loud chords will saturate — the user
    // sets velocity/voice count accordingly.
    int activeCount = 0;
    for (int v = 0; v < MAX_VOICES; ++v)
        if (g_voices[v].active) ++activeCount;
    g_audioActiveVoices.store(activeCount, std::memory_order_relaxed);

    float localPeakPre = 0.0f, localPeakPost = 0.0f;

    for (unsigned int i = 0; i < nFrames; ++i) {
        float voiceSum = 0.0f;

        // Mix active streaming voices (live DSP pull, one next() per voice per sample)
        for (int v = 0; v < MAX_VOICES; ++v) {
            auto& voice = g_voices[v];
            if (!voice.active) continue;
            // P3 sample clock: bend + wheel/pressure smoothers advance here,
            // exactly once per voice per sample, never inside consumer pulls.
            if (voice.performSource) voice.performSource->tick();
            voiceSum += voice.source->next() * voice.gain;
            for (auto& a : voice.advanceList) a->next();  // tap-only loop tails
            voice.samplesRemaining--;
            if (voice.samplesRemaining <= 0) {
                // Flag writes only (pool release is a flag too — RT-safe).
                // Do NOT reset() the source/patch shared_ptrs here — dropping
                // the last ref would destruct the whole DSP graph on the audio
                // thread, hitting the Windows heap lock and causing glitches.
                // voice_gc() on the UI thread does the actual destruction.
                voice_deactivate_unlocked(voice);
            }
        }

        float s = voiceSum;

        // Buffer playback (passage/chords)
        if (g_bufferPlayback && g_bufferPlaybackPos < g_bufferPlaybackLen) {
            s += g_bufferPlayback[g_bufferPlaybackPos++];
            if (g_bufferPlaybackPos >= g_bufferPlaybackLen)
                g_bufferPlayback = nullptr;
        }

        // Live DSP stream (continuous mode)
        if (g_streamSource && g_streamRemaining != 0) {
            s += g_streamSource->next() * g_streamVelocity;
            if (g_streamRemaining > 0) {
                g_streamRemaining--;
                if (g_streamRemaining == 0)
                    g_streamSource = nullptr;
            }
        }

        // All mono contributions duplicate to both sides; the node-graph
        // stereo stream below adds per-side.
        float sL = s;
        float sR = s;

        // Node-graph stereo stream: per-sample mirror of the engine's
        // StereoMixer::render (mixer.cpp) — channel volume, equal-power pan,
        // master gainL/gainR, soft_clip at the mix. Mixer output is already
        // stereo, so it lands directly on L/R with no extra panning.
        if (!g_streamChannels.empty()) {
            float gl = g_streamGainL ? g_streamGainL->next() : 1.0f;
            float gr = g_streamGainR ? g_streamGainR->next() : 1.0f;
            for (auto& ch : g_streamChannels) {
                float v = ch.source->next() * (ch.volume ? ch.volume->next() : 1.0f);
                float p = ch.pan ? ch.pan->next() : 0.0f;
                p = std::clamp(p, -1.0f, 1.0f);
                // Equal-power panning: map [-1,1] -> [0,1]
                float t = (p + 1.0f) * 0.5f;
                float aL = std::cos(t * 0.5f * 3.14159265358979323846f);
                float aR = std::sin(t * 0.5f * 3.14159265358979323846f);
                sL += v * aL * gl * g_streamVelocity;
                sR += v * aR * gr * g_streamVelocity;
            }
        }

        // Diagnostic: pre-clip peak (raw mixer output)
        float absS = std::max(std::fabs(sL), std::fabs(sR));
        if (absS > localPeakPre) localPeakPre = absS;

        // Soft-clip the mix so a polyphonic stack of loud voices doesn't
        // hard-distort — matches the engine's render-path peak guard.
        sL = soft_clip(sL);
        sR = soft_clip(sR);

        // Diagnostic: post-clip peak (what actually goes to the device)
        absS = std::max(std::fabs(sL), std::fabs(sR));
        if (absS > localPeakPost) localPeakPost = absS;

        out[i * 2]     = sL;
        out[i * 2 + 1] = sR;
    }

    // Decay-blend with stored peaks so the UI sees a slowly-fading reading
    // (~170ms half-life) instead of jittery per-buffer values.
    constexpr float DECAY = 0.92f;
    float prevPre = g_audioPeakPre.load(std::memory_order_relaxed);
    g_audioPeakPre.store(std::max(localPeakPre,  prevPre  * DECAY), std::memory_order_relaxed);
    float prevPost = g_audioPeakPost.load(std::memory_order_relaxed);
    g_audioPeakPost.store(std::max(localPeakPost, prevPost * DECAY), std::memory_order_relaxed);

    return 0;  // 0 = continue stream
}

// MessageBox helper for audio init failures — fprintf(stderr) is invisible
// in a WIN32_EXECUTABLE build, so route startup-fatal audio errors through
// a modal popup the user will actually see.
static void audio_init_error(const char* msg) {
    MessageBoxA(nullptr, msg, "MForce audio init failed",
                MB_OK | MB_ICONERROR);
}

// quiet=true (watchdog reopen): failures must not raise a modal every retry —
// the watchdog keeps retrying with backoff and reports via the status bar.
static bool init_audio(bool quiet = false);

static bool init_audio(bool quiet) {
    try {
        g_audio = std::make_unique<RtAudio>();
    } catch (const std::exception& e) {
        char buf[512];
        std::snprintf(buf, sizeof(buf), "RtAudio constructor threw: %s", e.what());
        if (!quiet) audio_init_error(buf);
        return false;
    } catch (...) {
        if (!quiet) audio_init_error("RtAudio constructor threw (unknown exception)");
        return false;
    }

    g_audio->setErrorCallback(&on_rtaudio_error);

    if (g_audio->getDeviceCount() < 1) {
        if (!quiet) audio_init_error("No audio output devices found.");
        g_audio.reset();
        return false;
    }

    RtAudio::StreamParameters params;
    params.deviceId    = g_audio->getDefaultOutputDevice();
    params.nChannels   = 2;
    params.firstChannel = 0;

    unsigned int bufFrames = AUDIO_BUF_FRAMES;
    RtAudioErrorType err = g_audio->openStream(
        &params, nullptr,
        RTAUDIO_FLOAT32, AUDIO_SAMPLE_RATE, &bufFrames,
        &audio_callback, nullptr);
    if (err != RTAUDIO_NO_ERROR) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "RtAudio openStream failed (error %d).\n%s",
            int(err), g_audio->getErrorText().c_str());
        if (!quiet) audio_init_error(buf);
        g_audio.reset();
        return false;
    }

    err = g_audio->startStream();
    if (err != RTAUDIO_NO_ERROR) {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "RtAudio startStream failed (error %d).\n%s",
            int(err), g_audio->getErrorText().c_str());
        if (!quiet) audio_init_error(buf);
        g_audio->closeStream();
        g_audio.reset();
        return false;
    }

    return true;
}

static void shutdown_audio() {
    if (g_audio) {
        if (g_audio->isStreamRunning()) g_audio->stopStream();
        if (g_audio->isStreamOpen())    g_audio->closeStream();
        g_audio.reset();
    }
}

static void transport_set_status(const char* msg, bool isError);

// UI-thread watchdog, called once per frame. If the audio callback stops
// firing (device grabbed by another app, sample-rate change, output device
// switch — WASAPI kills the stream silently), tear the stream down and
// reopen it. Before this, playing a video in a browser meant no sound until
// a full app restart (2026-08-10).
static void audio_watchdog() {
    static uint64_t lastBeat      = 0;
    static double   lastBeatTime  = 0.0;
    static double   lastAttempt   = -10.0;
    static bool     wasDead       = false;

    double now = glfwGetTime();

    if (g_audioRestartRequest.exchange(false)) {
        shutdown_audio();
        if (init_audio(/*quiet=*/true))
            transport_set_status("Audio stream restarted (app refocus)", false);
        else
            transport_set_status("Audio restart on refocus FAILED — watchdog will retry", true);
        lastBeatTime = now;
        return;
    }

    uint64_t beat = g_audioHeartbeat.load(std::memory_order_relaxed);
    if (beat != lastBeat || !g_audio) {
        lastBeat = beat;
        lastBeatTime = now;
        if (wasDead && g_audio) {
            transport_set_status("Audio callback ALIVE again (post-reopen)", false);
            wasDead = false;
        }
        if (g_audio) return;
    }

    // No callback in 1s on a stream that should be running = dead.
    if (now - lastBeatTime < 1.0) return;
    if (now - lastAttempt < 2.0) return;   // retry backoff
    lastAttempt = now;
    wasDead = true;

    static int attempt = 0;
    ++attempt;
    shutdown_audio();
    std::string lastErr;
    { std::lock_guard<std::mutex> lk(g_audioErrMutex); lastErr = g_lastAudioError; }
    if (init_audio(/*quiet=*/true)) {
        std::string devName = "?";
        if (g_audio) {
            auto info = g_audio->getDeviceInfo(g_audio->getDefaultOutputDevice());
            devName = info.name;
        }
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "Audio stream reopened (attempt %d) on '%s'%s%s",
            attempt, devName.c_str(),
            lastErr.empty() ? "" : " — died: ", lastErr.c_str());
        transport_set_status(buf, false);
        lastBeatTime = now;   // give the new stream a fresh grace period
    } else {
        char buf[512];
        std::snprintf(buf, sizeof(buf),
            "Audio reopen FAILED (attempt %d)%s%s — retrying...",
            attempt, lastErr.empty() ? "" : " — ", lastErr.c_str());
        transport_set_status(buf, true);
    }
}

// Called once per UI frame. Picks up shared_ptrs that the audio callback
// marked dead (active=false but source still set) and releases them on the
// UI thread. The std::move inside the lock is cheap (pointer swap); the
// actual destruction happens after the lock is released, so no heap-lock
// stall blocks the audio callback.
static void voice_gc() {
    std::vector<std::shared_ptr<ValueSource>>     dyingSources;
    std::vector<std::shared_ptr<InstrumentPatch>> dyingPatches;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        for (auto& v : g_voices) {
            if (!v.active && (v.source || v.patch)) {
                if (v.source) dyingSources.push_back(std::move(v.source));
                if (v.patch)  dyingPatches.push_back(std::move(v.patch));
                for (auto& a : v.advanceList)   // loop tails die off-thread too
                    dyingSources.push_back(std::move(a));
                v.advanceList.clear();
                v.performSource.reset();  // pool keeps its own ref; the bend
                                          // envelope dies off-thread with it
                v.envs.clear();   // non-owning; graph dies with the patch
                v.held = false;
            }
        }
    }
    // Destructors fire here, off the audio thread.
}

// Find the DSP source connected to the Output node
static ValueSource* find_output_source() {
    // Listen tap (spec §3): monitor the tapped node's output in-context —
    // the graph runs exactly as wired, only the monitored signal moves.
    if (s_listenTapNode >= 0)
        for (auto& n : s_nodes)
            if (n.id == s_listenTapNode && n.dspSource)
                return n.dspSource.get();
    for (auto& n : s_nodes) {
        if (n.typeName != NT_PATCH_OUTPUT) continue;
        if (n.inputs.empty()) return nullptr;
        int srcPinId = n.inputs[0].id;
        for (auto& link : s_links) {
            int outPinId = -1;
            if (link.endPinId == srcPinId) outPinId = link.startPinId;
            if (link.startPinId == srcPinId) outPinId = link.endPinId;
            if (outPinId >= 0) {
                GraphNode* srcNode = find_node_for_pin(outPinId);
                if (srcNode && srcNode->dspSource)
                    return srcNode->dspSource.get();
            }
        }
    }
    return nullptr;
}

// Prepare the whole DSP graph for a given duration
static void prepare_graph(int samples) {
    // Prepare all nodes' DSP sources (order doesn't matter for prepare)
    RenderContext ctx{DSP_SAMPLE_RATE};
    for (auto& n : s_nodes) {
        if (n.dspSource) n.dspSource->prepare(ctx, samples);
    }
}

// ---------------------------------------------------------------------------
// Continuous-stream envelope handling (dsp run 24 issue 1).
//
// A stream has no note duration, but Envelope::prepare(frames) bakes one in:
// stage lengths are FRACTIONS of the prepared duration, and past the last
// stage next() returns 0 forever. The old 30 s stream prepare therefore made
// every stream a 30-second envelope pass — for the common adsr-with-
// release-0 shape, the release stage is the expand stage, i.e. the whole
// stream was one long sustain→0 fade that hit exact silence at t=30 s.
//
// Streaming fix, UI-side only (Envelope's public API, no engine change):
//  1. prepare for STREAM_PREP_SECONDS (hours, not seconds) so end-of-
//     envelope is beyond any real session;
//  2. flip each node Envelope to absolute_time for the stream so its
//     attack/decay/release stay literal seconds instead of stretching with
//     the huge duration (percent 0.05 → 50 ms attack, not 6 min), with the
//     expand stage soaking up the rest as sustain;
//  3. envelopes with no expand stage get a temporary hold stage appended so
//     they sustain their final value instead of cutting to 0.
// stop_streams() restores every envelope to its pre-stream state, so note
// renders / Generate are untouched. True forever-streaming (and streams
// >2 h) needs an engine-side Envelope hold/loop mode — engine is owned by
// another agent this run.
// ---------------------------------------------------------------------------
static constexpr int STREAM_PREP_SECONDS = 7200;  // 2 h; int-safe at 48 kHz

struct StreamEnvHold {
    Envelope* env;
    bool prevAbsolute;
    int addedStageIdx;   // -1 = no hold stage appended
};
static std::vector<StreamEnvHold> g_streamEnvHolds;

static void stream_envelopes_restore() {
    for (auto& h : g_streamEnvHolds) {
        h.env->absolute_time = h.prevAbsolute;
        // Remove the appended hold stage only if it's still recognizably
        // ours (last stage, expand). A mid-stream stage edit rebuilds the
        // envelope's stage list wholesale, in which case there is nothing
        // of ours left to remove.
        if (h.addedStageIdx >= 0 && h.addedStageIdx == h.env->stage_count() - 1 &&
            h.env->stage(h.addedStageIdx).percent == 0.0f)
            h.env->remove_stage(h.addedStageIdx);
    }
    g_streamEnvHolds.clear();
}

static void stream_envelopes_hold() {
    stream_envelopes_restore();  // idempotent if a stream restarts
    for (auto& n : s_nodes) {
        auto* env = dynamic_cast<Envelope*>(n.dspSource.get());
        if (!env) continue;
        StreamEnvHold h{env, env->absolute_time, -1};
        env->absolute_time = true;
        bool hasExpand = false;
        for (int i = 0; i < env->stage_count(); ++i)
            if (env->stage(i).percent == 0.0f) { hasExpand = true; break; }
        if (!hasExpand && env->stage_count() > 0) {
            float endV = env->stage(env->stage_count() - 1).ramp.endVal;
            env->add_stage({{endV, endV, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
            h.addedStageIdx = env->stage_count() - 1;
        }
        g_streamEnvHolds.push_back(h);
    }
}

// Forward decl — defined further down with the other transport helpers.
// Used by the *_authoritative render paths so silent failures (load throws,
// non-pitched instrument, missing patch path) surface to the UI status line
// instead of stderr (where they're easy to miss; bug 2026-04-28 Multiplex
// "ignored" — Generate fell back to render_waveforms' single-voice buffer).
static void transport_set_status(const char* msg, bool isError);

// Offline render: populate per-node waveformData and g_outputWaveform for display
// Overwrite g_outputWaveform with authoritative audio for a passage (note
// sequence) via load_instrument_patch + PitchedInstrument. Per-node
// waveform data still comes from render_passage_waveforms' UI DSP pass.
static bool render_passage_output_authoritative(
    const std::vector<ParsedNote>& notes, float velocity)
{
    if (notes.empty()) return false;
    std::string path = get_playback_patch_path();
    if (path.empty()) {
        transport_set_status("Authoritative passage render: no patch path "
                             "(get_playback_patch_path empty)", true);
        return false;
    }

    try {
        auto ip = load_instrument_patch(path);
        auto* pitched = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        if (!pitched) {
            transport_set_status("Authoritative passage render: loaded patch is not "
                                 "a PitchedInstrument", true);
            return false;
        }

        float timeCursor = 0.0f;
        for (const auto& pn : notes) {
            pitched->play_note(pn.noteNumber, velocity, pn.durationSeconds, timeCursor);
            timeCursor += pn.durationSeconds;
        }

        // Note-contained sound (2026-08-13): all sound ends by the last
        // note's duration end.
        int frames = int(timeCursor * float(ip.sampleRate));
        g_outputWaveform.assign(frames, 0.0f);
        g_waveformSamples = frames;
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, g_outputWaveform.data(), frames);

        wave_view_after_render(frames);
        compute_output_spectrum();
        return true;
    } catch (const std::exception& e) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Authoritative passage render failed: %s", e.what());
        transport_set_status(buf, true);
        std::fprintf(stderr, "render_passage_output_authoritative failed: %s\n", e.what());
        return false;
    }
}

// Overwrite g_outputWaveform with authoritative audio produced via
// load_instrument_patch — the same path CLI `mforce_cli` uses, so
// MultiplexSource fan-out (and any other load-time constructs) apply.
// Called after render_waveforms during Generate so per-node displays still
// use the UI DSP tree for per-node waveforms, but the main g_outputWaveform
// and Play path reflect what the patch will really sound like.
static bool render_output_authoritative(float noteNum, float velocity,
                                        float durationSeconds) {
    std::string path = get_playback_patch_path();
    if (path.empty()) {
        transport_set_status("Authoritative render: no patch path "
                             "(get_playback_patch_path empty)", true);
        return false;
    }

    try {
        auto ip = load_instrument_patch(path);
        auto* pitched = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        if (!pitched) {
            transport_set_status("Authoritative render: loaded patch is not "
                                 "a PitchedInstrument", true);
            return false;
        }

        pitched->play_note(noteNum, velocity, durationSeconds, 0.0f);

        int frames = int(durationSeconds * float(ip.sampleRate));
        g_outputWaveform.assign(frames, 0.0f);
        g_waveformSamples = frames;
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, g_outputWaveform.data(), frames);

        wave_view_after_render(frames);
        compute_output_spectrum();
        return true;
    } catch (const std::exception& e) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "Authoritative render failed: %s", e.what());
        transport_set_status(buf, true);
        std::fprintf(stderr, "render_output_authoritative failed: %s\n", e.what());
        return false;
    }
}

// Apply the paramMap stash for one note, mirroring the engine's note-on
// application exactly (instrument.h map/vmap): curve = log-frequency
// interpolation, end-clamped; vcurve = linear in velocity, multiplicative;
// bare targets receive the raw note frequency. Pin targets set the pin's
// ConstantSource; config targets go through set_setting (and refresh the
// node's cached settingValues so Properties shows the per-note value).
// Replaces the old Parameter-node scan, which pushed RAW frequency into
// curve-bearing pin targets — the UI render ignored curves entirely.
static float eval_map_curve(const nlohmann::json& curve, float freq,
                            bool loglog = false) {
    auto x = [&](size_t i) { return curve[i][0].get<float>(); };
    auto y = [&](size_t i) { return curve[i][1].get<float>(); };
    size_t n = curve.size();
    if (n == 0) return freq;
    if (freq <= x(0))     return y(0);
    if (freq >= x(n - 1)) return y(n - 1);
    for (size_t i = 1; i < n; ++i) {
        if (freq <= x(i)) {
            float lf = std::log(freq / x(i - 1)) / std::log(x(i) / x(i - 1));
            // "interp":"loglog" (engine parity): straight line in log-log
            // = exact y = k*f^n; positive values only.
            if (loglog && y(i - 1) > 0.0f && y(i) > 0.0f)
                return y(i - 1) * std::pow(y(i) / y(i - 1), lf);
            return y(i - 1) + (y(i) - y(i - 1)) * lf;
        }
    }
    return y(n - 1);
}

static float eval_map_vcurve(const nlohmann::json& vcurve, float vel) {
    auto x = [&](size_t i) { return vcurve[i][0].get<float>(); };
    auto y = [&](size_t i) { return vcurve[i][1].get<float>(); };
    size_t n = vcurve.size();
    if (n == 0) return 1.0f;
    if (vel <= x(0))     return y(0);
    if (vel >= x(n - 1)) return y(n - 1);
    for (size_t i = 1; i < n; ++i) {
        if (vel <= x(i)) {
            float t = (vel - x(i - 1)) / (x(i) - x(i - 1));
            return y(i - 1) + (y(i) - y(i - 1)) * t;
        }
    }
    return y(n - 1);
}

// The UI-side equivalent of PerformSource::set_note, for offline note renders
// (waveform previews and the audition tap). A PerformNode's editor DSP is a
// stand-in ConstantSource — the real adapter belongs to a voice, and the
// editor has none — so retuning a preview means writing the note into those
// constants. Without this, every preview of a converted patch would draw at
// the stand-in's 440 Hz no matter which key was pressed.
static void apply_perform_nodes(float freq, float velocity) {
    for (auto& n : s_nodes) {
        if (n.typeName != NT_PERFORM) continue;
        // Per-pin stand-ins; wheel/pressure previews stay 0 (untouched
        // controller).
        for (auto& p : n.outputs) {
            if (!p.constantSrc) continue;
            if (p.name == "frequency")     p.constantSrc->set(freq);
            else if (p.name == "velocity") p.constantSrc->set(velocity);
        }
    }
}

static void apply_param_map(float freq, float velocity) {
    if (!s_loadedParamMap.is_object()) return;
    auto it = s_loadedParamMap.find("frequency");
    if (it == s_loadedParamMap.end()) return;

    auto apply_one = [&](const nlohmann::json& e) {
        std::string target;
        float v = freq;
        if (e.is_string()) {
            target = e.get<std::string>();
        } else if (e.is_object() && e.contains("target") && e["target"].is_string()) {
            target = e["target"].get<std::string>();
            if (e.contains("curve"))
                v = eval_map_curve(e["curve"], freq,
                                   e.value("interp", std::string()) == "loglog");
            if (e.contains("vcurve")) v *= eval_map_vcurve(e["vcurve"], velocity);
        } else {
            return;
        }
        auto dot = target.find('.');
        std::string nodeId = (dot == std::string::npos) ? target : target.substr(0, dot);
        std::string pname  = (dot == std::string::npos) ? "frequency" : target.substr(dot + 1);
        for (auto& n : s_nodes) {
            if (n.label != nodeId) continue;
            if (auto* p = n.find_input(pname)) {
                if (p->constantSrc && !is_pin_connected(p->id)) {
                    p->defaultValue = v;
                    p->constantSrc->set(v);
                }
            } else if (n.dspSource) {
                n.dspSource->set_setting(pname, v);
                for (auto& [desc, val] : n.settingValues)
                    if (pname == desc.name) val = n.dspSource->get_setting(desc.name);
            }
            return;
        }
    };
    if (it->is_array()) {
        for (const auto& e : *it) apply_one(e);
    } else {
        apply_one(*it);
    }
}

static void render_waveforms(float noteNum, float velocity, float durationSeconds) {
    ValueSource* src = find_output_source();
    if (!src) return;

    // If a continuous stream flipped envelopes to streaming semantics,
    // restore them so this offline note render behaves exactly as before.
    stream_envelopes_restore();

    // Retune per the paramMap (curves/vcurves honored — engine parity);
    // NodeGraph mode has no paramMap and keeps its Parameter frequency node.
    float freq = note_to_freq(noteNum);
    apply_param_map(freq, velocity);
    apply_perform_nodes(freq, velocity);
    for (auto& n : s_nodes) {
        if (n.typeName == NT_PARAMETER && n.paramName == "frequency") {
            if (auto* p = n.find_input("default"))
                p->constantSrc->set(freq);
        }
    }

    int samples = int(durationSeconds * float(AUDIO_SAMPLE_RATE));
    prepare_graph(samples);

    // Allocate per-node buffers for DSP nodes
    for (auto& n : s_nodes) {
        if (n.dspSource && !is_special_ui_type(n.typeName))
            n.waveformData.resize(samples);
        else
            n.waveformData.clear();
    }
    buffer_playback_detach();  // it points into this vector (3k)
    g_outputWaveform.resize(samples);
    g_waveformSamples = samples;

    // Snapshot capture setup: for every Partials/Formant node, record which
    // input pins are connected to live ValueSources so we can sample their
    // current() values at EVO_SNAP_COUNT evenly-spaced time points during
    // the render loop. This populates g_evoSnapshots used by the scrubber.
    g_evoSnapshots.clear();
    struct EvoCap { int nodeId; std::string pin; ValueSource* vs; std::vector<float>* vec; };
    std::vector<EvoCap> evoCaptures;
    int snapStride = std::max(1, samples / EVO_SNAP_COUNT);
    for (auto& n : s_nodes) {
        if (!is_partials_type(n.typeName) && !is_formant_type(n.typeName)) continue;
        auto& byPin = g_evoSnapshots[n.id];
        for (auto& pin : n.inputs) {
            GraphNode* sn = find_source_node(pin.id);
            if (!sn || !sn->dspSource) continue;
            byPin[pin.name].assign(EVO_SNAP_COUNT, 0.0f);
            evoCaptures.push_back({n.id, pin.name, sn->dspSource.get(), &byPin[pin.name]});
        }
    }

    // Render the full note offline
    for (int i = 0; i < samples; ++i) {
        float s = src->next();
        g_outputWaveform[i] = s * velocity;

        // Capture each node's current output
        for (auto& n : s_nodes) {
            if (!n.waveformData.empty())
                n.waveformData[i] = n.dspSource->current();
        }

        // Snapshot evolving inputs at stride boundaries.
        if (i % snapStride == 0) {
            int idx = i / snapStride;
            if (idx < EVO_SNAP_COUNT) {
                for (auto& c : evoCaptures) (*c.vec)[idx] = c.vs->current();
            }
        }
    }

    wave_view_after_render(samples);

    // Refresh the output spectrum from the freshly-rendered g_outputWaveform.
    compute_output_spectrum();
}

// Play a note: render offline into a voice buffer for polyphonic mixing.
// Also updates the waveform display with the most recent note.
//
// Audio path routes through load_instrument_patch(temp) so the CLI loader's
// voice-pool + paramMap machinery (including MultiplexSource fan-out into
// internal clones) applies to live-keyboard playback. The UI's in-memory
// DSP tree is used only for the waveform display — that path shows the
// UI's solo-preview state without the fan-out, which is fine for a visual.
static void note_played(float noteNum, float velocity);
static bool collect_envelopes(mforce::ValueSource* vs,
                              std::vector<mforce::Envelope*>& out,
                              std::vector<mforce::ValueSource*>& seen);

// ---------------------------------------------------------------------------
// Live-playback instrument cache (BACKLOG dsp 17, 2026-08-18). The play
// paths used to rebuild the whole instrument per keypress — serialize-if-
// dirty + JSON parse + polyphony × graph build, ~0.6-1.3 s on heavy patches
// — which was the live-keyboard lag. Now the loaded InstrumentPatch is
// cached and notes go through its voicePool (the engine's own polyphony
// machinery, round-robin slots). The shared loader path is preserved: the
// cache is BUILT by exactly the old serialize→load route, just once per
// graph change instead of once per note. Keyed on g_graphEditCounter plus
// the listen tap; any mismatch rebuilds on the next note-on.
// ---------------------------------------------------------------------------
static constexpr int LIVE_MIN_POLYPHONY = 8;
static std::shared_ptr<InstrumentPatch> g_cachedInstrument;
static uint64_t g_cachedEditCounter = ~0ull;
static int      g_cachedTapNode     = -1;

// Returns the cached instrument, rebuilding if the graph changed. Throws on
// load failure (callers' try/catch reports). Null = no playable patch path.
static std::shared_ptr<InstrumentPatch> get_cached_instrument() {
    if (g_cachedInstrument &&
        g_cachedEditCounter == g_graphEditCounter &&
        g_cachedTapNode == s_listenTapNode)
        return g_cachedInstrument;
    std::string path = get_playback_patch_path();
    if (path.empty()) return nullptr;
    auto t0 = std::chrono::steady_clock::now();
    // shared_ptr so Voice slots keep the whole DSP graph alive while sounding
    // (and across cache invalidation — an old instrument survives until its
    // last voice ends).
    auto ip = std::make_shared<InstrumentPatch>(
        load_instrument_patch(path, LIVE_MIN_POLYPHONY));
    if (!ip->instrument || ip->instrument->voicePool.empty()) return nullptr;
    g_cachedInstrument  = ip;
    g_cachedEditCounter = g_graphEditCounter;
    g_cachedTapNode     = s_listenTapNode;
    int ms = int(std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - t0).count());
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Instrument rebuilt: %d voices, %d ms",
                  int(ip->instrument->voicePool.size()), ms);
    transport_set_status(buf, false);
    return ip;
}

static void play_note(float noteNum, float velocity, float durationSeconds) {
    if (s_graphMode != GraphMode::PatchGraph) return;
    note_played(noteNum, velocity);

    // (Waveform-display render intentionally skipped here — live keyboard
    // mode prioritizes audio responsiveness over visual feedback. Use the
    // Render button / offline render path when you want to see the waveform.)

    try {
        auto ip = get_cached_instrument();
        if (!ip) return;
        auto* pitched = ip->instrument.get();

        // Slot acquire, envelope un-gate, prepare and schedule all under one
        // lock: the steal path reads voice actives, and prepare mutates graph
        // state the audio callback may be reading if a steal is happening.
        std::lock_guard<std::mutex> lock(g_audioMutex);
        int slot = acquire_pool_slot(ip.get());
        if (slot < 0) return;

        // A previous HELD note on this slot leaves its envelopes in gated
        // mode (gated_ survives prepare), which would make a scheduled note
        // sustain forever — un-gate them explicitly.
        std::vector<mforce::Envelope*> envs;
        std::vector<mforce::ValueSource*> seen;
        collect_envelopes(pitched->voicePool[slot].source.get(), envs, seen);
        for (auto* e : envs) e->set_gated(false);

        // Prepare the voice (set frequency, prep the source) but DON'T render —
        // streaming voice mixer will pull samples on demand in fill_audio_buffer.
        // sv.gain carries the patch's pre-clip volume (calibrated gain staging).
        auto sv = pitched->prepare_voice_at(slot, noteNum, velocity,
                                           durationSeconds);

        // Note-contained sound (2026-08-13): the voice lives exactly
        // durSamples — release is inside the note, no tail window.
        voice_schedule_unlocked(ip, sv.source, sv.durSamples, sv.gain,
                                int(noteNum), false, {}, slot,
                                sv.performSource, sv.advanceList);
    } catch (const std::exception& e) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "play_note failed: %s", e.what());
        transport_set_status(buf, true);
    }
}

// Walk a loaded voice graph collecting Envelope nodes (for live gating).
// Returns false if the graph contains a MultiplexSource — its internal
// clones are not reachable by this walk, so gating the template would
// silently do nothing; callers fall back to scheduled notes there.
static bool collect_envelopes(mforce::ValueSource* vs,
                              std::vector<mforce::Envelope*>& out,
                              std::vector<mforce::ValueSource*>& seen) {
    if (!vs) return true;
    for (auto* s : seen) if (s == vs) return true;
    seen.push_back(vs);
    if (std::string_view(vs->type_name()).find("Multiplex") != std::string_view::npos)
        return false;
    if (auto* env = dynamic_cast<mforce::Envelope*>(vs)) out.push_back(env);
    bool ok = true;
    for (const auto& d : vs->input_descriptors())
        ok = collect_envelopes(vs->get_param(d.name).get(), out, seen) && ok;
    for (const auto& d : vs->param_descriptors())
        ok = collect_envelopes(vs->get_param(d.name).get(), out, seen) && ok;
    return ok;
}

// Key-down entry: hold the note until the matching key-up. Envelopes are
// flipped to gated mode BEFORE prepare so the expand stage holds; pct
// stages resolve against the transport duration as nominal. Falls back to
// a scheduled note when the graph's envelopes aren't reachable
// (MultiplexSource) or absent.
static void play_note_held(float noteNum, float velocity, float nominalSeconds) {
    if (s_graphMode != GraphMode::PatchGraph) return;
    try {
        auto ip = get_cached_instrument();
        if (!ip) return;
        auto* pitched = ip->instrument.get();

        {
            // Slot acquire, gating, prepare and schedule under one lock — see
            // play_note. Scoped so the non-gateable fallback below can call
            // play_note (which takes the lock itself) without deadlocking.
            std::lock_guard<std::mutex> lock(g_audioMutex);
            int slot = acquire_pool_slot(ip.get());
            if (slot < 0) return;

            // Gate only the slot this note will occupy — with the shared
            // cached instrument, other pool slots may be sounding other notes.
            std::vector<mforce::Envelope*> envs;
            std::vector<mforce::ValueSource*> seen;
            bool gateable = collect_envelopes(
                pitched->voicePool[slot].source.get(), envs, seen);
            if (gateable && !envs.empty()) {
                note_played(noteNum, velocity);
                for (auto* e : envs) e->set_gated(true);
                auto sv = pitched->prepare_voice_at(slot, noteNum, velocity,
                                                    nominalSeconds);
                voice_schedule_unlocked(ip, sv.source, INT_MAX / 2, sv.gain,
                                        int(noteNum), true, std::move(envs),
                                        slot, sv.performSource, sv.advanceList);
                return;
            }
            // Not gateable: hand the acquired slot back before falling
            // through — play_note will re-acquire it.
            pitched->release_voice(slot);
        }
        play_note(noteNum, velocity, nominalSeconds);   // scheduled fallback
    } catch (const std::exception& e) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "play_note_held failed: %s", e.what());
        transport_set_status(buf, true);
    }
}

// Key-up: gate the matching held voice's envelopes and bound its life to
// the longest release + the reflection allowance.
static void release_note_held(int midiNote) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    for (int v = 0; v < MAX_VOICES; ++v) {
        auto& voice = g_voices[v];
        if (!voice.active || !voice.held || voice.midiNote != midiNote) continue;
        int maxRel = 0;
        for (auto* e : voice.envs) maxRel = std::max(maxRel, e->gate_release());
        int allow = int(mforce::Envelope::kReflectionAllowanceSec
                        * float(AUDIO_SAMPLE_RATE));
        voice.samplesRemaining = maxRel + allow + 1;
        voice.held = false;
        return;
    }
}

// Start continuous streaming. Streams prepare the graph for
// STREAM_PREP_SECONDS with envelopes held at streaming semantics (see
// stream_envelopes_hold) so the output is genuinely continuous instead of
// fading out at a fixed note-duration horizon.
// Patch graphs stream the mono source feeding the Output node; node graphs
// stream the Mixer (Channel → Mixer termination) in stereo.
static void play_continuous(float velocity) {
    // Clear any active stream first: re-preparing the graph while the audio
    // thread pulls it is a race, and this also restores envelope overrides
    // from a previous stream before we snapshot state again.
    stop_streams();

    if (s_graphMode == GraphMode::PatchGraph) {
        ValueSource* src = find_output_source();
        if (!src) return;

        stream_envelopes_hold();
        int samples = AUDIO_SAMPLE_RATE * STREAM_PREP_SECONDS;
        prepare_graph(samples);

        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_streamVelocity = velocity;
        g_streamRemaining = -1;
        g_streamSource = src;
        return;
    }

    // Node graph: stream the StereoMixer live. Resolve each mixer "ch N" pin
    // to its SoundChannel and the channel's source/volume/pan taps. Accepting
    // SoundChannel only matches the CLI loader, which rejects anything else
    // wired into a mixer channel ("Node is not SoundChannel").
    GraphNode* mixer = nullptr;
    for (auto& n : s_nodes)
        if (n.typeName == NT_STEREO_MIXER) { mixer = &n; break; }
    if (!mixer) {
        transport_set_status("Node graph has no Mixer to stream", true);
        return;
    }

    // Pins resolved to a connected node's dspSource are prepared by
    // prepare_graph below; unconnected pins fall back to the pin's own
    // ConstantSource (the value Properties edits live), which is NOT a graph
    // node — collect those for explicit prepare.
    std::vector<ValueSource*> prepExtra;
    auto resolve_pin = [&prepExtra](GraphNode& node, const char* name) -> ValueSource* {
        for (auto& pin : node.inputs) {
            if (pin.name != name) continue;
            GraphNode* src = find_source_node(pin.id);
            if (src && src->dspSource) return src->dspSource.get();
            if (pin.constantSrc) prepExtra.push_back(pin.constantSrc.get());
            return pin.constantSrc.get();
        }
        return nullptr;
    };

    std::vector<StreamChannel> channels;
    for (auto& pin : mixer->inputs) {
        if (pin.name.substr(0, 3) != "ch ") continue;
        GraphNode* chNode = find_source_node(pin.id);
        if (!chNode || chNode->typeName != NT_SOUND_CHANNEL) continue;

        StreamChannel sc;
        for (auto& cp : chNode->inputs) {
            if (cp.name != "source") continue;
            GraphNode* sn = find_source_node(cp.id);
            if (sn && sn->dspSource) sc.source = sn->dspSource.get();
            break;
        }
        if (!sc.source) continue;  // channel with nothing wired in
        sc.volume = resolve_pin(*chNode, "volume");
        sc.pan    = resolve_pin(*chNode, "pan");
        channels.push_back(sc);
    }
    if (channels.empty()) {
        transport_set_status(
            "Mixer has no Channel with a connected source — wire "
            "source → Channel → Mixer", true);
        return;
    }

    ValueSource* gainL = resolve_pin(*mixer, "gainL");
    ValueSource* gainR = resolve_pin(*mixer, "gainR");

    stream_envelopes_hold();
    int samples = AUDIO_SAMPLE_RATE * STREAM_PREP_SECONDS;
    prepare_graph(samples);
    RenderContext ctx{DSP_SAMPLE_RATE};
    for (auto* vs : prepExtra)
        if (vs) vs->prepare(ctx, samples);

    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_streamVelocity = velocity;
        g_streamRemaining = -1;
        g_streamChannels = std::move(channels);
        g_streamGainL = gainL;
        g_streamGainR = gainR;
    }
    transport_set_status("Streaming node graph (Space or Stop to end)", false);
}

// Stop only the continuous streams (forward-declared above delete_node) —
// they hold raw pointers into node dspSources, so any structural edit that
// can destroy a node must clear them first.
static void stop_streams() {
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_streamSource = nullptr;
        g_streamChannels.clear();
        g_streamGainL = nullptr;
        g_streamGainR = nullptr;
    }
    // Audio thread no longer touches the graph — safe to put envelopes back
    // to their pre-stream (note-duration) semantics.
    stream_envelopes_restore();
}

static void stop_playback() {
    stop_streams();
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_bufferPlayback = nullptr;
    g_bufferPlaybackPos = 0;
    g_bufferPlaybackLen = 0;
    for (int i = 0; i < MAX_VOICES; ++i)
        voice_deactivate_unlocked(g_voices[i]);
}

// g_bufferPlayback points INTO g_outputWaveform — detach it (under the
// audio lock) before that vector is resized or cleared. See backlog 3k.
static void buffer_playback_detach() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_bufferPlayback = nullptr;
    g_bufferPlaybackPos = 0;
    g_bufferPlaybackLen = 0;
}

// Start playing from the pre-rendered g_outputWaveform buffer
static void play_buffer() {
    stop_playback();
    if (g_outputWaveform.empty()) return;
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_bufferPlayback = g_outputWaveform.data();
    g_bufferPlaybackPos = 0;
    g_bufferPlaybackLen = g_waveformSamples;
}

// ===========================================================================
// Keyboard Panel
// ===========================================================================

struct KeyboardState {
    int octave = 4;
    float duration = 0.5f;
    float velocity = 0.8f;
    bool sustain = false;
};
static KeyboardState g_keyboard;

// ===========================================================================
// MIDI input (RtMidi) — hardware keyboard into the same held-note path as
// QWERTY. Polled from the UI thread once per frame (pump_midi); RtMidi's
// WinMM backend queues incoming messages internally, so there is no callback
// threading on our side. Unlike QWERTY there is no noteMode gate: a key
// struck on a MIDI keyboard is always intent to play.
// ===========================================================================
static std::unique_ptr<RtMidiIn> g_midiIn;
static int g_midiPort = -1;            // opened port index, -1 = none
static std::string g_midiPortName;     // display name of the opened port

static void midi_close() {
    if (g_midiIn && g_midiIn->isPortOpen()) g_midiIn->closePort();
    g_midiPort = -1;
    g_midiPortName.clear();
}

static bool midi_open_port(unsigned int port) {
    if (!g_midiIn) return false;
    midi_close();
    try {
        std::string name = g_midiIn->getPortName(port);
        g_midiIn->openPort(port);
        g_midiPort = int(port);
        g_midiPortName = name;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "MIDI in: %s", name.c_str());
        transport_set_status(buf, false);
        return true;
    } catch (RtMidiError& e) {
        transport_set_status(e.getMessage().c_str(), true);
        return false;
    }
}

// Auto-sense at startup: open the first MIDI input if one is present. Every
// failure mode is non-fatal — MIDI is an optional input source.
static void init_midi() {
    try {
        g_midiIn = std::make_unique<RtMidiIn>();
        if (g_midiIn->getPortCount() > 0) midi_open_port(0);
    } catch (RtMidiError&) {
        g_midiIn.reset();
    }
}

static void shutdown_midi() {
    midi_close();
    g_midiIn.reset();
}

// Per-frame drain. Note bytes map directly to engine note numbers
// (note_to_freq is MIDI-standard: 69 = A440). Velocity 1..127 scales to the
// same 0..1 range the velocity slider produces, but is per-note — the slider
// keeps governing QWERTY/on-screen keys only. Duration is the keyboard
// panel's Duration value (nominal length for envelope pacing; the actual
// release fires on key-up, exactly like QWERTY).
static void pump_midi() {
    if (!g_midiIn || g_midiPort < 0) return;
    std::vector<unsigned char> msg;
    for (;;) {
        try { g_midiIn->getMessage(&msg); }
        catch (RtMidiError&) { return; }
        if (msg.empty()) return;
        if (msg.size() < 2) continue;
        unsigned char status = msg[0] & 0xF0;

        // P3 liveness (plan_perform_source_p3.md T4): wheel + channel
        // pressure land in the instrument's InstrumentState (atomic stores;
        // each voice's PerformSource smooths them on the audio thread).
        // Channel pressure is a TWO-byte message — it must be handled before
        // the 3-byte guard below or it is silently dropped.
        if (status == 0xD0) {                     // channel pressure
            if (auto ip = get_cached_instrument(); ip && ip->instrument)
                ip->instrument->instrumentState->pressure.store(
                    float(msg[1]) / 127.0f, std::memory_order_relaxed);
            continue;
        }
        if (msg.size() < 3) continue;
        int note = msg[1];
        int vel  = msg[2];
        if (status == 0xB0 && note == 1) {        // CC1 mod wheel
            if (auto ip = get_cached_instrument(); ip && ip->instrument)
                ip->instrument->instrumentState->wheel.store(
                    float(vel) / 127.0f, std::memory_order_relaxed);
            continue;
        }
        if (status == 0x90 && vel > 0) {          // note on
            play_note_held(float(note), float(vel) / 127.0f,
                           g_keyboard.duration);
        } else if (status == 0x80 ||              // note off (0x90 vel 0 =
                   (status == 0x90 && vel == 0)) {  // running-status note off)
            release_note_held(note);
        }
        // CC64 sustain pedal: deferred (needs release-hold semantics in the
        // voice layer before it can do anything). MIDI pitch wheel (0xE0):
        // deferred — InstrumentState has no field for it in the spec; poly
        // aftertouch (0xA0) likewise until hardware exists to test it.
    }
}

// Seed transport + keyboard playback defaults from a loaded patch's score so
// UI playback matches the CLI render of the same file. The patch's note
// duration is part of the sound for percent-based envelopes (a compressed
// note re-paces the whole envelope word), so defaults must come from the
// score, not hardcoded constants. Values remain fully user-editable after
// load; a patch without a score leaves the previous defaults untouched.
static void apply_score_defaults(const nlohmann::json& score) {
    if (!score.is_array() || score.empty()) return;
    const auto& n0 = score[0];
    if (!n0.is_object()) return;

    float note = n0.value("note", 60.0f);
    float vel  = n0.value("velocity", 0.8f);
    float dur  = n0.value("duration", 2.0f);

    snprintf(g_transport.noteStr, sizeof(g_transport.noteStr), "%g", note);
    g_transport.velocity = vel;
    g_transport.duration = dur;

    g_keyboard.velocity = vel;
    g_keyboard.duration = std::clamp(dur, 0.05f, 30.0f);
    // Keyboard base octave so the score's note is reachable on the home row.
    // HOUSE octave convention (comp REVIEW item 19): absNote = octave * 12 +
    // offset — matches parse_note_input and Pitch::note_number. The panel
    // was the app's lone scientific-pitch holdout until 2026-08-12.
    g_keyboard.octave = std::clamp(int(note) / 12, 0, 20);
}

// QWERTY-to-chromatic-offset mapping (from legacy LBKeyboard.cs).
// Two zones (AF/SAVIHost-style, Matt 2026-09-03): Q row + number row =
// upper zone at the keyboard octave; Z row + home row = lower zone, the
// same layout 2 octaves down (negative offsets from octave*12). Both
// zones shift together with the octave keys. This claimed G/H/V/B as
// note keys, so the action keys moved to the arrows (octave Up/Down,
// duration Right/Left).
struct QwertyMapping { ImGuiKey key; int offset; const char* label; };
static const QwertyMapping s_qwertyMap[] = {
    // Lower zone: Z-row whites, home-row blacks, 2 octaves down.
    { ImGuiKey_Z,          -24, "Z" },
    { ImGuiKey_S,          -23, "S" },
    { ImGuiKey_X,          -22, "X" },
    { ImGuiKey_D,          -21, "D" },
    { ImGuiKey_C,          -20, "C" },
    { ImGuiKey_V,          -19, "V" },
    { ImGuiKey_G,          -18, "G" },
    { ImGuiKey_B,          -17, "B" },
    { ImGuiKey_H,          -16, "H" },
    { ImGuiKey_N,          -15, "N" },
    { ImGuiKey_J,          -14, "J" },
    { ImGuiKey_M,          -13, "M" },
    { ImGuiKey_Comma,      -12, "," },
    { ImGuiKey_L,          -11, "L" },
    { ImGuiKey_Period,     -10, "." },
    { ImGuiKey_Semicolon,   -9, ";" },
    { ImGuiKey_Slash,       -8, "/" },
    // Upper zone at the keyboard octave.
    { ImGuiKey_Q,            0, "Q" },
    { ImGuiKey_2,            1, "2" },
    { ImGuiKey_W,            2, "W" },
    { ImGuiKey_3,            3, "3" },
    { ImGuiKey_E,            4, "E" },
    { ImGuiKey_R,            5, "R" },
    { ImGuiKey_5,            6, "5" },
    { ImGuiKey_T,            7, "T" },
    { ImGuiKey_6,            8, "6" },
    { ImGuiKey_Y,            9, "Y" },
    { ImGuiKey_7,           10, "7" },
    { ImGuiKey_U,           11, "U" },
    { ImGuiKey_I,           12, "I" },
    { ImGuiKey_9,           13, "9" },
    { ImGuiKey_O,           14, "O" },
    { ImGuiKey_0,           15, "0" },
    { ImGuiKey_P,           16, "P" },
    { ImGuiKey_LeftBracket, 17, "[" },
    { ImGuiKey_Equal,       18, "=" },
    { ImGuiKey_RightBracket,19, "]" },
};
static constexpr int QWERTY_MAP_COUNT = sizeof(s_qwertyMap) / sizeof(s_qwertyMap[0]);

static const char* qwerty_label_for_offset(int offset) {
    for (int i = 0; i < QWERTY_MAP_COUNT; ++i)
        if (s_qwertyMap[i].offset == offset) return s_qwertyMap[i].label;
    return "";
}

// Triangle-button spinner for int values (tight auto-width)
static bool spinner_int(const char* id, int* val, int step, int minVal, int maxVal) {
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) { *val = std::max(minVal, *val - step); changed = true; }
    ImGui::SameLine(0, 2);
    // Size to fit the widest possible value in the range
    char maxBuf[32];
    int wider = (std::abs(minVal) > std::abs(maxVal)) ? minVal : maxVal;
    snprintf(maxBuf, sizeof(maxBuf), "%d", wider);
    float w = ImGui::CalcTextSize(maxBuf).x + ImGui::GetStyle().FramePadding.x * 2 + 4;
    ImGui::SetNextItemWidth(std::max(w, 20.0f));
    if (ImGui::InputInt("##v", val, 0, 0)) { *val = std::clamp(*val, minVal, maxVal); changed = true; }
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) { *val = std::min(maxVal, *val + step); changed = true; }
    ImGui::PopID();
    return changed;
}

// Triangle-button spinner for float values (tight auto-width)
static bool spinner_float(const char* id, float* val, float step, float minVal, float maxVal, const char* fmt = "%.1f") {
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) { *val = std::max(minVal, *val - step); changed = true; }
    ImGui::SameLine(0, 2);
    // Size to fit the widest plausible formatted value
    char maxBuf[32];
    float wider = (std::fabs(minVal) > std::fabs(maxVal)) ? minVal : maxVal;
    snprintf(maxBuf, sizeof(maxBuf), fmt, wider);
    float w = ImGui::CalcTextSize(maxBuf).x + ImGui::GetStyle().FramePadding.x * 2 + 4;
    ImGui::SetNextItemWidth(std::max(w, 24.0f));
    if (ImGui::InputFloat("##v", val, 0, 0, fmt)) { *val = std::clamp(*val, minVal, maxVal); changed = true; }
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) { *val = std::min(maxVal, *val + step); changed = true; }
    ImGui::PopID();
    return changed;
}

// ===========================================================================
// Audition: walk through WAVs in a --explore sweep folder, save the patch
// behind any interesting one to the user's curated folder.
// ===========================================================================

struct AuditionState {
    std::string folder;                          // current sweep folder
    std::vector<std::string> wavFiles;           // basenames, sorted
    int  currentIdx     = -1;                    // -1 = nothing loaded
    bool autoAdvance    = true;
    bool playing        = false;                 // user wants playback active
    bool open           = false;                 // window visible

    // Save modal
    bool showSaveModal       = false;
    bool wasPlayingBeforeSave= false;  // restore audio on modal close
    bool overwriteExisting   = false;  // checkbox in the modal
    char saveName[128]       = {};
    char saveStatus[256]     = {};

    // Target pane: JSON patches in the curated folder. Selection is by
    // basename so it survives list refreshes (save/delete re-scan).
    std::vector<std::string> jsonFiles;          // basenames, sorted
    int  jsonSelectedIdx = -1;                   // -1 = nothing selected
    bool showDeleteModal = false;                // Delete pressed on a selection

    // Loaded sample buffer (left channel of the stereo WAV, mono content)
    std::vector<float> currentBuffer;
    int                currentSampleRate = 48000;
};
static AuditionState g_audition;

// Re-scan the Target (curated) folder's .json patches. Called on window
// open, Target folder change, after a successful Save, and after Delete.
static void audition_refresh_target_list() {
    std::string keep = (g_audition.jsonSelectedIdx >= 0 &&
                        g_audition.jsonSelectedIdx < (int)g_audition.jsonFiles.size())
        ? g_audition.jsonFiles[g_audition.jsonSelectedIdx] : std::string();
    g_audition.jsonFiles.clear();
    g_audition.jsonSelectedIdx = -1;
    const std::string& folder = g_settings.curatedFolder;
    if (folder.empty() || !std::filesystem::exists(folder)) return;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        std::string ext = p.extension().string();
        for (auto& c : ext) c = (char)std::tolower(c);
        if (ext == ".json") g_audition.jsonFiles.push_back(p.filename().string());
    }
    std::sort(g_audition.jsonFiles.begin(), g_audition.jsonFiles.end());
    if (!keep.empty()) {
        auto it = std::find(g_audition.jsonFiles.begin(), g_audition.jsonFiles.end(), keep);
        if (it != g_audition.jsonFiles.end())
            g_audition.jsonSelectedIdx = (int)(it - g_audition.jsonFiles.begin());
    }
}

static void audition_load_folder(const std::string& folder) {
    g_audition.folder = folder;
    g_audition.wavFiles.clear();
    g_audition.currentIdx = -1;
    g_audition.playing    = false;
    if (folder.empty() || !std::filesystem::exists(folder)) return;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file()) continue;
        auto p = entry.path();
        std::string ext = p.extension().string();
        for (auto& c : ext) c = (char)std::tolower(c);
        if (ext == ".wav") g_audition.wavFiles.push_back(p.filename().string());
    }
    std::sort(g_audition.wavFiles.begin(), g_audition.wavFiles.end());
}

// Real folder picker: IFileOpenDialog with FOS_PICKFOLDERS. Replaces the old
// "pick any file in the target folder, use its parent" workaround.
static std::string pick_folder_dialog(const std::string& initialDir = std::string()) {
    std::string result;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // S_OK and S_FALSE (already initialized) both require CoUninitialize;
    // RPC_E_CHANGED_MODE (FAILED) must not call it.
    bool needUninit = SUCCEEDED(hrInit);
    IFileOpenDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&dlg)))) {
        FILEOPENDIALOGOPTIONS opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        std::error_code ec;
        if (!initialDir.empty() && std::filesystem::exists(initialDir, ec)) {
            int wlen = MultiByteToWideChar(CP_ACP, 0, initialDir.c_str(), -1, nullptr, 0);
            if (wlen > 1) {
                std::wstring wdir(size_t(wlen), L'\0');
                MultiByteToWideChar(CP_ACP, 0, initialDir.c_str(), -1, wdir.data(), wlen);
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(wdir.c_str(), nullptr,
                                                          IID_PPV_ARGS(&folder)))) {
                    dlg->SetFolder(folder);
                    folder->Release();
                }
            }
        }
        if (SUCCEEDED(dlg->Show(nullptr))) {   // fails with cancel HRESULT if dismissed
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item))) {
                PWSTR wpath = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &wpath))) {
                    // Convert to the ANSI code page — the rest of the app's
                    // file paths (GetOpenFileNameA, std::filesystem from
                    // narrow strings) use ACP, not UTF-8.
                    int len = WideCharToMultiByte(CP_ACP, 0, wpath, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 1) {
                        std::string buf(size_t(len - 1), '\0');
                        WideCharToMultiByte(CP_ACP, 0, wpath, -1, buf.data(), len, nullptr, nullptr);
                        result = std::move(buf);
                    }
                    CoTaskMemFree(wpath);
                }
                item->Release();
            }
        }
        dlg->Release();
    }
    if (needUninit) CoUninitialize();
    return result;
}

static void audition_load_at(int idx) {
    if (idx < 0 || idx >= (int)g_audition.wavFiles.size()) return;
    g_audition.currentIdx = idx;

    std::filesystem::path p =
        std::filesystem::path(g_audition.folder) / g_audition.wavFiles[idx];
    std::vector<float> interleaved;
    int sr = 48000;
    if (!read_wav_16le_stereo(p.string(), interleaved, sr)) {
        char buf[300];
        snprintf(buf, sizeof(buf), "Audition: could not read %s", p.string().c_str());
        transport_set_status(buf, true);
        return;
    }
    g_audition.currentSampleRate = sr;
    // Convert interleaved L/R to mono (take L; --explore writes L=R)
    int frames = int(interleaved.size() / 2);
    g_audition.currentBuffer.resize(size_t(frames));
    for (int i = 0; i < frames; ++i)
        g_audition.currentBuffer[size_t(i)] = interleaved[size_t(i) * 2];

    // Hand off to the existing buffer-playback path (mono, in-place).
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        g_bufferPlayback   = g_audition.currentBuffer.data();
        g_bufferPlaybackPos= 0;
        g_bufferPlaybackLen= int(g_audition.currentBuffer.size());
    }
    g_audition.playing = true;
}

static void audition_stop() {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    g_bufferPlayback = nullptr;
    g_bufferPlaybackPos = 0;
    g_bufferPlaybackLen = 0;
    g_audition.playing = false;
}

static void audition_play_current() {
    if (g_audition.currentIdx < 0 && !g_audition.wavFiles.empty()) {
        audition_load_at(0);
    } else if (g_audition.currentIdx >= 0) {
        audition_load_at(g_audition.currentIdx);  // restart from beginning
    }
}

static void audition_next() {
    if (g_audition.wavFiles.empty()) return;
    int next = g_audition.currentIdx + 1;
    if (next >= (int)g_audition.wavFiles.size()) {
        audition_stop();
        return;
    }
    audition_load_at(next);
}

static void audition_prev() {
    if (g_audition.wavFiles.empty()) return;
    int prev = std::max(0, g_audition.currentIdx - 1);
    audition_load_at(prev);
}

// Called once per UI frame: if user asked for playback, the buffer was
// finished by the audio callback (g_bufferPlayback set to null), and
// autoAdvance is on, queue the next file.
static void audition_tick() {
    if (!g_audition.playing) return;
    bool bufferActive = false;
    {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        bufferActive = (g_bufferPlayback != nullptr);
    }
    if (bufferActive) return;
    // Buffer finished
    if (g_audition.autoAdvance) {
        audition_next();
    } else {
        g_audition.playing = false;
    }
}

// Copy the .patch.json corresponding to the current WAV into the curated
// folder under a user-supplied name. The curated folder is created if it
// doesn't exist. If `overwrite` is false, a name collision is an error;
// if true, the existing file is replaced.
static bool audition_save_current_patch(const std::string& userName,
                                        bool overwrite,
                                        std::string& errOut) {
    if (g_audition.currentIdx < 0) {
        errOut = "No variant selected.";
        return false;
    }
    std::string wav = g_audition.wavFiles[g_audition.currentIdx];
    // <basename>.wav -> <basename>.patch.json
    std::string base = wav.substr(0, wav.size() - 4);
    std::filesystem::path src =
        std::filesystem::path(g_audition.folder) / (base + ".patch.json");
    if (!std::filesystem::exists(src)) {
        errOut = "Source patch not found: " + src.string();
        return false;
    }
    std::filesystem::path dstDir = g_settings.curatedFolder;
    std::error_code ec;
    std::filesystem::create_directories(dstDir, ec);
    // Sanitize user-supplied name: strip extension, disallow path separators.
    std::string name = userName;
    for (char& c : name) if (c == '/' || c == '\\') c = '_';
    if (name.empty()) { errOut = "Name is empty."; return false; }
    // Add .json if user didn't include an extension.
    if (name.size() < 5 || name.substr(name.size() - 5) != ".json")
        name += ".json";
    std::filesystem::path dst = dstDir / name;
    if (std::filesystem::exists(dst) && !overwrite) {
        errOut = "A patch with that name already exists. "
                 "Tick 'Overwrite' to replace it.";
        return false;
    }
    auto opts = overwrite
        ? std::filesystem::copy_options::overwrite_existing
        : std::filesystem::copy_options::none;
    std::filesystem::copy_file(src, dst, opts, ec);
    if (ec) {
        errOut = "Copy failed: " + ec.message();
        return false;
    }
    errOut = "Saved to " + dst.string();
    return true;
}

static void draw_audition_window() {
    // If window was just closed (X clicked: ImGui flipped open to false on
    // the prior frame), kill audio. Otherwise the WAVs keep playing in the
    // background with no UI to control them. On open, re-scan the Target
    // folder so the patch list is current.
    static bool prevOpen = false;
    if (prevOpen && !g_audition.open) audition_stop();
    if (!prevOpen && g_audition.open) audition_refresh_target_list();
    prevOpen = g_audition.open;

    if (!g_audition.open) return;

    ImGui::SetNextWindowSize(ImVec2(780, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Audition", &g_audition.open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    float paneW = ImGui::GetContentRegionAvail().x * 0.5f
                - ImGui::GetStyle().ItemSpacing.x * 0.5f;

    // ---------------------------------------------------------------------
    // Left pane: Source (sweep) folder, transport controls, WAV list
    // ---------------------------------------------------------------------
    if (ImGui::BeginChild("##audition_left", ImVec2(paneW, 0), true)) {
        ImGui::Text("Source folder:");
        ImGui::SameLine();
        if (ImGui::Button("Browse##audition_browse")) {
            std::string f = pick_folder_dialog(g_settings.lastAuditionDir);
            if (!f.empty()) {
                g_settings.lastAuditionDir = f;
                settings_save();
                audition_load_folder(f);
            }
        }
        ImGui::TextWrapped("%s", g_audition.folder.empty() ? "(none)" : g_audition.folder.c_str());

        // --- Now playing ---
        if (g_audition.wavFiles.empty()) {
            ImGui::TextDisabled("(no .wav files in the selected folder)");
        } else {
            int n = (int)g_audition.wavFiles.size();
            ImGui::Text("Variant %d of %d:  %s",
                        g_audition.currentIdx + 1, n,
                        g_audition.currentIdx >= 0
                            ? g_audition.wavFiles[g_audition.currentIdx].c_str()
                            : "(none)");
        }

        // --- Transport ---
        bool canTransport = !g_audition.wavFiles.empty();
        ImGui::BeginDisabled(!canTransport);
        if (ImGui::Button("Prev"))   audition_prev();
        ImGui::SameLine();
        if (g_audition.playing) {
            if (ImGui::Button("Stop"))   audition_stop();
        } else {
            if (ImGui::Button("Play"))   audition_play_current();
        }
        ImGui::SameLine();
        if (ImGui::Button("Repeat")) audition_load_at(g_audition.currentIdx);
        ImGui::SameLine();
        if (ImGui::Button("Next"))   audition_next();
        ImGui::SameLine();
        ImGui::Checkbox("Auto-advance", &g_audition.autoAdvance);
        ImGui::SameLine();
        if (ImGui::Button("Save...")) {
            // Pause audio while the modal is up so you can focus on naming;
            // resume on Save success or Cancel.
            g_audition.wasPlayingBeforeSave = g_audition.playing;
            if (g_audition.playing) audition_stop();

            g_audition.showSaveModal     = true;
            g_audition.overwriteExisting = false;
            g_audition.saveStatus[0]     = '\0';
            // Pre-fill name with the variant id
            if (g_audition.currentIdx >= 0) {
                const auto& wav = g_audition.wavFiles[g_audition.currentIdx];
                std::string base = wav.substr(0, wav.size() - 4);
                snprintf(g_audition.saveName, sizeof(g_audition.saveName),
                         "%s", base.c_str());
            }
        }
        ImGui::EndDisabled();

        // --- WAV file list ---
        ImGui::Separator();
        if (ImGui::BeginChild("##audition_list", ImVec2(0, 0), true)) {
            for (int i = 0; i < (int)g_audition.wavFiles.size(); ++i) {
                bool selected = (i == g_audition.currentIdx);
                if (ImGui::Selectable(g_audition.wavFiles[i].c_str(), selected)) {
                    audition_load_at(i);
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ---------------------------------------------------------------------
    // Right pane: Target (curated) folder, saved-patch list. Only action:
    // click a filename, hit Delete → confirm modal → file removed.
    // ---------------------------------------------------------------------
    bool wantDelete = false;
    if (ImGui::BeginChild("##audition_right", ImVec2(0, 0), true)) {
        ImGui::Text("Target folder:");
        ImGui::SameLine();
        if (ImGui::Button("Browse##curated_browse")) {
            std::string f = pick_folder_dialog(g_settings.curatedFolder);
            if (!f.empty()) {
                g_settings.curatedFolder = f;
                settings_save();
                audition_refresh_target_list();
            }
        }
        ImGui::TextWrapped("%s", g_settings.curatedFolder.c_str());

        // --- Patch file list ---
        ImGui::Separator();
        if (ImGui::BeginChild("##audition_jsonlist", ImVec2(0, 0), true)) {
            if (g_audition.jsonFiles.empty())
                ImGui::TextDisabled("(no .json patches in the target folder)");
            for (int i = 0; i < (int)g_audition.jsonFiles.size(); ++i) {
                bool selected = (i == g_audition.jsonSelectedIdx);
                if (ImGui::Selectable(g_audition.jsonFiles[i].c_str(), selected))
                    g_audition.jsonSelectedIdx = i;
            }
            // Delete key acts on the selection — gated on THIS list having
            // focus, so it can't collide with the node editor's Delete.
            if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput &&
                g_audition.jsonSelectedIdx >= 0 &&
                ImGui::IsKeyPressed(ImGuiKey_Delete))
                wantDelete = true;
        }
        ImGui::EndChild();
    }
    ImGui::EndChild();

    ImGui::End();

    if (wantDelete) g_audition.showDeleteModal = true;

    // --- Delete-confirm modal (one keypress: Delete → Enter) ---
    if (g_audition.showDeleteModal) {
        ImGui::OpenPopup("Delete curated patch");
        g_audition.showDeleteModal = false;
    }
    if (ImGui::BeginPopupModal("Delete curated patch", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        bool haveSel = g_audition.jsonSelectedIdx >= 0 &&
                       g_audition.jsonSelectedIdx < (int)g_audition.jsonFiles.size();
        if (!haveSel) {
            ImGui::CloseCurrentPopup();
        } else {
            // Copy the name — the refresh below invalidates the list entry.
            std::string name = g_audition.jsonFiles[g_audition.jsonSelectedIdx];
            ImGui::Text("Delete %s from the Target folder?", name.c_str());
            ImGui::TextDisabled("Enter to delete, Esc to cancel.");
            ImGui::Spacing();
            bool doDelete = ImGui::Button("Delete", ImVec2(100, 0)) ||
                            ImGui::IsKeyPressed(ImGuiKey_Enter) ||
                            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
            ImGui::SameLine();
            bool doCancel = ImGui::Button("Cancel", ImVec2(100, 0)) ||
                            ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (doDelete) {
                std::filesystem::path p =
                    std::filesystem::path(g_settings.curatedFolder) / name;
                std::error_code ec;
                std::filesystem::remove(p, ec);
                char buf[300];
                if (ec) {
                    snprintf(buf, sizeof(buf), "Delete failed: %s", ec.message().c_str());
                    transport_set_status(buf, true);
                } else {
                    snprintf(buf, sizeof(buf), "Deleted %s", p.string().c_str());
                    transport_set_status(buf, false);
                }
                g_audition.jsonSelectedIdx = -1;
                audition_refresh_target_list();
                ImGui::CloseCurrentPopup();
            } else if (doCancel) {
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    // --- Save modal ---
    if (g_audition.showSaveModal) {
        ImGui::OpenPopup("Save curated patch");
        g_audition.showSaveModal = false;
    }
    if (ImGui::BeginPopupModal("Save curated patch", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save the .patch.json behind this variant.");
        ImGui::Text("Target folder: %s", g_settings.curatedFolder.c_str());
        ImGui::Spacing();
        ImGui::Text("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(320.0f);
        ImGui::InputText("##save_name", g_audition.saveName,
                         sizeof(g_audition.saveName));
        ImGui::Checkbox("Overwrite if exists", &g_audition.overwriteExisting);
        if (g_audition.saveStatus[0]) {
            ImGui::TextWrapped("%s", g_audition.saveStatus);
        }
        ImGui::Spacing();
        if (ImGui::Button("Save")) {
            std::string err;
            if (audition_save_current_patch(g_audition.saveName,
                                            g_audition.overwriteExisting, err)) {
                snprintf(g_audition.saveStatus, sizeof(g_audition.saveStatus),
                         "%s", err.c_str());
                audition_refresh_target_list();   // new patch shows up immediately
                ImGui::CloseCurrentPopup();
                if (g_audition.wasPlayingBeforeSave)
                    audition_load_at(g_audition.currentIdx);
                g_audition.wasPlayingBeforeSave = false;
            } else {
                snprintf(g_audition.saveStatus, sizeof(g_audition.saveStatus),
                         "%s", err.c_str());
                // Stay in modal — collision / empty name etc. — keep paused.
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            g_audition.saveStatus[0] = '\0';
            ImGui::CloseCurrentPopup();
            if (g_audition.wasPlayingBeforeSave)
                audition_load_at(g_audition.currentIdx);
            g_audition.wasPlayingBeforeSave = false;
        }
        ImGui::EndPopup();
    }
}

// ===========================================================================
// ===========================================================================
// Shape editor (docs/shape_editor_design.md) — unified breakpoint canvas,
// opened by double-clicking a shape preview. Increment 2: window shell +
// CurveNode client. Always points; the canvas draws the editing skeleton
// (straight grey lines between points) with the ENGINE-evaluated curve
// overlaid, so what you shape and what renders stay distinguishable.
// ===========================================================================
struct ShapeEditorState {
    bool  open{false};
    int   nodeId{-1};
    enum class Client { Curve, Segment, Shaper } client{Client::Curve};
    bool  logX{true};
    float xMin{20.0f}, xMax{16000.0f}, yMin{0.0f}, yMax{1.0f};
    int   dragIdx{-1};
    bool  fitPending{false};
    bool  readOnly{false};   // segment shapes above the density limit
    // Shaper client per-segment overrides: which segment's power is being
    // dragged (via its mid-segment dot or the creating right-drag), and the
    // SIGNED curvature at drag start. Curvature c = ±log2(power): positive
    // = InverseExpo (bulge up/out), negative = Expo (bulge down/in), 0 =
    // linear. Dragging sweeps c, so the curve crosses smoothly through
    // straight and out the other side — no fractional-power kinks.
    int   segDragIdx{-1};
    float segDragStartCurv{0.0f};
    // One-shot revert stash for preset insertion (the editor has no undo
    // stack; this delivers "undoable" as a single Revert).
    bool  hasRevert{false};
    std::vector<std::pair<float, float>> revertPts;
    std::vector<Curve::Seg> revertSegs;
    // Morph A/B workflow (curve-morph spec §5): which curve is being
    // edited. 0 = A (values/segs), 1 = B (values2/segs2). Structure
    // (point count, order, segment types) is shared; geometry per-curve.
    int   activeCurve{0};
};
static ShapeEditorState s_shapeEd;

// Segment shapes above this many points open read-only — sampled textures
// are generator territory, not point-dragging territory (spec, non-goals).
static constexpr int kShapeEdMaxPoints = 512;

// Generator panel state (Pulse Train, the founding preset —
// docs/notes/SegmentRevisit.md parameterized). Params live here, not in the
// patch: the emitted points are the artifact; seed shown for reproduction.
struct ShapeEdGenState {
    int   count{8};
    float width{40.0f};        // samples per pulse side (triangle half-width)
    float widthRamp{1.0f};     // last pulse's width as x of the first (click->clack->clunk)
    float spacing{80.0f};      // samples between pulses
    float spacingRamp{1.0f};   // <1 sparse->dense ("pile up into the mountain"), >1 dense->sparse
    float peakEnd{1.0f};       // last peak as fraction of the first (tail-off decay)
    float varPct{0.10f};       // generate-time jitter on widths/spacings/peaks
    bool  bipolar{false};      // alternate pulse sign (SegmentRevisit's open question)
    int   seed{1234};
    bool  showVariance{true};  // draw the varPct ghost traces on the canvas
};
static ShapeEdGenState s_shapeEdGen;

// Emit a pulse train as absolute (x, y) points: each pulse is rise-to-peak
// then fall-to-zero (two points), then a gap to the next.
static std::vector<std::pair<float, float>> shape_ed_gen_pulse_train(
        const ShapeEdGenState& g) {
    std::vector<std::pair<float, float>> pts;
    Randomizer rng(uint32_t(g.seed));
    const int n = std::max(1, g.count);
    float t = 0.0f, prevEnd = 0.0f;
    for (int k = 0; k < n; ++k) {
        const float u = n > 1 ? float(k) / float(n - 1) : 0.0f;
        const float ramp = std::pow(std::max(0.01f, g.widthRamp), u);
        const float sramp = std::pow(std::max(0.01f, g.spacingRamp), u);
        float w = g.width * ramp
                * rng.range(1.0f - g.varPct, 1.0f + g.varPct);
        float s = g.spacing * sramp
                * rng.range(1.0f - g.varPct, 1.0f + g.varPct);
        float peak = (1.0f + (g.peakEnd - 1.0f) * u)
                   * rng.range(1.0f - g.varPct, 1.0f + g.varPct);
        peak = std::clamp(peak, -1.0f, 1.0f);
        if (g.bipolar && (k & 1)) peak = -peak;
        w = std::max(1.0f, w);
        s = std::max(0.0f, s);
        // Hold zero through the gap: without this point the engine ramps
        // from the previous pulse's base straight up to this peak across
        // spacing+width samples, and "spacing" is never silent.
        if (k > 0 && t > prevEnd + 1e-3f)
            pts.emplace_back(t, 0.0f);
        pts.emplace_back(t + w, peak);          // rise to the peak
        pts.emplace_back(t + 2.0f * w, 0.0f);   // fall back to zero
        prevEnd = t + 2.0f * w;
        t = prevEnd + s;
    }
    return pts;
}

// Shaper preset curves (curve-morph spec §4): the five junction
// nonlinearities nature provides + the degenerate sixth, plus the standard
// waveforms as transfer curves (sine/triangle fold, saw wraps). Hardcoded
// by design — a curve-library file format is deferred until this list
// feels cramped. segs tuples are (segment index, typeIdx 1..5, power)
// in the engine encoding (2 Expo, 4 Sine, 1 Linear, 5 Hold).
struct ShaperPreset {
    const char* name;
    std::vector<std::pair<float, float>> pts;
    std::vector<std::tuple<int, int, float>> segs;
};
// Point tables are dense samples of the physics formulas from the 09-05
// junction-curve session (bow friction, Bernoulli reeds, lip valve, jet
// tanh) — the shapes hold without leaning on the smoothness pin, and no
// decorative overrides (reed2's Hold tail is the one structural override:
// the closed reed). Odd-symmetric families list negative-x side explicitly
// because opening clamps differ from closing clamps.
static const ShaperPreset kShaperPresets[] = {
    {"bow",      // sign(x)*(0.33 + 0.67/(1+9|x|)): stick spike, slip flanks
     {{-1.00f,-0.40f},{-0.80f,-0.41f},{-0.55f,-0.44f},{-0.35f,-0.49f},
      {-0.20f,-0.57f},{-0.10f,-0.68f},{-0.05f,-0.79f},{-0.015f,-0.92f},
      { 0.015f, 0.92f},{ 0.05f, 0.79f},{ 0.10f, 0.68f},{ 0.20f, 0.57f},
      { 0.35f, 0.49f},{ 0.55f, 0.44f},{ 0.80f, 0.41f},{ 1.00f, 0.40f}}, {}},
    {"reed1",    // 1.15*clamp(1-x,0,1.25)*sign(x)*sqrt|x|: closes at x=1
     {{-1.00f,-1.44f},{-0.75f,-1.24f},{-0.55f,-1.07f},{-0.35f,-0.85f},
      {-0.20f,-0.62f},{-0.10f,-0.40f},{-0.05f,-0.27f},{ 0.00f, 0.00f},
      { 0.05f, 0.24f},{ 0.10f, 0.33f},{ 0.20f, 0.41f},{ 0.33f, 0.44f},
      { 0.45f, 0.42f},{ 0.60f, 0.36f},{ 0.75f, 0.25f},{ 0.90f, 0.11f},
      { 1.00f, 0.00f}}, {}},
    {"reed2",    // steeper blades, slam shut at x=0.556; Hold = closed
     {{-1.00f,-1.44f},{-0.75f,-1.24f},{-0.50f,-1.02f},{-0.30f,-0.79f},
      {-0.14f,-0.54f},{-0.07f,-0.34f},{ 0.00f, 0.00f},{ 0.05f, 0.23f},
      { 0.10f, 0.30f},{ 0.185f,0.33f},{ 0.27f, 0.31f},{ 0.35f, 0.25f},
      { 0.45f, 0.15f},{ 0.52f, 0.05f},{ 0.556f,0.00f},{ 1.00f, 0.00f}},
     {{14,5,0.0f}}},
    {"lip",      // shallow leak below, min(1, 2.1*x^1.8) rising to sat
     {{-1.00f,-0.32f},{-0.50f,-0.16f},{ 0.00f, 0.00f},{ 0.10f, 0.03f},
      { 0.20f, 0.12f},{ 0.30f, 0.24f},{ 0.40f, 0.40f},{ 0.50f, 0.60f},
      { 0.60f, 0.84f},{ 0.66f, 1.00f},{ 1.00f, 1.00f}}, {}},
    {"jet",      // 0.85*tanh(2.6x)
     {{-1.00f,-0.84f},{-0.80f,-0.82f},{-0.60f,-0.78f},{-0.45f,-0.70f},
      {-0.30f,-0.56f},{-0.20f,-0.41f},{-0.10f,-0.22f},{ 0.00f, 0.00f},
      { 0.10f, 0.22f},{ 0.20f, 0.41f},{ 0.30f, 0.56f},{ 0.45f, 0.70f},
      { 0.60f, 0.78f},{ 0.80f, 0.82f},{ 1.00f, 0.84f}}, {}},
    {"hard",     {{-0.52f,-0.60f},{ 0.52f, 0.60f}}, {}},
    {"sine",     // sin(pi*x), one cycle across the domain
     {{-1.00f, 0.00f},{-0.83f,-0.50f},{-0.67f,-0.87f},{-0.50f,-1.00f},
      {-0.33f,-0.87f},{-0.17f,-0.50f},{ 0.00f, 0.00f},{ 0.17f, 0.50f},
      { 0.33f, 0.87f},{ 0.50f, 1.00f},{ 0.67f, 0.87f},{ 0.83f, 0.50f},
      { 1.00f, 0.00f}}, {}},
    {"saw",      {{-1.00f,-1.00f},{-0.002f, 1.00f},{ 0.002f,-1.00f},
                  { 1.00f, 1.00f}}, {}},
    {"triangle", {{-1.00f, 0.00f},{-0.50f,-1.00f},{ 0.00f, 0.00f},
                  { 0.50f, 1.00f},{ 1.00f, 0.00f}}, {}},
};

static GraphNode* shape_ed_node() {
    for (auto& n : s_nodes)
        if (n.id == s_shapeEd.nodeId) return &n;
    return nullptr;
}

static void shape_editor_open_curve(GraphNode& node) {
    s_shapeEd = ShapeEditorState{};
    s_shapeEd.open = true;
    s_shapeEd.nodeId = node.id;
    s_shapeEd.client = ShapeEditorState::Client::Curve;
    // logx/loglog curves default to a log x-axis; linear curves to linear.
    s_shapeEd.logX = node.curveInterp != 0;
    s_shapeEd.fitPending = true;
    ImGui::SetWindowFocus("###shapeEditor");
}

static void shape_editor_open_segment(GraphNode& node) {
    s_shapeEd = ShapeEditorState{};
    s_shapeEd.open = true;
    s_shapeEd.nodeId = node.id;
    s_shapeEd.client = ShapeEditorState::Client::Segment;
    s_shapeEd.logX = false;  // time axis
    s_shapeEd.fitPending = true;
    ImGui::SetWindowFocus("###shapeEditor");
}

static void shape_editor_open_shaper(GraphNode& node) {
    s_shapeEd = ShapeEditorState{};
    s_shapeEd.open = true;
    s_shapeEd.nodeId = node.id;
    s_shapeEd.client = ShapeEditorState::Client::Shaper;
    s_shapeEd.logX = false;  // input-value axis, four-quadrant
    s_shapeEd.fitPending = true;
    ImGui::SetWindowFocus("###shapeEditor");
}

// Segment client accessors: the values array lives in arrayValues (the same
// storage the pair table edits, so the two stay in sync frame to frame).
static std::vector<float>* shape_ed_segment_values(GraphNode& node) {
    for (auto& [d, v] : node.arrayValues)
        if (std::string_view(d.name) == "values") return &v;
    return nullptr;
}

// Shaper client: values are ABSOLUTE (x, y) pairs — no delta math, no
// timeMode; the flat<->pairs conversion is the whole adapter.
static std::vector<std::pair<float, float>> shape_ed_shaper_load(
        const std::vector<float>& vals) {
    std::vector<std::pair<float, float>> pts;
    for (size_t i = 0; i + 1 < vals.size(); i += 2)
        pts.emplace_back(vals[i], vals[i + 1]);
    return pts;
}

static void shape_editor_apply_shaper_named(
        GraphNode& node, std::vector<std::pair<float, float>>& pts,
        const char* name);

static void shape_editor_apply_shaper(
        GraphNode& node, std::vector<std::pair<float, float>>& pts) {
    shape_editor_apply_shaper_named(node, pts, "values");
}

// Per-segment override accessors (Shaper client). The "segs" array uses the
// engine encoding (ShaperSource::decode_segs/encode_segs: flat [typeIdx,
// power] pairs, typeIdx 0 = default). Loaded per frame like the points so
// table edits and editor edits never go stale against each other.
static std::vector<float>* shape_ed_array(GraphNode& node, const char* name) {
    for (auto& [d, v] : node.arrayValues)
        if (std::string_view(d.name) == name) return &v;
    return nullptr;
}

static std::vector<Curve::Seg> shape_ed_segs_load(GraphNode& node,
                                                  size_t nPts,
                                                  const char* name = "segs") {
    std::vector<Curve::Seg> segs;
    if (auto* raw = shape_ed_array(node, name))
        segs = ShaperSource::decode_segs(*raw);
    segs.resize(nPts > 0 ? nPts - 1 : 0);
    return segs;
}

// Signed curvature of a power-family segment override (see
// ShapeEditorState::segDragStartCurv). Non-power types report 0.
static float shape_ed_seg_curv(const Curve::Seg& s) {
    if (!s.overridden) return 0.0f;
    if (s.type == RampType::Expo)
        return -std::log2(std::max(s.power, 1e-3f));
    if (s.type == RampType::InverseExpo)
        return std::log2(std::max(s.power, 1e-3f));
    return 0.0f;
}
static void shape_ed_seg_from_curv(Curve::Seg& s, float c) {
    c = std::clamp(c, -4.32f, 4.32f);   // power capped at ~20
    s.overridden = true;
    s.type = c >= 0.0f ? RampType::InverseExpo : RampType::Expo;
    s.power = std::pow(2.0f, std::fabs(c));
}

static void shape_editor_apply_segs(GraphNode& node,
                                    const std::vector<Curve::Seg>& segs,
                                    const char* name = "segs") {
    std::vector<float>* raw = shape_ed_array(node, name);
    if (!raw) return;
    *raw = ShaperSource::encode_segs(segs);
    node.push_array(name);
    mark_graph_dirty();
}

// Flat write of absolute (x, y) points into a named Shaper array.
static void shape_editor_apply_shaper_named(
        GraphNode& node, std::vector<std::pair<float, float>>& pts,
        const char* name) {
    std::stable_sort(pts.begin(), pts.end(),
                     [](const std::pair<float, float>& a,
                        const std::pair<float, float>& b) {
                         return a.first < b.first;
                     });
    std::vector<float>* vals = shape_ed_array(node, name);
    if (!vals) return;
    vals->clear();
    vals->reserve(pts.size() * 2);
    for (auto& [x, y] : pts) {
        vals->push_back(x);
        vals->push_back(y);
    }
    node.push_array(name);
    mark_graph_dirty();
}

// Deltas -> absolute points (x = cumulative width, y = arrival value).
static std::vector<std::pair<float, float>> shape_ed_segment_load(
        const std::vector<float>& vals) {
    std::vector<std::pair<float, float>> pts;
    float acc = 0.0f;
    for (size_t i = 0; i + 1 < vals.size(); i += 2) {
        acc += std::max(0.0f, vals[i]);
        pts.emplace_back(acc, vals[i + 1]);
    }
    return pts;
}

// Write an explicit timeMode setting (1=samples, 2=seconds) to the node.
static void shape_ed_set_time_mode(GraphNode& node, int mode) {
    for (auto& [d, v] : node.settingValues)
        if (std::string_view(d.name) == "timeMode") v = float(mode);
    if (node.dspSource) {
        std::lock_guard<std::mutex> lock(g_audioMutex);
        node.dspSource->set_setting("timeMode", float(mode));
    }
    node.jsonExtras.erase("timeMode");
    mark_graph_dirty();
}

// Absolute points -> deltas, pushed live (same path as the pair table).
static void shape_editor_apply_segment(
        GraphNode& node, const std::vector<std::pair<float, float>>& pts) {
    std::vector<float>* vals = shape_ed_segment_values(node);
    if (!vals) return;
    // Pin the units BEFORE the edit can change values[0]: in auto timeMode
    // the engine re-infers seconds-vs-samples from the first width on every
    // values write, so an edit that pushed the first width under 1.0
    // (inserting a point left of the first, or a 0-width lead-in) silently
    // flipped the whole shape to seconds — heard as silence. Resolve auto
    // against the pre-edit shape and make it explicit.
    if (node.dspSource && int(node.dspSource->get_setting("timeMode")) == 0) {
        const bool secs = !vals->empty() && (*vals)[0] < 1.0f;
        shape_ed_set_time_mode(node, secs ? 2 : 1);
    }
    vals->clear();
    vals->reserve(pts.size() * 2);
    float prevX = 0.0f;
    for (auto& [x, y] : pts) {
        vals->push_back(std::max(0.0f, x - prevX));
        vals->push_back(y);
        prevX = x;
    }
    node.push_array("values");
    mark_graph_dirty();
}

// Push the (sorted) points into the live CurveNode — same semantics as the
// Properties-pane table editor.
static void shape_editor_apply_curve(GraphNode& node) {
    std::stable_sort(node.curveKnots.begin(), node.curveKnots.end(),
                     [](const std::pair<float, float>& a,
                        const std::pair<float, float>& b) {
                         return a.first < b.first;
                     });
    if (auto* cn = dynamic_cast<CurveNode*>(node.dspSource.get())) {
        cn->knots  = node.curveKnots;
        cn->interp = static_cast<CurveNode::CurveInterp>(node.curveInterp);
    }
    mark_graph_dirty();
}

static void draw_shape_editor() {
    if (!s_shapeEd.open) return;
    GraphNode* nodePtr = shape_ed_node();
    const bool isSeg    = s_shapeEd.client == ShapeEditorState::Client::Segment;
    const bool isShaper = s_shapeEd.client == ShapeEditorState::Client::Shaper;
    if (!nodePtr ||
        (!isSeg && !isShaper && nodePtr->typeName != "CurveNode") ||
        (isSeg && nodePtr->typeName != "SegmentSource") ||
        (isShaper && nodePtr->typeName != "Shaper")) {
        s_shapeEd.open = false;
        return;
    }
    GraphNode& node = *nodePtr;

    // Segment/Shaper points reload from arrayValues every frame (cheap at
    // <=512), so table edits and editor edits never go stale against each
    // other. Segment = delta-encoded widths; Shaper = absolute (x, y).
    // With morph (values2 present, same length) the Shaper client edits the
    // ACTIVE curve; the other one is loaded for ghosting and structural
    // mirroring (spec §5: shared structure, two geometries).
    std::vector<std::pair<float, float>> segPts, otherPts;
    std::vector<Curve::Seg> otherSegs;
    bool morphOn = false, editB = false;
    if (isSeg || isShaper) {
        std::vector<float>* valsA = shape_ed_segment_values(node);
        if (!valsA) { s_shapeEd.open = false; return; }
        if (isShaper) {
            std::vector<float>* valsB = shape_ed_array(node, "values2");
            morphOn = valsB && !valsB->empty()
                   && valsB->size() == valsA->size();
            if (!morphOn) s_shapeEd.activeCurve = 0;
            editB = morphOn && s_shapeEd.activeCurve == 1;
            segPts = shape_ed_shaper_load(editB ? *valsB : *valsA);
            if (morphOn)
                otherPts = shape_ed_shaper_load(editB ? *valsA : *valsB);
        } else {
            segPts = shape_ed_segment_load(*valsA);
        }
        s_shapeEd.readOnly = (int)segPts.size() > kShapeEdMaxPoints;
    }
    auto& pts = (isSeg || isShaper) ? segPts : node.curveKnots;
    std::vector<Curve::Seg> segs;
    if (isShaper) {
        segs = shape_ed_segs_load(node, pts.size(),
                                  editB ? "segs2" : "segs");
        if (morphOn)
            otherSegs = shape_ed_segs_load(node, pts.size(),
                                           editB ? "segs" : "segs2");
    }
    const char* activePtsName  = editB ? "values2" : "values";
    const char* activeSegsName = editB ? "segs2"   : "segs";
    const char* otherPtsName   = editB ? "values"  : "values2";
    const char* otherSegsName  = editB ? "segs"    : "segs2";
    bool otherChanged = false;

    ImGui::SetNextWindowSize(ImVec2(760, 440), ImGuiCond_FirstUseEver);
    char title[160];
    snprintf(title, sizeof(title), "Shape — %s###shapeEditor", node.label.c_str());
    bool keepOpen = true;
    if (!ImGui::Begin(title, &keepOpen, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        if (!keepOpen) s_shapeEd.open = false;
        return;
    }

    // --- Toolbar --- (no log x for the Shaper client: its x axis spans
    // negative input values, which a log axis cannot represent)
    if (isShaper) s_shapeEd.logX = false;
    else {
        ImGui::Checkbox("log x", &s_shapeEd.logX);
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("Fit")) s_shapeEd.fitPending = true;
    ImGui::SameLine();
    if (isSeg) {
        // Units from the live node: explicit timeMode wins, else inference.
        const char* units = "samples";
        if (node.dspSource) {
            int tm = int(node.dspSource->get_setting("timeMode"));
            if (tm == 2) units = "seconds";
            else if (tm == 0 && !pts.empty() && pts.front().first < 1.0f)
                units = "seconds";
        }
        ImGui::TextDisabled("x: %s", units);
        ImGui::SameLine();
    } else if (isShaper) {
        if (!s_shapeEd.readOnly) {
            if (!morphOn) {
                if (ImGui::SmallButton("Add Morph")) {
                    // Birth B as a copy of A: correspondence by construction.
                    auto cpPts = segPts;
                    auto cpSegs = segs;
                    shape_editor_apply_shaper_named(node, cpPts, "values2");
                    shape_editor_apply_segs(node, cpSegs, "segs2");
                    s_shapeEd.activeCurve = 1;   // land on B, ready to shape
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Add a second curve; the morph pin blends A to B");
                ImGui::SameLine();
            } else {
                int ac = s_shapeEd.activeCurve;
                if (ImGui::RadioButton("A", ac == 0)) s_shapeEd.activeCurve = 0;
                ImGui::SameLine();
                if (ImGui::RadioButton("B", ac == 1)) s_shapeEd.activeCurve = 1;
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove Morph"))
                    ImGui::OpenPopup("Remove morph?");
                ImGui::SameLine();
                if (ImGui::BeginPopupModal("Remove morph?", nullptr,
                                           ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted(
                        "Collapse to a single curve?\n"
                        "The ACTIVE curve is kept; the other is deleted.");
                    if (ImGui::Button("Remove", ImVec2(120, 0))) {
                        if (editB) {   // keep B: it becomes curve A
                            auto keepPts = segPts;
                            auto keepSegs = segs;
                            shape_editor_apply_shaper_named(node, keepPts,
                                                            "values");
                            shape_editor_apply_segs(node, keepSegs, "segs");
                        }
                        std::vector<std::pair<float, float>> none;
                        shape_editor_apply_shaper_named(node, none, "values2");
                        std::vector<Curve::Seg> noSegs;
                        shape_editor_apply_segs(node, noSegs, "segs2");
                        s_shapeEd.activeCurve = 0;
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cancel", ImVec2(120, 0)))
                        ImGui::CloseCurrentPopup();
                    ImGui::EndPopup();
                }
            }
            if (ImGui::SmallButton("Clear"))
                ImGui::OpenPopup("Clear curve?");
            ImGui::SameLine();
            if (ImGui::BeginPopupModal("Clear curve?", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted(morphOn
                    ? "Reset to the identity curve?\nBoth morph curves and "
                      "all segment overrides are cleared."
                    : "Reset to the identity curve?\nAll points and segment "
                      "overrides are cleared.");
                if (ImGui::Button("Clear", ImVec2(120, 0))) {
                    s_shapeEd.revertPts = pts;
                    s_shapeEd.revertSegs = segs;
                    s_shapeEd.hasRevert = true;
                    pts = {{-1.0f, -1.0f}, {1.0f, 1.0f}};
                    segs.assign(1, Curve::Seg{});
                    shape_editor_apply_shaper_named(node, pts, activePtsName);
                    shape_editor_apply_segs(node, segs, activeSegsName);
                    if (morphOn) {
                        otherPts = pts;
                        otherSegs = segs;
                        otherChanged = true;
                    }
                    s_shapeEd.fitPending = true;
                    s_shapeEd.dragIdx = -1;
                    s_shapeEd.segDragIdx = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            ImGui::PushItemWidth(110.0f);
            if (ImGui::BeginCombo("##shaperPreset", "Preset…")) {
                for (const auto& P : kShaperPresets) {
                    if (ImGui::Selectable(P.name)) {
                        s_shapeEd.revertPts = pts;
                        s_shapeEd.revertSegs = segs;
                        s_shapeEd.hasRevert = true;
                        pts = P.pts;
                        segs.assign(pts.size() - 1, Curve::Seg{});
                        for (const auto& [si, ti, pw] : P.segs)
                            if (si >= 0 && size_t(si) < segs.size())
                                segs[si] = Curve::Seg{true,
                                    RampType(ti - 1), pw};
                        shape_editor_apply_shaper_named(node, pts,
                                                        activePtsName);
                        shape_editor_apply_segs(node, segs, activeSegsName);
                        if (morphOn) {
                            // Correspondence is structural: the other curve
                            // resets to a copy of the preset (point counts
                            // must match; edit it apart afterwards).
                            otherPts = pts;
                            otherSegs = segs;
                            otherChanged = true;
                        }
                        s_shapeEd.fitPending = true;
                        s_shapeEd.dragIdx = -1;
                        s_shapeEd.segDragIdx = -1;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Replaces the current points");
            if (s_shapeEd.hasRevert) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Revert")) {
                    pts = s_shapeEd.revertPts;
                    segs = s_shapeEd.revertSegs;
                    segs.resize(pts.empty() ? 0 : pts.size() - 1);
                    shape_editor_apply_shaper_named(node, pts, activePtsName);
                    shape_editor_apply_segs(node, segs, activeSegsName);
                    if (morphOn && otherPts.size() != pts.size()) {
                        otherPts = pts;
                        otherSegs = segs;
                        otherChanged = true;
                    }
                    s_shapeEd.hasRevert = false;
                    s_shapeEd.fitPending = true;
                }
            }
            ImGui::SameLine();
        }
    }
    bool segsChanged = false;
    if (s_shapeEd.readOnly)
        ImGui::TextColored(ImVec4(0.9f, 0.6f, 0.3f, 1),
            "%d points — too dense to edit; regenerate instead (read-only)",
            (int)pts.size());
    else
        ImGui::TextDisabled("(%d points)", (int)pts.size());
    if (isShaper && !s_shapeEd.readOnly) {
        bool anyOverride = false;
        for (auto& sg : segs) anyOverride |= sg.overridden;
        if (anyOverride) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear overrides"))
                ImGui::OpenPopup("Clear segment overrides?");
        }
        if (ImGui::BeginPopupModal("Clear segment overrides?", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted(
                "Reset every segment to the default interpolation?\n"
                "All per-segment curve shapes on this node are cleared.");
            if (ImGui::Button("Clear", ImVec2(120, 0))) {
                for (auto& sg : segs) sg = Curve::Seg{};
                segsChanged = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    // --- Generator panel (segment client; spec: presets emit points, the
    // points are the artifact, params stay dialog-local) ---
    if (isSeg && !s_shapeEd.readOnly &&
        ImGui::CollapsingHeader("Generate — Pulse Train")) {
        auto& G = s_shapeEdGen;
        ImGui::PushItemWidth(110.0f);
        ImGui::DragInt("pulses", &G.count, 0.2f, 1, 64);
        ImGui::SameLine();
        ImGui::DragFloat("width", &G.width, 1.0f, 1.0f, 4000.0f, "%.0f");
        ImGui::SameLine();
        ImGui::DragFloat("widthRamp", &G.widthRamp, 0.02f, 0.1f, 8.0f, "%.2f");
        ImGui::DragFloat("spacing", &G.spacing, 1.0f, 0.0f, 8000.0f, "%.0f");
        ImGui::SameLine();
        ImGui::DragFloat("spacingRamp", &G.spacingRamp, 0.02f, 0.05f, 8.0f, "%.2f");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("<1: sparse->dense (pile up)\n>1: dense->sparse");
        ImGui::SameLine();
        ImGui::DragFloat("peakEnd", &G.peakEnd, 0.01f, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("varPct", &G.varPct, 0.005f, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::Checkbox("bipolar", &G.bipolar);
        ImGui::SameLine();
        ImGui::InputInt("seed", &G.seed, 0, 0);
        ImGui::PopItemWidth();
        bool emit = false;
        if (ImGui::Button("Generate (new seed)")) {
            G.seed = int(ImGui::GetFrameCount() * 2654435761u & 0x7fffffff);
            emit = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reuse seed")) emit = true;
        ImGui::SameLine();
        ImGui::Checkbox("Show variance", &G.showVariance);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Ghost traces of the node's widthVarPct/"
                              "valVarPct re-randomization\n(nothing to show "
                              "while both are 0)");
        ImGui::SameLine();
        ImGui::TextDisabled("replaces the current points");
        if (emit) {
            auto gen = shape_ed_gen_pulse_train(G);
            // Generator params are defined in samples; a seconds-authored
            // shape must not reinterpret a 40-sample pulse as 40 seconds.
            shape_ed_set_time_mode(node, 1);
            shape_editor_apply_segment(node, gen);
            pts = std::move(gen);
            s_shapeEd.fitPending = true;
            s_shapeEd.dragIdx = -1;
        }
        ImGui::Separator();
    }

    if (s_shapeEd.fitPending && !pts.empty()) {
        float x0 = pts.front().first, x1 = pts.front().first;
        float y0 = pts.front().second, y1 = pts.front().second;
        for (auto& [x, y] : pts) {
            x0 = std::min(x0, x); x1 = std::max(x1, x);
            y0 = std::min(y0, y); y1 = std::max(y1, y);
        }
        if (isSeg) { x0 = 0.0f; y0 = std::min(y0, -1.0f); y1 = std::max(y1, 1.0f); }
        if (isShaper) { y0 = std::min(y0, -1.0f); y1 = std::max(y1, 1.0f); }
        if (x1 - x0 < 1e-6f) { x0 -= 1.0f; x1 += 1.0f; }
        if (y1 - y0 < 1e-6f) { y0 -= 0.5f; y1 += 0.5f; }
        const float xm = (x1 - x0) * 0.08f, ym = (y1 - y0) * 0.10f;
        s_shapeEd.xMin = x0 - xm; s_shapeEd.xMax = x1 + xm;
        s_shapeEd.yMin = y0 - ym; s_shapeEd.yMax = y1 + ym;
        if (s_shapeEd.logX) s_shapeEd.xMin = std::max(s_shapeEd.xMin, x0 * 0.8f);
        s_shapeEd.fitPending = false;
    }
    if (s_shapeEd.logX && s_shapeEd.xMin <= 0.0f) s_shapeEd.xMin = 1e-3f;

    // --- Canvas ---
    ImVec2 cp = ImGui::GetCursorScreenPos();
    ImVec2 cs = ImGui::GetContentRegionAvail();
    if (cs.x < 120.0f) cs.x = 120.0f;
    if (cs.y < 100.0f) cs.y = 100.0f;
    ImGui::InvisibleButton("##shapeCanvas", cs,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight |
        ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(cp, ImVec2(cp.x + cs.x, cp.y + cs.y), IM_COL32(24, 24, 28, 255));
    dl->AddRect(cp, ImVec2(cp.x + cs.x, cp.y + cs.y), IM_COL32(70, 70, 78, 255));
    dl->PushClipRect(cp, ImVec2(cp.x + cs.x, cp.y + cs.y), true);

    const float lxMin = s_shapeEd.logX ? std::log(s_shapeEd.xMin) : s_shapeEd.xMin;
    const float lxMax = s_shapeEd.logX ? std::log(s_shapeEd.xMax) : s_shapeEd.xMax;
    auto tx = [&](float x) {
        float lx = s_shapeEd.logX ? std::log(std::max(x, 1e-6f)) : x;
        return cp.x + (lx - lxMin) / (lxMax - lxMin) * cs.x;
    };
    auto ty = [&](float y) {
        return cp.y + (1.0f - (y - s_shapeEd.yMin) /
                       (s_shapeEd.yMax - s_shapeEd.yMin)) * cs.y;
    };
    auto fx = [&](float sx) {
        float t = (sx - cp.x) / cs.x;
        float lx = lxMin + t * (lxMax - lxMin);
        return s_shapeEd.logX ? std::exp(lx) : lx;
    };
    auto fy = [&](float sy) {
        float t = 1.0f - (sy - cp.y) / cs.y;
        return s_shapeEd.yMin + t * (s_shapeEd.yMax - s_shapeEd.yMin);
    };

    // Grid: y zero-line + quarters; x decades when log, quarters when linear.
    const ImU32 gridCol = IM_COL32(50, 50, 58, 255);
    for (int q = 0; q <= 4; ++q) {
        float y = s_shapeEd.yMin + (s_shapeEd.yMax - s_shapeEd.yMin) * q / 4.0f;
        dl->AddLine(ImVec2(cp.x, ty(y)), ImVec2(cp.x + cs.x, ty(y)), gridCol);
        char lbl[32]; snprintf(lbl, sizeof(lbl), "%.4g", y);
        dl->AddText(ImVec2(cp.x + 4, ty(y) - 14), IM_COL32(120, 120, 130, 255), lbl);
    }
    if (s_shapeEd.logX) {
        for (float d = 1e-3f; d <= 1e5f; d *= 10.0f) {
            if (d < s_shapeEd.xMin || d > s_shapeEd.xMax) continue;
            dl->AddLine(ImVec2(tx(d), cp.y), ImVec2(tx(d), cp.y + cs.y), gridCol);
            char lbl[32]; snprintf(lbl, sizeof(lbl), "%.4g", d);
            dl->AddText(ImVec2(tx(d) + 3, cp.y + cs.y - 16), IM_COL32(120, 120, 130, 255), lbl);
        }
    } else {
        for (int q = 0; q <= 4; ++q) {
            float x = s_shapeEd.xMin + (s_shapeEd.xMax - s_shapeEd.xMin) * q / 4.0f;
            dl->AddLine(ImVec2(tx(x), cp.y), ImVec2(tx(x), cp.y + cs.y), gridCol);
            char lbl[32]; snprintf(lbl, sizeof(lbl), "%.4g", x);
            dl->AddText(ImVec2(tx(x) + 3, cp.y + cs.y - 16), IM_COL32(120, 120, 130, 255), lbl);
        }
    }

    // Variation ghosts (decision 3): 3 faint re-randomized instances of the
    // shape, drawn only while a varPct is nonzero — they show width wobble
    // shifting points in TIME, which a vertical band cannot.
    if (isSeg && !pts.empty()) {
        // pin.defaultValue, NOT constantSrc->current(): ConstantSource::set
        // only stages the value for the next next(), and nothing on the
        // editor canvas ever advances these sources — current() stays at
        // the construction-time default forever.
        float wvp = 0.0f, vvp = 0.0f;
        for (auto& pin : node.inputs) {
            if (pin.name == "widthVarPct") wvp = pin.defaultValue;
            if (pin.name == "valVarPct")   vvp = pin.defaultValue;
        }
        if (s_shapeEdGen.showVariance && (wvp > 0.001f || vvp > 0.001f)) {
            float gsm = 0.5f;
            for (auto& pin : node.inputs)
                if (pin.name == "smoothness") {  // defaultValue: see below
                    gsm = pin.defaultValue;
                    break;
                }
            SmoothnessInterpolator gsi(gsm, false);
            // Warm orange, distinct from the cyan engine overlay — at the
            // overlay's own hue the ghosts read as noise. Each jittered
            // segment is subdivided by pixel width and run through the
            // engine's interpolator so ghosts curve/step like the render.
            const ImU32 gcol = IM_COL32(235, 160, 90, 96);
            for (int gh = 0; gh < 3; ++gh) {
                Randomizer grng(0xB00B1E5u + uint32_t(gh) * 7919u);
                float prevX = 0.0f, px = 0.0f, pv = 0.0f;
                for (auto& [x, y] : pts) {
                    const float w = std::max(0.0f, (x - prevX)
                            * grng.range(1.0f - wvp, 1.0f + wvp));
                    const float qx = px + w;
                    const float qv = y * grng.range(1.0f - vvp, 1.0f + vvp);
                    const int steps = std::clamp(
                        int((tx(qx) - tx(px)) / 3.0f), 1, 64);
                    ImVec2 prev(tx(px), ty(pv));
                    for (int st = 1; st <= steps; ++st) {
                        const float pos = float(st) / float(steps);
                        ImVec2 p(tx(px + w * pos),
                                 ty(gsi.interpolate(pv, qv, pos)));
                        dl->AddLine(prev, p, gcol, 1.0f);
                        prev = p;
                    }
                    px = qx; pv = qv; prevX = x;
                }
            }
        }
    }

    // Engine-evaluated overlay: CurveNode::map for curves; ShaperSource::map
    // for transfer curves; the segment transition chain
    // (SmoothnessInterpolator, departs from 0) for shapes.
    if (isShaper && pts.size() >= 2) {
        ShaperSource probe;
        std::vector<float> flat;
        flat.reserve(pts.size() * 2);
        auto sorted = pts;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& [x, y] : sorted) { flat.push_back(x); flat.push_back(y); }
        probe.set_array("values", std::move(flat));
        probe.set_array("segs", ShaperSource::encode_segs(segs));
        float smooth = 0.5f;
        for (auto& pin : node.inputs)
            if (pin.name == "smoothness") {  // defaultValue: see ghost note
                smooth = pin.defaultValue;
                break;
            }
        probe.set_param("smoothness", std::make_shared<ConstantSource>(smooth));
        probe.next();  // loads smoothness into the interpolator
        // Overridden segments draw as their own green curves — the blue
        // engine overlay breaks around them instead of double-drawing.
        auto inOverride = [&](float x) {
            for (size_t i = 0; i + 1 < sorted.size() && i < segs.size(); ++i)
                if (segs[i].overridden && x >= sorted[i].first
                    && x <= sorted[i + 1].first) return true;
            return false;
        };
        const int N = std::max(64, int(cs.x / 3.0f));
        ImVec2 prev{};
        bool run = false;
        for (int i = 0; i < N; ++i) {
            float sx = cp.x + cs.x * float(i) / float(N - 1);
            const float x = fx(sx);
            if (inOverride(x)) { run = false; continue; }
            ImVec2 p(sx, ty(probe.map(x)));
            if (run) dl->AddLine(prev, p, IM_COL32(120, 200, 220, 255), 1.6f);
            prev = p;
            run = true;
        }
    } else if (!isSeg && !isShaper && pts.size() >= 2) {
        auto sorted = pts;
        std::stable_sort(sorted.begin(), sorted.end(),
                         [](auto& a, auto& b) { return a.first < b.first; });
        CurveNode probe;
        probe.knots = sorted;
        probe.interp = static_cast<CurveNode::CurveInterp>(node.curveInterp);
        const int N = std::max(64, int(cs.x / 3.0f));
        ImVec2 prev{};
        for (int i = 0; i < N; ++i) {
            float sx = cp.x + cs.x * float(i) / float(N - 1);
            float x = fx(sx);
            if (node.curveInterp != 0 && x <= 0.0f) continue;
            ImVec2 p(sx, ty(probe.map(x)));
            if (i > 0) dl->AddLine(prev, p, IM_COL32(120, 200, 220, 255), 1.6f);
            prev = p;
        }
    } else if (isSeg && !pts.empty()) {
        float smooth = 0.5f;
        for (auto& pin : node.inputs)
            if (pin.name == "smoothness") {  // defaultValue: see ghost note
                smooth = pin.defaultValue;
                break;
            }
        SmoothnessInterpolator si(smooth, false);
        const int N = std::max(64, int(cs.x / 2.0f));
        ImVec2 prev{};
        bool started = false;
        for (int i = 0; i < N; ++i) {
            float sx = cp.x + cs.x * float(i) / float(N - 1);
            float x = fx(sx);
            if (x < 0.0f) continue;
            float pX = 0.0f, pV = 0.0f, out;
            if (x >= pts.back().first) {
                out = pts.back().second;
            } else {
                out = pts.back().second;
                for (auto& [qx, qv] : pts) {
                    if (x <= qx) {
                        float pos = qx > pX
                            ? std::clamp((x - pX) / (qx - pX), 0.0f, 1.0f)
                            : 1.0f;
                        out = si.interpolate(pV, qv, pos);
                        break;
                    }
                    pX = qx; pV = qv;
                }
            }
            ImVec2 p(sx, ty(out));
            if (started) dl->AddLine(prev, p, IM_COL32(120, 200, 220, 255), 1.6f);
            prev = p;
            started = true;
        }
    }

    // Editing skeleton + points (segment shapes depart from the origin).
    if (isSeg && !pts.empty())
        dl->AddLine(ImVec2(tx(0.0f), ty(0.0f)),
                    ImVec2(tx(pts[0].first), ty(pts[0].second)),
                    IM_COL32(110, 110, 120, 160));
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        // An overridden Shaper segment's skeleton line is REPLACED by its
        // green curve below — one line per segment, no double image.
        if (isShaper && i < segs.size() && segs[i].overridden) continue;
        dl->AddLine(ImVec2(tx(pts[i].first), ty(pts[i].second)),
                    ImVec2(tx(pts[i + 1].first), ty(pts[i + 1].second)),
                    IM_COL32(110, 110, 120, 160));
    }

    // Morph ghost + live blend (Shaper client): the inactive curve faint
    // behind the skeleton; the engine's current blend as a thin overlay so
    // both endpoints and the audible in-between stay visible at once.
    if (isShaper && morphOn && otherPts.size() == pts.size()) {
        for (size_t i = 0; i + 1 < otherPts.size(); ++i)
            dl->AddLine(
                ImVec2(tx(otherPts[i].first), ty(otherPts[i].second)),
                ImVec2(tx(otherPts[i + 1].first), ty(otherPts[i + 1].second)),
                IM_COL32(140, 140, 150, 90));
        float mv = 0.0f;
        if (node.dspSource)
            if (auto mp = node.dspSource->get_param("morph"))
                mv = std::clamp(mp->current(), 0.0f, 1.0f);
        if (mv > 0.001f) {
            const auto& A = editB ? otherPts : pts;
            const auto& B = editB ? pts : otherPts;
            const auto& As = editB ? otherSegs : segs;
            const auto& Bs = editB ? segs : otherSegs;
            ImVec2 prev{};
            bool started = false;
            for (size_t i = 0; i + 1 < A.size(); ++i) {
                Curve::Seg sg = i < As.size() ? As[i] : Curve::Seg{};
                if (sg.overridden && i < Bs.size() && Bs[i].overridden)
                    sg.power += (Bs[i].power - sg.power) * mv;
                const float x0 = A[i].first + (B[i].first - A[i].first) * mv;
                const float y0 = A[i].second + (B[i].second - A[i].second) * mv;
                const float x1 = A[i+1].first + (B[i+1].first - A[i+1].first) * mv;
                const float y1 = A[i+1].second + (B[i+1].second - A[i+1].second) * mv;
                if (!started) { prev = ImVec2(tx(x0), ty(y0)); started = true; }
                for (int k = 1; k <= 12; ++k) {
                    const float t = float(k) / 12.0f;
                    ImVec2 p(tx(x0 + (x1 - x0) * t),
                             ty(Curve::eval_seg(y0, y1, t, sg, 0.5f)));
                    dl->AddLine(prev, p, IM_COL32(120, 200, 220, 130), 1.0f);
                    prev = p;
                }
            }
        }
    }

    // Overridden segments (Shaper client): the segment's true shape in
    // green (replacing skeleton + engine overlay there), with an orange
    // mid-segment power dot. Dot positions are reused by the interaction
    // pass below.
    std::vector<std::pair<int, ImVec2>> segDots;
    if (isShaper) {
        for (size_t i = 0; i + 1 < pts.size() && i < segs.size(); ++i) {
            if (!segs[i].overridden) continue;
            ImVec2 prev(tx(pts[i].first), ty(pts[i].second));
            for (int k = 1; k <= 24; ++k) {
                const float t = float(k) / 24.0f;
                const float xx = pts[i].first
                    + (pts[i + 1].first - pts[i].first) * t;
                const float yy = Curve::eval_seg(
                    pts[i].second, pts[i + 1].second, t, segs[i], 0.5f);
                ImVec2 p(tx(xx), ty(yy));
                dl->AddLine(prev, p, IM_COL32(110, 220, 120, 235), 1.8f);
                prev = p;
            }
            const float mx = 0.5f * (pts[i].first + pts[i + 1].first);
            const float my = Curve::eval_seg(
                pts[i].second, pts[i + 1].second, 0.5f, segs[i], 0.5f);
            ImVec2 dot(tx(mx), ty(my));
            const bool lit = int(i) == s_shapeEd.segDragIdx;
            dl->AddCircleFilled(dot, lit ? 6.0f : 5.0f,
                                IM_COL32(255, 150, 60, 255));
            segDots.emplace_back(int(i), dot);
        }
    }

    const ImVec2 m = ImGui::GetIO().MousePos;
    int hot = -1;
    float bestD2 = 100.0f;  // 10 px pick radius
    for (int i = 0; i < (int)pts.size(); ++i) {
        float dx = tx(pts[i].first) - m.x, dy = ty(pts[i].second) - m.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) { bestD2 = d2; hot = i; }
    }
    for (int i = 0; i < (int)pts.size(); ++i) {
        bool lit = (i == hot && hovered) || i == s_shapeEd.dragIdx;
        dl->AddCircleFilled(ImVec2(tx(pts[i].first), ty(pts[i].second)),
                            lit ? 6.0f : 4.0f,
                            lit ? IM_COL32(255, 210, 90, 255)
                                : IM_COL32(210, 210, 220, 255));
    }
    dl->PopClipRect();

    // Hovered-point readout in the toolbar line's right edge.
    if (hot >= 0 && hovered) {
        char ro[64];
        snprintf(ro, sizeof(ro), "(%.6g, %.6g)", pts[hot].first, pts[hot].second);
        dl->AddText(ImVec2(cp.x + cs.x - ImGui::CalcTextSize(ro).x - 8, cp.y + 4),
                    IM_COL32(255, 210, 90, 255), ro);
    }

    // --- Interaction ---
    bool changed = false;
    const bool editable = !s_shapeEd.readOnly;

    // Shaper: nearest power dot (10 px pick, same radius as points) and
    // nearest segment line (8 px) for the override gestures.
    int hotDot = -1;
    if (isShaper && !segDots.empty()) {
        float bestDot = 100.0f;
        for (auto& [si, p] : segDots) {
            const float dx = p.x - m.x, dy = p.y - m.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < bestDot) { bestDot = d2; hotDot = si; }
        }
    }
    auto seg_hit = [&]() -> int {
        float best = 64.0f;  // 8 px
        int bi = -1;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            const ImVec2 a(tx(pts[i].first), ty(pts[i].second));
            const ImVec2 b(tx(pts[i + 1].first), ty(pts[i + 1].second));
            const float abx = b.x - a.x, aby = b.y - a.y;
            const float len2 = abx * abx + aby * aby;
            const float t = len2 > 0.0f
                ? std::clamp(((m.x - a.x) * abx + (m.y - a.y) * aby) / len2,
                             0.0f, 1.0f) : 0.0f;
            const float dx = a.x + abx * t - m.x;
            const float dy = a.y + aby * t - m.y;
            const float d2 = dx * dx + dy * dy;
            if (d2 < best) { best = d2; bi = int(i); }
        }
        return bi;
    };

    if (hovered && editable) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hot >= 0) {
                s_shapeEd.dragIdx = hot;
            } else if (isShaper && hotDot >= 0) {
                s_shapeEd.segDragIdx = hotDot;
                s_shapeEd.segDragStartCurv = shape_ed_seg_curv(segs[hotDot]);
            } else {
                float nx = fx(m.x);
                float ny = fy(m.y);
                if (isSeg) {
                    nx = std::max(nx, 1e-3f);
                    ny = std::clamp(ny, -1.0f, 1.0f);  // decision 2
                } else if (isShaper) {
                    nx = std::clamp(nx, -2.0f, 2.0f);  // array descriptor range
                    ny = std::clamp(ny, -2.0f, 2.0f);
                } else if (node.curveInterp != 0) {
                    nx = std::max(nx, 1e-4f);
                }
                auto it = std::lower_bound(pts.begin(), pts.end(), nx,
                    [](const std::pair<float, float>& p, float x) {
                        return p.first < x;
                    });
                s_shapeEd.dragIdx = int(it - pts.begin());
                pts.insert(it, {nx, ny});
                // Structural edit: splitting a segment duplicates its
                // override onto both halves; a new outer segment is default.
                // With morph on, the OTHER curve gains a point ON its own
                // line at the same segment fraction — shape-preserving on
                // both sides (spec §5).
                if (isShaper && pts.size() >= 2) {
                    const int k = s_shapeEd.dragIdx;
                    if (morphOn && otherPts.size() + 1 == pts.size()) {
                        std::pair<float, float> op;
                        if (k <= 0) {
                            op = otherPts.front();
                            op.first -= 1e-3f;
                            otherPts.insert(otherPts.begin(), op);
                        } else if (k >= int(pts.size()) - 1) {
                            op = otherPts.back();
                            op.first += 1e-3f;
                            otherPts.push_back(op);
                        } else {
                            const auto& l = pts[k - 1];
                            const auto& r = pts[k + 1];
                            const float tf = (r.first - l.first) > 1e-9f
                                ? (nx - l.first) / (r.first - l.first) : 0.5f;
                            const auto& ol = otherPts[k - 1];
                            const auto& orr = otherPts[k];
                            const size_t j = size_t(k - 1);
                            const Curve::Seg oseg =
                                j < otherSegs.size() ? otherSegs[j]
                                                     : Curve::Seg{};
                            op.first = ol.first
                                + (orr.first - ol.first) * tf;
                            op.second = Curve::eval_seg(
                                ol.second, orr.second, tf, oseg, 0.5f);
                            otherPts.insert(otherPts.begin() + k, op);
                        }
                        otherChanged = true;
                    }
                    if (k <= 0) segs.insert(segs.begin(), Curve::Seg{});
                    else if (k >= int(pts.size()) - 1)
                        segs.push_back(Curve::Seg{});
                    else {
                        const size_t j = size_t(k - 1);
                        const Curve::Seg cp =
                            j < segs.size() ? segs[j] : Curve::Seg{};
                        segs.insert(segs.begin() + j, cp);
                    }
                    if (morphOn) {
                        otherSegs.resize(pts.empty() ? 0 : pts.size() - 2);
                        if (k <= 0)
                            otherSegs.insert(otherSegs.begin(), Curve::Seg{});
                        else if (k >= int(pts.size()) - 1)
                            otherSegs.push_back(Curve::Seg{});
                        else {
                            const size_t j = size_t(k - 1);
                            const Curve::Seg cp =
                                j < otherSegs.size() ? otherSegs[j]
                                                     : Curve::Seg{};
                            otherSegs.insert(otherSegs.begin() + j, cp);
                        }
                        otherChanged = true;
                    }
                    segsChanged = true;
                }
                changed = true;
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            if (hot >= 0 && pts.size() > (isSeg ? size_t(1) : size_t(2))) {
                // Structural edit: deleting point i merges its segments —
                // the left segment's override survives (left point owns).
                // Removed from BOTH curves when morphing.
                if (isShaper && !segs.empty()) {
                    const size_t drop =
                        std::min(size_t(hot), segs.size() - 1);
                    segs.erase(segs.begin() + drop);
                    if (morphOn) {
                        if (drop < otherSegs.size())
                            otherSegs.erase(otherSegs.begin() + drop);
                        if (size_t(hot) < otherPts.size())
                            otherPts.erase(otherPts.begin() + hot);
                        otherChanged = true;
                    }
                    segsChanged = true;
                }
                pts.erase(pts.begin() + hot);
                if (s_shapeEd.dragIdx == hot) s_shapeEd.dragIdx = -1;
                changed = true;
            } else if (isShaper && hot < 0 && hotDot >= 0) {
                // Type/override state is shared structure: clear on both.
                segs[hotDot] = Curve::Seg{};      // reset to default interp
                if (morphOn && size_t(hotDot) < otherSegs.size()) {
                    otherSegs[hotDot] = Curve::Seg{};
                    otherChanged = true;
                }
                if (s_shapeEd.segDragIdx == hotDot) s_shapeEd.segDragIdx = -1;
                segsChanged = true;
            } else if (isShaper && hot < 0) {
                const int si = seg_hit();
                if (si >= 0 && size_t(si) < segs.size()) {
                    if (!segs[si].overridden) {
                        // Born at curvature 0 (power 1): visually identical
                        // to the segment it replaces until dragged.
                        segs[si] = Curve::Seg{true, RampType::Expo, 1.0f};
                        if (morphOn && size_t(si) < otherSegs.size()) {
                            otherSegs[si] =
                                Curve::Seg{true, RampType::Expo, 1.0f};
                            otherChanged = true;
                        }
                    }
                    s_shapeEd.segDragIdx = si;
                    s_shapeEd.segDragStartCurv = shape_ed_seg_curv(segs[si]);
                    segsChanged = true;
                }
            }
        }
    }
    if (hovered) {  // view navigation works even in read-only mode
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            const float f = std::pow(1.18f, -wheel);
            if (ImGui::GetIO().KeyShift) {
                float cy = fy(m.y);
                s_shapeEd.yMin = cy + (s_shapeEd.yMin - cy) * f;
                s_shapeEd.yMax = cy + (s_shapeEd.yMax - cy) * f;
            } else if (s_shapeEd.logX) {
                float clx = std::log(std::max(fx(m.x), 1e-6f));
                float nMin = clx + (std::log(s_shapeEd.xMin) - clx) * f;
                float nMax = clx + (std::log(s_shapeEd.xMax) - clx) * f;
                s_shapeEd.xMin = std::exp(nMin);
                s_shapeEd.xMax = std::exp(nMax);
            } else {
                float cx = fx(m.x);
                s_shapeEd.xMin = cx + (s_shapeEd.xMin - cx) * f;
                s_shapeEd.xMax = cx + (s_shapeEd.xMax - cx) * f;
            }
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            ImVec2 d = ImGui::GetIO().MouseDelta;
            if (s_shapeEd.logX) {
                float shift = -d.x / cs.x * (lxMax - lxMin);
                s_shapeEd.xMin = std::exp(std::log(s_shapeEd.xMin) + shift);
                s_shapeEd.xMax = std::exp(std::log(s_shapeEd.xMax) + shift);
            } else {
                float shift = -d.x / cs.x * (s_shapeEd.xMax - s_shapeEd.xMin);
                s_shapeEd.xMin += shift; s_shapeEd.xMax += shift;
            }
            float yShift = d.y / cs.y * (s_shapeEd.yMax - s_shapeEd.yMin);
            s_shapeEd.yMin += yShift; s_shapeEd.yMax += yShift;
        }
    }
    if (editable && s_shapeEd.dragIdx >= 0 &&
        s_shapeEd.dragIdx < (int)pts.size()) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const int di = s_shapeEd.dragIdx;
            float nx = fx(m.x), ny = fy(m.y);
            const bool slideTail = ImGui::GetIO().KeyShift;
            // Clamp x against the previous neighbor always; against the next
            // only when NOT sliding the tail (decision 1: plain drag moves
            // one point, shift-drag preserves the tail's deltas).
            const float eps = s_shapeEd.logX
                ? pts[di].first * 1e-4f + 1e-6f : 1e-6f;
            if (di > 0) nx = std::max(nx, pts[di - 1].first + eps);
            if (!slideTail && di + 1 < (int)pts.size())
                nx = std::min(nx, pts[di + 1].first - eps);
            if (isSeg) {
                nx = std::max(nx, 1e-3f);
                ny = std::clamp(ny, -1.0f, 1.0f);  // decision 2
            } else if (isShaper) {
                nx = std::clamp(nx, -2.0f, 2.0f);  // array descriptor range
                ny = std::clamp(ny, -2.0f, 2.0f);
            } else if (node.curveInterp != 0) {
                nx = std::max(nx, 1e-4f);
            }
            const float dxTail = nx - pts[di].first;
            pts[di] = {nx, ny};
            if (slideTail && dxTail != 0.0f)
                for (int j = di + 1; j < (int)pts.size(); ++j)
                    pts[j].first += dxTail;
            changed = true;
        } else {
            s_shapeEd.dragIdx = -1;
        }
    }

    // Curvature drag: vertical drag sweeps signed curvature, so the curve
    // follows the mouse — drag up, bulge up (InverseExpo); drag down,
    // bulge down (Expo); through linear in the middle, no kinks. Works for
    // the creating right-drag and the left-drag on an existing dot.
    // Exclusive with a point drag (dragIdx wins the left button); only the
    // power family participates (a Hold/Sine override's dot just clears).
    if (isShaper && editable && s_shapeEd.segDragIdx >= 0 &&
        s_shapeEd.segDragIdx < int(segs.size()) && s_shapeEd.dragIdx < 0) {
        const bool downR = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        const bool downL = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        auto& sg = segs[s_shapeEd.segDragIdx];
        const bool powFamily = sg.overridden &&
            (sg.type == RampType::Expo || sg.type == RampType::InverseExpo);
        if ((downR || downL) && powFamily) {
            const float dy = ImGui::GetMouseDragDelta(
                downR ? ImGuiMouseButton_Right : ImGuiMouseButton_Left).y;
            // Screen-up (negative dy) = bulge up = positive curvature.
            // Ramp's ascending/descending branch swap makes Expo bulge
            // toward LOWER y and InverseExpo toward HIGHER y regardless of
            // segment slope, so this mapping is direction-true everywhere.
            float c = s_shapeEd.segDragStartCurv - dy / 60.0f;
            shape_ed_seg_from_curv(sg, c);
            segsChanged = true;
        } else if (!downR && !downL) {
            s_shapeEd.segDragIdx = -1;
        }
    }

    if (changed) {
        if (isSeg)         shape_editor_apply_segment(node, pts);
        else if (isShaper) shape_editor_apply_shaper_named(node, pts,
                                                           activePtsName);
        else               shape_editor_apply_curve(node);
    }
    if (isShaper && (changed || segsChanged))
        shape_editor_apply_segs(node, segs, activeSegsName);
    if (isShaper && otherChanged) {
        shape_editor_apply_shaper_named(node, otherPts, otherPtsName);
        shape_editor_apply_segs(node, otherSegs, otherSegsName);
    }

    ImGui::End();
    if (!keepOpen) s_shapeEd.open = false;
}

// Curve editors. CurveNode shapes are edited in the node's Properties pane
// (draw_curve_node); the legacy stash editor (draw_one_curve) lives in the
// Parameter-mapping dialog. The separate Curves window was retired
// 2026-08-22 (Matt).
//
// Curve-bearing paramMap entries ({"target": "node.paramOrConfig", "curve":
// [[hz, value], ...]}) live verbatim in s_loadedParamMap (the UI node graph
// cannot model them — see the stash comment near its declaration). The
// legacy editor edits that stash directly. Because save_patch_graph carries the
// stash forward (with node-id remapping) and get_playback_patch_path
// re-serializes the patch through save_patch_graph whenever the graph is
// dirty, every edit here is immediately audible on keyboard playback and
// lands in the file on Save. All edits set s_graphDirty.
//
// Engine semantics (CurveNode::map, LogX/LogLog modes, in engine/include/
// mforce/core/curve_node.h — ParamSlot retired 2026-08-18, PerformSource
// P1): breakpoints are ascending (hz, value) pairs, >= 2 of them;
// evaluation is linear in log-frequency, clamped past the ends; curves are
// evaluated per note-on from the note frequency, so only entries under the
// "frequency" paramMap name have any effect at present.
// ===========================================================================

// Mirror of CurveNode::map (LogX/LogLog) for the plot preview.
static float curve_eval(const nlohmann::json& curve, float freq) {
    size_t n = curve.size();
    if (n == 0) return freq;
    auto hz  = [&](size_t i) { return curve[i][0].get<float>(); };
    auto val = [&](size_t i) { return curve[i][1].get<float>(); };
    if (freq <= hz(0))     return val(0);
    if (freq >= hz(n - 1)) return val(n - 1);
    for (size_t i = 1; i < n; ++i) {
        if (freq <= hz(i)) {
            float lf = std::log(freq / hz(i - 1)) / std::log(hz(i) / hz(i - 1));
            return val(i - 1) + (val(i) - val(i - 1)) * lf;
        }
    }
    return val(n - 1);
}

// Keep breakpoints ascending in hz (engine's map() assumes it). Stable sort
// so equal-hz rows keep their relative order while the user is mid-edit.
static void curve_sort(nlohmann::json& curve) {
    std::stable_sort(curve.begin(), curve.end(),
        [](const nlohmann::json& a, const nlohmann::json& b) {
            return a[0].get<float>() < b[0].get<float>();
        });
}

// Link-derived targets for a Parameter node, in stash form ("label.pinName" —
// original loaded node ids; save_patch_graph remaps them to serialized ids).
// True when the stash already holds a curve entry for target ("label.name")
// under paramName. Used to annotate the Add-curve dropdown — targets are
// never hidden, but ones that already carry a curve get flagged so adding a
// duplicate is a deliberate act, not an accident.
static bool curve_exists_for(const std::string& paramName, const std::string& target) {
    auto it = s_loadedParamMap.find(paramName);
    if (it == s_loadedParamMap.end()) return false;
    auto matches = [&](const nlohmann::json& o) {
        return o.is_object() && o.contains("curve") &&
               o.value("target", std::string()) == target;
    };
    if (matches(*it)) return true;
    if (it->is_array())
        for (const auto& sub : *it)
            if (matches(sub)) return true;
    return false;
}

// Append a new {target, curve} entry under paramName in the stash.
static void curves_add_entry(const std::string& paramName, const std::string& target,
                             float currentValue) {
    using json = nlohmann::json;
    // Explicit 20/16000 endpoints per the curve-endpoint convention
    // (2026-08-13): a clamp is a visible repeated value, never implicit.
    json curveObj = {
        {"target", target},
        {"curve", json::array({ json::array({   20.0f, currentValue}),
                                json::array({16000.0f, currentValue}) })},
    };
    if (!s_loadedParamMap.contains(paramName))
        s_loadedParamMap[paramName] = json::array();
    json& e = s_loadedParamMap[paramName];
    if (e.is_array()) {
        e.push_back(std::move(curveObj));
    } else {
        json arr = json::array();
        arr.push_back(e);
        arr.push_back(std::move(curveObj));
        s_loadedParamMap[paramName] = std::move(arr);
    }
    mark_graph_dirty();
}

// Draw the editor for one curve-bearing entry. Returns true if the user asked
// to delete the whole curve entry.
static bool draw_one_curve(const std::string& paramName, nlohmann::json& obj) {
    using json = nlohmann::json;
    std::string target = obj.value("target", std::string("?"));
    json& curve = obj["curve"];

    bool deleteMe = false;
    char header[256];
    snprintf(header, sizeof(header), "%s -> %s", paramName.c_str(), target.c_str());
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    // --- Breakpoint table ---
    bool changed = false;
    bool needSort = false;
    int removeIdx = -1;
    const bool canRemove = curve.size() > 2;  // engine requires >= 2 breakpoints
    if (ImGui::BeginTable("##bp", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Freq (Hz)", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableHeadersRow();
        for (int r = 0; r < (int)curve.size(); ++r) {
            ImGui::PushID(r);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            float hz = curve[r][0].get<float>();
            if (ImGui::InputFloat("##hz", &hz, 0.0f, 0.0f, "%.1f")) {
                curve[r][0] = std::max(1.0f, hz);  // log-x needs > 0
                changed = true;
            }
            // Re-sort only when the edit is committed, so rows don't jump
            // around under the cursor mid-typing.
            if (ImGui::IsItemDeactivatedAfterEdit()) needSort = true;
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            float v = curve[r][1].get<float>();
            if (ImGui::InputFloat("##val", &v, 0.0f, 0.0f, "%.4f")) {
                curve[r][1] = v;
                changed = true;
            }
            ImGui::TableSetColumnIndex(2);
            ImGui::BeginDisabled(!canRemove);
            if (ImGui::SmallButton(" x ")) removeIdx = r;
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIdx >= 0) {
        curve.erase(curve.begin() + removeIdx);
        changed = true;
    }
    if (ImGui::SmallButton(" + Add point ")) {
        // Extend past the current top breakpoint (keeps ascending order).
        size_t n = curve.size();
        float lastHz  = n ? curve[n - 1][0].get<float>() : 50.0f;
        float lastVal = n ? curve[n - 1][1].get<float>() : 0.0f;
        curve.push_back(json::array({lastHz * 2.0f, lastVal}));
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(" Delete curve ")) deleteMe = true;

    if (needSort) curve_sort(curve);
    if (changed || needSort) mark_graph_dirty();

    // --- Plot: interpolated curve, log-x from first to last breakpoint ---
    if (curve.size() >= 2) {
        // Plot against a sorted copy so an out-of-order row mid-edit doesn't
        // feed the engine-mirror evaluator garbage.
        json sorted = curve;
        curve_sort(sorted);
        float lo = sorted.front()[0].get<float>();
        float hi = sorted.back()[0].get<float>();
        if (lo > 0.0f && hi > lo) {
            constexpr int N = 128;
            float samples[N];
            for (int i = 0; i < N; ++i) {
                float f = lo * std::pow(hi / lo, float(i) / float(N - 1));
                samples[i] = curve_eval(sorted, f);
            }
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.0f Hz .. %.0f Hz (log)", lo, hi);
            ImGui::PlotLines("##plot", samples, N, 0, overlay, FLT_MAX, FLT_MAX,
                             ImVec2(ImGui::GetContentRegionAvail().x, 70.0f));
        }
    }
    ImGui::Spacing();
    return deleteMe;
}

// ---------------------------------------------------------------------------
// Editor for a real CurveNode in the graph (P2b). The stash-based editor above
// still serves entries that could not be converted (owned formant children);
// this one edits the node, which is where every converted patch's curves now
// live. Same table + plot idiom deliberately — Matt asked for the existing
// table extended, not a new 2D canvas.
//
// Describes itself by what it FEEDS, not by its own id: "__curve_3" means
// nothing, "-> KSPianoString1.t60" is the thing being shaped.
// ---------------------------------------------------------------------------
static std::string curve_node_destination(const GraphNode& curve) {
    for (const auto& n : s_nodes) {
        for (auto& [key, val] : n.dynamicPins.items())
            if (val.is_object() && val.value("ref", std::string()) == curve.label)
                return n.label + "." + key + "  (per note)";
        for (const auto& link : s_links) {
            const Pin* src = find_pin(link.startPinId);
            const Pin* dst = find_pin(link.endPinId);
            if (!src || !dst) continue;
            const GraphNode* srcNode = find_node_for_pin(link.startPinId);
            const GraphNode* dstNode = find_node_for_pin(link.endPinId);
            if (srcNode && dstNode && srcNode->id == curve.id && dstNode->id == n.id)
                return n.label + "." + dst->name;
        }
    }
    return "(not connected)";
}

// Expressions-mode row table: x | form | a | b(=p) | remove. Coefficients
// like keytrack slopes can be ~1e-5, hence %.8g. Returns true on change and
// pushes the edited rows into the live node + draws the extrapolating plot.
static bool draw_curve_expr_rows(GraphNode& node) {
    bool changed = false;
    int removeIdx = -1;
    bool needSort = false;
    if (ImGui::BeginTable("exprknots", 5, ImGuiTableFlags_SizingFixedFit)) {
        for (int r = 0; r < (int)node.curveExprKnots.size(); ++r) {
            auto& k = node.curveExprKnots[r];
            ImGui::TableNextRow();
            ImGui::PushID(r);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(80.0f);
            float x = k.x;
            if (ImGui::InputFloat("##x", &x, 0.0f, 0.0f, "%.6g")) {
                k.x = std::max(0.0001f, x);
                changed = true;
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) needSort = true;
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(70.0f);
            int form = (k.form == CurveNode::KnotForm::Power) ? 1 : 0;
            const char* kForm[] = {"a*x+b", "a*x^p"};
            if (ImGui::Combo("##form", &form, kForm, 2)) {
                k.form = form ? CurveNode::KnotForm::Power
                              : CurveNode::KnotForm::Linear;
                changed = true;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(90.0f);
            float a = k.a;
            if (ImGui::InputFloat("##a", &a, 0.0f, 0.0f, "%.8g")) {
                k.a = a;
                changed = true;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(90.0f);
            float b = k.b;
            if (ImGui::InputFloat("##b", &b, 0.0f, 0.0f, "%.8g")) {
                k.b = b;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(k.form == CurveNode::KnotForm::Power
                                  ? "p — the exponent in a*x^p"
                                  : "b — the offset in a*x+b");
            ImGui::TableNextColumn();
            // One knot is legitimate here — it's the global-formula case.
            if (node.curveExprKnots.size() > 1 && ImGui::SmallButton("x"))
                removeIdx = r;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIdx >= 0) {
        node.curveExprKnots.erase(node.curveExprKnots.begin() + removeIdx);
        changed = true;
        needSort = true;
    }
    if (ImGui::SmallButton("+ point")) {
        auto k = node.curveExprKnots.empty()
                 ? CurveNode::ExprKnot{100.0f, CurveNode::KnotForm::Linear, 1.0f, 0.0f}
                 : node.curveExprKnots.back();
        k.x = node.curveExprKnots.empty() ? 100.0f : k.x * 2.0f;
        node.curveExprKnots.push_back(k);
        changed = true;
        needSort = true;
    }

    auto by_x = [](const CurveNode::ExprKnot& a, const CurveNode::ExprKnot& b) {
        return a.x < b.x;
    };
    if (needSort)
        std::stable_sort(node.curveExprKnots.begin(), node.curveExprKnots.end(), by_x);
    auto sorted = node.curveExprKnots;
    std::stable_sort(sorted.begin(), sorted.end(), by_x);

    if (changed) {
        if (auto* cn = dynamic_cast<CurveNode*>(node.dspSource.get())) {
            cn->exprMode = true;
            cn->exprKnots = sorted;
            cn->interp = static_cast<CurveNode::CurveInterp>(node.curveInterp);
        }
        mark_graph_dirty();
    }

    // Plot through the engine's own map_expr; extend an octave past the end
    // knots so the live extrapolation — the point of this mode — is visible.
    if (!sorted.empty()) {
        CurveNode probe;
        probe.exprMode = true;
        probe.exprKnots = sorted;
        probe.interp = static_cast<CurveNode::CurveInterp>(node.curveInterp);
        float lo = std::max(0.0001f, sorted.front().x * 0.5f);
        float hi = std::max(lo * 4.0f, sorted.back().x * 2.0f);
        constexpr int N = 128;
        float samples[N];
        for (int i = 0; i < N; ++i)
            samples[i] = probe.map(lo * std::pow(hi / lo, float(i) / float(N - 1)));
        char overlay[64];
        snprintf(overlay, sizeof(overlay), "%.4g .. %.4g (log x, extrapolated)",
                 lo, hi);
        ImGui::PlotLines("##cnexprplot", samples, N, 0, overlay, FLT_MAX, FLT_MAX,
                         ImVec2(ImGui::GetContentRegionAvail().x, 70.0f));
    }
    return changed;
}

// Returns true if anything changed.
static bool draw_curve_node(GraphNode& node) {
    bool changed = false;
    char header[256];
    snprintf(header, sizeof(header), "%s -> %s##cn%d",
             node.label.c_str(), curve_node_destination(node).c_str(), node.id);
    if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
        return false;

    ImGui::PushID(node.id);
    ImGui::SetNextItemWidth(120.0f);
    int mode = node.curveExprMode ? 1 : 0;
    const char* kMode[] = {"points", "expressions"};
    if (ImGui::Combo("mode", &mode, kMode, 2)) {
        node.curveExprMode = (mode == 1);
        // First switch into expressions: seed one identity-ish row so the
        // node maps something instead of going silent.
        if (node.curveExprMode && node.curveExprKnots.empty())
            node.curveExprKnots.push_back({100.0f, CurveNode::KnotForm::Linear,
                                           1.0f, 0.0f});
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "points: knot values are literals (clamped past the end knots).\n"
            "expressions: each knot's value is a formula of x — a*x+b or\n"
            "a*x^p — evaluated at the CURRENT x and blended between knots.\n"
            "Past the end knots the edge formula keeps evaluating, so one\n"
            "Linear knot (a=8, b=0) is global keytrack: no 16000 knot needed.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120.0f);
    const char* kInterp[] = {"linear", "logx", "loglog"};
    if (ImGui::Combo("interp", &node.curveInterp, kInterp, 3)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("logx: value linear in log(x) — this is exactly\n"
                          "linear in semitones, which is why pitch curves use it.\n"
                          "loglog: a 2-point segment is exactly y = k*x^n.\n"
                          "In expressions mode this weights the blend between knots.");

    if (node.curveExprMode) {
        changed |= draw_curve_expr_rows(node);
        ImGui::PopID();
        ImGui::Spacing();
        return changed;
    }

    int removeIdx = -1;
    bool needSort = false;
    if (ImGui::BeginTable("knots", 3, ImGuiTableFlags_SizingFixedFit)) {
        for (int r = 0; r < (int)node.curveKnots.size(); ++r) {
            ImGui::TableNextRow();
            ImGui::PushID(r);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(90.0f);
            float x = node.curveKnots[r].first;
            if (ImGui::InputFloat("##x", &x, 0.0f, 0.0f, "%.6g")) {
                node.curveKnots[r].first = std::max(0.0001f, x);  // log-x needs > 0
                changed = true;
            }
            // Re-sort only when the X edit is COMMITTED. Sorting on every
            // keystroke moved the row being typed out from under the active
            // widget, so the digits landed in a previously created knot
            // (Matt, 2026-08-22).
            if (ImGui::IsItemDeactivatedAfterEdit()) needSort = true;
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(90.0f);
            float y = node.curveKnots[r].second;
            if (ImGui::InputFloat("##y", &y, 0.0f, 0.0f, "%.6g")) {
                node.curveKnots[r].second = y;
                changed = true;
            }
            ImGui::TableNextColumn();
            // The engine needs >= 2 knots; below that CurveNode is identity.
            if (node.curveKnots.size() > 2 && ImGui::SmallButton("x")) removeIdx = r;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeIdx >= 0) {
        node.curveKnots.erase(node.curveKnots.begin() + removeIdx);
        changed = true;
        needSort = true;
    }
    if (ImGui::SmallButton("+ point")) {
        float lastX = node.curveKnots.empty() ? 100.0f : node.curveKnots.back().first;
        float lastY = node.curveKnots.empty() ? 0.0f   : node.curveKnots.back().second;
        node.curveKnots.emplace_back(lastX * 2.0f, lastY);
        changed = true;
        needSort = true;
    }

    auto by_x = [](const auto& a, const auto& b) { return a.first < b.first; };
    if (needSort)
        std::stable_sort(node.curveKnots.begin(), node.curveKnots.end(), by_x);
    // The engine (and the plot) assume ascending knots; while the table is
    // mid-edit the editor order may lag, so hand them a sorted copy.
    auto sortedKnots = node.curveKnots;
    std::stable_sort(sortedKnots.begin(), sortedKnots.end(), by_x);

    if (changed) {
        if (auto* cn = dynamic_cast<CurveNode*>(node.dspSource.get())) {
            cn->knots = sortedKnots;
            cn->interp = static_cast<CurveNode::CurveInterp>(node.curveInterp);
        }
        mark_graph_dirty();
    }

    // Plot, evaluated through the ENGINE's own map() rather than a mirror of
    // it — the node is right here, so there is nothing to keep in sync.
    if (sortedKnots.size() >= 2) {
        CurveNode probe;
        probe.knots = sortedKnots;
        probe.interp = static_cast<CurveNode::CurveInterp>(node.curveInterp);
        float lo = sortedKnots.front().first;
        float hi = sortedKnots.back().first;
        if (lo > 0.0f && hi > lo) {
            constexpr int N = 128;
            float samples[N];
            for (int i = 0; i < N; ++i)
                samples[i] = probe.map(lo * std::pow(hi / lo, float(i) / float(N - 1)));
            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.4g .. %.4g (log x)", lo, hi);
            ImGui::PlotLines("##cnplot", samples, N, 0, overlay, FLT_MAX, FLT_MAX,
                             ImVec2(ImGui::GetContentRegionAvail().x, 70.0f));
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    shape_editor_open_curve(node);
                else
                    ImGui::SetTooltip("Double-click to open the shape editor");
            }
        }
    }
    ImGui::PopID();
    ImGui::Spacing();
    return changed;
}

// Eligible mapping targets on a node: pins the loader can resolve to a
// ConstantSource (value pins not wired to a source) plus all scalar
// configs (delivered via set_setting). Shared by the Mappings dialog Add-with-curve
// section and the Parameter-mappings dialog.
struct TargetOpt { std::string name; float current; bool isSetting; };
static std::vector<TargetOpt> eligible_targets(GraphNode* tn) {
    std::vector<TargetOpt> opts;
    for (auto& pin : tn->inputs) {
        if (pin.inputOnly || pin.kind != PinKind::Input) continue;
        if (pin.name.substr(0, 3) == "ch ") continue;
        if (is_pin_connected(pin.id)) continue;  // loader rejects ref-wired targets
        opts.push_back({pin.name, pin.defaultValue, false});
    }
    for (auto& [desc, val] : tn->settingValues)
        opts.push_back({desc.name, val, true});
    return opts;
}

// True when target ("label.name") appears under paramName in any form —
// bare string or {target,...} object. Guards duplicate adds in the dialog.
static bool target_exists_for(const std::string& paramName, const std::string& target) {
    auto it = s_loadedParamMap.find(paramName);
    if (it == s_loadedParamMap.end()) return false;
    auto matches = [&](const nlohmann::json& e) {
        if (e.is_string()) return e.get<std::string>() == target;
        return e.is_object() && e.value("target", std::string()) == target;
    };
    if (it->is_array()) {
        for (const auto& e : *it) if (matches(e)) return true;
        return false;
    }
    return matches(*it);
}

// ---------------------------------------------------------------------------
// Parameter-mappings dialog (spec §2): the one table of every binding in the
// patch. Rows come straight from the stash; deletes edit it in place; adds
// go through the same append rules as the Mappings dialog.
// ---------------------------------------------------------------------------
static bool s_mappingsOpen = false;

static void draw_mappings_dialog() {
    if (!s_mappingsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(560, 380), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Parameter mapping", &s_mappingsOpen)) { ImGui::End(); return; }
    if (s_graphMode != GraphMode::PatchGraph) {
        ImGui::TextDisabled("Mappings apply to instrument patches only.");
        ImGui::End();
        return;
    }

    // --- Bindings that live in the GRAPH (P2b) ---
    // A binding is now a wire, so this table is a VIEW: every pin fed by a
    // chain rooted at a PerformNode. Dynamic pins (settings, pushed once per
    // note) and fixed pins (pulled) are listed together but distinguished,
    // because that difference is the whole point of the pin model.
    {
        // Walk back from a node to see whether a PerformNode feeds it, and
        // through what. Depth-limited rather than visited-set — these chains
        // are 1-3 nodes by construction.
        std::function<const GraphNode*(const GraphNode*, int, bool&)> root_perform =
            [&](const GraphNode* n, int depth, bool& viaCurve) -> const GraphNode* {
                if (!n || depth > 6) return nullptr;
                if (n->typeName == NT_PERFORM) return n;
                if (n->typeName == "CurveNode") viaCurve = true;
                for (const auto& link : s_links) {
                    const GraphNode* dst = find_node_for_pin(link.endPinId);
                    if (!dst || dst->id != n->id) continue;
                    const GraphNode* src = find_node_for_pin(link.startPinId);
                    if (const GraphNode* r = root_perform(src, depth + 1, viaCurve))
                        return r;
                }
                return nullptr;
            };

        int rows = 0;
        if (ImGui::BeginTable("graphbindings", 4,
                ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
            ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Kind",   ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Driven by", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Shape",  ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();

            for (auto& n : s_nodes) {
                // Dynamic pins: promoted settings, pushed once per note.
                for (auto& [key, val] : n.dynamicPins.items()) {
                    const std::string ref = val.is_object()
                        ? val.value("ref", std::string()) : std::string();
                    const GraphNode* srcNode = nullptr;
                    for (auto& c : s_nodes) if (c.label == ref) { srcNode = &c; break; }
                    ImGui::TableNextRow(); ++rows;
                    ImGui::TableNextColumn();
                    ImGui::Text("%s.%s", n.label.c_str(), key.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.30f, 1), "dynamic pin");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("A setting. Evaluated once at note-on and\n"
                                          "frozen for the life of the note.");
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", ref.empty() ? "-" : ref.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", srcNode && srcNode->typeName == "CurveNode"
                                          ? "curve" : "direct");
                }
                // Fixed pins wired to a perform-rooted chain: pulled per sample.
                for (const auto& pin : n.inputs) {
                    const GraphNode* src = find_source_node(pin.id);
                    if (!src) continue;
                    bool viaCurve = false;
                    const GraphNode* perf = root_perform(src, 0, viaCurve);
                    if (!perf) continue;
                    ImGui::TableNextRow(); ++rows;
                    ImGui::TableNextColumn();
                    ImGui::Text("%s.%s", n.label.c_str(), pin.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("fixed pin");
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("An ordinary pin. Pulled every sample.");
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", perf->label.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", viaCurve ? "curve" : "direct");
                }
            }
            ImGui::EndTable();
        }
        if (rows == 0)
            ImGui::TextDisabled("Nothing in this patch is driven by the note.");
    }

    if (!s_loadedParamMap.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Legacy paramMap entries (not convertible to nodes)");
    }

    // --- Legacy stash entries ---
    struct DeleteReq { std::string param; int subIdx; };  // -1 = whole entry
    std::vector<DeleteReq> deletes;
    if (ImGui::BeginTable("mappings", 5,
            ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Target", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Curve",  ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("VCurve", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("",       ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableHeadersRow();

        auto row = [&](const std::string& pname, const nlohmann::json& e, int subIdx) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", pname.c_str());
            if (pname != "frequency") {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.4f, 1), "(inert)");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Only 'frequency' is evaluated at note-on today.");
            }
            ImGui::TableNextColumn();
            std::string target = e.is_string() ? e.get<std::string>()
                                               : e.value("target", std::string("?"));
            ImGui::Text("%s", target.c_str());
            ImGui::TableNextColumn();
            if (e.is_object() && e.contains("curve")) {
                ImGui::Text("%d pts", (int)e["curve"].size());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Shape editor is below the table");
            } else {
                ImGui::TextDisabled("-");
            }
            ImGui::TableNextColumn();
            if (e.is_object() && e.contains("vcurve"))
                ImGui::Text("%d pts", (int)e["vcurve"].size());
            else
                ImGui::TextDisabled("-");
            ImGui::TableNextColumn();
            char xlbl[48];
            snprintf(xlbl, sizeof(xlbl), "X##x%s%d", pname.c_str(), subIdx);
            if (ImGui::SmallButton(xlbl)) deletes.push_back({pname, subIdx});
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove this binding (the target's widget returns)");
        };

        for (auto& [pname, entry] : s_loadedParamMap.items()) {
            if (entry.is_array()) {
                for (int i = 0; i < (int)entry.size(); ++i) row(pname, entry[i], i);
            } else {
                row(pname, entry, -1);
            }
        }
        ImGui::EndTable();
    }
    if (!s_loadedParamMap.is_object() || s_loadedParamMap.empty())
        ImGui::TextDisabled("No bindings. Notes will not retune until 'frequency' maps to something.");

    // --- Legacy curve SHAPES (the Curves window retired 2026-08-22) ---
    // Unconvertible stash entries keep their breakpoint editor here;
    // CurveNode shapes are edited in the Curve node's Properties pane.
    // Deleting a shape deletes the BINDING (same semantics as the table's X:
    // a curveless binding would push raw note frequency into a
    // non-frequency target, which is never what the delete meant).
    {
        bool anyShape = false;
        auto shape_header = [&]() {
            if (anyShape) return;
            ImGui::Separator();
            ImGui::TextDisabled("Legacy curve shapes");
            anyShape = true;
        };
        for (auto& [pname, entry] : s_loadedParamMap.items()) {
            ImGui::PushID(pname.c_str());
            if (entry.is_object() && entry.contains("curve")) {
                shape_header();
                if (draw_one_curve(pname, entry)) deletes.push_back({pname, -1});
            } else if (entry.is_array()) {
                for (int i = 0; i < (int)entry.size(); ++i) {
                    if (!entry[i].is_object() || !entry[i].contains("curve")) continue;
                    shape_header();
                    ImGui::PushID(i);
                    if (draw_one_curve(pname, entry[i])) deletes.push_back({pname, i});
                    ImGui::PopID();
                }
            }
            ImGui::PopID();
        }
    }

    for (const auto& d : deletes) {
        if (!s_loadedParamMap.contains(d.param)) continue;
        nlohmann::json& entry = s_loadedParamMap[d.param];
        if (d.subIdx < 0) {
            s_loadedParamMap.erase(d.param);
        } else if (entry.is_array() && d.subIdx < (int)entry.size()) {
            entry.erase(entry.begin() + d.subIdx);
            if (entry.empty()) s_loadedParamMap.erase(d.param);
            else if (entry.size() == 1 && entry[0].is_string())
                s_loadedParamMap[d.param] = entry[0];
        }
        mark_graph_dirty();
    }

    // --- Add a binding ---
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Add binding");

    std::vector<GraphNode*> targetNodes;
    for (auto& n : s_nodes)
        if (!is_special_ui_type(n.typeName) && !n.label.empty())
            targetNodes.push_back(&n);
    if (targetNodes.empty()) {
        ImGui::TextDisabled("No target nodes in graph.");
        ImGui::End();
        return;
    }

    static int selNode = 0, selTarget = 0;
    // Pre-select the node selected in the editor, tracked across changes.
    static int lastEditorSel = -1;
    if (ImNodes::NumSelectedNodes() == 1) {
        int selId = -1;
        ImNodes::GetSelectedNodes(&selId);
        if (selId != lastEditorSel) {
            lastEditorSel = selId;
            for (int i = 0; i < (int)targetNodes.size(); ++i)
                if (targetNodes[i]->id == selId) { selNode = i; selTarget = 0; break; }
        }
    }
    selNode = std::clamp(selNode, 0, (int)targetNodes.size() - 1);
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Node##map", targetNodes[selNode]->label.c_str())) {
        for (int i = 0; i < (int)targetNodes.size(); ++i) {
            ImGui::PushID(i);
            if (ImGui::Selectable(targetNodes[i]->label.c_str(), i == selNode)) {
                if (i != selNode) selTarget = 0;
                selNode = i;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    GraphNode* tn = targetNodes[selNode];
    std::vector<TargetOpt> opts = eligible_targets(tn);
    if (opts.empty()) {
        ImGui::TextDisabled("Selected node has no mappable params/settings.");
        ImGui::End();
        return;
    }
    selTarget = std::clamp(selTarget, 0, (int)opts.size() - 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Param / setting##map", opts[selTarget].name.c_str())) {
        for (int i = 0; i < (int)opts.size(); ++i) {
            char lbl[160];
            snprintf(lbl, sizeof(lbl), "%s%s##mt%d", opts[i].name.c_str(),
                     opts[i].isSetting ? "  (setting)" : "", i);
            if (ImGui::Selectable(lbl, i == selTarget)) selTarget = i;
        }
        ImGui::EndCombo();
    }

    std::string target = tn->label + "." + opts[selTarget].name;
    bool exists = target_exists_for("frequency", target);
    ImGui::BeginDisabled(exists);
    if (ImGui::Button("Add (follows note)##mapbare")) {
        // Bare binding: the target receives the raw note frequency.
        if (!s_loadedParamMap.contains("frequency")) {
            s_loadedParamMap["frequency"] = target;
        } else {
            nlohmann::json& e = s_loadedParamMap["frequency"];
            if (!e.is_array()) {
                nlohmann::json arr = nlohmann::json::array();
                arr.push_back(e);
                e = std::move(arr);
            }
            e.push_back(target);
        }
        mark_graph_dirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add with curve##mapcurve")) {
        curves_add_entry("frequency", target, opts[selTarget].current);
        mark_graph_dirty();
    }
    ImGui::EndDisabled();
    if (exists) {
        ImGui::SameLine();
        ImGui::TextDisabled("(already bound)");
    }

    ImGui::End();
}


static void draw_keyboard_panel() {
    ImGui::Begin("Keyboard", nullptr, ImGuiWindowFlags_NoCollapse);

    // --- Header bar ---
    ImGui::Text("Octave");
    ImGui::SameLine();
    spinner_int("kb_oct", &g_keyboard.octave, 1, 0, 20);

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    ImGui::Text("Duration");
    ImGui::SameLine();
    spinner_float("kb_dur", &g_keyboard.duration, 0.05f, 0.05f, 30.0f, "%.2f");

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    ImGui::Text("Velocity");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    // One velocity, everywhere: this slider and the Note tab's edit the same
    // value — two independent velocities meant the Note tab looked ignored
    // when playing via QWERTY (Matt 2026-08-12).
    ImGui::SliderFloat("##vel", &g_transport.velocity, 0.0f, 1.0f, "%.2f");

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    if (ImGui::Checkbox("Sustain", &g_keyboard.sustain)) {}

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    // Wire to Transport's noteMode. Disabled in node-graph mode: notes need
    // an instrument (see draw_transport_panel's PC Keyboard button).
    bool kbDisabled = (s_graphMode != GraphMode::PatchGraph);
    ImGui::BeginDisabled(kbDisabled);
    ImGui::Checkbox("PC Keyboard##kb", &g_transport.noteMode);
    ImGui::EndDisabled();
    if (kbDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Keyboard needs an instrument patch");
    if (kbDisabled) {
        ImGui::SameLine();
        ImGui::TextDisabled("(keyboard needs an instrument patch)");
    }

    // --- Audio peak meter (diagnostic, fresh line so it's always visible) ---
    // pre = raw mixer output before soft_clip; post = what hit the device.
    // Color: green ≤ 0.7, yellow ≤ 0.95, red > 0.95 (soft_clip range).
    {
        float pre  = g_audioPeakPre.load(std::memory_order_relaxed);
        float post = g_audioPeakPost.load(std::memory_order_relaxed);
        int   nV   = g_audioActiveVoices.load(std::memory_order_relaxed);
        auto peakColor = [](float v) {
            if (v > 0.95f) return ImVec4(1.0f, 0.3f, 0.3f, 1.0f); // red
            if (v > 0.70f) return ImVec4(1.0f, 0.9f, 0.3f, 1.0f); // yellow
            return ImVec4(0.4f, 1.0f, 0.4f, 1.0f);                // green
        };
        ImGui::Text("AUDIO:  voices=%d", nV);
        ImGui::SameLine();
        ImGui::TextColored(peakColor(pre),  "   pre=%.2f",  pre);
        ImGui::SameLine();
        ImGui::TextColored(peakColor(post), "   post=%.2f", post);
    }

    // --- QWERTY input (gated by note mode and not typing in text field;
    // dead in node-graph mode — play_note needs an instrument) ---
    if (g_transport.noteMode && !kbDisabled && !ImGui::GetIO().WantTextInput) {
        // Key-up gating (note-contained sound, 2026-08-13): a QWERTY key
        // HOLDS its note; release fires gate_release on the voice's
        // envelopes. Per-key bookkeeping so an octave change mid-hold still
        // releases the right note.
        static int s_qwertyHeldNote[QWERTY_MAP_COUNT];
        static bool s_qwertyHeldInit = false;
        if (!s_qwertyHeldInit) {
            for (int i = 0; i < QWERTY_MAP_COUNT; ++i) s_qwertyHeldNote[i] = -1;
            s_qwertyHeldInit = true;
        }
        for (int i = 0; i < QWERTY_MAP_COUNT; ++i) {
            if (ImGui::IsKeyPressed(s_qwertyMap[i].key, false)) {
                int absNote = g_keyboard.octave * 12 + s_qwertyMap[i].offset;
                if (absNote < 0) continue;   // lower zone below MIDI 0 at low octaves
                s_qwertyHeldNote[i] = absNote;
                play_note_held(float(absNote), g_transport.velocity, g_keyboard.duration);
            }
            if (ImGui::IsKeyReleased(s_qwertyMap[i].key) && s_qwertyHeldNote[i] >= 0) {
                release_note_held(s_qwertyHeldNote[i]);
                s_qwertyHeldNote[i] = -1;
            }
        }
        // Action keys — moved off G/H (octave) and V/B (duration) 2026-09-03:
        // those letters are now lower-zone notes. Both zones track the octave.
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
            g_keyboard.octave = std::max(0, g_keyboard.octave - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
            g_keyboard.octave = std::min(20, g_keyboard.octave + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
            g_keyboard.duration = std::max(0.05f, g_keyboard.duration * 0.5f);
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
            g_keyboard.duration = std::min(30.0f, g_keyboard.duration * 2.0f);
    }

    // --- Piano keyboard rendering via ImDrawList ---
    const int WHITE_KEYS_PER_OCT = 7;

    float availW = ImGui::GetContentRegionAvail().x;
    float availH = ImGui::GetContentRegionAvail().y;

    // Octave count is chosen from the available width so white-key width stays
    // in a comfortable band instead of shrinking/expanding without limit
    // (Matt, 2026-08-21). Default 4 octaves; add an octave when keys get too
    // wide, drop one when they get too narrow. The layout always ends on the
    // C above the top octave, so the range is a whole number of octaves + 1.
    // Band 20/40 -> 30/60 -> 25/45 (Matt 2026-08-22, two passes). The
    // octave count is recomputed from width every frame (no stored state),
    // so any band is stable; with MAX/MIN < 15/8 the 1->2 octave step
    // (8 -> 15 white keys) has a small width window where neither bound
    // can be met and the shrink loop wins (1 octave, keys a hair over MAX).
    const float MIN_KEY_W = 25.0f;   // narrower than this → drop an octave
    const float MAX_KEY_W = 45.0f;   // wider than this   → add an octave
    const int   MIN_OCTAVES = 1;
    const int   MAX_OCTAVES = 10;    // stays inside the MIDI range from any base
    auto keyWidthFor = [&](int oct) { return availW / float(oct * WHITE_KEYS_PER_OCT + 1); };
    int NUM_OCTAVES = 4;
    while (NUM_OCTAVES < MAX_OCTAVES && keyWidthFor(NUM_OCTAVES) > MAX_KEY_W) ++NUM_OCTAVES;
    while (NUM_OCTAVES > MIN_OCTAVES && keyWidthFor(NUM_OCTAVES) < MIN_KEY_W) --NUM_OCTAVES;

    // White keys = full octaves + the trailing top C.
    const int TOTAL_WHITE = NUM_OCTAVES * WHITE_KEYS_PER_OCT + 1;

    float keyW = availW / float(TOTAL_WHITE);
    float whiteH = std::max(40.0f, availH - 4.0f);
    float blackH = whiteH * 0.6f;
    float blackW = keyW * 0.65f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Check if a MIDI note is currently sounding in any voice
    auto isNoteActive = [](int midi) {
        for (int vi = 0; vi < MAX_VOICES; ++vi)
            if (g_voices[vi].active && g_voices[vi].midiNote == midi) return true;
        return false;
    };

    static const int whiteOffsets[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const char* whiteNames[7] = { "C", "D", "E", "F", "G", "A", "B" };

    struct BlackKeyInfo { int afterWhite; int chromaticOffset; };
    static const BlackKeyInfo blackKeys[5] = {
        {0, 1}, {1, 3}, {3, 6}, {4, 8}, {5, 10}
    };

    int baseNote = g_keyboard.octave * 12;

    // Draw white keys (flat index so the trailing top C is included).
    for (int idx = 0; idx < TOTAL_WHITE; ++idx) {
        int oct = idx / WHITE_KEYS_PER_OCT;
        int w   = idx % WHITE_KEYS_PER_OCT;
        float x0 = origin.x + idx * keyW;
        float y0 = origin.y;
        float x1 = x0 + keyW - 1.0f;
        float y1 = y0 + whiteH;

        int midiNote = baseNote + oct * 12 + whiteOffsets[w];
        bool isActive = isNoteActive(midiNote);

        ImU32 fillColor = isActive ? IM_COL32(140, 200, 255, 255) : IM_COL32(240, 240, 240, 255);
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fillColor);
        dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(80, 80, 80, 255));

        const char* name = whiteNames[w];
        ImVec2 textSize = ImGui::CalcTextSize(name);
        dl->AddText(ImVec2(x0 + (keyW - 1.0f - textSize.x) * 0.5f, y1 - textSize.y - 4.0f),
                    IM_COL32(60, 60, 60, 255), name);

        if (g_transport.noteMode && !kbDisabled) {
            int chromOffset = oct * 12 + whiteOffsets[w];
            if (chromOffset < 20) {
                const char* ql = qwerty_label_for_offset(chromOffset);
                if (ql[0]) {
                    ImVec2 qlSize = ImGui::CalcTextSize(ql);
                    dl->AddText(ImVec2(x0 + (keyW - 1.0f - qlSize.x) * 0.5f, y0 + 4.0f),
                                IM_COL32(0, 0, 0, 255), ql);
                }
            }
        }
    }

    // Draw black keys on top
    for (int oct = 0; oct < NUM_OCTAVES; ++oct) {
        for (int b = 0; b < 5; ++b) {
            int whiteIdx = oct * WHITE_KEYS_PER_OCT + blackKeys[b].afterWhite;
            float x0 = origin.x + (whiteIdx + 1) * keyW - blackW * 0.5f;
            float y0 = origin.y;
            float x1 = x0 + blackW;
            float y1 = y0 + blackH;

            int midiNote = baseNote + oct * 12 + blackKeys[b].chromaticOffset;
            bool isActive = isNoteActive(midiNote);

            ImU32 fillColor = isActive ? IM_COL32(100, 170, 240, 255) : IM_COL32(40, 40, 40, 255);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), fillColor);
            dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(20, 20, 20, 255));

            if (g_transport.noteMode && !kbDisabled) {
                int chromOffset = oct * 12 + blackKeys[b].chromaticOffset;
                if (chromOffset < 20) {
                    const char* ql = qwerty_label_for_offset(chromOffset);
                    if (ql[0]) {
                        ImVec2 qlSize = ImGui::CalcTextSize(ql);
                        dl->AddText(ImVec2(x0 + (blackW - qlSize.x) * 0.5f, y0 + 4.0f),
                                    IM_COL32(200, 200, 100, 255), ql);
                    }
                }
            }
        }
    }

    // --- Click interaction: black keys first, then white ---
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    bool clicked = ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered();

    if (clicked) {
        int hitNote = -1;
        float hitVelocity = g_transport.velocity;

        for (int oct = 0; oct < NUM_OCTAVES && hitNote < 0; ++oct) {
            for (int b = 0; b < 5; ++b) {
                int whiteIdx = oct * WHITE_KEYS_PER_OCT + blackKeys[b].afterWhite;
                float x0 = origin.x + (whiteIdx + 1) * keyW - blackW * 0.5f;
                float y0 = origin.y;
                float x1 = x0 + blackW;
                float y1 = y0 + blackH;

                if (mousePos.x >= x0 && mousePos.x <= x1 && mousePos.y >= y0 && mousePos.y <= y1) {
                    hitNote = baseNote + oct * 12 + blackKeys[b].chromaticOffset;
                    float t = (mousePos.y - y0) / (y1 - y0);
                    hitVelocity = 0.125f + t * 0.875f;
                    break;
                }
            }
        }

        if (hitNote < 0) {
            for (int idx = 0; idx < TOTAL_WHITE; ++idx) {
                int oct = idx / WHITE_KEYS_PER_OCT;
                int w   = idx % WHITE_KEYS_PER_OCT;
                float x0 = origin.x + idx * keyW;
                float y0 = origin.y;
                float x1 = x0 + keyW - 1.0f;
                float y1 = y0 + whiteH;

                if (mousePos.x >= x0 && mousePos.x <= x1 && mousePos.y >= y0 && mousePos.y <= y1) {
                    hitNote = baseNote + oct * 12 + whiteOffsets[w];
                    float t = (mousePos.y - y0) / (y1 - y0);
                    hitVelocity = 0.125f + t * 0.875f;
                    break;
                }
            }
        }

        if (hitNote >= 0) {
            play_note(float(hitNote), hitVelocity, g_keyboard.duration);
        }
    }

    ImGui::Dummy(ImVec2(availW, whiteH));
    ImGui::End();
}

// ===========================================================================
// Transport: generate actions
// ===========================================================================

// Render a passage (sequence of notes) through the UI DSP graph into the
// output waveform buffers.  Each note sets the frequency parameter, prepares
// the graph for the note duration, renders, then concatenates.
static void render_passage_waveforms(const std::vector<ParsedNote>& notes, float velocity) {
    ValueSource* src = find_output_source();
    if (!src || notes.empty()) return;

    // Undo any streaming envelope overrides before an offline render.
    stream_envelopes_restore();

    // Compute total samples
    int totalSamples = 0;
    for (const auto& n : notes)
        totalSamples += int(n.durationSeconds * float(AUDIO_SAMPLE_RATE));

    buffer_playback_detach();  // it points into this vector (3k)
    g_outputWaveform.resize(totalSamples);
    g_waveformSamples = totalSamples;
    for (auto& node : s_nodes) {
        if (node.dspSource && !is_special_ui_type(node.typeName))
            node.waveformData.resize(totalSamples);
        else
            node.waveformData.clear();
    }

    int offset = 0;
    for (const auto& pn : notes) {
        float freq = note_to_freq(pn.noteNumber);
        apply_param_map(freq, velocity);
        apply_perform_nodes(freq, velocity);
        for (auto& n : s_nodes) {
            if (n.typeName == NT_PARAMETER && n.paramName == "frequency") {
                if (auto* p = n.find_input("default"))
                    p->constantSrc->set(freq);
            }
        }

        int samples = int(pn.durationSeconds * float(AUDIO_SAMPLE_RATE));
        prepare_graph(samples);

        for (int i = 0; i < samples && (offset + i) < totalSamples; ++i) {
            float s = src->next();
            g_outputWaveform[offset + i] = s * velocity;
            for (auto& node : s_nodes) {
                if (!node.waveformData.empty())
                    node.waveformData[offset + i] = node.dspSource->current();
            }
        }
        offset += samples;
    }

    wave_view_after_render(totalSamples);
}

// Render chords through the Conductor/ChordPerformer pipeline using the
// current patch file loaded as a PitchedInstrument.
static void render_chords_waveforms(const std::vector<ParsedChord>& chords, float bpm,
                                     const std::string& figurePrefix, float spreadMs) {
    if (chords.empty()) return;
    if (s_currentFilePath.empty()) {
        transport_set_status("No patch file loaded — save/load a patch first", true);
        return;
    }

    try {
        auto ip = load_instrument_patch(get_playback_patch_path());
        ip.instrument->volume = 1.0f;
        // Disable the engine's per-sample soft_clip so we can see the *real*
        // un-clipped peak below. Otherwise the peak-normalize is a no-op —
        // soft_clip caps every sample at ~0.999 before we can measure it,
        // and the original signal's harmonic distortion is already baked in.
        ip.instrument->peakGuard = false;

        Part part;
        part.name = "chords";
        for (const auto& pc : chords) {
            part.add_chord(pc.chord);
        }

        Conductor conductor;
        conductor.chordPerformer.defaultSpreadMs = spreadMs;
        conductor.chordPerformer.register_josie_figures();
        conductor.perform(part, bpm, *ip.instrument);

        float totalSeconds = part.totalBeats() * 60.0f / bpm + 1.0f;
        int frames = int(totalSeconds * float(ip.sampleRate));
        std::vector<float> mono(frames, 0.0f);
        RenderContext _ctx{ip.sampleRate};
        ip.instrument->render(_ctx, mono.data(), frames);

        // Peak-normalize to prevent distortion from overlapping notes.
        // With peakGuard disabled above, mono contains the un-clipped sum, so
        // a chord that sums to peak 4.0 gets a real 0.2375× scale instead of
        // being silently soft-clipped at 0.999 and then "normalized" by 1.0×.
        float peak = 0.0f;
        for (int i = 0; i < frames; ++i) {
            float a = std::fabs(mono[i]);
            if (a > peak) peak = a;
        }
        if (peak > 0.95f) {
            float scale = 0.95f / peak;
            for (int i = 0; i < frames; ++i)
                mono[i] *= scale;
        }

        // Copy into UI waveform buffers (resample if needed, but likely same rate)
        int uiFrames = frames;
        if (ip.sampleRate != AUDIO_SAMPLE_RATE) {
            uiFrames = int(totalSeconds * float(AUDIO_SAMPLE_RATE));
        }

        buffer_playback_detach();  // it points into this vector (3k)
        g_outputWaveform.resize(uiFrames);
        g_waveformSamples = uiFrames;
        for (auto& node : s_nodes) node.waveformData.clear();

        if (ip.sampleRate == AUDIO_SAMPLE_RATE) {
            for (int i = 0; i < uiFrames; ++i)
                g_outputWaveform[i] = (i < frames) ? mono[i] : 0.0f;
        } else {
            // Simple nearest-neighbor resampling
            float ratio = float(ip.sampleRate) / float(AUDIO_SAMPLE_RATE);
            for (int i = 0; i < uiFrames; ++i) {
                int srcIdx = int(float(i) * ratio);
                g_outputWaveform[i] = (srcIdx < frames) ? mono[srcIdx] : 0.0f;
            }
        }

        wave_view_after_render(uiFrames);
    } catch (const std::exception& e) {
        fprintf(stderr, "Chords render error: %s\n", e.what());
    }
}

// Parse drum map string: "KK=patches/kick.json;SN=patches/snare.json;..."
// Returns map of drum number → patch file path.
static std::unordered_map<int, std::string> parse_drum_map(const char* str) {
    std::unordered_map<int, std::string> result;
    if (!str || !str[0]) return result;

    std::istringstream iss(str);
    std::string entry;
    while (std::getline(iss, entry, ';')) {
        auto eq = entry.find('=');
        if (eq == std::string::npos || eq < 2) continue;
        std::string id = entry.substr(0, 2);
        std::string path = entry.substr(eq + 1);
        // Trim whitespace
        while (!path.empty() && path.front() == ' ') path.erase(0, 1);
        while (!path.empty() && path.back() == ' ') path.pop_back();
        if (path.empty()) continue;
        result[parse_drum_id(id)] = path;
    }
    return result;
}

// Render a drum pattern through DrumKit loaded from per-drum patches.
static void render_drums_waveforms(const ParsedDrumPattern& pat, float bpm, const char* drumMapStr) {
    if (pat.figure.hits.empty()) return;

    auto drumPaths = parse_drum_map(drumMapStr);
    if (drumPaths.empty()) {
        transport_set_status("No drum patches specified — set Drums field (e.g. KK=patches/kick.json;SN=patches/snare.json)", true);
        return;
    }

    try {
        // Build DrumKit: load a patch per drum number
        // Keep the InstrumentPatch objects alive so shared_ptrs don't die
        std::vector<InstrumentPatch> loadedPatches;
        int maxDrum = 0;
        for (const auto& hit : pat.figure.hits)
            maxDrum = std::max(maxDrum, hit.drumNumber);

        DrumKit kit;
        kit.sampleRate = AUDIO_SAMPLE_RATE;
        kit.sources.resize(maxDrum + 1);

        int loadedCount = 0;
        for (const auto& [drumNum, path] : drumPaths) {
            if (drumNum > maxDrum) continue;
            if (!std::filesystem::exists(path)) {
                char buf[320];
                snprintf(buf, sizeof(buf), "Drum patch not found: %s", path.c_str());
                transport_set_status(buf, true);
                return;
            }
            loadedPatches.push_back(load_instrument_patch(path));
            auto& ip = loadedPatches.back();
            if (!ip.instrument->voicePool.empty()) {
                kit.sources[drumNum].source = ip.instrument->voicePool[0].source;
                loadedCount++;
            }
        }

        if (loadedCount == 0) {
            transport_set_status("No drum patches could be loaded", true);
            return;
        }

        // Perform all hits
        for (int rep = 0; rep < pat.repeats; ++rep) {
            float repOffsetBeats = float(rep) * pat.figure.totalTime;
            for (const auto& hit : pat.figure.hits) {
                if (hit.drumNumber >= (int)kit.sources.size()) continue;
                if (!kit.sources[hit.drumNumber].source) continue;
                float startSec = (repOffsetBeats + hit.time) * 60.0f / bpm;
                float durSec = (hit.duration > 0.0f ? hit.duration : 0.25f) * 60.0f / bpm;
                if (durSec < 0.02f) durSec = 0.02f;
                kit.play_hit(hit.drumNumber, hit.velocity, durSec, startSec);
            }
        }

        float totalSeconds = pat.figure.totalTime * 60.0f / bpm * float(pat.repeats) + 1.0f;
        int frames = int(totalSeconds * float(AUDIO_SAMPLE_RATE));
        std::vector<float> mono(frames, 0.0f);
        RenderContext _ctx{AUDIO_SAMPLE_RATE};
        kit.render(_ctx, mono.data(), frames);

        // Peak-normalize
        float peak = 0.0f;
        for (int i = 0; i < frames; ++i) {
            float a = std::fabs(mono[i]);
            if (a > peak) peak = a;
        }
        if (peak > 1.0f) {
            float scale = 0.95f / peak;
            for (int i = 0; i < frames; ++i) mono[i] *= scale;
        }

        g_outputWaveform.assign(mono.begin(), mono.end());
        g_waveformSamples = frames;
        for (auto& node : s_nodes) node.waveformData.clear();

        wave_view_after_render(frames);
    } catch (const std::exception& e) {
        transport_set_status(e.what(), true);
    }
}

// Last note played (Play/Generate, QWERTY, on-screen keys). Displayed
// right-aligned in the transport bar; label uses the HOUSE octave
// convention (name = midi%12, octave = midi/12 — comp REVIEW item 19).
static int   g_lastNoteMidi = -1;
static float g_lastNoteVel  = -1.0f;
static void note_played(float noteNum, float velocity) {
    g_lastNoteMidi = int(noteNum + 0.5f);
    g_lastNoteVel  = velocity;
}

static void transport_set_status(const char* msg, bool isError) {
    snprintf(g_transport.statusMsg, sizeof(g_transport.statusMsg), "%s", msg);
    g_transport.statusIsError = isError;
}

// Shared precondition for Note/Passage generate. The old check was just
// find_output_source() != nullptr with a blanket "No patch loaded" message —
// misleading (and blocking) whenever a patch WAS loaded but the UI node graph
// couldn't model the output link. The authoritative render path goes through
// the CLI patch loader (via get_playback_patch_path) and does not need the UI
// graph at all, so only refuse when there is genuinely nothing to render, and
// say precisely what is wrong otherwise.
static bool transport_can_generate(ValueSource* uiSrc) {
    if (uiSrc) return true;
    if (!s_currentFilePath.empty()) return true;  // authoritative path can still render
    bool hasOutputNode = false;
    for (auto& n : s_nodes)
        if (n.typeName == NT_PATCH_OUTPUT) hasOutputNode = true;
    transport_set_status(hasOutputNode
        ? "Output node has no source connected — wire a node into Output"
        : "No patch loaded — open a patch first", true);
    return false;
}

// Transport settings captured at the last successful Note-mode Generate, so
// Play can detect staleness (see PlayMode::Note in transport_play).
static struct {
    char  noteStr[64] = {};
    float velocity = -1.0f;
    float duration = -1.0f;
    bool  valid = false;
} g_noteGenSnap;

static void transport_generate() {
    g_transport.statusMsg[0] = '\0';
    g_transport.statusIsError = false;

    switch (g_transport.mode) {
        case PlayMode::Note: {
            ValueSource* uiSrc = find_output_source();
            if (!transport_can_generate(uiSrc)) break;
            float noteNum = parse_note_input(g_transport.noteStr);
            // Per-node waveforms from UI DSP tree (fast, for inspector views).
            // Skipped when the UI graph has no modeled output link — the
            // authoritative render below is what actually matters.
            if (uiSrc)
                render_waveforms(noteNum, g_transport.velocity, g_transport.duration);
            // Authoritative audio into g_outputWaveform (slow for fat Multiplex;
            // UI blocks here until done — that's the visible feedback that
            // generation is running. Play then just streams the buffer.)
            // On failure it sets its own status message — don't overwrite it.
            if (render_output_authoritative(noteNum, g_transport.velocity,
                                            g_transport.duration)) {
                transport_set_status("Generated note", false);
                note_played(noteNum, g_transport.velocity);
                std::snprintf(g_noteGenSnap.noteStr, sizeof(g_noteGenSnap.noteStr),
                              "%s", g_transport.noteStr);
                g_noteGenSnap.velocity = g_transport.velocity;
                g_noteGenSnap.duration = g_transport.duration;
                g_noteGenSnap.valid = true;
            }
            break;
        }
        case PlayMode::Passage: {
            ValueSource* uiSrc = find_output_source();
            if (!transport_can_generate(uiSrc)) break;
            try {
                auto notes = parse_passage(g_transport.passageStr, g_transport.octave, g_transport.bpm);
                if (notes.empty()) {
                    transport_set_status("No notes parsed from passage string", true);
                } else {
                    if (uiSrc)
                        render_passage_waveforms(notes, g_transport.velocity);
                    if (render_passage_output_authoritative(notes, g_transport.velocity)) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "Generated %d notes", (int)notes.size());
                        transport_set_status(buf, false);
                    }
                }
            } catch (const std::exception& e) {
                transport_set_status(e.what(), true);
            }
            break;
        }
        case PlayMode::Chords: {
            try {
                std::string dictName(g_transport.defChordGrp);
                if (dictName.empty()) dictName = "Default";
                std::string fig(g_transport.figure);
                auto chords = parse_chord_string(g_transport.chordsStr, g_transport.octave,
                                                  dictName, fig);
                if (chords.empty()) {
                    transport_set_status("No chords parsed from string", true);
                } else {
                    float spreadMs = g_transport.chordDelay > 0.0f ? g_transport.chordDelay : 15.0f;
                    render_chords_waveforms(chords, g_transport.bpm, fig, spreadMs);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Generated %d chords", (int)chords.size());
                    transport_set_status(buf, false);
                }
            } catch (const std::exception& e) {
                transport_set_status(e.what(), true);
            }
            break;
        }
        case PlayMode::Drums: {
            try {
                auto pat = parse_drum_pattern(g_transport.pattern);
                if (pat.figure.hits.empty()) {
                    transport_set_status("No hits parsed from drum pattern", true);
                } else {
                    render_drums_waveforms(pat, g_transport.bpm, g_transport.drumMap);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Generated %d hits x %d repeats",
                             (int)pat.figure.hits.size(), pat.repeats);
                    transport_set_status(buf, false);
                }
            } catch (const std::exception& e) {
                transport_set_status(e.what(), true);
            }
            break;
        }
    }
}

static void transport_play() {
    if (s_graphMode != GraphMode::PatchGraph) {
        // Node graphs have no instrument, so Note/Passage/Chords/Drums don't
        // apply — Play means "stream the Mixer live", same as Stream. This is
        // the continuous-sound pathway (crackling fire etc.), the analogue of
        // legacy Mixer → AudioAdapter wiring.
        play_continuous(g_transport.velocity);
        return;
    }

    switch (g_transport.mode) {
        case PlayMode::Note: {
            // Stream the pre-rendered buffer produced during Generate — but
            // regenerate when note/velocity/duration changed since the last
            // Generate (the old empty-buffer check replayed the stale buffer,
            // making the velocity field look ignored; Matt 2026-08-12).
            bool stale = g_outputWaveform.empty() || g_waveformSamples == 0 ||
                         !g_noteGenSnap.valid ||
                         std::strcmp(g_noteGenSnap.noteStr, g_transport.noteStr) != 0 ||
                         g_noteGenSnap.velocity != g_transport.velocity ||
                         g_noteGenSnap.duration != g_transport.duration;
            if (stale)
                transport_generate();
            play_buffer();
            break;
        }
        case PlayMode::Passage: {
            // Generate into buffer, then stream the pre-rendered result
            auto notes = parse_passage(g_transport.passageStr, g_transport.octave, g_transport.bpm);
            if (!notes.empty()) {
                render_passage_waveforms(notes, g_transport.velocity);
                play_buffer();
            }
            break;
        }
        case PlayMode::Chords:
            // Generate into buffer, then stream the pre-rendered result
            transport_generate();
            play_buffer();
            break;
        case PlayMode::Drums:
            transport_generate();
            play_buffer();
            break;
    }
}

static std::string save_wav_dialog() {
    char filename[MAX_PATH] = "output.wav";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "WAV Files\0*.wav\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    const char* rememberedWav = feature_initial_dir("wav");
    ofn.lpstrInitialDir = rememberedWav ? rememberedWav : "renders";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "wav";
    if (GetSaveFileNameA(&ofn)) {
        remember_feature_dir("wav", filename);
        return filename;
    }
    return "";
}

static void transport_save_wav() {
    if (g_outputWaveform.empty()) {
        transport_set_status("Nothing to save — generate first", true);
        return;
    }

    std::string path = save_wav_dialog();
    if (path.empty()) return;

    // Convert mono to stereo
    std::vector<float> stereo(g_outputWaveform.size() * 2);
    for (size_t i = 0; i < g_outputWaveform.size(); ++i) {
        stereo[i * 2]     = g_outputWaveform[i];
        stereo[i * 2 + 1] = g_outputWaveform[i];
    }

    if (write_wav_16le_stereo(path, AUDIO_SAMPLE_RATE, stereo)) {
        char buf[320];
        snprintf(buf, sizeof(buf), "Saved: %s (%d frames)", path.c_str(), (int)g_outputWaveform.size());
        transport_set_status(buf, false);
    } else {
        char buf[320];
        snprintf(buf, sizeof(buf), "Failed to save: %s", path.c_str());
        transport_set_status(buf, true);
    }
}

// ===========================================================================
// Transport panel UI
// ===========================================================================

// Helper: label at start of line (absolute position)
static void transport_label(const char* label, float labelW) {
    ImGui::Text("%s", label);
    ImGui::SameLine(labelW);
}
// Helper: inline label with generous gap before it
static void transport_label_inline(const char* label) {
    ImGui::SameLine(0, 30);
    ImGui::Text("%s", label);
    ImGui::SameLine();
}

static void draw_transport_panel() {
    ImGui::Begin("Transport", nullptr, ImGuiWindowFlags_NoCollapse);

    float lw = 70.0f; // label width

    // Mode selection via tab bar
    if (ImGui::BeginTabBar("##transport_tabs")) {
        if (ImGui::BeginTabItem("Note"))    { g_transport.mode = PlayMode::Note;    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Passage")) { g_transport.mode = PlayMode::Passage; ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Chords"))  { g_transport.mode = PlayMode::Chords;  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Drums"))   { g_transport.mode = PlayMode::Drums;   ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    // Per-mode fields
    switch (g_transport.mode) {
        case PlayMode::Note:
            ImGui::Text("Note"); ImGui::SameLine();
            ImGui::SetNextItemWidth(50);
            ImGui::InputText("##note", g_transport.noteStr, sizeof(g_transport.noteStr));
            transport_label_inline("Velocity");
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("##vel", &g_transport.velocity, 0.0f, 1.0f);
            transport_label_inline("Duration");
            spinner_float("dur", &g_transport.duration, 0.1f, 0.1f, 30.0f, "%.1f");
            break;

        case PlayMode::Passage:
            ImGui::Text("Passage"); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##passage", g_transport.passageStr, sizeof(g_transport.passageStr));
            ImGui::Text("Octave"); ImGui::SameLine();
            spinner_int("oct", &g_transport.octave, 1, 0, 8);
            transport_label_inline("BPM");
            spinner_float("bpm", &g_transport.bpm, 5.0f, 20.0f, 300.0f, "%.0f");
            transport_label_inline("Velocity");
            ImGui::SetNextItemWidth(100);
            ImGui::SliderFloat("##pvel", &g_transport.velocity, 0.0f, 1.0f);
            break;

        case PlayMode::Chords:
            ImGui::Text("Chords"); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##chords", g_transport.chordsStr, sizeof(g_transport.chordsStr));
            ImGui::Text("Group"); ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("##grp", g_transport.defChordGrp, sizeof(g_transport.defChordGrp));
            transport_label_inline("Figure");
            ImGui::SetNextItemWidth(100);
            ImGui::InputText("##fig", g_transport.figure, sizeof(g_transport.figure));
            ImGui::Text("Octave"); ImGui::SameLine();
            spinner_int("coct", &g_transport.octave, 1, 0, 8);
            transport_label_inline("BPM");
            spinner_float("cbpm", &g_transport.bpm, 5.0f, 20.0f, 300.0f, "%.0f");
            transport_label_inline("Inversion");
            spinner_int("inv", &g_transport.inversion, 1, 0, 4);
            transport_label_inline("Spread");
            spinner_int("sprd", &g_transport.spread, 1, 0, 4);
            transport_label_inline("Delay");
            spinner_float("cdly", &g_transport.chordDelay, 5.0f, 0.0f, 200.0f, "%.0f");
            break;

        case PlayMode::Drums:
            ImGui::Text("Pattern"); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##pat", g_transport.pattern, sizeof(g_transport.pattern));
            ImGui::Text("Drums"); ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##dmap", g_transport.drumMap, sizeof(g_transport.drumMap));
            ImGui::Text("BPM"); ImGui::SameLine();
            spinner_float("dbpm", &g_transport.bpm, 5.0f, 20.0f, 300.0f, "%.0f");
            break;
    }

    ImGui::Separator();

    // Action buttons. Generate is synchronous and can block for seconds on
    // fat patches (Multiplex:50 etc.). 3-phase state so the user sees the
    // button flip to "Generating..." (and the waveform clear) BEFORE the
    // blocking render starts.
    if (s_genState >= 1) {
        ImVec4 col(0.75f, 0.35f, 0.10f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, col);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, col);
        ImGui::Button("Generating...");
        ImGui::PopStyleColor(3);
    } else {
        if (ImGui::Button("Generate")) {
            s_genState = 1;
        }
    }
    ImGui::SameLine();

    bool isPlaying = is_playing();
    if (!isPlaying) {
        if (ImGui::Button("Play")) {
            transport_play();
        }
    } else {
        if (ImGui::Button("Stop")) {
            stop_playback();
        }
    }
    ImGui::SameLine();

    {
        // Notes need an instrument (play_note routes through the instrument
        // loader) — in node-graph mode the keyboard stays visible but
        // disabled so it doesn't look alive-but-dead.
        bool kbDisabled = (s_graphMode != GraphMode::PatchGraph);
        bool noteMode = g_transport.noteMode && !kbDisabled;
        if (noteMode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
        }
        ImGui::BeginDisabled(kbDisabled);
        if (ImGui::Button("PC Keyboard")) {
            g_transport.noteMode = !g_transport.noteMode;
        }
        ImGui::EndDisabled();
        if (kbDisabled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Keyboard needs an instrument patch");
        if (noteMode) {
            ImGui::PopStyleColor(2);
        }
    }
    ImGui::SameLine();

    // MIDI input selector — startup auto-opens the first device; this combo
    // is for switching when there is more than one, or picking up a keyboard
    // plugged in after launch (ports re-enumerate every time it opens).
    {
        char label[192];
        if (g_midiPort >= 0)
            std::snprintf(label, sizeof(label), "MIDI: %s",
                          g_midiPortName.c_str());
        else
            std::snprintf(label, sizeof(label), "MIDI: none");
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("##midiIn", label)) {
            unsigned int count = 0;
            if (g_midiIn) {
                try { count = g_midiIn->getPortCount(); }
                catch (RtMidiError&) {}
            }
            if (ImGui::Selectable("(none)", g_midiPort < 0)) midi_close();
            for (unsigned int p = 0; p < count; ++p) {
                std::string name;
                try { name = g_midiIn->getPortName(p); }
                catch (RtMidiError&) { continue; }
                char item[192];
                std::snprintf(item, sizeof(item), "%s##midi%u",
                              name.c_str(), p);
                if (ImGui::Selectable(item, int(p) == g_midiPort))
                    midi_open_port(p);
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();

    if (ImGui::Button("Save WAV")) {
        transport_save_wav();
    }

    // Status message
    if (g_transport.statusMsg[0]) {
        ImGui::SameLine();
        ImVec4 col = g_transport.statusIsError ? ImVec4(1,0.3f,0.3f,1) : ImVec4(0.5f,0.8f,0.5f,1);
        ImGui::TextColored(col, "%s", g_transport.statusMsg);
    }

    // Last note played — right-aligned (Matt 2026-08-12).
    if (g_lastNoteMidi >= 0) {
        static const char* kNames[12] = {"C","C#","D","D#","E","F",
                                         "F#","G","G#","A","A#","B"};
        char lastBuf[80];
        snprintf(lastBuf, sizeof(lastBuf), "Last note: %d  %s%d  %.1fHz  vel %.2f",
                 g_lastNoteMidi, kNames[g_lastNoteMidi % 12],
                 g_lastNoteMidi / 12, note_to_freq(float(g_lastNoteMidi)),
                 g_lastNoteVel);
        float w = ImGui::CalcTextSize(lastBuf).x;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f,
                        ImGui::GetWindowContentRegionMax().x - w));
        ImGui::TextColored(ImVec4(0.75f, 0.75f, 0.9f, 1.0f), "%s", lastBuf);
    }

    ImGui::End();
}

// ===========================================================================
// Node rendering
// ===========================================================================

static void draw_node(GraphNode& node) {
    ImU32 titleCol = node_title_color(node.typeName);
    ImU32 bgCol = node_bg_color(titleCol);
    // Selected slots NOT pushed: imnodes' default blue marks selection
    // (Matt preferred it); hover keeps the subtle per-type lift, so the
    // two states are distinct.
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleCol);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, lighten(titleCol, 12));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, bgCol);
    ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, lighten(bgCol, 6));

    ImNodes::BeginNode(node.id);

    // Title bar
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(node.label.c_str());
    if (node.id == s_listenTapNode) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "<)))");
    }

    // Mixer "+" button stays — it's structural (adding channels), not parameter editing
    if (node.typeName == NT_STEREO_MIXER) {
        ImGui::SameLine();
        char btnLabel[32];
        snprintf(btnLabel, sizeof(btnLabel), " + ##addch%d", node.id);
        if (ImGui::SmallButton(btnLabel))
            node.add_channel_input();
    }

    ImNodes::EndNodeTitleBar();

    // Parameter node: show param name in body
    if (node.typeName == NT_PARAMETER && !node.paramName.empty()) {
        ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.9f, 1.0f), "%s", node.paramName.c_str());
    }


    // Input pins — compact: show name + read-only value, no editing widgets
    for (auto& pin : node.inputs) {
        // Hide the "default" pin on Parameter nodes (it's internal plumbing)
        if (node.typeName == NT_PARAMETER && pin.name == "default") continue;

        ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
        ImNodes::BeginInputAttribute(pin.id);

        if (is_pin_connected(pin.id)) {
            // Connected: just show pin name in normal white (wire makes connection obvious)
            ImGui::TextUnformatted(pin.name.c_str());
        } else if (pin.inputOnly || pin.name.substr(0, 3) == "ch ") {
            // Input-only or channel pin: gray text
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", pin.name.c_str());
        } else {
            // Unconnected value pin: show name + current value
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s", pin.name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.95f, 1.0f), "%.3f", pin.defaultValue);
        }

        ImNodes::EndInputAttribute();
        ImNodes::PopAttributeFlag();
    }

    // Dynamic pins (pin_model_design.md §6): the settings THIS PATCH drives
    // once per note, below the fixed pins. Gold and quad-shaped — visibly not
    // a fixed pin, because the value is frozen for the life of the note.
    // dynamicPins is read live, so Settings-pane promote/demote shows up the
    // same frame.
    for (auto& [setting, val] : node.dynamicPins.items()) {
        const bool wired = val.is_object();
        const std::string ref = wired ? val.value("ref", std::string("?"))
                                      : std::string();
        ImNodes::PushColorStyle(ImNodesCol_Pin, kDynPinGold);
        ImNodes::PushColorStyle(ImNodesCol_PinHovered, lighten(kDynPinGold, 25));
        // Same detach affordance as real pins: dragging the gold wire off
        // unwires (handled in the IsLinkDestroyed block).
        ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
        // Unwired promoted pins render hollow — same gold, waiting for a wire.
        ImNodes::BeginInputAttribute(node.dyn_attr_id(setting),
                                     wired ? ImNodesPinShape_QuadFilled
                                           : ImNodesPinShape_Quad);
        ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.30f, 1.0f), "%s",
                           setting.c_str());
        if (ImGui::IsItemHovered()) {
            if (wired)
                ImGui::SetTooltip("Dynamic pin: set once at note-on by '%s',\n"
                                  "frozen for the life of the note.\n"
                                  "Drag a Curve output here to change the driver;\n"
                                  "delete the wire to unwire, demote via the\n"
                                  "Settings-pane circle.",
                                  ref.c_str());
            else
                ImGui::SetTooltip("Dynamic pin (unwired): drag a Curve output\n"
                                  "here. Until wired, the Settings-pane value\n"
                                  "applies. Demote via the Settings-pane circle.");
        }
        ImNodes::EndInputAttribute();
        ImNodes::PopAttributeFlag();
        ImNodes::PopColorStyle();  // PinHovered
        ImNodes::PopColorStyle();  // Pin
    }

    // Output pins. The tap pin (feedback_loop_design.md §4.1) renders
    // small/dim in teal — wires from it read the node's previous sample,
    // the only legal way to close a feedback cycle.
    for (auto& pin : node.outputs) {
        if (pin.isTap) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, IM_COL32(70, 150, 160, 255));
            ImNodes::BeginOutputAttribute(pin.id, ImNodesPinShape_Quad);
            float textWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
            ImGui::Indent(150.0f - textWidth - 20);
            ImGui::TextColored(ImVec4(0.35f, 0.65f, 0.7f, 1.0f), "%s",
                               pin.name.c_str());
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
            continue;
        }
        ImNodes::BeginOutputAttribute(pin.id);
        float nodeWidth = 150.0f;
        float textWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
        ImGui::Indent(nodeWidth - textWidth - 20);
        ImGui::TextUnformatted(pin.name.c_str());
        ImNodes::EndOutputAttribute();
    }

    ImNodes::EndNode();
    ImNodes::PopColorStyle(); // NodeBackgroundHovered
    ImNodes::PopColorStyle(); // NodeBackground
    ImNodes::PopColorStyle(); // TitleBarHovered
    ImNodes::PopColorStyle(); // TitleBar
}

// Forward decl — used by the FormantSpectrum inline preview in the
// properties panel; defined later in the file with the other strip draws.
static bool draw_formant_strip(GraphNode* node, ImU32 color,
                               float x, float y, float width, float height,
                               bool showPopoutBtn, int popoutBtnId);

// ===========================================================================
// Collapsed-group drawing + boundary-link projection (spec §3). Rebuilt
// every frame the editor draws.
// ===========================================================================
struct GroupProjection {
    // group input pin (synthetic) -> the real inside input pin it represents
    std::unordered_map<int, int> synthToRealIn;
    // real inside input pin -> its synthetic pin (for link projection)
    std::unordered_map<int, int> realInToSynth;
    // group output pin -> real inside OUTPUT pin (the group output node's)
    std::unordered_map<int, int> groupOutToReal;
    // real inside output pin -> group output pin
    std::unordered_map<int, int> realOutToGroupPin;
};
static GroupProjection s_groupProj;

static void draw_group_node(NodeGroup& g) {
    std::vector<GroupBoundaryIn> ins;
    std::vector<GraphNode*> outs;
    group_boundary(g, ins, outs);
    GraphNode* outNode = group_output_node(g);

    while (g.inPinIds.size() < ins.size()) g.inPinIds.push_back(next_id());

    if (!g.posApplied && !s_headless) {
        ImNodes::SetNodeGridSpacePos(g.editorId, g.pos);
        g.posApplied = true;
    }

    ImU32 titleCol = IM_COL32(90, 60, 120, 255);   // distinct: groups are purple
    ImU32 gbg = IM_COL32(45, 38, 55, 255);
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleCol);
    ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, lighten(titleCol, 12));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, gbg);
    ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, lighten(gbg, 6));

    ImNodes::BeginNode(g.editorId);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(g.name.c_str());
    bool tapped = outNode && outNode->id == s_listenTapNode;
    if (tapped) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "<)))");
    }
    ImNodes::EndNodeTitleBar();
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
                       "%d nodes  (double-click)", (int)g.members.size());

    for (size_t i = 0; i < ins.size(); ++i) {
        // Same detach affordance as any real input pin (Matt 2026-08-20:
        // dragging on a group pin drew a NEW wire instead of detaching).
        // The projected link keeps its REAL id, so the existing destroy
        // handler removes the right link.
        ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
        ImNodes::BeginInputAttribute(g.inPinIds[i]);
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s",
                           ins[i].label.c_str());
        ImNodes::EndInputAttribute();
        ImNodes::PopAttributeFlag();
        s_groupProj.synthToRealIn[g.inPinIds[i]] = ins[i].realInPin;
        s_groupProj.realInToSynth[ins[i].realInPin] = g.inPinIds[i];
    }
    if (outNode && !outNode->outputs.empty()) {
        ImNodes::BeginOutputAttribute(g.outPinId);
        float textWidth = ImGui::CalcTextSize("out").x;
        ImGui::Indent(150.0f - textWidth - 20);
        ImGui::TextUnformatted("out");
        ImNodes::EndOutputAttribute();
        int realOut = outNode->outputs[0].id;
        s_groupProj.groupOutToReal[g.outPinId] = realOut;
        s_groupProj.realOutToGroupPin[realOut] = g.outPinId;
    }
    ImNodes::EndNode();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();
    ImNodes::PopColorStyle();

    if (!s_headless) g.pos = ImNodes::GetNodeGridSpacePos(g.editorId);
}

// The pin to draw for a real pin at the current view level: the pin itself
// when its node is visible; the collapsing ancestor group's projected pin
// when the node is inside a collapsed group at this level; -1 when the
// endpoint has no representation here (e.g. an outside node while drilled
// into a group — that wire is implied by the group's interface).
static int project_pin(int realPin, bool isSource) {
    GraphNode* n = find_node_for_pin(realPin);
    if (!n) return -1;
    if (visible_at_path(n->label)) return realPin;
    NodeGroup* g = group_of(n->label);
    while (g && !visible_at_path(g->name)) g = group_of(g->name);
    if (!g) return -1;
    if (isSource) {
        auto it = s_groupProj.realOutToGroupPin.find(realPin);
        return it == s_groupProj.realOutToGroupPin.end() ? -1 : it->second;
    }
    auto it = s_groupProj.realInToSynth.find(realPin);
    return it == s_groupProj.realInToSynth.end() ? -1 : it->second;
}

// ===========================================================================
// Properties panel — full editing UI for selected node
// ===========================================================================

// Badge for a pin/config that a paramMap binding drives per note (spec §2,
// absorbs backlog 3m): "<frequency>" for bare targets, "<curve>" for
// curve/vcurve entries, nullptr when unmapped. The widget is suppressed
// while mapped — the mapping stomps the scalar at every note-on, so a
// live-looking widget is a lie; removing the mapping restores it.
static const char* mapping_badge(const std::string& nodeLabel, const char* name) {
    if (!s_loadedParamMap.is_object()) return nullptr;
    std::string target = nodeLabel + "." + name;
    const char* badge = nullptr;
    auto check = [&](const nlohmann::json& e) {
        if (e.is_string()) {
            if (e.get<std::string>() == target) badge = "<frequency>";
        } else if (e.is_object() && e.value("target", std::string()) == target) {
            badge = (e.contains("curve") || e.contains("vcurve")) ? "<curve>"
                                                                  : "<frequency>";
        }
    };
    for (auto& [pname, entry] : s_loadedParamMap.items()) {
        if (entry.is_array()) {
            for (const auto& e : entry) { check(e); if (badge) return badge; }
        } else {
            check(entry); if (badge) return badge;
        }
    }
    return badge;
}

static void draw_properties_panel() {
    ImGui::Begin("Properties", nullptr,
                 ImGuiWindowFlags_NoCollapse);

    GraphNode* node = find_selected_node();
    if (!node) {
        // A collapsed group can be the selection: name + membership info.
        NodeGroup* grp = nullptr;
        for (auto& g : s_groups)
            if (g.editorId == g_selectedNodeId) { grp = &g; break; }
        if (grp) {
            ImGui::TextColored(ImVec4(0.75f, 0.6f, 0.9f, 1), "%s", grp->name.c_str());
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "(Group)");
            static int  renameGroupId = -1;
            static char groupBuf[64];
            static std::string groupErr;
            if (renameGroupId != grp->editorId) {
                renameGroupId = grp->editorId;
                snprintf(groupBuf, sizeof(groupBuf), "%s", grp->name.c_str());
                groupErr.clear();
            }
            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##groupName", groupBuf, sizeof(groupBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (rename_group(*grp, groupBuf, groupErr)) groupErr.clear();
            }
            if (!groupErr.empty())
                ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "%s", groupErr.c_str());
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Members");
            for (auto& m : grp->members)
                ImGui::BulletText("%s%s", m.c_str(),
                                  group_by_name(m) ? "  (group)" : "");
            ImGui::End();
            return;
        }
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Select a node to edit");
        ImGui::End();
        return;
    }

    // Header: just the type — the rename field below carries the name.
    ImGui::TextColored(ImColor(node_title_color(node->typeName)).Value, "%s",
                       node_display_name(node->typeName).c_str());

    // Rename-in-place: the label IS the serialized id (stable identity).
    if (node->typeName != NT_PATCH_OUTPUT && node->typeName != NT_PARAMETER) {
        static int  renameNodeId = -1;
        static char renameBuf[64];
        static std::string renameErr;
        if (renameNodeId != node->id) {
            renameNodeId = node->id;
            snprintf(renameBuf, sizeof(renameBuf), "%s", node->label.c_str());
            renameErr.clear();
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("##nodeName", renameBuf, sizeof(renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (rename_node(*node, renameBuf, renameErr)) renameErr.clear();
        }
        if (!renameErr.empty())
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "%s", renameErr.c_str());
    }
    ImGui::Separator();

    // A Curve node's Properties pane IS its curve editor (Matt 2026-08-22;
    // the separate Curves window is retired). Input side first so the pane
    // reads source -> curve -> destination.
    if (node->typeName == "CurveNode") {
        const GraphNode* src = node->inputs.empty()
            ? nullptr : find_source_node(node->inputs[0].id);
        ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "in: %s",
                           src ? src->label.c_str() : "(not connected)");
        draw_curve_node(*node);
        ImGui::End();
        return;
    }

    // Layout: label on left (120px), widget on right
    float labelW = 120.0f;
    // Cap widget width so spinners/combos don't stretch absurdly when the
    // properties panel is docked as a full-width tab.
    float widgetW = std::min(120.0f, ImGui::GetContentRegionAvail().x - labelW);

    // Parameter pins (connectable + editable value)
    bool hasParams = false;
    for (auto& pin : node->inputs) {
        if (pin.inputOnly) continue;
        if (pin.name.substr(0, 3) == "ch ") continue;  // skip "ch 1", "ch 2" mixer channels
        // Hide "default" pin on Parameter nodes — it's internal
        if (node->typeName == NT_PARAMETER && pin.name == "default") continue;
        hasParams = true;

        bool connected = is_pin_connected(pin.id);
        ImGui::Text("%s", pin.name.c_str());
        if (!pin.hint.empty()) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1), "(%s)", pin.hint.c_str());
        }
        ImGui::SameLine(labelW);
        const char* badge = mapping_badge(node->label, pin.name.c_str());
        if (connected) {
            GraphNode* srcNode = find_source_node(pin.id);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "<-- %s", srcNode ? srcNode->label.c_str() : "?");
        } else if (badge) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "%s", badge);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Driven per note by the paramMap — edit in\nEdit > Parameter mapping (shape editor is there too).");
        } else {
            ImGui::PushItemWidth(widgetW);
            char label[64];
            snprintf(label, sizeof(label), "##prop%d", pin.id);
            // Step = 1% of current value (min 0.001), fast step = 10x
            float step = std::max(0.001f, std::abs(pin.defaultValue) * 0.01f);
            if (ImGui::InputFloat(label, &pin.defaultValue, step, step * 10.0f, "%.4f")) {
                if (pin.constantSrc) pin.constantSrc->set(pin.defaultValue);
                node->jsonExtras.erase(pin.name);  // edit wins over carried value
                update_node_dsp(*node);
                mark_graph_dirty();
            }
            ImGui::PopItemWidth();
        }
    }

    // Input-only pins (connection status)
    for (auto& pin : node->inputs) {
        if (!pin.inputOnly) continue;
        bool connected = is_pin_connected(pin.id);
        ImGui::Text("%s", pin.name.c_str());
        if (!pin.hint.empty()) {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1), "(%s)", pin.hint.c_str());
        }
        ImGui::SameLine(labelW);
        if (connected) {
            GraphNode* srcNode = find_source_node(pin.id);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "<-- %s", srcNode ? srcNode->label.c_str() : "?");
        } else if (pin.hasConstant) {
            // The loader accepts a scalar on any input pin (wrapped in a
            // ConstantSource); expose it for editing the same way param
            // pins are (afp31_gt rev_amp.source2 was invisible without
            // this — a stored constant the panel could not show).
            ImGui::PushItemWidth(widgetW);
            char label[64];
            snprintf(label, sizeof(label), "##propin%d", pin.id);
            float step = std::max(0.001f, std::abs(pin.defaultValue) * 0.01f);
            if (ImGui::InputFloat(label, &pin.defaultValue, step, step * 10.0f, "%.4f")) {
                if (pin.constantSrc) pin.constantSrc->set(pin.defaultValue);
                node->jsonExtras.erase(pin.name);
                update_node_dsp(*node);
                mark_graph_dirty();
            }
            ImGui::PopItemWidth();
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "(not connected)");
        }
    }

    // Config values. Int configs that are logically enums (e.g.
    // CombinedSource.operation) render as a dropdown using labels
    // declared on the SettingDescriptor.
    if (!node->settingValues.empty()) {
        // Envelope nodes lay their settings out differently (Matt
        // 2026-08-29): accuracy knobs flow inline under the params with no
        // "Settings" heading; preset envelopes put their time/level knobs
        // under a "Duration" heading and their per-stage Curve/Power pairs
        // in a "Shape" table. Every other node keeps the flat list.
        const bool isEnvNode =
            dynamic_cast<Envelope*>(node->dspSource.get()) != nullptr;
        const bool isPresetEnv = isEnvNode && node->typeName != NT_ENVELOPE;
        auto is_env_inline = [](std::string_view n) {
            return n == "stage_accuracy" || n == "ramp_accuracy" ||
                   n == "timeScale";
        };
        auto is_env_shape = [](std::string_view n) {
            return n.size() > 5 && (n.substr(n.size() - 5) == "Curve" ||
                                    n.substr(n.size() - 5) == "Power");
        };
        auto apply_setting_value = [&](const char* name, float v) {
            if (!node->dspSource) return;
            {
                std::lock_guard<std::mutex> lock(g_audioMutex);
                node->dspSource->set_setting(name, v);
            }
            node->jsonExtras.erase(name);
            for (auto& [d2, v2] : node->arrayValues)
                v2 = node->dspSource->get_array(d2.name);
            mark_graph_dirty();
        };

        auto render_setting_row = [&](const SettingDescriptor& desc, float& val) {
            ImGui::Text("%s", desc.name);
            ImGui::SameLine(labelW);

            // --- Promotion (pin_model_design.md §6) ---
            // Float settings are eligible; int/bool/enum are structural and
            // never get a pin. Promoted = a DYNAMIC PIN: evaluated once at
            // note-on and frozen for the note.
            const bool eligible = (desc.type == SettingType::Float) && !desc.enum_labels;
            const bool promoted = node->dynamicPins.contains(desc.name);
            ImGui::PushID(desc.name);

            // Promotion circle (Matt 2026-08-29, replaces the promote/demote
            // buttons): hollow grey = click to promote — ONLY a gold pin
            // appears on the node; wiring it happens in the editor like any
            // pin. Gold-filled = promoted; click again to demote, which drops
            // the pin (and wire) and restores the STOWED SCALAR — never the
            // last value the chain produced (Matt 2026-08-19): the scalar
            // lives in settingValues and is never overwritten.
            if (eligible || promoted) {
                const float r = ImGui::GetFontSize() * 0.30f;
                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImVec2 center(pos.x + r + 2.0f,
                              pos.y + ImGui::GetFrameHeight() * 0.5f);
                if (ImGui::InvisibleButton("##promoCircle",
                        ImVec2(2.0f * r + 6.0f, ImGui::GetFrameHeight()))) {
                    if (promoted) {
                        node->dynamicPins.erase(desc.name);
                        node->apply_config();   // stowed scalar back to the DSP
                    } else {
                        node->dynamicPins[desc.name] = nullptr;  // pin, unwired
                    }
                    mark_graph_dirty();
                }
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (promoted)
                    dl->AddCircleFilled(center, r, IM_COL32(217, 184, 77, 255));
                else
                    dl->AddCircle(center, r, IM_COL32(115, 115, 115, 255), 0, 1.5f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(promoted
                        ? "Promoted: gold pin on the node (wire a Curve to it there).\nClick to demote — the value below takes over."
                        : "Click to promote to a dynamic pin (set once per note).\nA gold pin appears on the node; wire it in the editor.");
                ImGui::SameLine();
            }

            // find(), NOT operator[] — json operator[] inserts a null entry
            // for a missing key, which resurrected the pin the same frame
            // the demote click erased it (stuck-gold-circle bug).
            auto dpIt = node->dynamicPins.find(desc.name);
            if (dpIt != node->dynamicPins.end() && dpIt->is_object()) {
                const std::string ref = dpIt->value("ref", std::string("?"));
                ImGui::TextColored(ImVec4(0.85f, 0.72f, 0.30f, 1), "<- %s", ref.c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Dynamic pin: driven once per note by '%s'.\n"
                                      "The value below is what it returns to if demoted.",
                                      ref.c_str());
                ImGui::PopID();
                return;
            }
            ImGui::PopID();

            if (const char* badge = mapping_badge(node->label, desc.name)) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "%s", badge);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Driven per note by a legacy paramMap entry\n"
                                      "(not convertible to a node). Edit in\nEdit > Parameter mapping.");
                return;
            }
            ImGui::PushItemWidth(widgetW);
            char cfgLabel[64];
            snprintf(cfgLabel, sizeof(cfgLabel), "##pcfg_%s_%d", desc.name, node->id);
            bool changed = false;
            if (desc.type == SettingType::Bool) {
                bool b = (val != 0.0f);
                if (ImGui::Checkbox(cfgLabel, &b)) { val = b ? 1.0f : 0.0f; changed = true; }
            } else if (desc.type == SettingType::Int && desc.enum_labels) {
                // Count labels (null-terminated).
                int count = 0;
                while (desc.enum_labels[count]) ++count;
                int iv = std::clamp(int(val), 0, count - 1);
                if (ImGui::Combo(cfgLabel, &iv, desc.enum_labels, count)) {
                    val = float(iv); changed = true;
                }
            } else if (desc.type == SettingType::Int) {
                int iv = int(val);
                if (ImGui::InputInt(cfgLabel, &iv, 1, 10)) {
                    iv = std::clamp(iv, int(desc.min_value), int(desc.max_value));
                    val = float(iv); changed = true;
                }
            } else {
                float step = std::max(0.001f, (desc.max_value - desc.min_value) * 0.01f);
                if (ImGui::InputFloat(cfgLabel, &val, step, step * 10.0f, "%.4f")) {
                    val = std::clamp(val, desc.min_value, desc.max_value);
                    changed = true;
                }
            }
            ImGui::PopItemWidth();
            if (changed && node->dspSource) {
                // Under the audio lock: set_setting can rebuild internal
                // arrays (ExplicitPartials) while a stream tap is mid-next()
                // on the same object (3k audit residual).
                {
                    std::lock_guard<std::mutex> lock(g_audioMutex);
                    node->dspSource->set_setting(desc.name, val);
                }
                node->jsonExtras.erase(desc.name);  // edit wins over carried value
                // set_setting may have mutated internal arrays (e.g. ExplicitPartials
                // mirrors _1 → _2 when evolve flips off). Re-pull cached values.
                for (auto& [d, v] : node->arrayValues)
                    v = node->dspSource->get_array(d.name);
                mark_graph_dirty();
            }
        };

        if (!isEnvNode) {
            if (hasParams) { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Settings");
            for (auto& [desc, val] : node->settingValues)
                render_setting_row(desc, val);
        } else if (!isPresetEnv) {
            // Generic Envelope: all settings inline, straight under maxValue.
            for (auto& [desc, val] : node->settingValues)
                render_setting_row(desc, val);
        } else {
            // Preset envelope: accuracy inline, then Duration, then Shape.
            for (auto& [desc, val] : node->settingValues)
                if (is_env_inline(desc.name))
                    render_setting_row(desc, val);

            bool anyDuration = false;
            for (auto& [desc, val] : node->settingValues)
                if (!is_env_inline(desc.name) && !is_env_shape(desc.name))
                    anyDuration = true;
            if (anyDuration) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Duration");
                for (auto& [desc, val] : node->settingValues)
                    if (!is_env_inline(desc.name) && !is_env_shape(desc.name))
                        render_setting_row(desc, val);
            }

            // Shape table: one row per stage that has a Curve setting, the
            // matching Power beside it.
            struct ShapeRow { std::string label; int curveIdx; int powerIdx; };
            std::vector<ShapeRow> shapeRows;
            for (int i = 0; i < (int)node->settingValues.size(); ++i) {
                std::string_view n = node->settingValues[i].first.name;
                if (n.size() <= 5 || n.substr(n.size() - 5) != "Curve") continue;
                std::string prefix(n.substr(0, n.size() - 5));
                int pIdx = -1;
                for (int j = 0; j < (int)node->settingValues.size(); ++j)
                    if (prefix + "Power" == node->settingValues[j].first.name) {
                        pIdx = j; break;
                    }
                std::string label = prefix;
                if (!label.empty())
                    label[0] = char(::toupper((unsigned char)label[0]));
                shapeRows.push_back({std::move(label), i, pIdx});
            }
            if (!shapeRows.empty()) {
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Shape");
                if (ImGui::BeginTable("envshape", 3,
                        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
                    ImGui::TableSetupColumn("");
                    ImGui::TableSetupColumn("Curve");
                    ImGui::TableSetupColumn("Power");
                    ImGui::TableHeadersRow();
                    for (auto& r : shapeRows) {
                        ImGui::TableNextRow();
                        ImGui::PushID(r.label.c_str());

                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("%s", r.label.c_str());

                        ImGui::TableNextColumn();
                        {
                            auto& [cd, cv] = node->settingValues[r.curveIdx];
                            int count = 0;
                            while (cd.enum_labels && cd.enum_labels[count]) ++count;
                            int iv = std::clamp(int(cv), 0, std::max(0, count - 1));
                            ImGui::PushItemWidth(110);
                            if (count > 0 &&
                                ImGui::Combo("##shpcurve", &iv, cd.enum_labels, count)) {
                                cv = float(iv);
                                apply_setting_value(cd.name, cv);
                            }
                            ImGui::PopItemWidth();
                        }

                        ImGui::TableNextColumn();
                        if (r.powerIdx >= 0) {
                            auto& [pd, pv] = node->settingValues[r.powerIdx];
                            ImGui::PushItemWidth(60);
                            if (ImGui::DragFloat("##shppow", &pv, 0.05f,
                                                 pd.min_value, pd.max_value, "%.2f"))
                                apply_setting_value(pd.name, pv);
                            ImGui::PopItemWidth();
                        }

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
        }
    }

    // SegmentSource: shape preview ABOVE the points table (Matt
    // 2026-08-29). Rendered from the cached values array + the smoothness
    // pin's constant, through the engine's own SmoothnessInterpolator, so
    // the drawing matches what renders (gap and varPct excluded).
    if (node->typeName == "SegmentSource") {
        const std::vector<float>* vals = nullptr;
        for (auto& [d, v] : node->arrayValues)
            if (std::string_view(d.name) == "values") { vals = &v; break; }
        if (vals && vals->size() >= 4) {
            const int pairs = int(vals->size()) / 2;
            float totalW = 0.0f;
            for (int i = 0; i < pairs; ++i)
                totalW += std::max(0.0f, (*vals)[i * 2]);
            if (totalW > 0.0f) {
                float smooth = 0.5f;
                for (auto& pin : node->inputs)
                    if (pin.name == "smoothness") {
                        // defaultValue, not constantSrc->current(): set()
                        // stages for next() and the panel never advances it.
                        smooth = pin.defaultValue;
                        break;
                    }
                SmoothnessInterpolator si(smooth, false);
                constexpr int N = 256;
                float pv[N];
                for (int k = 0; k < N; ++k) {
                    const float t = (float(k) + 0.5f) / float(N) * totalW;
                    float acc = 0.0f, prev = 0.0f, out = 0.0f;
                    for (int i = 0; i < pairs; ++i) {
                        const float w = std::max(0.0f, (*vals)[i * 2]);
                        const float v = (*vals)[i * 2 + 1];
                        if (t <= acc + w || i == pairs - 1) {
                            const float pos = w > 0.0f
                                ? std::clamp((t - acc) / w, 0.0f, 1.0f) : 1.0f;
                            out = si.interpolate(prev, v, pos);
                            break;
                        }
                        acc += w;
                        prev = v;
                    }
                    pv[k] = out;
                }
                float vmin = pv[0], vmax = pv[0];
                for (int k = 1; k < N; ++k) {
                    vmin = std::min(vmin, pv[k]);
                    vmax = std::max(vmax, pv[k]);
                }
                if (vmax - vmin < 0.001f) vmax = vmin + 1.0f;
                const float vpad = (vmax - vmin) * 0.05f;
                ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
                float plotW = ImGui::GetContentRegionAvail().x;
                if (plotW < 80.0f) plotW = 200.0f;
                ImGui::PlotLines("##segprev", pv, N, 0, nullptr,
                                 vmin - vpad, vmax + vpad, ImVec2(plotW, 90.0f));
                if (ImGui::IsItemHovered()) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        shape_editor_open_segment(*node);
                    else
                        ImGui::SetTooltip("Double-click to open the shape editor");
                }
            }
        }
    }

    // Shaper: transfer-curve preview (y = curve(x) across the input range),
    // rendered through the engine's own map() so drawing matches render.
    if (node->typeName == "Shaper") {
        const std::vector<float>* vals = nullptr;
        for (auto& [d, v] : node->arrayValues)
            if (std::string_view(d.name) == "values") { vals = &v; break; }
        if (vals && vals->size() >= 4) {
            ShaperSource probe;
            probe.set_array("values", *vals);
            float smooth = 0.5f;
            for (auto& pin : node->inputs)
                if (pin.name == "smoothness") {  // defaultValue: stale-read rule
                    smooth = pin.defaultValue;
                    break;
                }
            probe.set_param("smoothness",
                            std::make_shared<ConstantSource>(smooth));
            probe.next();
            constexpr int N = 256;
            float pv[N];
            for (int k = 0; k < N; ++k)
                pv[k] = probe.map(-1.5f + 3.0f * float(k) / float(N - 1));
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            float plotW = ImGui::GetContentRegionAvail().x;
            if (plotW < 80.0f) plotW = 200.0f;
            ImGui::PlotLines("##shaperprev", pv, N, 0, nullptr,
                             -1.6f, 1.6f, ImVec2(plotW, 90.0f));
            if (ImGui::IsItemHovered()) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    shape_editor_open_shaper(*node);
                else
                    ImGui::SetTooltip("Double-click to open the shape editor");
            }
        }
    }

    // Array values — grouped into parallel-columns tables by groupName.
    if (!node->arrayValues.empty()) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        size_t i = 0;
        while (i < node->arrayValues.size()) {
            const auto& firstDesc = node->arrayValues[i].first;
            const bool grouped = (firstDesc.groupName != nullptr);

            // Find end of this group (consecutive entries with same groupName).
            size_t groupEnd = i + 1;
            if (grouped) {
                while (groupEnd < node->arrayValues.size() &&
                       node->arrayValues[groupEnd].first.groupName != nullptr &&
                       std::string_view(node->arrayValues[groupEnd].first.groupName) ==
                           firstDesc.groupName)
                    ++groupEnd;
            }

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s",
                               grouped ? firstDesc.groupName : firstDesc.name);

            // ExplicitPartials: _2 columns mirror _1 and are disabled when the
            // Settings-panel "evolve" checkbox is off. Just read the state here.
            bool evolveOff = false;
            if (grouped && node->typeName == "ExplicitPartials" &&
                firstDesc.groupName && std::string_view(firstDesc.groupName) == "partials")
            {
                for (auto& [cdesc, cval] : node->settingValues) {
                    if (std::string_view(cdesc.name) == "evolve") {
                        evolveOff = (cval == 0.0f);
                        break;
                    }
                }
            }

            if (!grouped && node->typeName == "SegmentSource" &&
                std::string_view(firstDesc.name) == "values") {
                // Interleaved [width, val] pairs — edited as POINT rows
                // (Matt 2026-08-29): the generic per-float delete shifted
                // the tail by one, turning widths into values (the
                // alternating-preview bug). Width column is unclamped by
                // the descriptor range (widths are samples/seconds, not
                // -10..10 values).
                auto& vec = node->arrayValues[i].second;
                const ArrayDescriptor d = firstDesc;
                bool changed = false;
                if (vec.size() % 2) {  // heal an odd (corrupted) tail
                    vec.push_back(0.0f);
                    changed = true;
                }
                int removePair = -1;
                ImGui::PushID((int)i);
                if (ImGui::BeginTable("segvals", 4,
                        ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("#");
                    ImGui::TableSetupColumn("Width");
                    ImGui::TableSetupColumn("Value");
                    ImGui::TableSetupColumn("");
                    ImGui::TableHeadersRow();
                    const int pairs = int(vec.size()) / 2;
                    for (int r = 0; r < pairs; ++r) {
                        ImGui::TableNextRow();
                        ImGui::PushID(r);
                        ImGui::TableNextColumn();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("%d", r);
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputFloat("##w", &vec[r * 2], 0.0f, 0.0f, "%.6g")) {
                            vec[r * 2] = std::max(0.0f, vec[r * 2]);
                            changed = true;
                        }
                        ImGui::TableNextColumn();
                        ImGui::SetNextItemWidth(90.0f);
                        if (ImGui::InputFloat("##v", &vec[r * 2 + 1], 0.0f, 0.0f, "%.4f")) {
                            vec[r * 2 + 1] = std::clamp(vec[r * 2 + 1],
                                                        d.min_value, d.max_value);
                            changed = true;
                        }
                        ImGui::TableNextColumn();
                        if (pairs > 1 && ImGui::SmallButton(" x ")) removePair = r;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (removePair >= 0) {
                    vec.erase(vec.begin() + removePair * 2,
                              vec.begin() + removePair * 2 + 2);
                    changed = true;
                }
                if (ImGui::SmallButton(" + point ")) {
                    const float lastW = vec.size() >= 2 ? vec[vec.size() - 2] : 10.0f;
                    vec.push_back(lastW);
                    vec.push_back(0.0f);
                    changed = true;
                }
                ImGui::PopID();
                if (changed) { node->push_array(d.name); mark_graph_dirty(); }
            } else if (!grouped) {
                // Standalone array — vertical list with per-row delete + append.
                auto& vec = node->arrayValues[i].second;
                const ArrayDescriptor d = firstDesc;
                int removeIdx = -1;
                bool changed = false;
                ImGui::PushID((int)i);
                for (size_t r = 0; r < vec.size(); ++r) {
                    ImGui::PushID((int)r);
                    ImGui::Text("[%zu]", r);
                    ImGui::SameLine(labelW);
                    ImGui::PushItemWidth(widgetW);
                    if (ImGui::InputFloat("##v", &vec[r], 0.0f, 0.0f, "%.4f")) {
                        vec[r] = std::clamp(vec[r], d.min_value, d.max_value);
                        changed = true;
                    }
                    ImGui::PopItemWidth();
                    ImGui::SameLine();
                    if (ImGui::SmallButton(" x ")) removeIdx = (int)r;
                    ImGui::PopID();
                }
                if (removeIdx >= 0) {
                    vec.erase(vec.begin() + removeIdx);
                    changed = true;
                }
                if (ImGui::SmallButton(" + Add row ")) {
                    vec.push_back(d.default_value);
                    changed = true;
                }
                ImGui::PopID();
                if (changed) { node->push_array(d.name); mark_graph_dirty(); }
            } else {
                // Grouped: render all columns in a table with a single row
                // count shared across columns (invariant-preserving).
                const size_t cols = groupEnd - i;
                size_t rows = 0;
                for (size_t c = i; c < groupEnd; ++c)
                    rows = std::max(rows, node->arrayValues[c].second.size());
                // Ensure all columns share the same length.
                for (size_t c = i; c < groupEnd; ++c) {
                    auto& v = node->arrayValues[c].second;
                    if (v.size() < rows)
                        v.resize(rows, node->arrayValues[c].first.default_value);
                }

                // Per-column width: divide available space across columns,
                // leaving room for the row-index prefix and delete button.
                const float rowPrefix = 28.0f;       // "NN " label
                const float delBtnW   = 22.0f;       // "x" SmallButton
                const float gutter    = 4.0f;
                const float avail = ImGui::GetContentRegionAvail().x - rowPrefix - delBtnW
                                    - gutter * float(cols);
                const float colW = std::max(40.0f, avail / float(cols));

                ImGui::PushID((int)i);
                // Column header
                ImGui::Text("  #");
                for (size_t c = i; c < groupEnd; ++c) {
                    ImGui::SameLine(rowPrefix + (float(c - i)) * (colW + gutter));
                    ImGui::Text("%s", node->arrayValues[c].first.name);
                }

                int removeIdx = -1;
                bool changedAny = false;
                for (size_t r = 0; r < rows; ++r) {
                    ImGui::PushID((int)r);
                    ImGui::Text("%2zu", r);
                    for (size_t c = i; c < groupEnd; ++c) {
                        ImGui::SameLine(rowPrefix + (float(c - i)) * (colW + gutter));
                        ImGui::PushItemWidth(colW);
                        char id[16]; snprintf(id, sizeof(id), "##c%zu", c);
                        const auto& d = node->arrayValues[c].first;
                        auto& v = node->arrayValues[c].second[r];
                        // Disable mult2/ampl2 when ExplicitPartials has evolve=false.
                        const bool disableThisCol = evolveOff &&
                            (std::string_view(d.name) == "mult2" ||
                             std::string_view(d.name) == "ampl2");
                        if (disableThisCol) ImGui::BeginDisabled();
                        if (ImGui::InputFloat(id, &v, 0.0f, 0.0f, "%.4f")) {
                            v = std::clamp(v, d.min_value, d.max_value);
                            changedAny = true;
                        }
                        if (disableThisCol) ImGui::EndDisabled();
                        ImGui::PopItemWidth();
                    }
                    ImGui::SameLine(rowPrefix + float(cols) * (colW + gutter));
                    if (ImGui::SmallButton(" x ")) removeIdx = (int)r;
                    ImGui::PopID();
                }

                if (removeIdx >= 0) {
                    for (size_t c = i; c < groupEnd; ++c)
                        node->arrayValues[c].second.erase(
                            node->arrayValues[c].second.begin() + removeIdx);
                    changedAny = true;
                }
                if (ImGui::SmallButton(" + Add row ")) {
                    for (size_t c = i; c < groupEnd; ++c)
                        node->arrayValues[c].second.push_back(
                            node->arrayValues[c].first.default_value);
                    changedAny = true;
                }
                ImGui::PopID();

                if (changedAny) {
                    // Push every column in the group (lengths must stay synced).
                    for (size_t c = i; c < groupEnd; ++c)
                        node->push_array(node->arrayValues[c].first.name);
                    mark_graph_dirty();
                }
            }

            i = groupEnd;
        }
    }

    // Inline formant table (FormantSpectrum)
    if (node->typeName == "FormantSpectrum" && !node->formantRows.empty()) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Formants");

        bool changed = false;
        int  removeIdx = -1;

        ImGui::BeginGroup();
        if (ImGui::BeginTable("formants", 5,
                ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Frequency");
            ImGui::TableSetupColumn("Gain");
            ImGui::TableSetupColumn("Width");
            ImGui::TableSetupColumn("Power");
            ImGui::TableSetupColumn("");

            // Centered header row
            {
                static const char* hdrs[] = { "Frequency", "Gain", "Width", "Power", "" };
                ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                for (int c = 0; c < 5; ++c) {
                    ImGui::TableSetColumnIndex(c);
                    float colW = ImGui::GetContentRegionAvail().x;
                    float txtW = ImGui::CalcTextSize(hdrs[c]).x;
                    float pad  = (colW - txtW) * 0.5f;
                    if (pad > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                    ImGui::TableHeader(hdrs[c]);
                }
            }

            for (int i = 0; i < (int)node->formantRows.size(); ++i) {
                auto& row = node->formantRows[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableNextColumn(); ImGui::PushItemWidth(70);
                changed |= ImGui::DragFloat("##freq", &row.frequency, 10.0f, 1.0f, 20000.0f, "%.0f");
                ImGui::PopItemWidth();

                ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                changed |= ImGui::DragFloat("##gain", &row.gain, 0.01f, 0.0f, 10.0f, "%.2f");
                ImGui::PopItemWidth();

                ImGui::TableNextColumn(); ImGui::PushItemWidth(70);
                changed |= ImGui::DragFloat("##wid", &row.width, 10.0f, 1.0f, 10000.0f, "%.0f");
                ImGui::PopItemWidth();

                ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                changed |= ImGui::DragFloat("##pow", &row.power, 0.05f, 0.01f, 10.0f, "%.2f");
                ImGui::PopItemWidth();

                ImGui::TableNextColumn();
                if (ImGui::SmallButton(" x ")) removeIdx = i;

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        if (removeIdx >= 0 && node->formantRows.size() > 1) {
            node->formantRows.erase(node->formantRows.begin() + removeIdx);
            changed = true;
        }
        if (ImGui::SmallButton(" + Add Formant ")) {
            node->formantRows.push_back({1000.0f, 1.0f, 500.0f, 2.0f});
            changed = true;
        }
        ImGui::EndGroup();
        ImVec2 leftSize = ImGui::GetItemRectSize();

        if (changed) { node->rebuild_formant_spectrum(); mark_graph_dirty(); }

        // Gain-vs-frequency preview to the right (reuses the existing strip draw)
        ImGui::SameLine();
        ImVec2 stripPos = ImGui::GetCursorScreenPos();
        float plotW = ImGui::GetContentRegionAvail().x;
        if (plotW < 80.0f) plotW = 200.0f;
        draw_formant_strip(node, IM_COL32(120, 200, 220, 255),
                           stripPos.x, stripPos.y, plotW, leftSize.y,
                           false, 0);
        ImGui::Dummy(ImVec2(plotW, leftSize.y));
    }

    // Inline stage table (bare Envelope) + envelope display below the
    // settings for ALL envelope types, presets included (Matt 2026-08-29).
    if (auto* env = dynamic_cast<Envelope*>(node->dspSource.get())) {
        bool envChanged = false;
        if (node->typeName == NT_ENVELOPE) {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Stages");
            bool absTime = env->absolute_time;
            if (ImGui::Checkbox("seconds (timeMode)", &absTime)) {
                env->absolute_time = absTime;
                mark_graph_dirty();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("off: stage Pct is a fraction of note duration\n"
                                  "on:  stage Pct is literal seconds (timeMode=seconds)");

            const char* typeNames[] = { "Linear", "Expo", "InverseExpo", "Sine",
                                        "Hold" };
            bool changed = false;
            int  removeIdx = -1;

            ImGui::BeginGroup();
            if (ImGui::BeginTable("stages", 8,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Pct");
                ImGui::TableSetupColumn("Start");
                ImGui::TableSetupColumn("End");
                ImGui::TableSetupColumn("Curve");
                ImGui::TableSetupColumn("Power");
                ImGui::TableSetupColumn("Min Secs");
                ImGui::TableSetupColumn("Max Secs");
                ImGui::TableSetupColumn("");

                // Centered header row (replaces TableHeadersRow's left-aligned labels)
                {
                    static const char* hdrs[] = {
                        "Pct", "Start", "End", "Curve", "Power", "Min Secs", "Max Secs", ""
                    };
                    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                    for (int c = 0; c < 8; ++c) {
                        ImGui::TableSetColumnIndex(c);
                        float colW = ImGui::GetContentRegionAvail().x;
                        float txtW = ImGui::CalcTextSize(hdrs[c]).x;
                        float pad  = (colW - txtW) * 0.5f;
                        if (pad > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                        ImGui::TableHeader(hdrs[c]);
                    }
                }

                for (int i = 0; i < env->stage_count(); ++i) {
                    auto& s = env->stage(i);
                    ImGui::TableNextRow();
                    ImGui::PushID(i);

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##pct", &s.percent,        0.005f, 0.0f, 1.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##start", &s.ramp.startVal, 0.01f, -10.0f, 10.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##end",   &s.ramp.endVal,   0.01f, -10.0f, 10.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(90);
                    int curType = int(s.ramp.type);
                    if (ImGui::Combo("##type", &curType, typeNames, IM_ARRAYSIZE(typeNames))) {
                        s.ramp.type = RampType(curType); changed = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##pow", &s.ramp.power,   0.05f, 0.0f, 10.0f, "%.2f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##min", &s.minSec, 0.005f, 0.0f, 99.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##max", &s.maxSec, 0.05f,  0.0f, 99.0f, "%.2f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton(" x ")) removeIdx = i;

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (removeIdx >= 0) { env->remove_stage(removeIdx); changed = true; }
            if (ImGui::SmallButton(" + Add Stage ")) { env->add_stage_default(); changed = true; }
            ImGui::EndGroup();
            envChanged = changed;
        }

        // --- Envelope display — full width, below the settings/table ---
        {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

            const int N = 256;
            float plotVals[N];
            int nStages = env->stage_count();
            if (nStages > 0) {
                // Engine semantics: only the LAST stage with percent==0 expands;
                // earlier percent==0 stages get zero width.
                std::vector<float> widths(nStages, 0.0f);
                int expandIdx = -1;
                float sumPct = 0.0f;
                for (int i = 0; i < nStages; ++i) {
                    float pct = env->stage(i).percent;
                    if (pct == 0.0f) {
                        expandIdx = i;
                    } else {
                        widths[i] = pct;
                        sumPct += pct;
                    }
                }
                if (expandIdx >= 0)
                    widths[expandIdx] = std::max(0.0f, 1.0f - sumPct);

                for (int k = 0; k < N; ++k) {
                    float t = float(k) / float(N - 1);
                    float acc = 0.0f;
                    int  si  = -1;
                    float pos = 0.0f;
                    for (int i = 0; i < nStages; ++i) {
                        if (widths[i] <= 0.0f) continue;
                        if (t <= acc + widths[i]) {
                            si  = i;
                            pos = (t - acc) / widths[i];
                            break;
                        }
                        acc += widths[i];
                    }
                    if (si < 0) {
                        // Past last stage with width — engine outputs 0
                        plotVals[k] = 0.0f;
                    } else {
                        plotVals[k] = env->stage(si).ramp.value(std::clamp(pos, 0.0f, 1.0f));
                    }
                }
            } else {
                for (int k = 0; k < N; ++k) plotVals[k] = 0.0f;
            }

            float vmin = plotVals[0], vmax = plotVals[0];
            for (int k = 1; k < N; ++k) {
                vmin = std::min(vmin, plotVals[k]);
                vmax = std::max(vmax, plotVals[k]);
            }
            if (vmax - vmin < 0.001f) vmax = vmin + 1.0f;
            float vpad = (vmax - vmin) * 0.05f;

            float plotW = ImGui::GetContentRegionAvail().x;
            if (plotW < 80.0f) plotW = 200.0f;
            ImGui::PlotLines("##envprev", plotVals, N, 0, nullptr,
                             vmin - vpad, vmax + vpad, ImVec2(plotW, 110.0f));
        }
        if (envChanged) mark_graph_dirty();
    }

    // PatchOutput: polyphony
    if (node->typeName == NT_PATCH_OUTPUT) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::PushItemWidth(-1);
        char polyLabel[32];
        snprintf(polyLabel, sizeof(polyLabel), "polyphony##ppoly%d", node->id);
        ImGui::DragInt(polyLabel, &node->polyphony, 0.1f, 1, 32);
        ImGui::PopItemWidth();
    }

    // Parameter: editable name
    if (node->typeName == NT_PARAMETER) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::PushItemWidth(-1);
        char nameLabel[32];
        snprintf(nameLabel, sizeof(nameLabel), "name##ppname%d", node->id);
        if (ImGui::InputText(nameLabel, node->paramNameBuf, sizeof(node->paramNameBuf))) {
            node->paramName = node->paramNameBuf;
        }
        ImGui::PopItemWidth();
    }

    ImGui::End();
}

// ===========================================================================
// Node clipboard — Edit > Cut/Copy/Paste, Ctrl+X/C/V.
//
// Copy serializes the imnodes selection into an in-memory JSON clipboard:
// per-node state (pin defaults, configs, arrays, formant rows, Envelope
// stages via the shared envelope_stages_* helpers, seed, polyphony,
// paramName) plus the links whose BOTH endpoints are inside the selection.
// Paste instantiates fresh nodes (fresh ids) from that state, rewires the
// intra-selection links by pin name, and offsets positions a little further
// on each successive paste. Cut = Copy + delete. The clipboard is process-
// local only — no OS clipboard involvement.
// ===========================================================================

static nlohmann::json s_clipboard;   // {"nodes": [...], "links": [...]}
static int s_clipPasteCount = 0;     // consecutive pastes fan out diagonally

static GraphNode* find_node_by_id(int nodeId) {
    for (auto& n : s_nodes) if (n.id == nodeId) return &n;
    return nullptr;
}

// Per-node UI state → clipboard JSON. Covers the same fields the save paths
// emit (and load restores); refs/links are handled separately by the caller.
static nlohmann::json clip_node_state(const GraphNode& node) {
    using json = nlohmann::json;
    json j;
    j["type"] = node.typeName;
    if (node.typeName == NT_PARAMETER)    j["paramName"] = node.paramName;
    if (node.typeName == NT_PATCH_OUTPUT) j["polyphony"] = node.polyphony;
    if (node.jsonSeed >= 0)               j["seed"]      = node.jsonSeed;

    // Editable pin defaults. Mixer "ch N" pins carry no value — only their
    // count matters (paste re-adds them via add_channel_input).
    json pins = json::object();
    int chPins = 0;
    for (const auto& p : node.inputs) {
        if (p.name.substr(0, 3) == "ch ") { chPins++; continue; }
        if (!p.inputOnly) pins[p.name] = p.defaultValue;
    }
    if (!pins.empty()) j["pins"]   = pins;
    if (chPins > 0)    j["chPins"] = chPins;

    if (!node.settingValues.empty()) {
        json cfg = json::object();
        for (const auto& [desc, val] : node.settingValues) cfg[desc.name] = val;
        j["settings"] = cfg;
    }
    for (const auto& [desc, vec] : node.arrayValues)
        if (!vec.empty()) j["arrays"][desc.name] = vec;

    if (!node.formantRows.empty()) {
        json rows = json::array();
        for (const auto& r : node.formantRows)
            rows.push_back({r.frequency, r.gain, r.width, r.power});
        j["formants"] = rows;
    }

    if (node.typeName == NT_ENVELOPE) {
        if (const auto* env = dynamic_cast<const Envelope*>(node.dspSource.get())) {
            j["stages"] = envelope_stages_to_json(*env);
            j["timeMode"] = env->absolute_time ? "seconds" : "fraction";
        }
    }
    return j;
}

// Instantiate a fresh node from clipboard state. Returns its id (state is
// applied in the same order the load path uses: pins → configs+apply →
// arrays → config re-sync from the DSP object).
static int clip_instantiate(const nlohmann::json& j) {
    std::string type = j["type"].get<std::string>();
    if (type == NT_PARAMETER)
        s_nodes.emplace_back(type, j.value("paramName", std::string("param")));
    else
        s_nodes.emplace_back(type);
    GraphNode& gn = s_nodes.back();

    if (j.contains("polyphony")) gn.polyphony = j["polyphony"].get<int>();
    if (j.contains("seed"))      gn.jsonSeed  = j["seed"].get<long long>();

    for (int c = 1; c < j.value("chPins", 1); ++c)
        gn.add_channel_input();

    if (j.contains("pins")) {
        for (auto& [name, val] : j["pins"].items()) {
            if (Pin* p = gn.find_input(name)) {
                p->defaultValue = val.get<float>();
                if (p->constantSrc) p->constantSrc->set(p->defaultValue);
            }
        }
    }

    if (j.contains("settings")) {
        for (auto& [desc, val] : gn.settingValues) {
            if (j["settings"].contains(desc.name))
                val = j["settings"][desc.name].get<float>();
        }
        gn.apply_config();
    }

    if (j.contains("arrays") && gn.dspSource) {
        for (auto& [desc, vec] : gn.arrayValues) {
            if (!j["arrays"].contains(desc.name)) continue;
            vec = j["arrays"][desc.name].get<std::vector<float>>();
            gn.dspSource->set_array(desc.name, vec);
        }
        // set_array can update derived configs (maxPartials from length) —
        // sync the UI table back from the DSP object, mirroring load.
        for (auto& [desc, val] : gn.settingValues)
            val = gn.dspSource->get_setting(desc.name);
    }

    if (j.contains("formants")) {
        gn.formantRows.clear();
        for (const auto& r : j["formants"]) {
            FormantRow row;
            row.frequency = r[0].get<float>();
            row.gain      = r[1].get<float>();
            row.width     = r[2].get<float>();
            row.power     = r[3].get<float>();
            gn.formantRows.push_back(row);
        }
        gn.rebuild_formant_spectrum();
    }

    if (j.contains("stages")) {
        if (auto* env = dynamic_cast<Envelope*>(gn.dspSource.get())) {
            envelope_stages_from_json(*env, j["stages"]);
            env->absolute_time = j.value("timeMode", std::string("fraction")) == "seconds";
        }
    }

    return gn.id;
}

static bool clipboard_has_content() {
    return s_clipboard.is_object() && s_clipboard.contains("nodes") &&
           !s_clipboard["nodes"].empty();
}

static void clipboard_copy() {
    using json = nlohmann::json;
    int n = ImNodes::NumSelectedNodes();
    if (n <= 0) return;
    std::vector<int> sel((size_t)n);
    ImNodes::GetSelectedNodes(sel.data());

    json nodes = json::array();
    std::unordered_map<int, int> idToIdx;   // node id → clipboard index
    for (int id : sel) {
        GraphNode* node = find_node_by_id(id);
        if (!node) continue;
        json j = clip_node_state(*node);
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(id);
        j["pos"] = {pos.x, pos.y};
        idToIdx[id] = (int)nodes.size();
        nodes.push_back(std::move(j));
    }
    if (nodes.empty()) return;

    // Links fully inside the selection, recorded by clipboard index + pin name.
    json links = json::array();
    for (const auto& link : s_links) {
        Pin* a = find_pin(link.startPinId);
        Pin* b = find_pin(link.endPinId);
        if (!a || !b || a->kind == b->kind) continue;
        int outPinId = (a->kind == PinKind::Output) ? link.startPinId : link.endPinId;
        int inPinId  = (a->kind == PinKind::Input)  ? link.startPinId : link.endPinId;
        GraphNode* outNode = find_node_for_pin(outPinId);
        GraphNode* inNode  = find_node_for_pin(inPinId);
        if (!outNode || !inNode) continue;
        auto oIt = idToIdx.find(outNode->id);
        auto iIt = idToIdx.find(inNode->id);
        if (oIt == idToIdx.end() || iIt == idToIdx.end()) continue;
        Pin* outPin = find_pin(outPinId);
        Pin* inPin  = find_pin(inPinId);
        links.push_back({
            {"from",    oIt->second},
            {"fromPin", outPin->name},
            {"to",      iIt->second},
            {"toPin",   inPin->name},
        });
    }

    s_clipboard = json{{"nodes", std::move(nodes)}, {"links", std::move(links)}};
    s_clipPasteCount = 0;

    char buf[64];
    snprintf(buf, sizeof(buf), "Copied %d node(s)", (int)s_clipboard["nodes"].size());
    transport_set_status(buf, false);
}

static void clipboard_paste() {
    if (!clipboard_has_content()) return;
    s_clipPasteCount++;
    float off = 40.0f * (float)s_clipPasteCount;

    bool hasOutput = false, hasMixer = false;
    for (auto& n : s_nodes) {
        if (n.typeName == NT_PATCH_OUTPUT) hasOutput = true;
        if (n.typeName == NT_STEREO_MIXER) hasMixer  = true;
    }

    const auto& jnodes = s_clipboard["nodes"];
    std::vector<int> newIds(jnodes.size(), -1);   // -1 = skipped
    int pasted = 0, skipped = 0;
    for (size_t i = 0; i < jnodes.size(); ++i) {
        const auto& jn = jnodes[i];
        std::string type = jn["type"].get<std::string>();
        // Structural singletons / mode fits: at most one Output (patch mode
        // only), Channel/Mixer are node-graph-only, one Mixer per graph.
        bool skip =
            (type == NT_PATCH_OUTPUT &&
             (s_graphMode != GraphMode::PatchGraph || hasOutput)) ||
            ((type == NT_SOUND_CHANNEL || type == NT_STEREO_MIXER) &&
             s_graphMode == GraphMode::PatchGraph) ||
            (type == NT_STEREO_MIXER && hasMixer) ||
            (!is_special_ui_type(type) && type != NT_ENVELOPE &&
             !SourceRegistry::instance().has(type));
        if (skip) { skipped++; continue; }

        int id = clip_instantiate(jn);
        if (type == NT_PATCH_OUTPUT) hasOutput = true;
        if (type == NT_STEREO_MIXER) hasMixer  = true;

        ImVec2 pos(200.0f, 200.0f);
        if (jn.contains("pos"))
            pos = ImVec2(jn["pos"][0].get<float>(), jn["pos"][1].get<float>());
        if (!s_headless)
            ImNodes::SetNodeGridSpacePos(id, ImVec2(pos.x + off, pos.y + off));

        newIds[i] = id;
        pasted++;
    }

    // Rewire intra-selection links by pin name.
    for (const auto& jl : s_clipboard["links"]) {
        int fi = jl["from"].get<int>();
        int ti = jl["to"].get<int>();
        if (fi < 0 || fi >= (int)newIds.size() || newIds[(size_t)fi] < 0) continue;
        if (ti < 0 || ti >= (int)newIds.size() || newIds[(size_t)ti] < 0) continue;
        GraphNode* fromNode = find_node_by_id(newIds[(size_t)fi]);
        GraphNode* toNode   = find_node_by_id(newIds[(size_t)ti]);
        if (!fromNode || !toNode) continue;
        std::string fromPin = jl["fromPin"].get<std::string>();
        std::string toPin   = jl["toPin"].get<std::string>();
        int outPinId = -1, inPinId = -1;
        for (auto& p : fromNode->outputs) if (p.name == fromPin) outPinId = p.id;
        for (auto& p : toNode->inputs)    if (p.name == toPin)   inPinId  = p.id;
        if (outPinId >= 0 && inPinId >= 0)
            s_links.emplace_back(outPinId, inPinId);
    }

    update_all_dsp();
    mark_graph_dirty();

    char buf[96];
    if (skipped > 0)
        snprintf(buf, sizeof(buf), "Pasted %d node(s), %d skipped (mode/singleton)", pasted, skipped);
    else
        snprintf(buf, sizeof(buf), "Pasted %d node(s)", pasted);
    transport_set_status(buf, skipped > 0);
}

static void clipboard_cut() {
    int n = ImNodes::NumSelectedNodes();
    if (n <= 0) return;
    clipboard_copy();
    std::vector<int> sel((size_t)n);
    ImNodes::GetSelectedNodes(sel.data());
    for (int id : sel) delete_node(id);
    ImNodes::ClearNodeSelection();
    ImNodes::ClearLinkSelection();
    update_all_dsp();
}

// ===========================================================================
// Graph conversion — Edit > Convert to <type> graph.
//
// Patch → Node: strip the instrument-level constructs (Output + Parameter
// nodes, paramMap/score/seconds/polyphony — parked in the conv stash, see
// its declaration) and terminate the graph with a Channel → Mixer pair fed
// by whatever fed the Output node.
//
// Node → Patch: drop Channel/Mixer, terminate with an Output node instead.
// If a conv stash exists (same-session round trip), the instrument data is
// restored verbatim — including paramMap curve entries, which flow back
// into s_loadedParamMap so save_patch_graph's carry-forward keeps working.
// Otherwise an instrument block is synthesized with the same heuristic as
// tools/add_instrument_block.py: first node owning an unconnected numeric
// `frequency` param drives the note pitch, polyphony 1; save_patch_graph
// supplies the default score when none is stashed.
// ===========================================================================

static void convert_patch_to_node_graph() {
    // Stash the instrument-level data for a verbatim same-session round trip.
    GraphNode* outNode = nullptr;
    for (auto& n : s_nodes) if (n.typeName == NT_PATCH_OUTPUT) { outNode = &n; break; }
    s_convParamMap   = s_loadedParamMap;
    s_convScore      = s_loadedScore;
    s_convSeconds    = s_loadedSeconds;
    s_convPolyphony  = outNode ? outNode->polyphony : 1;
    s_convStashValid = true;
    s_loadedParamMap = nlohmann::json::object();
    s_loadedScore    = nlohmann::json();
    s_loadedInstrumentExtras = nlohmann::json::object();
    s_loadedSeconds  = nlohmann::json();

    // Whatever fed Output.source now feeds the Channel. Capture the pin id
    // BEFORE any emplace_back — node creation can reallocate s_nodes.
    int srcOutPin = -1;
    ImVec2 anchor(600.0f, 200.0f);
    if (outNode) {
        if (GraphNode* src = find_source_node(outNode->inputs[0].id))
            if (!src->outputs.empty()) srcOutPin = src->outputs[0].id;
        if (!s_headless) anchor = ImNodes::GetNodeGridSpacePos(outNode->id);
    }

    // Drop Output + Parameter nodes (delete_node also prunes their links).
    std::vector<int> doomed;
    for (auto& n : s_nodes)
        if (n.typeName == NT_PATCH_OUTPUT || n.typeName == NT_PARAMETER)
            doomed.push_back(n.id);
    for (int id : doomed) delete_node(id);

    // Terminate with Channel → Mixer (the node-graph output convention).
    s_nodes.emplace_back(std::string(NT_SOUND_CHANNEL));
    int chId     = s_nodes.back().id;
    int chSrcPin = s_nodes.back().find_input("source")->id;
    int chOutPin = s_nodes.back().outputs[0].id;
    s_nodes.emplace_back(std::string(NT_STEREO_MIXER));
    int mixId    = s_nodes.back().id;
    int mixCh1Pin = s_nodes.back().find_input("ch 1")->id;
    if (!s_headless) {
        ImNodes::SetNodeGridSpacePos(chId,  anchor);
        ImNodes::SetNodeGridSpacePos(mixId, ImVec2(anchor.x + 220.0f, anchor.y));
    }
    if (srcOutPin >= 0) s_links.emplace_back(srcOutPin, chSrcPin);
    s_links.emplace_back(chOutPin, mixCh1Pin);

    s_graphMode  = GraphMode::NodeGraph;
    mark_graph_dirty();
    update_all_dsp();
    transport_set_status("Converted to Node graph (instrument data stashed for convert-back)", false);
}

static void convert_node_to_patch_graph() {
    // Main source: whatever feeds the first Channel's source pin; else the
    // Mixer's ch 1; else the topologically-last node with an output.
    GraphNode* channel = nullptr;
    GraphNode* mixer   = nullptr;
    for (auto& n : s_nodes) {
        if (!channel && n.typeName == NT_SOUND_CHANNEL) channel = &n;
        if (!mixer   && n.typeName == NT_STEREO_MIXER)  mixer   = &n;
    }
    int srcOutPin = -1;
    ImVec2 anchor(600.0f, 200.0f);
    if (channel) {
        if (Pin* sp = channel->find_input("source"))
            if (GraphNode* src = find_source_node(sp->id))
                if (!src->outputs.empty()) srcOutPin = src->outputs[0].id;
    }
    if (srcOutPin < 0 && mixer) {
        if (Pin* cp = mixer->find_input("ch 1"))
            if (GraphNode* src = find_source_node(cp->id))
                if (!src->outputs.empty()) srcOutPin = src->outputs[0].id;
    }
    if (srcOutPin < 0) {
        auto sorted = topo_sort();
        for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
            GraphNode* n = *it;
            if (n->typeName == NT_SOUND_CHANNEL || n->typeName == NT_STEREO_MIXER ||
                n->typeName == NT_PARAMETER || n->typeName == NT_PATCH_OUTPUT)
                continue;
            if (!n->outputs.empty()) { srcOutPin = n->outputs[0].id; break; }
        }
    }
    if (!s_headless) {
        if (mixer)        anchor = ImNodes::GetNodeGridSpacePos(mixer->id);
        else if (channel) anchor = ImNodes::GetNodeGridSpacePos(channel->id);
        else if (GraphNode* srcNode = srcOutPin >= 0 ? find_node_for_pin(srcOutPin) : nullptr)
        {
            ImVec2 p = ImNodes::GetNodeGridSpacePos(srcNode->id);
            anchor = ImVec2(p.x + 240.0f, p.y);
        }
    }

    // Drop Channel/Mixer nodes.
    std::vector<int> doomed;
    for (auto& n : s_nodes)
        if (n.typeName == NT_SOUND_CHANNEL || n.typeName == NT_STEREO_MIXER)
            doomed.push_back(n.id);
    for (int id : doomed) delete_node(id);

    // Terminate with an Output node.
    s_nodes.emplace_back(std::string(NT_PATCH_OUTPUT));
    int outId     = s_nodes.back().id;
    int outSrcPin = s_nodes.back().inputs[0].id;
    s_nodes.back().polyphony = s_convStashValid ? s_convPolyphony : 1;
    if (!s_headless) ImNodes::SetNodeGridSpacePos(outId, anchor);
    if (srcOutPin >= 0) s_links.emplace_back(srcOutPin, outSrcPin);

    bool restored = false;
    if (s_convStashValid) {
        // Same-session round trip: restore the stripped instrument data
        // verbatim. Curve/config-only paramMap entries flow back through
        // s_loadedParamMap, so save_patch_graph's carry-forward re-emits
        // them exactly as loaded.
        s_loadedParamMap = s_convParamMap;
        s_loadedScore    = s_convScore;
        s_loadedSeconds  = s_convSeconds;
        conv_stash_clear();
        restored = true;
    } else {
        // Synthesize: first node owning an unconnected numeric `frequency`
        // param drives the pitch (add_instrument_block.py heuristic).
        // Capture ids first — emplace_back below may reallocate s_nodes.
        int freqPinId = -1, freqNodeId = -1;
        float freqDefault = 440.0f;
        for (auto& n : s_nodes) {
            if (is_special_ui_type(n.typeName)) continue;
            for (auto& p : n.inputs) {
                if (p.name != "frequency" || p.inputOnly) continue;
                if (is_pin_connected(p.id)) continue;
                freqPinId   = p.id;
                freqNodeId  = n.id;
                freqDefault = p.defaultValue;
                break;
            }
            if (freqPinId >= 0) break;
        }
        if (freqPinId >= 0) {
            // Synthesized binding lives in the stash (no Parameter node):
            // "frequency" -> the found node's frequency pin.
            (void)freqDefault;
            for (auto& n : s_nodes) {
                if (n.id == freqNodeId && !n.label.empty()) {
                    s_loadedParamMap = nlohmann::json::object();
                    s_loadedParamMap["frequency"] = n.label + ".frequency";
                    break;
                }
            }
        }
    }

    s_graphMode  = GraphMode::PatchGraph;
    mark_graph_dirty();
    update_all_dsp();
    transport_set_status(restored
        ? "Converted to Patch graph (stashed instrument data restored)"
        : "Converted to Patch graph (instrument block synthesized)", false);
}

static void convert_graph_mode() {
    if (s_graphMode == GraphMode::PatchGraph) convert_patch_to_node_graph();
    else                                      convert_node_to_patch_graph();
}

// ===========================================================================
// ===========================================================================
// Context menus
// ===========================================================================

static bool s_wantCreateMenu = false;
static bool s_wantNodeMenu = false;
static ImVec2 s_createMenuPos;
static int s_contextNodeId = -1;

// Helper: add a menu item that creates a registered source node
static void menu_source(const char* label, const char* typeName) {
    if (ImGui::MenuItem(label)) {
        s_nodes.emplace_back(std::string(typeName));
        ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
        // A node created while drilled into a group belongs to that group.
        if (!s_groupPath.empty())
            if (NodeGroup* g = group_by_name(s_groupPath.back()))
                g->members.push_back(s_nodes.back().label);
        update_node_dsp(s_nodes.back());
        mark_graph_dirty();
    }
}

// Helper: visible separator with vertical padding for submenus
static void menu_sep() {
    ImGui::Spacing();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y),
        IM_COL32(140, 140, 140, 255), 1.5f);
    ImGui::Dummy(ImVec2(0, 2));
    ImGui::Spacing();
}

// Helper: grayed-out placeholder for unimplemented types
static void menu_placeholder(const char* label) {
    ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), "%s", label);
}

static void show_create_menu() {
    if (!ImGui::BeginPopup("CreateNodeMenu")) return;

    // --- Top level quick access (Matt 2026-08-26 rearrangement) ---
    // Note and Curve promoted from the old Performance submenu.
    if (s_graphMode == GraphMode::PatchGraph) {
        // A PerformNode resolves to the voice's adapter; the node-graph
        // render path has no voice, so patch mode only.
        if (ImGui::MenuItem("Note")) {
            s_nodes.emplace_back(std::string(NT_PERFORM));
            ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
            if (!s_groupPath.empty())
                if (NodeGroup* g = group_by_name(s_groupPath.back()))
                    g->members.push_back(s_nodes.back().label);
            update_node_dsp(s_nodes.back());
            mark_graph_dirty();
        }
    } else {
        menu_placeholder("Note (patch graph only)");
    }
    if (ImGui::MenuItem("Curve")) {
        s_nodes.emplace_back(std::string("CurveNode"));
        auto& cn = s_nodes.back();
        // Identity seed — an empty knot list maps everything to 0,
        // which reads as a broken node. Edit in the node's Properties.
        cn.curveKnots = {{0.0f, 0.0f}, {1.0f, 1.0f}};
        ImNodes::SetNodeScreenSpacePos(cn.id, s_createMenuPos);
        if (!s_groupPath.empty())
            if (NodeGroup* g = group_by_name(s_groupPath.back()))
                g->members.push_back(cn.label);
        update_node_dsp(cn);
        mark_graph_dirty();
    }
    menu_source("Envelope", "Envelope");
    menu_source("Var", "VarSource");
    menu_source("Range", "RangeSource");

    if (s_graphMode == GraphMode::PatchGraph) {
        bool hasOutput = false;
        for (auto& n : s_nodes) if (n.typeName == NT_PATCH_OUTPUT) hasOutput = true;
        if (!hasOutput) {
            if (ImGui::MenuItem("Output")) {
                s_nodes.emplace_back(std::string(NT_PATCH_OUTPUT));
                ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
            }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Output (exists)");
        }
    }

    // Parameter nodes exist only in NodeGraph mode (keyboard playability);
    // instrument patches bind via the Parameter-mapping dialog instead.
    if (s_graphMode == GraphMode::NodeGraph && ImGui::MenuItem("Parameter")) {
        s_nodes.emplace_back(std::string(NT_PARAMETER), "frequency");
        ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
    }

    ImGui::Separator();

    // --- Generators ---
    if (ImGui::BeginMenu("Generators")) {
        menu_source("Sine", "SineSource");
        menu_source("Saw", "SawSource");
        menu_source("Pulse", "PulseSource");
        menu_source("Triangle", "TriangleSource");
        menu_sep();
        menu_source("FM", "FMSource");
        menu_source("Hybrid KS", "HybridKSSource");
        menu_source("KS String", "KSString");
        menu_source("Allpass Resonator", "AllpassResonator");
        menu_sep();
        menu_source("Phased", "PhasedValueSource");
        menu_source("Repeating", "RepeatingSource");
        ImGui::EndMenu();
    }

    // --- Noise (mainstream set; the experimental noises live under
    // Experimental > Noise) ---
    if (ImGui::BeginMenu("Noise")) {
        menu_source("Red", "RedNoiseSource");
        menu_source("Pink", "PinkNoiseSource");
        menu_source("White", "WhiteNoiseSource");
        menu_source("Blue", "BlueNoiseSource");
        menu_source("Violet", "VioletNoiseSource");
        menu_sep();
        menu_source("Segment", "SegmentSource");
        menu_source("Velvet", "VelvetNoiseSource");
        menu_source("Perlin", "PerlinNoiseSource");
        menu_source("Crackle", "CrackleNoiseSource");
        ImGui::EndMenu();
    }

    // --- Wavetable (physical + algorithmic evolutions live under
    // Experimental) ---
    if (ImGui::BeginMenu("Wavetable")) {
        menu_source("Wavetable", "WavetableSource");
        menu_sep();
        menu_source("EKS Evolution", "EKSEvolution");
        menu_source("Pluck Evolution", "PluckEvolution");
        menu_source("Averaging Evolution", "AveragingEvolution");
        ImGui::EndMenu();
    }

    // --- Additive ---
    if (ImGui::BeginMenu("Additive")) {
        menu_source("Full", "AdditiveSource");
        menu_source("Basic", "BasicAdditiveSource");
        menu_source("Alternate", "AdditiveSource2");
        menu_sep();
        menu_source("Full Partials", "FullPartials");
        menu_source("Sequence Partials", "SequencePartials");
        menu_source("Explicit Partials", "ExplicitPartials");
        menu_source("Composite Partials", "CompositePartials");
        menu_source("Partials Expand Rule", "ExpandRule");
        menu_sep();
        menu_source("Formant", "Formant");
        menu_source("Formant Sequence", "FormantSequence");
        menu_source("Formant Spectrum", "FormantSpectrum");
        menu_source("Band Spectrum", "BandSpectrum");
        menu_source("Fixed Spectrum", "FixedSpectrum");
        ImGui::EndMenu();
    }

    ImGui::Separator();

    // --- Envelopes ---
    if (ImGui::BeginMenu("Envelopes")) {
        menu_source("ADSR", "ADSREnvelope");
        menu_source("ASR", "ASREnvelope");
        menu_source("ADR", "ADREnvelope");
        menu_source("AR", "AREnvelope");
        menu_source("AS", "ASEnvelope");
        menu_source("ADS", "ADSEnvelope");
        menu_sep();
        menu_source("Envelope", "Envelope");
        menu_source("Vibrato", "Vibrato");
        ImGui::EndMenu();
    }

    // --- Filters ---
    if (ImGui::BeginMenu("Filters")) {
        menu_source("Delay", "DelayFilter");
        menu_source("Reverb", "Reverb");
        menu_source("Limiter", "Limiter");
        menu_source("Hammer Bank", "HammerBank");
        menu_source("Distortion", "DistortedSource");
        menu_sep();
        menu_source("BW Bandpass", "BWBandpassFilter");
        menu_source("BW Lowpass", "BWLowpassFilter");
        menu_source("BW Highpass", "BWHighpassFilter");
        menu_source("SVF (State Variable)", "SVFSource");
        ImGui::EndMenu();
    }

    // --- Combiners ---
    if (ImGui::BeginMenu("Combiners")) {
        menu_source("Combined", "CombinedSource");
        menu_source("Crossfade", "CrossfadeSource");
        menu_source("Multi", "MultiSource");
        menu_source("Multiplex", "MultiplexSource");
        ImGui::EndMenu();
    }

    ImGui::Separator();

    // --- Output (special UI sink types, gated by graph mode) ---
    // Output terminates a Patch graph (one per graph); Channel/Mixer
    // terminate a Node graph (the engine's render path expects
    // graph.output = StereoMixer there, so one Mixer per graph).
    if (ImGui::BeginMenu("Output")) {
        bool hasOutput = false, hasMixer = false;
        for (auto& n : s_nodes) {
            if (n.typeName == NT_PATCH_OUTPUT) hasOutput = true;
            if (n.typeName == NT_STEREO_MIXER) hasMixer  = true;
        }
        if (s_graphMode == GraphMode::PatchGraph) {
            if (!hasOutput) {
                if (ImGui::MenuItem("Output")) {
                    s_nodes.emplace_back(std::string(NT_PATCH_OUTPUT));
                    ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
                    mark_graph_dirty();
                }
            } else {
                menu_placeholder("Output (exists)");
            }
            menu_placeholder("Channel (node graph only)");
            menu_placeholder("Mixer (node graph only)");
        } else {
            menu_placeholder("Output (patch graph only)");
            if (ImGui::MenuItem("Channel")) {
                s_nodes.emplace_back(std::string(NT_SOUND_CHANNEL));
                ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
                mark_graph_dirty();
            }
            if (!hasMixer) {
                if (ImGui::MenuItem("Mixer")) {
                    s_nodes.emplace_back(std::string(NT_STEREO_MIXER));
                    ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
                    mark_graph_dirty();
                }
            } else {
                menu_placeholder("Mixer (exists)");
            }
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();

    // --- Experimental: parked / in-progress node families, mirroring the
    // main category names (Matt 2026-08-26). Physical + Algorithmic
    // WaveEvolutions moved here from their own top-level menus.
    if (ImGui::BeginMenu("Experimental")) {
        if (ImGui::BeginMenu("Generators")) {
            menu_placeholder("(none yet)");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Noise")) {
            menu_source("Layered Red", "LayeredRedNoiseSource");
            menu_source("Murmuration", "MurmurationNoiseSource");
            menu_source("Wander 1", "WanderNoiseSource");
            menu_source("Wander 2", "WanderNoise2Source");
            menu_source("Wander 3", "WanderNoise3Source");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Wavetable")) {
            menu_source("Bowed String Evolution", "BowedStringEvolution");
            menu_source("Reed Evolution", "ReedEvolution");
            menu_source("Brass Evolution", "BrassEvolution");
            menu_sep();
            menu_source("Reaction-Diffusion (Gray-Scott)", "ReactionDiffusionEvolution");
            menu_source("Sort Erosion (->saw)", "SortErosionEvolution");
            menu_source("Cellular Automaton (Wolfram)", "CellularAutomatonEvolution");
            menu_source("Histogram Equalize", "HistogramEqualizeEvolution");
            menu_source("Bezier Pull (->curve)", "BezierPullEvolution");
            menu_source("Bit Rotate (glitch)", "BitRotateEvolution");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Filters")) {
            menu_placeholder("(none yet)");
            ImGui::EndMenu();
        }
        ImGui::EndMenu();
    }

    ImGui::EndPopup();
}

// Group the current editor selection (spec §3). Members = selected nodes'
// labels + selected collapsed groups at this level. Refuses the Output node
// and two-output selections BY NAME. Returns the new group's name or "".
static std::string group_selection(std::string& err) {
    int n = ImNodes::NumSelectedNodes();
    if (n < 1) { err = "nothing selected"; return ""; }
    std::vector<int> ids(n);
    ImNodes::GetSelectedNodes(ids.data());

    NodeGroup g;
    ImVec2 centroid(0, 0);
    int posCount = 0;
    for (int id : ids) {
        bool matched = false;
        for (auto& node : s_nodes) {
            if (node.id != id) continue;
            matched = true;
            if (node.typeName == NT_PATCH_OUTPUT) {
                err = "the Output node cannot be grouped";
                return "";
            }
            g.members.push_back(node.label);
            centroid.x += ImNodes::GetNodeGridSpacePos(id).x;
            centroid.y += ImNodes::GetNodeGridSpacePos(id).y;
            ++posCount;
        }
        if (!matched) {
            for (auto& og : s_groups) {
                if (og.editorId == id) {
                    g.members.push_back(og.name);
                    centroid.x += og.pos.x;
                    centroid.y += og.pos.y;
                    ++posCount;
                }
            }
        }
    }
    if (g.members.empty()) { err = "nothing groupable selected"; return ""; }

    // Two-output rule: refuse with the offenders' names.
    std::vector<GroupBoundaryIn> ins;
    std::vector<GraphNode*> outs;
    group_boundary(g, ins, outs);
    if (outs.size() > 1) {
        err = "selection has " + std::to_string(outs.size()) + " outputs:";
        for (auto* o : outs) err += " " + o->label;
        return "";
    }

    // Unique name in the shared namespace.
    std::string base = "Group", name = base;
    auto taken = [&](const std::string& s) {
        if (group_by_name(s)) return true;
        for (auto& node : s_nodes) if (node.label == s) return true;
        return false;
    };
    for (int i = 2; taken(name); ++i) name = base + std::to_string(i);
    g.name = name;
    g.pos = posCount ? ImVec2(centroid.x / posCount, centroid.y / posCount)
                     : ImVec2(200, 200);
    g.posApplied = false;
    g.editorId = next_id();
    g.outPinId = next_id();

    // The new group lives at the CURRENT level: if we're inside a group,
    // it becomes a member there (and its members leave that list).
    if (!s_groupPath.empty()) {
        if (NodeGroup* parent = group_by_name(s_groupPath.back())) {
            for (auto& m : g.members)
                parent->members.erase(std::remove(parent->members.begin(),
                                                  parent->members.end(), m),
                                      parent->members.end());
            parent->members.push_back(g.name);
        }
    }
    s_groups.push_back(std::move(g));
    mark_graph_dirty();
    return name;
}

// Ungroup: members rejoin the parent level; fully reverses group_selection.
static void ungroup(const std::string& name) {
    NodeGroup* g = group_by_name(name);
    if (!g) return;
    NodeGroup* parent = group_of(name);
    std::vector<std::string> members = g->members;
    if (parent) {
        parent->members.erase(std::remove(parent->members.begin(),
                                          parent->members.end(), name),
                              parent->members.end());
        for (auto& m : members) parent->members.push_back(m);
    }
    // Path entries pointing at the dead group fall back to its parent level.
    while (!s_groupPath.empty() && s_groupPath.back() == name)
        s_groupPath.pop_back();
    s_groups.erase(std::remove_if(s_groups.begin(), s_groups.end(),
                                  [&](const NodeGroup& x) { return x.name == name; }),
                   s_groups.end());
    mark_graph_dirty();
}

// Node context menu (right-click on existing node)
static void show_node_context_menu() {
    if (!ImGui::BeginPopup("NodeContextMenu")) return;

    GraphNode* node = nullptr;
    for (auto& n : s_nodes) if (n.id == s_contextNodeId) { node = &n; break; }

    // Right-click on a collapsed GROUP node: group menu.
    if (!node) {
        NodeGroup* grp = nullptr;
        for (auto& g : s_groups)
            if (g.editorId == s_contextNodeId) { grp = &g; break; }
        if (!grp) { ImGui::EndPopup(); return; }
        GraphNode* gOut = group_output_node(*grp);
        bool tapped = gOut && gOut->id == s_listenTapNode;
        if (ImGui::MenuItem(tapped ? "Stop listening here" : "Listen here",
                            nullptr, false, gOut != nullptr)) {
            s_listenTapNode = tapped ? -1 : gOut->id;
            s_groupListen[grp->name] = !tapped;
        }
        if (ImGui::MenuItem("Open")) {
            s_groupPath.push_back(grp->name);
        }
        if (ImGui::MenuItem("Ungroup")) {
            ungroup(grp->name);
        }
        ImGui::EndPopup();
        return;
    }

    if (ImGui::MenuItem("Duplicate")) {
        mark_graph_dirty();
        // Create a copy of the node with same type and settings
        if (node->typeName == NT_PARAMETER) {
            s_nodes.emplace_back(node->typeName, node->paramName);
        } else {
            s_nodes.emplace_back(node->typeName);
        }
        auto& dup = s_nodes.back();

        // Copy pin default values
        for (int i = 0; i < (int)node->inputs.size() && i < (int)dup.inputs.size(); ++i) {
            dup.inputs[i].defaultValue = node->inputs[i].defaultValue;
            if (dup.inputs[i].constantSrc)
                dup.inputs[i].constantSrc->set(node->inputs[i].defaultValue);
        }

        // Copy config values
        for (int i = 0; i < (int)node->settingValues.size() && i < (int)dup.settingValues.size(); ++i) {
            dup.settingValues[i].second = node->settingValues[i].second;
            if (dup.dspSource)
                dup.dspSource->set_setting(dup.settingValues[i].first.name, dup.settingValues[i].second);
        }

        // Offset position
        ImVec2 pos = ImNodes::GetNodeScreenSpacePos(node->id);
        ImNodes::SetNodeScreenSpacePos(dup.id, ImVec2(pos.x + 30, pos.y + 30));
    }

    if (ImGui::MenuItem("Delete")) {
        delete_node(s_contextNodeId);
        g_selectedNodeId = -1;
    }

    ImGui::Separator();

    // Listen tap (spec §3): monitor this node's output in-context.
    {
        bool tapped = node->id == s_listenTapNode;
        bool can = !node->outputs.empty() && node->typeName != NT_PATCH_OUTPUT;
        if (ImGui::MenuItem(tapped ? "Stop listening here" : "Listen here",
                            nullptr, false, can)) {
            s_listenTapNode = tapped ? -1 : node->id;
        }
    }

    // Group the selection (right-clicked node included via imnodes select).
    {
        int nSel = ImNodes::NumSelectedNodes();
        char lbl[48];
        snprintf(lbl, sizeof(lbl), "Group selection (%d)", nSel);
        if (ImGui::MenuItem(lbl, nullptr, false, nSel >= 1)) {
            std::string err;
            std::string name = group_selection(err);
            if (name.empty())
                transport_set_status(("Group refused: " + err).c_str(), true);
            else
                transport_set_status(("Grouped as '" + name +
                                      "' — double-click to open, rename in Properties").c_str(), false);
        }
    }

    // Membership editing (Matt 2026-08-20: until now a group could only be
    // built from scratch — adding a node later meant ungroup-and-redo).
    // Acts on the whole selection when the clicked node is part of it,
    // otherwise on just the clicked node.
    {
        std::vector<GraphNode*> targets;
        {
            int nSel = ImNodes::NumSelectedNodes();
            std::vector<int> sel(std::max(nSel, 1));
            if (nSel > 0) ImNodes::GetSelectedNodes(sel.data());
            bool clickedInSel = false;
            for (int i = 0; i < nSel; ++i)
                if (sel[i] == node->id) clickedInSel = true;
            if (clickedInSel) {
                for (int i = 0; i < nSel; ++i)
                    for (auto& n : s_nodes)
                        if (n.id == sel[i]) targets.push_back(&n);
            } else {
                targets.push_back(node);
            }
        }

        if (!s_groups.empty()) {
            char lbl[48];
            snprintf(lbl, sizeof(lbl), "Add to group (%d)", (int)targets.size());
            if (ImGui::BeginMenu(lbl)) {
                for (auto& g : s_groups) {
                    // Skip groups every target is already in.
                    bool allIn = true;
                    for (auto* t : targets)
                        if (std::find(g.members.begin(), g.members.end(),
                                      t->label) == g.members.end())
                            allIn = false;
                    if (allIn) continue;
                    if (!ImGui::MenuItem(g.name.c_str())) continue;

                    // Same two-output rule as group creation, checked on a
                    // TRIAL membership before anything moves.
                    NodeGroup trial = g;
                    for (auto* t : targets)
                        if (std::find(trial.members.begin(), trial.members.end(),
                                      t->label) == trial.members.end())
                            trial.members.push_back(t->label);
                    std::vector<GroupBoundaryIn> ins;
                    std::vector<GraphNode*> outs;
                    group_boundary(trial, ins, outs);
                    if (outs.size() > 1) {
                        std::string err = "Add refused: '" + g.name + "' would have " +
                                          std::to_string(outs.size()) + " outputs:";
                        for (auto* o : outs) err += " " + o->label;
                        transport_set_status(err.c_str(), true);
                        continue;
                    }
                    for (auto* t : targets) {
                        for (auto& og : s_groups)
                            og.members.erase(std::remove(og.members.begin(),
                                                         og.members.end(),
                                                         t->label),
                                             og.members.end());
                        g.members.push_back(t->label);
                    }
                    transport_set_status(
                        (std::to_string(targets.size()) + " node(s) -> '" +
                         g.name + "'").c_str(), false);
                    mark_graph_dirty();
                }
                ImGui::EndMenu();
            }
        }

        if (NodeGroup* cur = group_of(node->label)) {
            std::string lbl = "Remove from '" + cur->name + "'";
            if (ImGui::MenuItem(lbl.c_str())) {
                // Out of its group, into that group's parent level (top
                // level when the group isn't nested) — where the group face
                // itself lives. Per target, so a mixed selection resolves
                // each node against its own group.
                for (auto* t : targets) {
                    NodeGroup* tg = group_of(t->label);
                    if (!tg) continue;
                    NodeGroup* parent = group_of(tg->name);
                    tg->members.erase(std::remove(tg->members.begin(),
                                                  tg->members.end(), t->label),
                                      tg->members.end());
                    if (parent) parent->members.push_back(t->label);
                }
                transport_set_status("removed from group", false);
                mark_graph_dirty();
            }
        }
    }

    ImGui::EndPopup();
}

// ===========================================================================
// Waveform display
// ===========================================================================

static constexpr float WAVE_MARGIN_W = 45.0f;  // left margin for peak values

// Returns true if the pop-out button was clicked this frame. Zoom/scroll are
// passed in so popouts can maintain independent view state from the main panel.
static bool draw_waveform(const char* label, const float* buf, int sampleCount,
                          ImU32 waveColor,
                          float x, float y, float width, float height,
                          int zoom, int scrollPos,
                          bool showPopoutBtn = true,
                          int popoutBtnId = 0) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Background (full area including margin)
    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(20, 20, 25, 255));

    // Waveform drawing area (after margin)
    float drawX = x + WAVE_MARGIN_W;
    float drawW = width - WAVE_MARGIN_W;
    float midY = y + height * 0.5f;

    // Center line
    dl->AddLine(ImVec2(drawX, midY), ImVec2(drawX + drawW, midY), IM_COL32(60, 60, 60, 255));

    // Label in margin area
    dl->AddText(ImVec2(x + 2, y + 2), IM_COL32(150, 150, 150, 255), label);

    if (sampleCount > 0 && buf) {
        // Find global peak for normalization
        float peakPos = 0.0f, peakNeg = 0.0f;
        for (int i = 0; i < sampleCount; ++i) {
            peakPos = std::max(peakPos, buf[i]);
            peakNeg = std::min(peakNeg, buf[i]);
        }
        float peakAbs = std::max(std::abs(peakPos), std::abs(peakNeg));
        float scale = (peakAbs > 0.0001f) ? (1.0f / peakAbs) : 1.0f;

        // Show peak values in margin
        char peakBuf[16];
        snprintf(peakBuf, sizeof(peakBuf), "%.3f", peakPos);
        dl->AddText(ImVec2(x + 2, y + height * 0.15f), IM_COL32(100, 100, 100, 255), peakBuf);
        snprintf(peakBuf, sizeof(peakBuf), "%.3f", peakNeg);
        dl->AddText(ImVec2(x + 2, y + height * 0.75f), IM_COL32(100, 100, 100, 255), peakBuf);

        // Draw waveform (normalized)
        int pixelCount = (int)drawW;
        for (int px = 0; px < pixelCount; ++px) {
            int sampleStart = scrollPos + px * zoom;
            if (sampleStart >= sampleCount) break;

            float minVal = 1.0f, maxVal = -1.0f;
            int sampleEnd = std::min(sampleStart + zoom, sampleCount);
            for (int s = sampleStart; s < sampleEnd; ++s) {
                float v = buf[s] * scale;
                minVal = std::min(minVal, v);
                maxVal = std::max(maxVal, v);
            }

            float y0 = midY - maxVal * (height * 0.45f);
            float y1 = midY - minVal * (height * 0.45f);
            if (y1 - y0 < 1.0f) { y0 -= 0.5f; y1 += 0.5f; }
            dl->AddLine(ImVec2(drawX + px, y0), ImVec2(drawX + px, y1), waveColor);
        }
    }

    // Border
    dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(80, 80, 80, 255));
    // Margin separator
    dl->AddLine(ImVec2(drawX, y), ImVec2(drawX, y + height), IM_COL32(60, 60, 60, 255));

    // Pop-out button in top-right corner (drawn last so it's on top).
    bool popped = false;
    if (showPopoutBtn) {
        ImVec2 savedCursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(x + width - 20, y + 2));
        ImGui::PushID("wpop");
        ImGui::PushID(popoutBtnId);
        popped = ImGui::ArrowButton("##pop", ImGuiDir_Up);
        ImGui::PopID();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(savedCursor);
    }
    return popped;
}

// ===========================================================================
// FFT (radix-2 Cooley-Tukey, in-place complex) + spectrum compute
// ===========================================================================

// fft_inplace lives in mforce/util/fft.h — shared with mforce_cli --explore.

// Compute the output spectrum: pull a sustain-region slice from g_outputWaveform,
// apply a Hann window, FFT, convert to dB. Stash into g_outputSpectrumDb.
static void compute_output_spectrum() {
    g_outputSpectrumDb.clear();
    g_outputSpectrumN = 0;
    if (g_outputWaveform.empty() || g_waveformSamples < 256) return;

    // Window size: largest power of 2 ≤ min(8192, samples). 8192 @ 48k = 170 ms,
    // ~5.9 Hz bin width — fine for formant peaks while keeping FFT cheap (<1 ms).
    int N = 8192;
    while (N > g_waveformSamples) N >>= 1;
    if (N < 256) return;

    // Pull from the middle of the buffer (avoid attack/release edges).
    int mid    = g_waveformSamples / 2;
    int offset = std::max(0, mid - N / 2);
    if (offset + N > g_waveformSamples) offset = g_waveformSamples - N;

    std::vector<std::complex<float>> buf(N);
    const float twoPi = 6.28318530718f;
    for (int i = 0; i < N; ++i) {
        // Hann window: 0.5 * (1 - cos(2π i / (N - 1)))
        float w = 0.5f * (1.0f - std::cos(twoPi * float(i) / float(N - 1)));
        buf[i] = std::complex<float>(g_outputWaveform[offset + i] * w, 0.0f);
    }

    fft_inplace(buf);

    // Magnitude in dB. Scale so the peak of a full-amplitude sine reads ~0 dB.
    // For a Hann-windowed sine: peak |X[k]| = N/4 (window coherent gain = 0.5).
    // So divide by N/4, then 20*log10. Floor at -120 dB.
    int Nover2 = N / 2;
    g_outputSpectrumDb.resize(Nover2);
    const float scale = 4.0f / float(N);
    const float floorDb = -120.0f;
    for (int k = 0; k < Nover2; ++k) {
        float mag = std::abs(buf[k]) * scale;
        g_outputSpectrumDb[k] = (mag > 1e-6f) ? 20.0f * std::log10(mag) : floorDb;
    }
    g_outputSpectrumN  = Nover2;
    g_outputSpectrumSR = 48000;  // matches the offline-render SR convention
}

// ===========================================================================
// Spectrum window (sibling tab to Waveforms / Keyboard)
// ===========================================================================

static void draw_spectrum_window() {
    ImGui::Begin("Spectrum", nullptr, ImGuiWindowFlags_NoCollapse);

    float w = ImGui::GetContentRegionAvail().x;
    float h = ImGui::GetContentRegionAvail().y;
    if (w < 60.0f || h < 60.0f) { ImGui::End(); return; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImVec2 p1 = ImVec2(p0.x + w, p0.y + h);

    // Background
    dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 25, 255));

    // Plot area: leave margins for axis labels (y on left, x along bottom).
    const float marginL = 36.0f;
    const float marginB = 18.0f;
    float plotX0 = p0.x + marginL;
    float plotX1 = p1.x - 4.0f;
    float plotY0 = p0.y + 4.0f;
    float plotY1 = p1.y - marginB;

    // Empty state
    if (g_outputSpectrumN <= 0) {
        dl->AddText(ImVec2(plotX0 + 8, plotY0 + 8),
                    IM_COL32(140, 140, 140, 255),
                    "No render yet — hit Generate.");
        dl->AddRect(ImVec2(plotX0, plotY0), ImVec2(plotX1, plotY1), IM_COL32(80, 80, 80, 255));
        ImGui::Dummy(ImVec2(w, h));
        ImGui::End();
        return;
    }

    // Axes
    const float fMin  = 20.0f;
    const float fMax  = float(g_outputSpectrumSR) * 0.5f;  // Nyquist
    const float dbMin = -100.0f;
    const float dbMax = 6.0f;
    const float logFmin = std::log10(fMin);
    const float logFmax = std::log10(fMax);

    auto freq_to_x = [&](float f) {
        float t = (std::log10(std::max(f, fMin)) - logFmin) / (logFmax - logFmin);
        return plotX0 + t * (plotX1 - plotX0);
    };
    auto db_to_y = [&](float db) {
        float t = (db - dbMin) / (dbMax - dbMin);
        return plotY1 - t * (plotY1 - plotY0);
    };
    auto x_to_freq = [&](float x) {
        float t = (x - plotX0) / (plotX1 - plotX0);
        return std::pow(10.0f, logFmin + t * (logFmax - logFmin));
    };

    // Grid: vertical lines at decade and 2/5 of each decade (1, 2, 5, 10, 20, ...).
    const ImU32 gridCol  = IM_COL32(50, 50, 55, 255);
    const ImU32 majorCol = IM_COL32(70, 70, 80, 255);
    const ImU32 lblCol   = IM_COL32(120, 120, 120, 255);
    static const float decMul[] = {1.0f, 2.0f, 5.0f};
    for (int dec = 1; dec <= 5; ++dec) {
        float base = std::pow(10.0f, float(dec));
        for (float m : decMul) {
            float f = base * m;
            if (f < fMin || f > fMax) continue;
            float x = freq_to_x(f);
            dl->AddLine(ImVec2(x, plotY0), ImVec2(x, plotY1),
                        (m == 1.0f) ? majorCol : gridCol);
            if (m == 1.0f || m == 2.0f) {
                char lbl[16];
                if (f >= 1000.0f) snprintf(lbl, sizeof(lbl), "%gk", f / 1000.0f);
                else              snprintf(lbl, sizeof(lbl), "%g", f);
                dl->AddText(ImVec2(x + 2, plotY1 + 2), lblCol, lbl);
            }
        }
    }
    // Grid: horizontal dB lines every 12 dB.
    for (int db = int(dbMax); db >= int(dbMin); db -= 12) {
        float y = db_to_y(float(db));
        dl->AddLine(ImVec2(plotX0, y), ImVec2(plotX1, y),
                    (db == 0) ? majorCol : gridCol);
        char lbl[16]; snprintf(lbl, sizeof(lbl), "%d", db);
        dl->AddText(ImVec2(p0.x + 2, y - 6), lblCol, lbl);
    }

    // Plot the spectrum line. Per-pixel max-bin reduction to avoid undersampling
    // gaps when bin spacing in log-x is sub-pixel.
    const ImU32 lineCol = IM_COL32(60, 200, 200, 255);  // teal — distinct from waveform green
    int prevBin = 1;
    float prevX = freq_to_x(float(g_outputSpectrumSR) * float(prevBin) / float(g_outputSpectrumN * 2));
    float prevDbMax = g_outputSpectrumDb[prevBin];
    float prevY = db_to_y(std::clamp(prevDbMax, dbMin, dbMax));

    for (float px = plotX0 + 1.0f; px <= plotX1; px += 1.0f) {
        float fHere = x_to_freq(px);
        int binHere = std::clamp(
            int(std::round(fHere * float(g_outputSpectrumN * 2) / float(g_outputSpectrumSR))),
            1, g_outputSpectrumN - 1);
        float dbHere = dbMin;
        for (int b = std::min(prevBin + 1, binHere); b <= binHere; ++b)
            dbHere = std::max(dbHere, g_outputSpectrumDb[b]);
        float y = db_to_y(std::clamp(dbHere, dbMin, dbMax));
        dl->AddLine(ImVec2(prevX, prevY), ImVec2(px, y), lineCol, 1.5f);
        prevX = px; prevY = y; prevBin = binHere;
    }

    // Border
    dl->AddRect(ImVec2(plotX0, plotY0), ImVec2(plotX1, plotY1), IM_COL32(80, 80, 80, 255));

    // Hover crosshair + readout
    if (ImGui::IsWindowHovered()) {
        ImVec2 m = ImGui::GetMousePos();
        if (m.x >= plotX0 && m.x <= plotX1 && m.y >= plotY0 && m.y <= plotY1) {
            float fAt  = x_to_freq(m.x);
            int   bin  = std::clamp(
                int(std::round(fAt * float(g_outputSpectrumN * 2) / float(g_outputSpectrumSR))),
                1, g_outputSpectrumN - 1);
            float dbAt = g_outputSpectrumDb[bin];

            const ImU32 crossCol = IM_COL32(180, 180, 180, 140);
            dl->AddLine(ImVec2(m.x, plotY0), ImVec2(m.x, plotY1), crossCol);
            float yDb = db_to_y(std::clamp(dbAt, dbMin, dbMax));
            dl->AddLine(ImVec2(plotX0, yDb), ImVec2(plotX1, yDb), crossCol);

            char lbl[64];
            if (fAt >= 1000.0f) snprintf(lbl, sizeof(lbl), "%.2f kHz   %.1f dB", fAt / 1000.0f, dbAt);
            else                snprintf(lbl, sizeof(lbl), "%.1f Hz   %.1f dB", fAt, dbAt);

            // Place the label so it stays in-bounds.
            float lblX = m.x + 8.0f;
            float lblY = m.y - 16.0f;
            ImVec2 sz = ImGui::CalcTextSize(lbl);
            if (lblX + sz.x > plotX1 - 4.0f) lblX = m.x - 8.0f - sz.x;
            if (lblY < plotY0 + 2.0f)        lblY = m.y + 8.0f;
            dl->AddRectFilled(ImVec2(lblX - 3, lblY - 1),
                              ImVec2(lblX + sz.x + 3, lblY + sz.y + 1),
                              IM_COL32(0, 0, 0, 200));
            dl->AddText(ImVec2(lblX, lblY), IM_COL32(255, 255, 200, 255), lbl);
        }
    }

    ImGui::Dummy(ImVec2(w, h));
    ImGui::End();
}

// Helper: small pop-out button shared by all strip kinds.
static bool draw_strip_popout_button(float x, float y, float width, int popoutBtnId) {
    ImVec2 saved = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(x + width - 20, y + 2));
    ImGui::PushID("wpop");
    ImGui::PushID(popoutBtnId);
    bool clicked = ImGui::ArrowButton("##pop", ImGuiDir_Up);
    ImGui::PopID();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(saved);
    return clicked;
}

// Draw a Partials node as a bar chart: x = partial number, y = amplitude.
// Reads live mult1/ampl1 (and mult2/ampl2 for evolving partials) via the
// node's array_descriptors() interface — what the additive synth actually uses.
static bool draw_partials_strip(GraphNode* node, ImU32 color,
                                float x, float y, float width, float height,
                                bool showPopoutBtn, int popoutBtnId) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(20, 20, 25, 255));

    const float marginL = WAVE_MARGIN_W;
    float plotX0 = x + marginL;
    float plotX1 = x + width - 4.0f;
    float plotY0 = y + 14.0f;          // leave room for label up top
    float plotY1 = y + height - 14.0f; // leave room for partial-# axis at bottom

    dl->AddText(ImVec2(x + 2, y + 2), IM_COL32(150, 150, 150, 255), node->label.c_str());

    auto* ipt = node->dspSource ? dynamic_cast<Partials*>(node->dspSource.get()) : nullptr;
    if (!ipt) {
        dl->AddText(ImVec2(plotX0 + 4, plotY0 + 4), IM_COL32(140, 140, 140, 255),
                    "(node not initialized)");
        dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(80, 80, 80, 255));
        bool popped = false;
        if (showPopoutBtn) popped = draw_strip_popout_button(x, y, width, popoutBtnId);
        return popped;
    }

    auto mult1 = ipt->get_array("mult1");
    auto ampl1 = ipt->get_array("ampl1");
    auto mult2 = ipt->get_array("mult2");
    auto ampl2 = ipt->get_array("ampl2");

    // Apply rolloff to the displayed amplitudes — same math the synth applies
    // at render time: ampl *= 1 / pmult^rolloff.  rolloff1 → _1 column,
    // rolloff2 → _2 column.  Without this, weight-only bars look misleading
    // (all 1.0) when rolloff is what's actually shaping the spectrum.
    float ro1 = ipt->get_setting("rolloff1");
    float ro2 = ipt->get_setting("rolloff2");
    auto apply_rolloff = [](std::vector<float>& a, const std::vector<float>& m, float ro) {
        if (ro == 0.0f) return;
        for (size_t i = 0; i < a.size(); ++i) {
            float pm = (i < m.size()) ? m[i] : 1.0f;
            if (pm > 0.0f) a[i] *= 1.0f / std::pow(pm, ro);
        }
    };
    apply_rolloff(ampl1, mult1, ro1);
    apply_rolloff(ampl2, mult2, ro2);

    if (mult1.empty() || ampl1.empty()) {
        dl->AddText(ImVec2(plotX0 + 4, plotY0 + 4), IM_COL32(140, 140, 140, 255),
                    "(empty)");
        dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(80, 80, 80, 255));
        bool popped = false;
        if (showPopoutBtn) popped = draw_strip_popout_button(x, y, width, popoutBtnId);
        return popped;
    }

    int N = std::min(int(mult1.size()), int(ampl1.size()));
    bool hasEvo = !mult2.empty() && !ampl2.empty()
               && mult2.size() == size_t(N) && ampl2.size() == size_t(N);

    // Y range: max amplitude across both arrays (so 1.0 isn't always the cap).
    float aMax = 0.0f;
    for (int i = 0; i < N; ++i) aMax = std::max(aMax, std::abs(ampl1[i]));
    if (hasEvo) for (int i = 0; i < N; ++i) aMax = std::max(aMax, std::abs(ampl2[i]));
    if (aMax < 1e-4f) aMax = 1.0f;

    auto idx_to_x = [&](float i) {
        // Place partial 1 at left edge of plot, partial N at right.
        float t = (N <= 1) ? 0.5f : (i - 1.0f) / float(N - 1);
        return plotX0 + t * (plotX1 - plotX0);
    };
    auto ampl_to_y = [&](float a) {
        float t = std::clamp(a / aMax, 0.0f, 1.0f);
        return plotY1 - t * (plotY1 - plotY0);
    };

    // Baseline
    dl->AddLine(ImVec2(plotX0, plotY1), ImVec2(plotX1, plotY1), IM_COL32(80, 80, 80, 255));

    // Bar width — make _1/_2 sit side-by-side when evolution is on.
    float pitch = (N <= 1) ? (plotX1 - plotX0) * 0.5f : (plotX1 - plotX0) / float(N);
    float fullW = std::max(2.0f, pitch * 0.7f);
    float barW  = hasEvo ? fullW * 0.5f : fullW;

    ImU32 col1 = color;
    ImU32 col2 = IM_COL32_BLACK;
    {
        // Faded version of the same hue for the _2 column.
        int r = (color >>  0) & 0xFF;
        int g = (color >>  8) & 0xFF;
        int b = (color >> 16) & 0xFF;
        col2 = IM_COL32(r * 6 / 10, g * 6 / 10, b * 6 / 10, 255);
    }

    for (int i = 0; i < N; ++i) {
        float bx  = idx_to_x(float(i + 1));
        float by1 = ampl_to_y(ampl1[i]);
        if (hasEvo) {
            float bx1 = bx - barW;
            dl->AddRectFilled(ImVec2(bx1, by1), ImVec2(bx1 + barW, plotY1), col1);
            float by2 = ampl_to_y(ampl2[i]);
            dl->AddRectFilled(ImVec2(bx, by2), ImVec2(bx + barW, plotY1), col2);
        } else {
            dl->AddRectFilled(ImVec2(bx - barW * 0.5f, by1),
                              ImVec2(bx + barW * 0.5f, plotY1), col1);
        }
    }

    // Scrubber overlay: when g_scrubberPos > 0 and we have envelope snapshots
    // for this node, render a third bar set at the lerped state at that
    // time fraction. Uses the same render-time math the synth applies.
    if (g_scrubberPos > 0.0f) {
        auto sIt = g_evoSnapshots.find(node->id);
        if (sIt != g_evoSnapshots.end() && hasEvo) {
            auto get_env = [&](const char* name) -> float {
                auto it = sIt->second.find(name);
                if (it == sIt->second.end() || it->second.empty()) return 0.0f;
                int idx = std::clamp(int(g_scrubberPos * float(int(it->second.size()) - 1)),
                                     0, int(it->second.size()) - 1);
                return it->second[idx];
            };
            float multE = std::clamp(get_env("multEnv"), 0.0f, 1.0f);
            float amplE = std::clamp(get_env("amplEnv"), 0.0f, 1.0f);
            float roE   = std::clamp(get_env("roEnv"),   0.0f, 1.0f);
            float ro_t  = ro1 + (ro2 - ro1) * roE;

            const ImU32 scrubCol = IM_COL32(255, 180, 60, 230);  // amber overlay

            // We need the ORIGINAL ampl (pre-rolloff) to recompose from
            // weights — but we only have post-rolloff arrays here. The cheap
            // workaround: undo rolloff with the per-column rolloff exponent
            // (which we know was applied above), then apply the lerped
            // rolloff with the lerped multiplier.
            for (int i = 0; i < N; ++i) {
                float pmult1 = mult1[i] > 0.0f ? mult1[i] : 1.0f;
                float pmult2 = mult2[i] > 0.0f ? mult2[i] : 1.0f;
                // Recover pre-rolloff weights.
                float w1 = ampl1[i] * std::pow(pmult1, ro1);
                float w2 = ampl2[i] * std::pow(pmult2, ro2);
                float wt = w1 + (w2 - w1) * amplE;
                float pmult_t = pmult1 + (pmult2 - pmult1) * multE;
                float roll_t = (ro_t == 0.0f || pmult_t <= 0.0f)
                             ? 1.0f : 1.0f / std::pow(pmult_t, ro_t);
                float a = wt * roll_t;
                float bx = idx_to_x(float(i + 1));
                float by = ampl_to_y(a);
                // Centered narrow bar over both _1 and _2 columns.
                float scrubW = std::max(1.0f, barW * 0.4f);
                dl->AddRectFilled(ImVec2(bx - scrubW * 0.5f, by),
                                  ImVec2(bx + scrubW * 0.5f, plotY1), scrubCol);
            }
        }
    }

    // X-axis ticks: every Nth label depending on N
    int step = (N <= 16) ? 1 : (N <= 40) ? 4 : (N <= 100) ? 10 : 20;
    for (int i = 1; i <= N; i += step) {
        float xt = idx_to_x(float(i));
        dl->AddLine(ImVec2(xt, plotY1), ImVec2(xt, plotY1 + 3), IM_COL32(100, 100, 100, 255));
        char lbl[8]; snprintf(lbl, sizeof(lbl), "%d", i);
        dl->AddText(ImVec2(xt - 3, plotY1 + 4), IM_COL32(120, 120, 120, 255), lbl);
    }

    // Y label
    char ylbl[16]; snprintf(ylbl, sizeof(ylbl), "%.2f", aMax);
    dl->AddText(ImVec2(x + 2, plotY0 - 2), IM_COL32(100, 100, 100, 255), ylbl);

    dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(80, 80, 80, 255));

    // Hover readout
    if (ImGui::IsWindowHovered()) {
        ImVec2 m = ImGui::GetMousePos();
        if (m.x >= plotX0 && m.x <= plotX1 && m.y >= plotY0 && m.y <= plotY1) {
            float t = (m.x - plotX0) / (plotX1 - plotX0);
            int i = std::clamp(int(std::round(t * float(N - 1))) + 1, 1, N);
            char lbl[96];
            if (hasEvo) snprintf(lbl, sizeof(lbl),
                                 "p%d  m1=%.2f a1=%.3f  m2=%.2f a2=%.3f",
                                 i, mult1[i-1], ampl1[i-1], mult2[i-1], ampl2[i-1]);
            else        snprintf(lbl, sizeof(lbl),
                                 "p%d  mult=%.2f  ampl=%.3f",
                                 i, mult1[i-1], ampl1[i-1]);
            ImVec2 sz = ImGui::CalcTextSize(lbl);
            float lblX = std::min(m.x + 8.0f, plotX1 - sz.x - 4.0f);
            float lblY = std::max(m.y - 16.0f, plotY0 + 2.0f);
            dl->AddRectFilled(ImVec2(lblX - 3, lblY - 1),
                              ImVec2(lblX + sz.x + 3, lblY + sz.y + 1),
                              IM_COL32(0, 0, 0, 200));
            dl->AddText(ImVec2(lblX, lblY), IM_COL32(255, 255, 200, 255), lbl);
        }
    }

    bool popped = false;
    if (showPopoutBtn) popped = draw_strip_popout_button(x, y, width, popoutBtnId);
    return popped;
}

// Draw a Formant / FormantSpectrum / FormantSequence as a gain-vs-frequency curve.
// Samples get_gain(f) at log-spaced frequencies across 20 Hz → ~Nyquist.
// fmt_prepare/fmt_next must have been called for the formant to have valid
// internal state — for a static node this happens at load time.
static bool draw_formant_strip(GraphNode* node, ImU32 color,
                               float x, float y, float width, float height,
                               bool showPopoutBtn, int popoutBtnId) {
    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(20, 20, 25, 255));

    const float marginL = WAVE_MARGIN_W;
    float plotX0 = x + marginL;
    float plotX1 = x + width - 4.0f;
    float plotY0 = y + 14.0f;
    float plotY1 = y + height - 14.0f;

    dl->AddText(ImVec2(x + 2, y + 2), IM_COL32(150, 150, 150, 255), node->label.c_str());

    auto* ifp = node->dspSource ? dynamic_cast<IFormant*>(node->dspSource.get()) : nullptr;
    if (!ifp) {
        dl->AddText(ImVec2(plotX0 + 4, plotY0 + 4), IM_COL32(140, 140, 140, 255),
                    "(node not initialized)");
        dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(80, 80, 80, 255));
        bool popped = false;
        if (showPopoutBtn) popped = draw_strip_popout_button(x, y, width, popoutBtnId);
        return popped;
    }

    const float fMin = 20.0f;
    const float fMax = 20000.0f;
    const float logFmin = std::log10(fMin);
    const float logFmax = std::log10(fMax);
    int samples = std::max(64, int(plotX1 - plotX0));

    // Snapshot-driven path: if this is a single Formant node and we have
    // captured envelope timelines, render up to three overlaid curves —
    // start (snapshot[0]), end (snapshot[N-1]), and (if scrubber is active)
    // a scrub overlay at the scrubbed time. For static-input Formants,
    // start and end coincide, so the overlay looks identical to the
    // single-curve path.
    Formant* fp = dynamic_cast<Formant*>(node->dspSource.get());
    auto sIt = g_evoSnapshots.find(node->id);
    bool haveSnaps = false;
    int snapN = 0;
    if (fp && sIt != g_evoSnapshots.end()) {
        for (auto& [_, vec] : sIt->second)
            if ((int)vec.size() > snapN) snapN = (int)vec.size();
        haveSnaps = (snapN > 1);
    }

    // Mirror Formant::get_gain math so we can compute gain from arbitrary
    // (cf, gn, wd, pw) tuples — without poking at the live formant's cache.
    auto formant_gain_at = [](float f, float cf, float gn, float wd, float pw) {
        float lo = cf - wd * 0.5f;
        float hi = cf + wd * 0.5f;
        if (f <= lo || f >= hi) return 0.0f;
        if (f < cf) return std::pow((f - lo) / std::max(cf - lo, 1e-6f), pw) * gn;
        return std::pow((hi - f) / std::max(hi - cf, 1e-6f), pw) * gn;
    };

    // Read pin value at snapshot index. Fall back to the formant's input
    // ValueSource current() — handles unsnapshotted (constant) pins.
    auto get_at = [&](const std::string& pin,
                      std::shared_ptr<ValueSource> fallback, int idx) {
        if (haveSnaps) {
            auto pIt = sIt->second.find(pin);
            if (pIt != sIt->second.end() && !pIt->second.empty()) {
                int i = std::clamp(idx, 0, int(pIt->second.size()) - 1);
                return pIt->second[i];
            }
        }
        return fallback ? fallback->current() : 0.0f;
    };

    auto sample_curve = [&](int snapIdx, std::vector<float>& out) {
        float cf = get_at("frequency", fp ? fp->get_frequency() : nullptr, snapIdx);
        float gn = get_at("gain",      fp ? fp->get_gain()      : nullptr, snapIdx);
        float wd = get_at("width",     fp ? fp->get_width()     : nullptr, snapIdx);
        float pw = get_at("power",     fp ? fp->get_power()     : nullptr, snapIdx);
        out.resize(samples);
        for (int i = 0; i < samples; ++i) {
            float t = float(i) / float(samples - 1);
            float f = std::pow(10.0f, logFmin + t * (logFmax - logFmin));
            out[i] = formant_gain_at(f, cf, gn, wd, pw);
        }
    };

    // gains = the curve used for hover readout. Fall back path populates it
    // from ifp->get_gain (works for FormantSpectrum/FormantSequence which
    // we don't snapshot-overlay).
    std::vector<float> gains(samples);
    std::vector<float> startCurve, endCurve, scrubCurve;
    int scrubIdx = haveSnaps
        ? std::clamp(int(g_scrubberPos * float(snapN - 1)), 0, snapN - 1)
        : 0;

    if (haveSnaps && fp) {
        sample_curve(0,         startCurve);
        sample_curve(snapN - 1, endCurve);
        if (g_scrubberPos > 0.0f) sample_curve(scrubIdx, scrubCurve);
        // Hover reads scrub curve when active, else end (most informative).
        gains = scrubCurve.empty() ? endCurve : scrubCurve;
    } else {
        // Fallback: single curve from cached formant state.
        ifp->fmt_next();
        for (int i = 0; i < samples; ++i) {
            float t = float(i) / float(samples - 1);
            float f = std::pow(10.0f, logFmin + t * (logFmax - logFmin));
            gains[i] = ifp->contains(f) ? ifp->get_gain(f) : 0.0f;
        }
    }

    float gMax = 0.0f;
    for (float v : gains)      gMax = std::max(gMax, v);
    for (float v : startCurve) gMax = std::max(gMax, v);
    for (float v : endCurve)   gMax = std::max(gMax, v);
    if (gMax < 1e-4f) gMax = 1.0f;

    auto freq_to_x = [&](float f) {
        float t = (std::log10(std::max(f, fMin)) - logFmin) / (logFmax - logFmin);
        return plotX0 + t * (plotX1 - plotX0);
    };
    auto gain_to_y = [&](float g) {
        float t = std::clamp(g / gMax, 0.0f, 1.0f);
        return plotY1 - t * (plotY1 - plotY0);
    };
    auto x_to_freq = [&](float xx) {
        float t = (xx - plotX0) / (plotX1 - plotX0);
        return std::pow(10.0f, logFmin + t * (logFmax - logFmin));
    };

    // Octave grid + labels
    static const float decMul[] = {1.0f, 2.0f, 5.0f};
    for (int dec = 1; dec <= 5; ++dec) {
        float base = std::pow(10.0f, float(dec));
        for (float m : decMul) {
            float f = base * m;
            if (f < fMin || f > fMax) continue;
            float xx = freq_to_x(f);
            dl->AddLine(ImVec2(xx, plotY0), ImVec2(xx, plotY1),
                        (m == 1.0f) ? IM_COL32(70, 70, 80, 255) : IM_COL32(50, 50, 55, 255));
            if (m == 1.0f) {
                char lbl[16];
                if (f >= 1000.0f) snprintf(lbl, sizeof(lbl), "%gk", f / 1000.0f);
                else              snprintf(lbl, sizeof(lbl), "%g", f);
                dl->AddText(ImVec2(xx + 2, plotY1 + 2), IM_COL32(120, 120, 120, 255), lbl);
            }
        }
    }
    // Baseline
    dl->AddLine(ImVec2(plotX0, plotY1), ImVec2(plotX1, plotY1), IM_COL32(80, 80, 80, 255));

    // Curve drawing helper. When `fillCol` != 0 the area below the line is
    // filled — used for the start/end "envelope of motion" curves; the scrub
    // overlay is drawn line-only so it stays crisp on top.
    auto draw_curve = [&](const std::vector<float>& c, ImU32 lineCol, ImU32 fillCol_, float thickness) {
        if (c.empty()) return;
        for (size_t i = 1; i < c.size(); ++i) {
            float x0 = plotX0 + float(i - 1);
            float x1 = plotX0 + float(i);
            float y0 = gain_to_y(c[i - 1]);
            float y1 = gain_to_y(c[i]);
            if (fillCol_ != 0u) {
                ImVec2 p0(x0, y0), p1(x1, y1), p2(x1, plotY1), p3(x0, plotY1);
                dl->AddQuadFilled(p0, p1, p2, p3, fillCol_);
            }
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), lineCol, thickness);
        }
    };

    if (haveSnaps && fp) {
        // Match Partials: light teal = start (snapshot[0]), dark teal = end
        // (snapshot[N-1]); for static-input Formants these coincide.
        ImU32 lightFill = IM_COL32(60, 200, 200, 36);
        ImU32 lightLine = IM_COL32(120, 220, 220, 220);
        ImU32 darkFill  = IM_COL32(60, 200, 200, 56);
        ImU32 darkLine  = IM_COL32(60, 160, 160, 230);
        draw_curve(startCurve, lightLine, lightFill, 1.3f);
        draw_curve(endCurve,   darkLine,  darkFill,  1.3f);
        if (!scrubCurve.empty()) {
            ImU32 scrubLine = IM_COL32(255, 180, 60, 240);  // amber overlay
            draw_curve(scrubCurve, scrubLine, 0u, 2.0f);
        }
    } else {
        // Fallback single-curve render (FormantSpectrum etc., or pre-render).
        ImU32 fillCol = (color & 0x00FFFFFF) | (40u << 24);
        draw_curve(gains, color, fillCol, 1.5f);
    }

    // Y label (peak gain)
    char ylbl[16]; snprintf(ylbl, sizeof(ylbl), "%.2f", gMax);
    dl->AddText(ImVec2(x + 2, plotY0 - 2), IM_COL32(100, 100, 100, 255), ylbl);

    dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height), IM_COL32(80, 80, 80, 255));

    // Hover readout
    if (ImGui::IsWindowHovered()) {
        ImVec2 m = ImGui::GetMousePos();
        if (m.x >= plotX0 && m.x <= plotX1 && m.y >= plotY0 && m.y <= plotY1) {
            float fAt = x_to_freq(m.x);
            int idx = std::clamp(int(((m.x - plotX0) / (plotX1 - plotX0)) * float(samples - 1)),
                                 0, samples - 1);
            float gAt = gains[idx];
            dl->AddLine(ImVec2(m.x, plotY0), ImVec2(m.x, plotY1), IM_COL32(180, 180, 180, 140));
            char lbl[64];
            if (fAt >= 1000.0f) snprintf(lbl, sizeof(lbl), "%.2f kHz   gain %.3f", fAt / 1000.0f, gAt);
            else                snprintf(lbl, sizeof(lbl), "%.1f Hz   gain %.3f",  fAt, gAt);
            ImVec2 sz = ImGui::CalcTextSize(lbl);
            float lblX = std::min(m.x + 8.0f, plotX1 - sz.x - 4.0f);
            float lblY = std::max(m.y - 16.0f, plotY0 + 2.0f);
            dl->AddRectFilled(ImVec2(lblX - 3, lblY - 1),
                              ImVec2(lblX + sz.x + 3, lblY + sz.y + 1),
                              IM_COL32(0, 0, 0, 200));
            dl->AddText(ImVec2(lblX, lblY), IM_COL32(255, 255, 200, 255), lbl);
        }
    }

    bool popped = false;
    if (showPopoutBtn) popped = draw_strip_popout_button(x, y, width, popoutBtnId);
    return popped;
}

// ===========================================================================
// Waveform window (dockable)
// ===========================================================================

static void draw_waveform_window() {
    ImGui::Begin("Waveforms", nullptr,
                 ImGuiWindowFlags_NoCollapse);

    float waveAreaW = ImGui::GetContentRegionAvail().x;
    float waveAreaH = ImGui::GetContentRegionAvail().y;

    // Zoom/scroll controls at top
    ImGui::Text("Zoom");
    ImGui::SameLine();
    ImGui::PushID("wz");
    if (ImGui::ArrowButton("##zdec", ImGuiDir_Left)) g_waveZoom = std::max(1, g_waveZoom / 2);
    ImGui::SameLine(0, 2);
    ImGui::Text("%dx", g_waveZoom);
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##zinc", ImGuiDir_Right)) g_waveZoom = std::min(4096, g_waveZoom * 2);
    ImGui::PopID();
    ImGui::SameLine();
    if (ImGui::SmallButton("Fit")) {
        if (g_waveformSamples > 0)
            g_waveZoom = std::max(1, g_waveformSamples / (int)waveAreaW);
        g_waveScrollPos = 0;
    }
    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::Text("Columns");
    ImGui::SameLine();
    spinner_int("wcol", &g_waveColumns, 1, 1, 4);
    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::Checkbox("Envelopes", &g_showEnvelopes);
    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();
    ImGui::Text("Scrub");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("##scrub", &g_scrubberPos, 0.0f, 1.0f, "%.2f");

    // Horizontal scrollbar
    int maxScroll = std::max(0, g_waveformSamples - (int)(waveAreaW) * g_waveZoom);
    g_waveScrollPos = std::clamp(g_waveScrollPos, 0, std::max(1, maxScroll));
    if (maxScroll > 0) {
        ImGui::PushItemWidth(waveAreaW);
        int scrollVal = g_waveScrollPos;
        if (ImGui::SliderInt("##hscroll", &scrollVal, 0, maxScroll, "")) {
            g_waveScrollPos = scrollVal;
        }
        ImGui::PopItemWidth();
    }

    // Collect all strips: output first, then per-node. Most strips are
    // time-domain waveforms; Partials and Formant nodes get specialized
    // visualizations (partial bars / formant gain curve) since their
    // waveformData has no useful audio-domain content.
    enum class StripKind { Wave, Partials, Formant };
    struct WaveStrip {
        std::string label;
        const float* buf;     // for Wave kind only
        int count;            // for Wave kind only
        ImU32 color;
        int nodeId;
        StripKind kind{StripKind::Wave};
    };
    std::vector<WaveStrip> strips;
    static const ImU32 audioColor    = IM_COL32(80, 220, 80, 255);   // bright green
    static const ImU32 envelopeColor = IM_COL32(220, 220, 60, 255);  // yellow
    static const ImU32 partialsColor = IM_COL32(60, 200, 200, 255);  // teal — matches spectrum
    static const ImU32 formantColor  = IM_COL32(60, 200, 200, 255);  // teal — matches spectrum

    strips.push_back({std::string("Output"),
                      g_outputWaveform.empty() ? nullptr : g_outputWaveform.data(),
                      g_waveformSamples, audioColor, -1, StripKind::Wave});

    auto& reg = SourceRegistry::instance();
    for (auto& n : s_nodes) {
        SourceCategory cat = reg.has(n.typeName) ? reg.get_category(n.typeName) : SourceCategory::Utility;
        bool isEnvelope = (cat == SourceCategory::Envelope);
        if (isEnvelope && !g_showEnvelopes) continue;

        if (is_partials_type(n.typeName)) {
            strips.push_back({n.label, nullptr, 0, partialsColor, n.id, StripKind::Partials});
        } else if (is_formant_type(n.typeName)) {
            strips.push_back({n.label, nullptr, 0, formantColor,  n.id, StripKind::Formant});
        } else {
            if (n.waveformData.empty()) continue;
            ImU32 col = isEnvelope ? envelopeColor : audioColor;
            strips.push_back({n.label, n.waveformData.data(), (int)n.waveformData.size(),
                              col, n.id, StripKind::Wave});
        }
    }

    // Calculate strip heights from remaining space (post-filter).
    float remainH = ImGui::GetContentRegionAvail().y;
    int totalStrips = (int)strips.size();
    int totalRows = std::max(1, (totalStrips + g_waveColumns - 1) / g_waveColumns);
    float stripH = std::max(25.0f, (remainH - totalRows * 2.0f) / float(totalRows));

    // Scrollable waveform area. NoScrollWithMouse so ImGui doesn't consume
    // wheel/trackpad events before our custom scroll handler can read them.
    ImGui::BeginChild("WaveScroll", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float yOff = 0.0f;

    // Layout strips in g_waveColumns columns
    int N = g_waveColumns;
    float gap = 4.0f;
    float colW = (waveAreaW - (N - 1) * gap) / float(N);

    for (int i = 0; i < (int)strips.size(); ++i) {
        int col = i % N;
        int row = i / N;
        float xPos = cursor.x + col * (colW + gap);
        float yPos = cursor.y + row * (stripH + 2.0f);
        bool popped = false;
        if (strips[i].kind == StripKind::Wave) {
            popped = draw_waveform(strips[i].label.c_str(), strips[i].buf, strips[i].count,
                                   strips[i].color, xPos, yPos, colW, stripH,
                                   g_waveZoom, g_waveScrollPos,
                                   /*showPopoutBtn*/ true, /*popoutBtnId*/ i);
        } else if (strips[i].kind == StripKind::Partials || strips[i].kind == StripKind::Formant) {
            // Find the node by id (linear search; node count is small).
            GraphNode* node = nullptr;
            for (auto& n : s_nodes) if (n.id == strips[i].nodeId) { node = &n; break; }
            if (node) {
                // Pop-out for these kinds isn't wired through draw_wave_popouts
                // (which assumes a time-domain buffer); disable the button
                // until Partials/Formant popouts are implemented.
                if (strips[i].kind == StripKind::Partials)
                    popped = draw_partials_strip(node, strips[i].color,
                                                 xPos, yPos, colW, stripH, false, i);
                else
                    popped = draw_formant_strip(node, strips[i].color,
                                                xPos, yPos, colW, stripH, false, i);
            }
        }
        if (popped) {
            WavePopout wp;
            wp.nodeId = strips[i].nodeId;
            wp.label  = strips[i].label;
            wp.zoom   = g_waveZoom;
            wp.scroll = g_waveScrollPos;
            g_wavePopouts.push_back(std::move(wp));
        }
    }

    int actualRows = ((int)strips.size() + N - 1) / N;
    yOff = actualRows * (stripH + 2.0f);
    ImGui::Dummy(ImVec2(waveAreaW, yOff));

    // Wheel: scroll horizontally (natural for horizontal data).
    // Ctrl+wheel: zoom (standard convention).
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyCtrl) {
                if (wheel > 0.0f && g_waveZoom > 1)
                    g_waveZoom = std::max(1, g_waveZoom / 2);
                else if (wheel < 0.0f && g_waveZoom < 4096)
                    g_waveZoom = std::min(4096, g_waveZoom * 2);
            } else {
                // Step = ~10% of visible range per notch; reverse direction so
                // wheel-up moves the view "up the timeline" (left) — matches DAW convention.
                int step = std::max(1, int(float(waveAreaW) * g_waveZoom * 0.1f));
                g_waveScrollPos -= (int)(wheel) * step;
                g_waveScrollPos = std::clamp(g_waveScrollPos, 0, std::max(1, maxScroll));
            }
        }
    }

    // Click-drag scroll (unchanged)
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float dx = ImGui::GetIO().MouseDelta.x;
        g_waveScrollPos -= (int)(dx * g_waveZoom);
        g_waveScrollPos = std::clamp(g_waveScrollPos, 0, std::max(1, maxScroll));
    }

    // Keyboard shortcuts — active when the waveform window is focused or
    // hovered. No mouse/trackpad required.
    //   Left/Right        : scroll by 10% of visible range
    //   Shift+Left/Right  : scroll by full visible range (page)
    //   Home / End        : jump to start/end
    //   + / =             : zoom in
    //   -                 : zoom out
    //   0                 : fit (same as Fit button)
    if (ImGui::IsWindowFocused() || ImGui::IsWindowHovered()) {
        int pageSamples = std::max(1, int(float(waveAreaW) * g_waveZoom));
        int step = std::max(1, pageSamples / 10);
        bool shift = ImGui::GetIO().KeyShift;
        int moveBy = shift ? pageSamples : step;

        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))  g_waveScrollPos -= moveBy;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) g_waveScrollPos += moveBy;
        if (ImGui::IsKeyPressed(ImGuiKey_Home, false))      g_waveScrollPos = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_End, false))       g_waveScrollPos = maxScroll;
        if (ImGui::IsKeyPressed(ImGuiKey_Equal, true) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, true)) {
            if (g_waveZoom > 1) g_waveZoom = std::max(1, g_waveZoom / 2);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus, true) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, true)) {
            if (g_waveZoom < 4096) g_waveZoom = std::min(4096, g_waveZoom * 2);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_0, false)) {
            if (g_waveformSamples > 0)
                g_waveZoom = std::max(1, g_waveformSamples / (int)waveAreaW);
            g_waveScrollPos = 0;
        }

        g_waveScrollPos = std::clamp(g_waveScrollPos, 0, std::max(1, maxScroll));
    }

    ImGui::EndChild();
    ImGui::End(); // Waveforms
}

// Draw each pop-out waveform as an independent ImGui window with its own
// zoom/scroll controls. Buffers are re-resolved from s_nodes each frame so
// popouts track live waveform updates after re-render.
static void draw_wave_popouts() {
    static const ImU32 audioColor = IM_COL32(80, 220, 80, 255);
    static const ImU32 envelopeColor = IM_COL32(220, 220, 60, 255);
    auto& reg = SourceRegistry::instance();

    for (size_t i = 0; i < g_wavePopouts.size(); ) {
        WavePopout& p = g_wavePopouts[i];
        if (!p.open) { g_wavePopouts.erase(g_wavePopouts.begin() + i); continue; }

        // Resolve current buffer + color.
        const float* buf = nullptr;
        int count = 0;
        ImU32 color = audioColor;
        if (p.nodeId == -1) {
            buf = g_outputWaveform.empty() ? nullptr : g_outputWaveform.data();
            count = g_waveformSamples;
        } else {
            for (auto& n : s_nodes) {
                if (n.id != p.nodeId) continue;
                if (!n.waveformData.empty()) {
                    buf = n.waveformData.data();
                    count = (int)n.waveformData.size();
                    SourceCategory cat = reg.has(n.typeName) ? reg.get_category(n.typeName) : SourceCategory::Utility;
                    if (cat == SourceCategory::Envelope) color = envelopeColor;
                }
                break;
            }
        }

        // Unique window id that survives label changes (###-suffix is the id).
        char title[128];
        snprintf(title, sizeof(title), "Waveform — %s###wavepop_%d_%d",
                 p.label.c_str(), p.nodeId, (int)i);

        ImGui::SetNextWindowSize(ImVec2(720, 260), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &p.open, ImGuiWindowFlags_None)) {
            ImGui::Text("Zoom");
            ImGui::SameLine();
            ImGui::PushID("pz");
            if (ImGui::ArrowButton("##zdec", ImGuiDir_Left))  p.zoom = std::max(1, p.zoom / 2);
            ImGui::SameLine(0, 2);
            ImGui::Text("%dx", p.zoom);
            ImGui::SameLine(0, 2);
            if (ImGui::ArrowButton("##zinc", ImGuiDir_Right)) p.zoom = std::min(4096, p.zoom * 2);
            ImGui::PopID();
            ImGui::SameLine();
            float innerW = ImGui::GetContentRegionAvail().x;
            if (ImGui::SmallButton("Fit")) {
                if (count > 0 && innerW > 0) p.zoom = std::max(1, count / (int)innerW);
                p.scroll = 0;
            }

            // Horizontal scroll
            float waveW = ImGui::GetContentRegionAvail().x;
            int maxScroll = std::max(0, count - (int)waveW * p.zoom);
            p.scroll = std::clamp(p.scroll, 0, std::max(1, maxScroll));
            if (maxScroll > 0) {
                ImGui::PushItemWidth(waveW);
                int sv = p.scroll;
                if (ImGui::SliderInt("##pop_hscroll", &sv, 0, maxScroll, "")) p.scroll = sv;
                ImGui::PopItemWidth();
            }

            float h = ImGui::GetContentRegionAvail().y;
            ImVec2 pos = ImGui::GetCursorScreenPos();
            draw_waveform(p.label.c_str(), buf, count, color,
                          pos.x, pos.y, waveW, h,
                          p.zoom, p.scroll,
                          /*showPopoutBtn*/ false);
            ImGui::Dummy(ImVec2(waveW, h));

            // Wheel: scroll. Ctrl+wheel: zoom.
            if (ImGui::IsWindowHovered()) {
                float wheel = ImGui::GetIO().MouseWheel;
                if (wheel != 0.0f) {
                    if (ImGui::GetIO().KeyCtrl) {
                        if (wheel > 0.0f && p.zoom > 1)
                            p.zoom = std::max(1, p.zoom / 2);
                        else if (wheel < 0.0f && p.zoom < 4096)
                            p.zoom = std::min(4096, p.zoom * 2);
                    } else {
                        int step = std::max(1, int(waveW * p.zoom * 0.1f));
                        p.scroll -= (int)(wheel) * step;
                        p.scroll = std::clamp(p.scroll, 0, std::max(1, maxScroll));
                    }
                }
            }
        }
        ImGui::End();
        ++i;
    }
}

// ===========================================================================
// Main
// ===========================================================================

// ---------------------------------------------------------------------------
// Crash logging — Windows GUI subsystem swallows stdout/stderr, so without
// these hooks a crash leaves no trace. We write to mforce_ui_crash.log in
// cwd: timestamp + exception code/address + symbolic stack trace.
// ---------------------------------------------------------------------------

static const char* CRASH_LOG_PATH = "mforce_ui_crash.log";

static void crash_log_timestamp(FILE* f) {
    time_t t = time(nullptr);
    struct tm tmv; localtime_s(&tmv, &t);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);
    fprintf(f, "[%s] ", ts);
}

static void crash_log_stack(FILE* f) {
    void* frames[40];
    USHORT count = CaptureStackBackTrace(0, 40, frames, NULL);

    HANDLE proc = GetCurrentProcess();
    static bool symInited = false;
    if (!symInited) {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(proc, NULL, TRUE);
        symInited = true;
    }

    char symBuf[sizeof(SYMBOL_INFO) + 256] = {0};
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;

    IMAGEHLP_LINE64 line; line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD lineDisp = 0;

    for (USHORT i = 0; i < count; ++i) {
        DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        DWORD64 disp = 0;
        const char* name = "???";
        if (SymFromAddr(proc, addr, &disp, sym)) name = sym->Name;
        if (SymGetLineFromAddr64(proc, addr, &lineDisp, &line)) {
            fprintf(f, "  #%-2d %s + 0x%llx  (%s:%lu)\n",
                    i, name, (unsigned long long)disp, line.FileName, line.LineNumber);
        } else {
            fprintf(f, "  #%-2d %s + 0x%llx  (0x%p)\n",
                    i, name, (unsigned long long)disp, frames[i]);
        }
    }
}

static void crash_log_write(const char* header) {
    FILE* f = fopen(CRASH_LOG_PATH, "a");
    if (!f) return;
    fprintf(f, "\n========================================\n");
    crash_log_timestamp(f);
    fprintf(f, "%s\n", header);
    crash_log_stack(f);
    fclose(f);
}

static LONG WINAPI seh_crash_filter(EXCEPTION_POINTERS* info) {
    FILE* f = fopen(CRASH_LOG_PATH, "a");
    if (!f) return EXCEPTION_EXECUTE_HANDLER;

    fprintf(f, "\n========================================\n");
    crash_log_timestamp(f);
    fprintf(f, "SEH crash\n");
    fprintf(f, "  exception code:    0x%08lx\n", info->ExceptionRecord->ExceptionCode);
    fprintf(f, "  exception address: 0x%p\n",    info->ExceptionRecord->ExceptionAddress);
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2)
    {
        ULONG_PTR rw   = info->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR addr = info->ExceptionRecord->ExceptionInformation[1];
        fprintf(f, "  access violation:  %s 0x%llx\n",
                rw == 0 ? "read of"  : rw == 1 ? "write to" :
                rw == 8 ? "exec at" : "?",
                (unsigned long long)addr);
    }

    // Symbolic faulting frame
    HANDLE proc = GetCurrentProcess();
    static bool symInited = false;
    if (!symInited) {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(proc, NULL, TRUE);
        symInited = true;
    }
    char symBuf[sizeof(SYMBOL_INFO) + 256] = {0};
    SYMBOL_INFO* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    DWORD64 disp = 0;
    if (SymFromAddr(proc, reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress),
                    &disp, sym))
    {
        fprintf(f, "  faulting symbol:   %s + 0x%llx\n", sym->Name, (unsigned long long)disp);
    }

    fprintf(f, "Stack:\n");
    crash_log_stack(f);
    fclose(f);

    return EXCEPTION_EXECUTE_HANDLER;
}

// Collapse/restore for the bottom pane — the dock node holding Waveforms/
// Spectrum/Keyboard (Matt 2026-08-29). A chevron overlays the pane's
// top-right corner: down collapses the pane to a sliver (tab bar stays
// visible, so the tabs and the restore chevron remain reachable), up
// restores the height it had when collapsed.
static float s_bottomPaneSavedH = 0.0f;
static void draw_bottom_pane_toggle() {
    ImGuiWindow* wf = ImGui::FindWindowByName("Waveforms");
    if (!wf || !wf->DockNode) return;   // undocked/floating: nothing to collapse
    ImGuiDockNode* dn = wf->DockNode;
    const float collapsedH = ImGui::GetFrameHeight() + 10.0f;
    const bool collapsed = dn->Size.y <= collapsedH + 6.0f;

    ImGui::SetNextWindowPos(ImVec2(dn->Pos.x + dn->Size.x - 40.0f, dn->Pos.y + 2.0f));
    ImGui::SetNextWindowBgAlpha(0.30f);
    ImGui::Begin("##bottomPaneToggle", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    if (ImGui::ArrowButton("##paneChevron", collapsed ? ImGuiDir_Up : ImGuiDir_Down)) {
        if (collapsed) {
            float h = (s_bottomPaneSavedH > collapsedH)
                    ? s_bottomPaneSavedH
                    : ImGui::GetIO().DisplaySize.y * 0.28f;
            ImGui::DockBuilderSetNodeSize(dn->ID, ImVec2(dn->Size.x, h));
        } else {
            s_bottomPaneSavedH = dn->Size.y;
            ImGui::DockBuilderSetNodeSize(dn->ID, ImVec2(dn->Size.x, collapsedH));
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(collapsed ? "Restore pane height" : "Collapse pane");
    ImGui::End();
}

// Engine-stamp guard (dsp backlog 3c) — detection lives in build_stamp.h so
// that tools/stamp_test can exercise it headlessly while a running mforce_ui
// holds this exe locked. Only the banner belongs here; it needs ImGui.
namespace stamp {

static void draw_banner() {
    if (!s_stale || s_dismissed) return;
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.45f, 0.08f, 0.08f, 0.96f));
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, 8.0f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    if (ImGui::Begin("##stalebanner", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav)) {
        ImGui::Text("This mforce_ui was built %s, but %s is newer.",
                    s_exeTime.c_str(), s_newestFile.c_str());
        ImGui::Text("Engine features added since then WILL APPEAR MISSING. Rebuild mforce_ui.");
        if (ImGui::Button("Dismiss")) s_dismissed = true;
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace stamp

int main(int argc, char** argv) {
    mforce::enable_flush_denormals();
    SetUnhandledExceptionFilter(seh_crash_filter);
    stamp::init();

    // mforce_ui links as a WIN32 subsystem app, so a printf from any of the
    // headless modes below goes nowhere when it is launched from a console.
    // Borrow the parent's console — but ONLY when stdout is not already a file
    // or a pipe, because reopening CONOUT$ would bypass the redirection the
    // caller asked for and the output would vanish again.
    if (argc >= 2 && argv[1][0] == '-') {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD  kind = (out && out != INVALID_HANDLE_VALUE) ? GetFileType(out)
                                                           : FILE_TYPE_UNKNOWN;
        if (kind != FILE_TYPE_DISK && kind != FILE_TYPE_PIPE &&
            AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* dummy;
            freopen_s(&dummy, "CONOUT$", "w", stdout);
            freopen_s(&dummy, "CONOUT$", "w", stderr);
        }
    }

    // Headless round-trip: load a patch and immediately re-save it, no GL
    // window. Exists to verify JSON preservation across the UI's load→save
    // path (dsp BACKLOG 3b — paramMap curve entries must survive). ImGui/ImNodes
    // contexts are created because load/save read/write node positions; no
    // frame or renderer is needed for that.
    // TEMP DEBUG: dump the curve-target options the Add-curve combos would list
    // for every node in a patch. Mirrors draw_curves_window's opts collection.
    if (argc >= 3 && std::string(argv[1]) == "--dump-curve-opts") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
        } catch (const std::exception& e) {
            fprintf(stderr, "load failed: %s\n", e.what());
            return 1;
        }
        for (auto& n : s_nodes) {
            if (is_special_ui_type(n.typeName) || n.label.empty()) continue;
            printf("node '%s' (%s): dsp=%s settingValues=%zu\n",
                   n.label.c_str(), n.typeName.c_str(),
                   n.dspSource ? n.dspSource->type_name() : "NULL",
                   n.settingValues.size());
            for (auto& pin : n.inputs) {
                if (pin.inputOnly || pin.kind != PinKind::Input) continue;
                if (pin.name.substr(0, 3) == "ch ") continue;
                if (is_pin_connected(pin.id)) { printf("    [pin skipped: connected] %s\n", pin.name.c_str()); continue; }
                printf("    pin  %s = %g%s\n", pin.name.c_str(), pin.defaultValue,
                       curve_exists_for("frequency", n.label + "." + pin.name) ? "  (has curve)" : "");
            }
            for (auto& [desc, val] : n.settingValues)
                printf("    cfg  %s = %g%s\n", desc.name, val,
                       curve_exists_for("frequency", n.label + "." + desc.name) ? "  (has curve)" : "");
        }
        return 0;
    }

    // Headless readout of the engine-stamp guard (dsp backlog 3c), so the
    // staleness logic is verifiable without launching the GUI.
    if (argc >= 2 && std::string(argv[1]) == "--stamp") {
        return stamp::print_report();
    }

    if (argc >= 4 && std::string(argv[1]) == "--roundtrip") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
            save_patch_graph(argv[3]);
        } catch (const std::exception& e) {
            fprintf(stderr, "roundtrip failed: %s\n", e.what());
            return 1;
        }
        printf("roundtrip ok: %s -> %s\n", argv[2], argv[3]);
        return 0;
    }

    // Headless rename: load, rename one node (label = id), save. Exercises
    // rename_node's validation + paramMap-target rewrite without a UI.
    if (argc >= 6 && std::string(argv[1]) == "--rename") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
            GraphNode* target = nullptr;
            for (auto& n : s_nodes) if (n.label == argv[3]) target = &n;
            if (!target) { fprintf(stderr, "rename: no node '%s'\n", argv[3]); return 1; }
            std::string err;
            if (!rename_node(*target, argv[4], err)) {
                fprintf(stderr, "rename refused: %s\n", err.c_str()); return 1;
            }
            save_patch_graph(argv[5]);
        } catch (const std::exception& e) {
            fprintf(stderr, "rename failed: %s\n", e.what());
            return 1;
        }
        printf("rename ok: %s %s->%s -> %s\n", argv[2], argv[3], argv[4], argv[5]);
        return 0;
    }

    // Headless conversion round-trip: exercises the Edit-menu conversion.
    // Patch input → convert Patch→Node→Patch (stash-restore path); node
    // input → convert Node→Patch once (instrument-synthesis heuristic).
    // Either way the result is saved as a patch for inspection.
    if (argc >= 4 && std::string(argv[1]) == "--convert-roundtrip") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
            if (s_graphMode == GraphMode::PatchGraph) {
                convert_patch_to_node_graph();
                convert_node_to_patch_graph();
            } else {
                convert_node_to_patch_graph();
            }
            save_patch_graph(argv[3]);
        } catch (const std::exception& e) {
            fprintf(stderr, "convert-roundtrip failed: %s\n", e.what());
            return 1;
        }
        printf("convert-roundtrip ok: %s -> %s\n", argv[2], argv[3]);
        return 0;
    }

    // Headless playback dump: write the exact audio the UI's playback paths
    // would produce for a patch, for numeric comparison against the CLI
    // render of the same file (UI-vs-CLI sound-mismatch debugging).
    //   --dump-playback <patch.json> <out.wav> [--note N] [--vel V] [--dur D] [--keyboard]
    // Default note/vel/dur are the transport values after load — i.e. the
    // patch's score (apply_score_defaults). Without --keyboard this runs the
    // Generate path (render_output_authoritative + the audio callback's
    // soft_clip); with --keyboard it runs the live-keyboard streaming path
    // (prepare_voice + per-sample pull at voice gain + soft_clip).
    if (argc >= 4 && std::string(argv[1]) == "--dump-playback") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);

            bool keyboardPath = false;
            float noteNum = parse_note_input(g_transport.noteStr);
            float vel = g_transport.velocity;
            float dur = g_transport.duration;
            for (int i = 4; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--keyboard") keyboardPath = true;
                else if (a == "--note" && i + 1 < argc) noteNum = std::stof(argv[++i]);
                else if (a == "--vel"  && i + 1 < argc) vel = std::stof(argv[++i]);
                else if (a == "--dur"  && i + 1 < argc) dur = std::stof(argv[++i]);
            }

            std::vector<float> mono;
            if (keyboardPath) {
                // Live-keyboard path: streaming voice, no Instrument::render
                // (so no instrument-level volume/peak-guard); the audio
                // callback applies gain per sample and one soft_clip.
                auto ip = load_instrument_patch(get_playback_patch_path());
                auto* pitched = ip.instrument.get();
                if (!pitched) throw std::runtime_error("not a PitchedInstrument");
                auto sv = pitched->prepare_voice(noteNum, vel, dur);
                mono.resize(sv.durSamples);
                for (int i = 0; i < sv.durSamples; ++i) {
                    if (sv.performSource) sv.performSource->tick();  // P3 clock
                    mono[i] = soft_clip(sv.source->next() * sv.gain);
                    for (auto& a : sv.advanceList) a->next();  // loop tails
                }
            } else {
                // Generate path: authoritative offline render into
                // g_outputWaveform, then the buffer-playback soft_clip the
                // audio callback would apply when streaming it.
                if (!render_output_authoritative(noteNum, vel, dur))
                    throw std::runtime_error(g_transport.statusMsg);
                mono.assign(g_outputWaveform.begin(), g_outputWaveform.end());
                for (auto& s : mono) s = soft_clip(s);
            }

            // Mono → both channels at unity, exactly like the audio callback.
            std::vector<float> stereo(mono.size() * 2);
            for (size_t i = 0; i < mono.size(); ++i) {
                stereo[i * 2]     = mono[i];
                stereo[i * 2 + 1] = mono[i];
            }
            if (!write_wav_16le_stereo(argv[3], AUDIO_SAMPLE_RATE, stereo))
                throw std::runtime_error(std::string("wav write failed: ") + argv[3]);

            float peak = 0.0f; double rms = 0.0;
            for (float s : mono) { peak = std::max(peak, std::fabs(s)); rms += double(s) * s; }
            rms = std::sqrt(rms / std::max<size_t>(1, mono.size()));
            printf("dump-playback ok: %s -> %s\n", argv[2], argv[3]);
            printf("  path=%s note=%g vel=%g dur=%g frames=%zu peak=%g rms=%g\n",
                   keyboardPath ? "keyboard" : "generate",
                   noteNum, vel, dur, mono.size(), peak, rms);
        } catch (const std::exception& e) {
            fprintf(stderr, "dump-playback failed: %s\n", e.what());
            return 1;
        }
        return 0;
    }

    // Headless layout dump: load a patch (paramMap conversion + the
    // synthesized-node placement pass included) and print every node's
    // resolved canvas position, its group (if any), and top-level
    // visibility. Exists because the placement pass could only ever be
    // "verified by construction" — this makes what the user will actually
    // see checkable from a terminal.
    //   --dump-layout <patch.json>
    if (argc >= 3 && std::string(argv[1]) == "--dump-layout") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
            for (auto& n : s_nodes) {
                NodeGroup* g = group_of(n.label);
                printf("node %-24s type %-18s pos %8.1f %8.1f %s%s%s\n",
                       n.label.c_str(), n.typeName.c_str(),
                       n.gridPos.x, n.gridPos.y,
                       n.gridPosKnown ? "" : "UNPLACED ",
                       g ? "group=" : "top-level",
                       g ? g->name.c_str() : "");
            }
            for (auto& g : s_groups)
                printf("group %-23s pos %8.1f %8.1f members %d\n",
                       g.name.c_str(), g.pos.x, g.pos.y, (int)g.members.size());
        } catch (const std::exception& e) {
            fprintf(stderr, "dump-layout failed: %s\n", e.what());
            return 1;
        }
        return 0;
    }

    // Headless continuous-stream dump: run the exact play_continuous +
    // audio_callback stream mixdown offline for N seconds and report
    // amplitude (and pan taps) over time. Exists to verify streaming is
    // genuinely continuous (dsp run 24 issue 1 — streams decayed to silence
    // at the old fixed 30 s prepare horizon). Node graphs use the stereo
    // channel/pan mixdown; patch graphs use the mono output stream.
    //   --dump-stream <patch.json> <out.wav> [--secs N] [--vel V]
    if (argc >= 4 && std::string(argv[1]) == "--dump-stream") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
            int secs = 40; float vel = 0.5f;
            for (int i = 4; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "--secs" && i + 1 < argc) secs = std::stoi(argv[++i]);
                else if (a == "--vel" && i + 1 < argc) vel = std::stof(argv[++i]);
            }
            play_continuous(vel);
            if (!g_streamSource && g_streamChannels.empty())
                throw std::runtime_error(std::string("stream did not start: ") +
                                         g_transport.statusMsg);

            constexpr float PI = 3.14159265358979323846f;
            std::vector<float> stereo(size_t(secs) * AUDIO_SAMPLE_RATE * 2);
            for (int sec = 0; sec < secs; ++sec) {
                double rmsL = 0.0, rmsR = 0.0, panSum = 0.0;
                float panMin = 1e9f, panMax = -1e9f;
                bool havePan = false;
                for (int i = 0; i < AUDIO_SAMPLE_RATE; ++i) {
                    float sL = 0.0f, sR = 0.0f;
                    if (g_streamSource) {
                        float m = g_streamSource->next() * g_streamVelocity;
                        sL = m; sR = m;
                    }
                    float gl = 1.0f, gr = 1.0f;
                    if (!g_streamChannels.empty()) {
                        gl = g_streamGainL ? g_streamGainL->next() : 1.0f;
                        gr = g_streamGainR ? g_streamGainR->next() : 1.0f;
                    }
                    for (auto& ch : g_streamChannels) {
                        float v = ch.source->next() * (ch.volume ? ch.volume->next() : 1.0f);
                        float p = ch.pan ? ch.pan->next() : 0.0f;
                        if (ch.pan) {
                            havePan = true;
                            panMin = std::min(panMin, p);
                            panMax = std::max(panMax, p);
                            panSum += p;
                        }
                        p = std::clamp(p, -1.0f, 1.0f);
                        float t = (p + 1.0f) * 0.5f;
                        sL += v * std::cos(t * 0.5f * PI) * gl * g_streamVelocity;
                        sR += v * std::sin(t * 0.5f * PI) * gr * g_streamVelocity;
                    }
                    sL = soft_clip(sL);
                    sR = soft_clip(sR);
                    size_t idx = (size_t(sec) * AUDIO_SAMPLE_RATE + i) * 2;
                    stereo[idx] = sL;
                    stereo[idx + 1] = sR;
                    rmsL += double(sL) * sL;
                    rmsR += double(sR) * sR;
                }
                rmsL = std::sqrt(rmsL / AUDIO_SAMPLE_RATE);
                rmsR = std::sqrt(rmsR / AUDIO_SAMPLE_RATE);
                if (havePan)
                    printf("t=%3d rmsL=%.4f rmsR=%.4f pan[min=%+.3f max=%+.3f mean=%+.3f]\n",
                           sec, rmsL, rmsR, panMin, panMax,
                           panSum / double(AUDIO_SAMPLE_RATE));
                else
                    printf("t=%3d rmsL=%.4f rmsR=%.4f\n", sec, rmsL, rmsR);
            }
            stop_playback();
            if (!write_wav_16le_stereo(argv[3], AUDIO_SAMPLE_RATE, stereo))
                throw std::runtime_error(std::string("wav write failed: ") + argv[3]);
            printf("dump-stream ok: %s -> %s\n", argv[2], argv[3]);
        } catch (const std::exception& e) {
            fprintf(stderr, "dump-stream failed: %s\n", e.what());
            return 1;
        }
        return 0;
    }

    try {
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "MForce - Patch Editor", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    // Anchor to primary monitor's work area so the window can't land
    // off-screen on multi-monitor setups where (0,0) might not be visible.
    {
        GLFWmonitor* mon = glfwGetPrimaryMonitor();
        int mx = 0, my = 0, mw = 0, mh = 0;
        if (mon) glfwGetMonitorWorkarea(mon, &mx, &my, &mw, &mh);
        glfwSetWindowPos(window, mx + 100, my + 60);
    }

    // Defer the actual close so the main loop can run a dirty check and
    // pop a save-prompt modal when there are unsaved edits. Cancel the
    // GLFW close flag here; main loop sets it (or doesn't) after the user
    // answers the prompt.
    glfwSetWindowCloseCallback(window, [](GLFWwindow* w) {
        s_closeRequested = true;
        glfwSetWindowShouldClose(w, GLFW_FALSE);
    });

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImNodes::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // Single-click on a Drag widget enters text-input if the mouse doesn't
    // move past a small threshold; click+drag still scrubs as before.
    io.ConfigDragClickToInputText = true;
    // ImGuiConfigFlags_ViewportsEnable was causing the main window to lose
    // its native title bar on this setup, locking Matt into whatever
    // position/size state it happened to be in. Trade-off: pop-out waveform
    // windows stay contained within the main window, they don't become
    // free-floating OS windows.
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.Fonts->AddFontFromFileTTF("engine/third_party/imgui/misc/fonts/Roboto-Medium.ttf", 15.0f);

    ImGui::StyleColorsDark();
    ImGui::GetStyle().AntiAliasedLines = false;
    ImGui::GetStyle().AntiAliasedLinesUseTex = false;
    ImGui::GetStyle().WindowMenuButtonPosition = ImGuiDir_None;  // hide collapse triangles

    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8, 8);
    style.PinCircleRadius = 4.0f;
    style.LinkThickness = 2.5f;
    style.Flags |= ImNodesStyleFlags_GridLines;

    // Canvas pan with Alt + left-drag (default is middle-mouse-drag, which
    // is awkward without a three-button mouse).
    ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &ImGui::GetIO().KeyAlt;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Init audio
    if (!init_audio()) {
        // Non-fatal — UI works without audio
    }

    // MIDI input: auto-open the first device if one is plugged in.
    init_midi();

    // Initialize the source registry before creating any nodes
    register_all_sources();

    // Load the recent-files list before any patch-load path runs.
    recents_load();

    // Load persistent UI settings (curated-patches folder, etc.).
    settings_load();

    // Start with a Patch Graph — then, if a patch path was passed on the
    // command line, load it so the user can launch the UI with a patch in
    // one step (e.g. `mforce_ui.exe patches/MPXTest3.json`). Status shows
    // in the transport panel whether the load succeeded or failed.
    new_graph(GraphMode::PatchGraph);
    if (argc >= 2) {
        std::string patchArg = argv[1];
        if (!std::filesystem::exists(patchArg)) {
            char buf[512];
            snprintf(buf, sizeof(buf), "Patch not found: %s", patchArg.c_str());
            transport_set_status(buf, true);
        } else {
            try {
                load_graph_from_path(patchArg);
                recents_push(patchArg);
                // Don't clobber a warning the load itself put up
                // (e.g. unknown node types).
                if (!g_transport.statusIsError) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "Loaded: %s", patchArg.c_str());
                    transport_set_status(buf, false);
                }
            } catch (const std::exception& e) {
                char buf[512];
                snprintf(buf, sizeof(buf), "Failed to load %s: %s",
                         patchArg.c_str(), e.what());
                transport_set_status(buf, true);
            }
        }
    }
    // (s_needsLayout is set by new_graph for the boot default; load clears it
    // when restoring saved positions.)
    bool s_dockLayoutInitialized = false;

    while (true) {
        glfwPollEvents();

        // Close handling — independent of GLFW's close-callback ordering.
        // If anything has flagged a close intent (window X via GLFW's
        // built-in shouldClose, taskbar X via our callback, or File→Quit
        // via s_closeRequested), check dirty state. Dirty + no modal up:
        // cancel the close, show the prompt. Clean + no modal up: bail.
        // Track focus transitions: regaining focus after >2 s away requests a
        // proactive audio-stream restart (zombie-stream guard, see
        // g_audioRestartRequest).
        {
            static bool wasFocused = true;
            static double unfocusedAt = 0.0;
            bool focused = glfwGetWindowAttrib(window, GLFW_FOCUSED) != 0;
            if (wasFocused && !focused) unfocusedAt = glfwGetTime();
            if (!wasFocused && focused && glfwGetTime() - unfocusedAt > 2.0)
                g_audioRestartRequest.store(true);
            wasFocused = focused;
        }

        bool wantClose = glfwWindowShouldClose(window) || s_closeRequested;
        if (wantClose && !s_showCloseConfirm) {
            if (s_closeApproved || !s_graphDirty) {
                break;  // clean / user already approved — exit
            }
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            s_closeRequested = false;
            s_showCloseConfirm = true;
        }

        // Generate pending → draw "Generating..." with cleared waveform this
        // frame; the blocking render then runs after glfwSwapBuffers so the
        // user sees the state change before the UI freezes.
        if (s_genState == 1) {
            s_genState = 2;
            buffer_playback_detach();  // it points into this vector (3k)
            g_outputWaveform.clear();
            g_waveformSamples = 0;
            for (auto& n : s_nodes) n.waveformData.clear();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Layout nodes — only when no saved positions were restored.
        // Triggered by: boot with no patch loaded, New menu action, or a
        // patch with no `ui.positions` block. Saved patches with positions
        // bypass this so reload preserves where you placed things.
        if (s_needsLayout) {
            float x = 50, y = 80;
            for (int i = 0; i < (int)s_nodes.size(); ++i) {
                ImNodes::SetNodeScreenSpacePos(s_nodes[i].id, ImVec2(x, y));
                x += 220;
                if (x > 900) { x = 50; y += 250; }
            }
            s_needsLayout = false;
        }

        // Update GLFW window title
        const char* modeLabel = s_graphMode == GraphMode::PatchGraph ? "Patch Graph" : "Node Graph";
        char titleBuf[64];
        if (s_currentFilePath.empty())
            snprintf(titleBuf, sizeof(titleBuf), "MForce - %s (unsaved)", modeLabel);
        else {
            const char* fname = s_currentFilePath.c_str();
            const char* slash = strrchr(fname, '/');
            const char* bslash = strrchr(fname, '\\');
            if (bslash && (!slash || bslash > slash)) slash = bslash;
            if (slash) fname = slash + 1;
            snprintf(titleBuf, sizeof(titleBuf), "MForce - %s - %s", fname, modeLabel);
        }
        // Build stamp + staleness marker (dsp backlog 3c) — appended rather
        // than folded into titleBuf so the 64-byte buffer above is untouched.
        std::string fullTitle = std::string(titleBuf) + stamp::title_suffix();
        glfwSetWindowTitle(window, fullTitle.c_str());

        // =================================================================
        // Master frame window (fullscreen, contains menu bar + DockSpace)
        // =================================================================
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("MForce", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoBackground);

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::BeginMenu("New")) {
                    if (ImGui::MenuItem("Patch Graph")) {
                        new_graph(GraphMode::PatchGraph);
                        s_needsLayout = true;
                    }
                    if (ImGui::MenuItem("Node Graph")) {
                        new_graph(GraphMode::NodeGraph);
                        s_needsLayout = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Open", "Ctrl+O")) {
                    load_graph();
                }
                if (ImGui::BeginMenu("Open Recent", !g_recentFiles.empty())) {
                    std::string toLoad;  // defer to avoid mutating list mid-iter
                    for (const auto& path : g_recentFiles) {
                        // Show just the filename; full path as tooltip on hover.
                        const char* slash = strrchr(path.c_str(), '/');
                        const char* bs    = strrchr(path.c_str(), '\\');
                        const char* fname = (bs && (!slash || bs > slash)) ? bs + 1
                                          : slash ? slash + 1 : path.c_str();
                        // ImGui uses the label as the widget ID, so two recents
                        // with the same basename would collide. Append the full
                        // path after "##" — ImGui treats it as part of the ID
                        // but doesn't display it.
                        std::string label = std::string(fname) + "##" + path;
                        if (ImGui::MenuItem(label.c_str())) toLoad = path;
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear")) {
                        g_recentFiles.clear();
                        recents_save();
                    }
                    ImGui::EndMenu();
                    if (!toLoad.empty()) {
                        if (std::filesystem::exists(toLoad)) {
                            try {
                                load_graph_from_path(toLoad);
                                recents_push(toLoad);
                            } catch (const std::exception& e) {
                                char buf[512];
                                snprintf(buf, sizeof(buf),
                                         "Failed to load %s: %s", toLoad.c_str(), e.what());
                                transport_set_status(buf, true);
                            }
                        } else {
                            // File no longer exists — remove from recents.
                            auto it = std::find(g_recentFiles.begin(), g_recentFiles.end(), toLoad);
                            if (it != g_recentFiles.end()) {
                                g_recentFiles.erase(it);
                                recents_save();
                            }
                            char buf[512];
                            snprintf(buf, sizeof(buf), "File not found: %s", toLoad.c_str());
                            transport_set_status(buf, true);
                        }
                    }
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    save_graph();
                }
                if (ImGui::MenuItem("Save As...")) {
                    save_graph_as();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Audition...")) {
                    g_audition.open = true;
                    audition_refresh_target_list();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    // Route through the dirty-check path (same as the OS X / hover-X).
                    s_closeRequested = true;
                }
                ImGui::EndMenu();
            }

            // Edit menu: node clipboard + graph-mode conversion. The menu
            // items act on the imnodes selection (which persists across
            // frames), so they work regardless of which window has focus —
            // the Ctrl-key shortcuts live in the Node Editor block and are
            // gated on its focus instead.
            if (ImGui::BeginMenu("Edit")) {
                int numSel = ImNodes::NumSelectedNodes();
                if (ImGui::MenuItem("Cut", "Ctrl+X", false, numSel > 0))
                    clipboard_cut();
                if (ImGui::MenuItem("Copy", "Ctrl+C", false, numSel > 0))
                    clipboard_copy();
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, clipboard_has_content()))
                    clipboard_paste();
                ImGui::Separator();
                if (ImGui::MenuItem("Parameter mapping...", nullptr, false,
                                    s_graphMode == GraphMode::PatchGraph))
                    s_mappingsOpen = true;
                ImGui::Separator();
                // Label flips to the "other" type of the loaded graph.
                const char* convLabel = (s_graphMode == GraphMode::NodeGraph)
                    ? "Convert to Patch graph" : "Convert to Node graph";
                if (ImGui::MenuItem(convLabel))
                    convert_graph_mode();
                ImGui::Separator();
                // Manual escape hatch for Bluetooth endpoints: BT profile
                // transitions replace the endpoint entirely, and a stream
                // opened mid-transition binds silently. Automatic layers
                // (watchdog, refocus restart) can't time that; the user can.
                if (ImGui::MenuItem("Restart audio", "Ctrl+Shift+A"))
                    g_audioRestartRequest.store(true);
                ImGui::EndMenu();
            }

            // Play/Stop in menu bar (wired through transport state). Shown in
            // both graph modes — node graphs stream the Mixer live (Play and
            // Stream are equivalent there; transport_play routes to
            // play_continuous when there's no instrument).
            {
                ImGui::Separator();
                bool isPlaying = is_playing();
                if (!isPlaying) {
                    if (ImGui::MenuItem("Play", "Space"))
                        transport_play();
                    if (ImGui::MenuItem("Stream", "S"))
                        play_continuous(g_transport.velocity);
                } else {
                    if (ImGui::MenuItem("Stop", "Space"))
                        stop_playback();
                }
            }

            ImGui::EndMenuBar();
        }

        // DockSpace fills the rest of the master window
        ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
        ImGui::DockSpace(dockspaceId);

        // Set up initial dock layout on first run
        if (!s_dockLayoutInitialized) {
            s_dockLayoutInitialized = true;

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetIO().DisplaySize);

            // Split: right side for properties (22%)
            ImGuiID dockRight;
            ImGuiID dockRemaining;
            ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Right, 0.22f, &dockRight, &dockRemaining);

            // Split remaining: bottom for waveforms + keyboard (28%)
            ImGuiID dockBottom;
            ImGuiID dockCenterArea;
            ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Down, 0.28f, &dockBottom, &dockCenterArea);

            // Split center area: transport at top (25%)
            ImGuiID dockTransport;
            ImGuiID dockCenter;
            ImGui::DockBuilderSplitNode(dockCenterArea, ImGuiDir_Up, 0.25f, &dockTransport, &dockCenter);

            // Bottom area holds Waveforms + Keyboard as tabs (Waveforms selected first).
            ImGui::DockBuilderDockWindow("Transport", dockTransport);
            ImGui::DockBuilderDockWindow("Node Editor", dockCenter);
            ImGui::DockBuilderDockWindow("Properties", dockRight);
            ImGui::DockBuilderDockWindow("Waveforms", dockBottom);  // docked first → selected tab
            ImGui::DockBuilderDockWindow("Spectrum",  dockBottom);
            ImGui::DockBuilderDockWindow("Keyboard",  dockBottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }

        draw_bottom_pane_toggle();

        // Global keyboard shortcuts (work regardless of focused window)
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            save_graph();
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
            load_graph();
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput) {
            if (is_playing())
                stop_playback();
            else
                transport_play();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) {
            if (is_playing())
                stop_playback();
            else
                play_continuous(g_transport.velocity);
        }

        ImGui::End(); // MForce master window

        // =================================================================
        // Node Editor window
        // =================================================================
        ImGui::Begin("Node Editor", nullptr,
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse);

        // Focus gate for the editor's destructive/clipboard shortcuts —
        // without it, Delete (and Ctrl+X/C/V) fired here would also act on
        // the node selection while e.g. the Audition window has focus.
        bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        // Breadcrumb bar (spec §3): PatchName ▸ Group ▸ ... with the Listen
        // toggle right-aligned. Only drawn when groups exist or we're inside.
        if (!s_groups.empty() || !s_groupPath.empty()) {
            std::string patchName = s_currentFilePath.empty() ? "Patch"
                : std::filesystem::path(s_currentFilePath).stem().string();
            if (ImGui::SmallButton(patchName.c_str()))
                s_groupPath.clear();
            for (size_t i = 0; i < s_groupPath.size(); ++i) {
                ImGui::SameLine();
                ImGui::TextUnformatted(">");
                ImGui::SameLine();
                char lbl[96];
                snprintf(lbl, sizeof(lbl), "%s##bc%d", s_groupPath[i].c_str(), (int)i);
                if (ImGui::SmallButton(lbl))
                    s_groupPath.resize(i + 1);
            }
            // Listen toggle: Patch | Group, active side in connection-green,
            // in-context tap semantics (task: chunk-3 §Listen).
            if (!s_groupPath.empty()) {
                NodeGroup* cur = group_by_name(s_groupPath.back());
                GraphNode* gOut = cur ? group_output_node(*cur) : nullptr;
                if (gOut) {
                    bool listenGroup = s_groupListen.count(cur->name)
                        ? s_groupListen[cur->name] : true;
                    // Manual taps elsewhere override the toggle display.
                    if (s_listenTapNode >= 0 && s_listenTapNode != gOut->id)
                        listenGroup = false;
                    float w = ImGui::CalcTextSize("Listen:  Patch | Group").x + 40.0f;
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x - w);
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Listen:");
                    ImGui::SameLine();
                    ImVec4 on(0.5f, 0.8f, 0.5f, 1), off(0.5f, 0.5f, 0.5f, 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, listenGroup ? off : on);
                    if (ImGui::SmallButton("Patch")) {
                        s_groupListen[cur->name] = false;
                        s_listenTapNode = -1;
                    }
                    ImGui::PopStyleColor();
                    ImGui::SameLine(); ImGui::TextUnformatted("|"); ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, listenGroup ? on : off);
                    if (ImGui::SmallButton("Group")) {
                        s_groupListen[cur->name] = true;
                        s_listenTapNode = gOut->id;
                    }
                    ImGui::PopStyleColor();
                }
            }
            ImGui::Separator();
        }

        // Drill-transition camera (Matt 2026-08-20: drilling in/out could
        // land on a blank pane, then F-and-hunt). Every path change funnels
        // through s_groupPath, so one detector covers double-click, context
        // Open, breadcrumb and ungroup. Drill-IN frames the new level's
        // content (top-left + 80px margin); drill-OUT restores the exact pan
        // you had at that level when you left it — panStack[d] = pan at
        // depth d. Breadcrumb-only moves are always prefix moves, so the
        // two cases cover everything.
        {
            static std::vector<std::string> prevPath;
            static std::vector<ImVec2> panStack;
            if (prevPath != s_groupPath) {
                auto frame_level = [&]() {
                    float minX = 1e9f, minY = 1e9f;
                    bool any = false;
                    for (auto& n : s_nodes)
                        if (visible_at_path(n.label) && n.gridPosKnown) {
                            minX = std::min(minX, n.gridPos.x);
                            minY = std::min(minY, n.gridPos.y);
                            any = true;
                        }
                    for (auto& g : s_groups)
                        if (visible_at_path(g.name)) {
                            minX = std::min(minX, g.pos.x);
                            minY = std::min(minY, g.pos.y);
                            any = true;
                        }
                    if (any)
                        ImNodes::EditorContextResetPanning(
                            ImVec2(80.0f - minX, 80.0f - minY));
                };
                const bool deeper =
                    s_groupPath.size() > prevPath.size() &&
                    std::equal(prevPath.begin(), prevPath.end(),
                               s_groupPath.begin());
                if (deeper) {
                    // Remember where we were at the departed depth.
                    panStack.resize(prevPath.size() + 1,
                                    ImNodes::EditorContextGetPanning());
                    panStack[prevPath.size()] =
                        ImNodes::EditorContextGetPanning();
                    frame_level();
                } else if (s_groupPath.size() < panStack.size()) {
                    ImNodes::EditorContextResetPanning(
                        panStack[s_groupPath.size()]);
                    panStack.resize(s_groupPath.size());
                } else {
                    frame_level();
                }
                // Listen tap follows the drill level (Matt 2026-08-29):
                // backing out retargets the tap the same way drilling in
                // set it — the new level's group face (per-group Listen
                // memory, default on), or no tap at the top level. Drill-IN
                // already handles itself at the double-click site.
                if (!deeper) {
                    if (s_groupPath.empty()) {
                        s_listenTapNode = -1;
                    } else if (NodeGroup* cur = group_by_name(s_groupPath.back())) {
                        bool listenGroup = s_groupListen.count(cur->name)
                            ? s_groupListen[cur->name] : true;
                        GraphNode* gOut = group_output_node(*cur);
                        s_listenTapNode = (listenGroup && gOut) ? gOut->id : -1;
                    }
                }
                prevPath = s_groupPath;
            }
        }

        ImNodes::BeginNodeEditor();

        s_groupProj = GroupProjection{};
        for (auto& node : s_nodes) {
            bool vis = visible_at_path(node.label);
            if (vis) {
                // Returning from hiding: imnodes forgot this node's origin
                // (pool entry destroyed) — restore the app-tracked one.
                if (!node.wasVisible && node.gridPosKnown)
                    ImNodes::SetNodeGridSpacePos(node.id, node.gridPos);
                draw_node(node);
                node.gridPos = ImNodes::GetNodeGridSpacePos(node.id);
                node.gridPosKnown = true;
            }
            node.wasVisible = vis;
        }
        for (auto& g : s_groups) {
            if (visible_at_path(g.name)) draw_group_node(g);
            else g.posApplied = false;  // re-apply g.pos when it reappears
        }

        // Links: both endpoints projected to this level (own pin when the
        // node is visible, the collapsing group's pin when it isn't). Links
        // with an unrepresentable endpoint are implied by group interfaces
        // and not drawn. The link keeps its REAL id either way, so the
        // existing destroy handler works untouched.
        for (auto& link : s_links) {
            Pin* sp = find_pin(link.startPinId);
            if (!sp) continue;
            bool startIsSource = sp->kind == PinKind::Output;
            int a = project_pin(link.startPinId, startIsSource);
            int b = project_pin(link.endPinId, !startIsSource);
            if (a >= 0 && b >= 0 && a != b) {
                // Tap wires (previous-sample feedback reads) draw teal so a
                // closed loop is visibly different from a forward wire.
                Pin* ep = find_pin(link.endPinId);
                bool isTap = (sp->isTap) || (ep && ep->isTap);
                if (isTap) {
                    ImNodes::PushColorStyle(ImNodesCol_Link,
                                            IM_COL32(70, 150, 160, 255));
                    ImNodes::Link(link.id, a, b);
                    ImNodes::PopColorStyle();
                } else {
                    ImNodes::Link(link.id, a, b);
                }
            }
        }

        // Dynamic-pin wires (pin_model_design.md §6): gold, from the driving
        // node's output to the promoted setting's gold pin. A VIEW of
        // dynamicPins — the JSON ref is the truth, there is no Link object.
        // Drawn only when the target node itself is visible: a node hidden
        // inside a collapsed group keeps its binding, the wire is implied by
        // the group face like any other unrepresentable endpoint. The source
        // end does project through group faces.
        for (auto& node : s_nodes) {
            if (node.dynamicPins.empty() || !visible_at_path(node.label))
                continue;
            for (auto& [setting, val] : node.dynamicPins.items()) {
                if (!val.is_object()) continue;
                const std::string ref = val.value("ref", std::string());
                GraphNode* src = nullptr;
                for (auto& c : s_nodes) if (c.label == ref) { src = &c; break; }
                if (!src || src->outputs.empty()) continue;
                int a = project_pin(src->outputs[0].id, true);
                if (a < 0) continue;
                ImNodes::PushColorStyle(ImNodesCol_Link, kDynPinGold);
                ImNodes::PushColorStyle(ImNodesCol_LinkHovered, lighten(kDynPinGold, 25));
                ImNodes::PushColorStyle(ImNodesCol_LinkSelected, lighten(kDynPinGold, 40));
                ImNodes::Link(node.dyn_link_id(setting), a, node.dyn_attr_id(setting));
                ImNodes::PopColorStyle();  // LinkSelected
                ImNodes::PopColorStyle();  // LinkHovered
                ImNodes::PopColorStyle();  // Link
            }
        }

        bool editorHovered = ImNodes::IsEditorHovered();

        // Navigable overview (imnodes built-in, never enabled until Matt's
        // 2026-08-20 navigation pass): drag inside it to jump.
        ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);

        ImNodes::EndNodeEditor();

        // Wheel pans the canvas: wheel = vertical, Shift+wheel = horizontal
        // (TrackPoint/middle-button scrolling emits exactly these events, so
        // the eraserhead pans without Alt-drag). Middle-drag still pans
        // natively; Alt+left stays as the no-middle-button fallback.
        if (editorHovered && !ImGui::GetIO().WantTextInput) {
            float wy = ImGui::GetIO().MouseWheel;
            float wx = ImGui::GetIO().MouseWheelH;
            if (ImGui::GetIO().KeyShift && wx == 0.0f) { wx = wy; wy = 0.0f; }
            if (wx != 0.0f || wy != 0.0f) {
                ImVec2 p = ImNodes::EditorContextGetPanning();
                ImNodes::EditorContextResetPanning(
                    ImVec2(p.x + wx * 40.0f, p.y + wy * 40.0f));
            }
        }

        // Ctrl-click DESELECTS an already-selected node (Matt, REVIEW 32):
        // imnodes' multi-select modifier only ever adds, which makes
        // assembling a precise selection for Group impossible. Compare
        // against the PREVIOUS frame's selection — this frame's set already
        // includes the node either way.
        {
            static std::unordered_set<int> prevSelection;
            if (editorHovered && ImGui::GetIO().KeyCtrl &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                int hovered = -1;
                if (!ImNodes::IsNodeHovered(&hovered)) {
                    // imnodes suppresses node-hover while a PIN is hovered
                    // (socket dots have a hover radius), which blinded both
                    // the native deselect and this handler near sockets —
                    // resolve the pin's owning node instead.
                    int pinId = -1;
                    if (ImNodes::IsPinHovered(&pinId)) {
                        if (GraphNode* pn = find_node_for_pin(pinId)) {
                            hovered = pn->id;
                        } else if (GraphNode* dn =
                                       find_node_for_dyn_attr(pinId, nullptr)) {
                            hovered = dn->id;
                        } else {
                            for (auto& g : s_groups) {
                                if (g.outPinId == pinId ||
                                    std::find(g.inPinIds.begin(), g.inPinIds.end(),
                                              pinId) != g.inPinIds.end())
                                    hovered = g.editorId;
                            }
                        }
                    }
                }
                if (hovered >= 0 && prevSelection.count(hovered)) {
                    ImNodes::ClearNodeSelection(hovered);
                    transport_set_status("deselected", false);
                }
            }
            prevSelection.clear();
            int n = ImNodes::NumSelectedNodes();
            if (n > 0) {
                std::vector<int> sel(n);
                ImNodes::GetSelectedNodes(sel.data());
                prevSelection.insert(sel.begin(), sel.end());
            }
        }

        // Double-click on a collapsed group drills in.
        if (editorHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            int hovered = -1;
            if (ImNodes::IsNodeHovered(&hovered)) {
                for (auto& g : s_groups) {
                    if (g.editorId == hovered) {
                        s_groupPath.push_back(g.name);
                        // Drill-in auto-selects Group (per-group memory).
                        bool listenGroup = s_groupListen.count(g.name)
                            ? s_groupListen[g.name] : true;
                        GraphNode* gOut = group_output_node(g);
                        s_listenTapNode = (listenGroup && gOut) ? gOut->id : -1;
                        break;
                    }
                }
            }
        }

        // Track selected node for properties panel
        if (ImNodes::NumSelectedNodes() == 1) {
            int sel;
            ImNodes::GetSelectedNodes(&sel);
            g_selectedNodeId = sel;
        } else if (ImNodes::NumSelectedNodes() == 0) {
            g_selectedNodeId = -1;
        }

        // New links (also handles rewiring: drag from connected pin removes old link)
        int startAttr, endAttr;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
            // A drag onto a collapsed group's projected pin wires the REAL
            // pin it represents — the interface is a view, not a boundary.
            auto translate = [&](int attr) {
                auto in = s_groupProj.synthToRealIn.find(attr);
                if (in != s_groupProj.synthToRealIn.end()) return in->second;
                auto out = s_groupProj.groupOutToReal.find(attr);
                if (out != s_groupProj.groupOutToReal.end()) return out->second;
                return attr;
            };
            startAttr = translate(startAttr);
            endAttr   = translate(endAttr);
            Pin* startPin = find_pin(startAttr);
            Pin* endPin = find_pin(endAttr);

            // A drag landing on (or starting from) a GOLD pin retargets the
            // dynamic pin's driver. Only a CurveNode may feed one (Matt
            // 2026-08-19) — the same floor the Settings pane enforces.
            std::string dynSetting;
            GraphNode* dynNode = find_node_for_dyn_attr(startAttr, &dynSetting);
            int otherAttr = endAttr;
            if (!dynNode) {
                dynNode = find_node_for_dyn_attr(endAttr, &dynSetting);
                otherAttr = startAttr;
            }
            if (dynNode) {
                GraphNode* srcNode = find_node_for_pin(otherAttr);
                Pin* srcPin = find_pin(otherAttr);
                if (srcNode && srcPin && srcPin->kind == PinKind::Output &&
                    srcNode->typeName == "CurveNode") {
                    dynNode->dynamicPins[dynSetting] = {{"ref", srcNode->label}};
                    char msg[128];
                    snprintf(msg, sizeof(msg), "%s.%s <- %s (per note)",
                             dynNode->label.c_str(), dynSetting.c_str(),
                             srcNode->label.c_str());
                    transport_set_status(msg, false);
                    mark_graph_dirty();
                } else {
                    transport_set_status(
                        "Only a Curve may feed a dynamic pin", true);
                }
            } else if (startPin && endPin && startPin->kind != endPin->kind) {
                int outPin = (startPin->kind == PinKind::Output) ? startAttr : endAttr;
                int inPin  = (startPin->kind == PinKind::Input)  ? startAttr : endAttr;

                // Reject structurally incompatible links. Target pins that
                // expect IFormant / IPartials can't accept arbitrary sources
                // — before this check the loader silently dropped mismatched
                // set_param calls, leaving a "lying wire" in the UI with no
                // corresponding DSP connection.
                GraphNode* outNode = find_node_for_pin(outPin);
                GraphNode* inNode  = find_node_for_pin(inPin);
                Pin* inPinDesc     = find_pin(inPin);
                Pin* outPinDesc    = find_pin(outPin);
                bool accept = true;
                if (outNode && inNode && inPinDesc) {
                    const char* err = pin_type_compat_error(
                        inNode->typeName, inPinDesc->name, outNode->typeName);
                    if (err) {
                        transport_set_status(err, true);
                        accept = false;
                    }
                    // Cycle legality (feedback_loop_design.md §2): normal
                    // wires may not close a cycle — loops close via taps.
                    if (accept && outPinDesc && !outPinDesc->isTap &&
                        would_close_normal_cycle(outNode, inNode)) {
                        transport_set_status(
                            "Cycle - close loops through a tap pin", true);
                        accept = false;
                    }
                }

                if (accept) {
                    Pin* inPinObj = find_pin(inPin);
                    if (!inPinObj || !inPinObj->multi) {
                        s_links.erase(
                            std::remove_if(s_links.begin(), s_links.end(),
                                [inPin](const Link& l) { return l.endPinId == inPin || l.startPinId == inPin; }),
                            s_links.end());
                    }

                    s_links.emplace_back(outPin, inPin);
                    update_all_dsp();
                    mark_graph_dirty();
                }
            }
        }

        // Detached link
        {
            int destroyedLinkId;
            if (ImNodes::IsLinkDestroyed(&destroyedLinkId)) {
                std::string setting;
                if (GraphNode* dn = find_node_for_dyn_link(destroyedLinkId,
                                                           &setting)) {
                    // Gold wire dragged off its pin: unwire — the pin stays
                    // promoted (hollow), the stowed scalar takes over.
                    dn->dynamicPins[setting] = nullptr;
                    dn->apply_config();
                    mark_graph_dirty();
                } else {
                    s_links.erase(
                        std::remove_if(s_links.begin(), s_links.end(),
                            [destroyedLinkId](const Link& l) { return l.id == destroyedLinkId; }),
                        s_links.end());
                    update_all_dsp();
                    mark_graph_dirty();
                }
            }
        }

        // Right-click context menu
        if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            s_createMenuPos = ImGui::GetMousePos();
            // Check if right-click is on a node
            int hoveredNode = -1;
            for (auto& n : s_nodes) {
                if (ImNodes::IsNodeHovered(&hoveredNode)) break;
            }
            if (hoveredNode >= 0) {
                s_contextNodeId = hoveredNode;
                s_wantNodeMenu = true;
            } else {
                s_wantCreateMenu = true;
            }
        }
        if (s_wantCreateMenu) {
            ImGui::OpenPopup("CreateNodeMenu");
            s_wantCreateMenu = false;
        }
        if (s_wantNodeMenu) {
            ImGui::OpenPopup("NodeContextMenu");
            s_wantNodeMenu = false;
        }
        show_create_menu();
        show_node_context_menu();

        // Fit/center helper lambda (used by F key and Fit button)
        auto fitAllNodes = [&]() {
            if (!s_nodes.empty()) {
                float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
                for (auto& node : s_nodes) {
                    ImVec2 pos = ImNodes::GetNodeGridSpacePos(node.id);
                    minX = std::min(minX, pos.x); minY = std::min(minY, pos.y);
                    maxX = std::max(maxX, pos.x + 200.0f);
                    maxY = std::max(maxY, pos.y + 100.0f);
                }
                float cx = (minX + maxX) * 0.5f;
                float cy = (minY + maxY) * 0.5f;
                float winW = ImGui::GetWindowWidth();
                float winH = ImGui::GetWindowHeight() - 50.0f;
                ImNodes::EditorContextResetPanning(ImVec2(winW * 0.5f - cx, winH * 0.5f - cy));
            }
        };

        // Node-editor-specific shortcuts: F (fit), arrow keys (pan), Delete
        if (ImGui::IsKeyPressed(ImGuiKey_F) && !ImGui::GetIO().WantTextInput && !ImGui::GetIO().KeyCtrl) {
            fitAllNodes();
        }

        // Arrow key panning (when not editing text). Speed is per-frame, so
        // at 60fps base=5 is ~300 px/sec, Shift=20 is ~1200 px/sec.
        if (!ImGui::GetIO().WantTextInput) {
            float panSpeed = ImGui::GetIO().KeyShift ? 20.0f : 5.0f;
            auto p = ImNodes::EditorContextGetPanning();
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  ImNodes::EditorContextResetPanning(ImVec2(p.x + panSpeed, p.y));
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) ImNodes::EditorContextResetPanning(ImVec2(p.x - panSpeed, p.y));
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    ImNodes::EditorContextResetPanning(ImVec2(p.x, p.y + panSpeed));
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  ImNodes::EditorContextResetPanning(ImVec2(p.x, p.y - panSpeed));
        }

        // Clipboard shortcuts — editor focus + no active text input.
        if (editorFocused && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_X)) clipboard_cut();
            if (ImGui::IsKeyPressed(ImGuiKey_C)) clipboard_copy();
            if (ImGui::IsKeyPressed(ImGuiKey_V)) clipboard_paste();
        }

        // Ctrl+Shift+A: manual audio-stream restart (works anywhere, no
        // editor focus needed — the whole point is escaping a dead stream).
        if (!ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl &&
            ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_A)) {
            g_audioRestartRequest.store(true);
        }

        if (editorFocused && !ImGui::GetIO().WantTextInput &&
            (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
            int numLinks = ImNodes::NumSelectedLinks();
            int numNodes = ImNodes::NumSelectedNodes();

            if (numLinks > 0) {
                std::vector<int> sel(numLinks);
                ImNodes::GetSelectedLinks(sel.data());
                for (int lid : sel) {
                    // A gold wire is a dynamic pin's driver: deleting it
                    // UNWIRES the pin (the pin itself stays — promotion is
                    // the Settings-pane circle's job, Matt 2026-08-29) and
                    // the stowed scalar (still in settingValues, never
                    // overwritten) takes over again.
                    std::string setting;
                    if (GraphNode* dn = find_node_for_dyn_link(lid, &setting)) {
                        dn->dynamicPins[setting] = nullptr;
                        dn->apply_config();
                        mark_graph_dirty();
                        continue;
                    }
                    delete_link(lid);
                }
            }
            if (numNodes > 0) {
                std::vector<int> sel(numNodes);
                ImNodes::GetSelectedNodes(sel.data());
                for (int nid : sel) delete_node(nid);
            }
            ImNodes::ClearNodeSelection();
            ImNodes::ClearLinkSelection();
        }

        // Status bar
        ImGui::Separator();
        ImGui::Text("%s  |  Nodes: %d  Links: %d",
                     modeLabel, (int)s_nodes.size(), (int)s_links.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Fit##fitbtn")) { fitAllNodes(); }
        ImGui::SameLine();
        if (ImGui::SmallButton("<##panL")) { auto p = ImNodes::EditorContextGetPanning(); ImNodes::EditorContextResetPanning(ImVec2(p.x + 100, p.y)); }
        ImGui::SameLine();
        if (ImGui::SmallButton(">##panR")) { auto p = ImNodes::EditorContextGetPanning(); ImNodes::EditorContextResetPanning(ImVec2(p.x - 100, p.y)); }
        ImGui::SameLine();
        if (ImGui::SmallButton("^##panU")) { auto p = ImNodes::EditorContextGetPanning(); ImNodes::EditorContextResetPanning(ImVec2(p.x, p.y + 100)); }
        ImGui::SameLine();
        if (ImGui::SmallButton("v##panD")) { auto p = ImNodes::EditorContextGetPanning(); ImNodes::EditorContextResetPanning(ImVec2(p.x, p.y - 100)); }

        ImGui::End(); // Node Editor

        // =================================================================
        // Transport panel
        // =================================================================
        draw_transport_panel();

        // =================================================================
        // Properties panel
        // =================================================================
        draw_properties_panel();

        // =================================================================
        // Parameter-mapping dialog (legacy paramMap bindings + shapes)
        // =================================================================
        draw_mappings_dialog();

        // Shape editor (docs/shape_editor_design.md)
        draw_shape_editor();

        // =================================================================
        // Waveform display window
        // =================================================================
        draw_waveform_window();
        draw_wave_popouts();

        // =================================================================
        // Spectrum window (sibling tab)
        // =================================================================
        draw_spectrum_window();

        // =================================================================
        // Keyboard panel
        // =================================================================
        draw_keyboard_panel();

        // =================================================================
        // Audition window (sweep folder walker)
        // =================================================================
        draw_audition_window();
        audition_tick();

        // RtAudio runs the audio callback on its own thread — no main-loop
        // pumping needed. (Used to call pump_audio() here under waveOut.)
        // But: collect any voices the callback finished, releasing their
        // DSP-graph shared_ptrs on the UI thread instead of the audio thread.
        voice_gc();
        audio_watchdog();
        pump_midi();

        // Save-prompt modal: shown when a close was requested with unsaved
        // edits. User picks Save / Don't Save / Cancel.
        if (s_showCloseConfirm) {
            ImGui::OpenPopup("Save changes?##closeConfirm");
        }
        if (ImGui::BeginPopupModal("Save changes?##closeConfirm", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove))
        {
            const char* fname = "(unsaved patch)";
            if (!s_currentFilePath.empty()) {
                const char* slash = strrchr(s_currentFilePath.c_str(), '/');
                const char* bs    = strrchr(s_currentFilePath.c_str(), '\\');
                fname = (bs && (!slash || bs > slash)) ? bs + 1
                      : slash ? slash + 1 : s_currentFilePath.c_str();
            }
            ImGui::Text("Save changes to %s?", fname);
            ImGui::Spacing();
            if (ImGui::Button("Save", ImVec2(100, 0))) {
                save_graph();
                // save_graph clears s_graphDirty on success. If the user
                // cancelled the file dialog (still dirty), keep the modal
                // open so they can choose again.
                if (!s_graphDirty) {
                    s_closeApproved = true;
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                    s_showCloseConfirm = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Don't Save", ImVec2(100, 0))) {
                s_closeApproved = true;
                glfwSetWindowShouldClose(window, GLFW_TRUE);
                s_showCloseConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) {
                s_showCloseConfirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        stamp::draw_banner();   // dsp backlog 3c — stale-binary warning

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // (Viewports disabled; no platform-window render pass.)

        glfwSwapBuffers(window);

        // Generate state machine: this frame drew "Generating..." with the
        // waveform cleared. Now run the blocking generation. UI freezes
        // until it completes; the frozen screen still shows "Generating...".
        if (s_genState == 2) {
            transport_generate();
            s_genState = 0;
        }
    }

    shutdown_midi();
    shutdown_audio();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImNodes::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
    } catch (const std::exception& e) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "C++ exception: %s (%s)", typeid(e).name(), e.what());
        crash_log_write(buf);
        return 1;
    } catch (...) {
        crash_log_write("Unknown C++ exception (non-std::exception)");
        return 1;
    }
}

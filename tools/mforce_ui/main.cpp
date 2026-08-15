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

#include <vector>
#include <string>
#include <cstdint>
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

static bool is_special_ui_type(const std::string& typeName) {
    return typeName == NT_SOUND_CHANNEL || typeName == NT_STEREO_MIXER
        || typeName == NT_PATCH_OUTPUT  || typeName == NT_PARAMETER;
}

static std::string node_display_name(const std::string& typeName) {
    if (typeName == NT_SOUND_CHANNEL) return "Channel";
    if (typeName == NT_STEREO_MIXER)  return "Mixer";
    if (typeName == NT_PATCH_OUTPUT)  return "Output";
    if (typeName == NT_PARAMETER)     return "Parameter";
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

static ImU32 node_title_color(const std::string& typeName) {
    if (typeName == NT_SOUND_CHANNEL || typeName == NT_STEREO_MIXER)
        return IM_COL32(120, 130, 145, 255);    // Blue grey — Output
    if (typeName == NT_PATCH_OUTPUT)
        return IM_COL32(120, 130, 145, 255);    // Blue grey — Output
    if (typeName == NT_PARAMETER)
        return IM_COL32(190, 140, 170, 255);    // Pink — Parameter

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
    std::vector<std::pair<ConfigDescriptor, float>> configValues;

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

    // Parameter-specific
    std::string paramName;
    char paramNameBuf[32]{};

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

        if (typeName == NT_ENVELOPE) {
            dspSource = std::make_shared<Envelope>(Envelope::make_adsr(DSP_SAMPLE_RATE,
                0.05f, 0.1f, 0.7f, 0.2f));
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

    void init_config() {
        if (!dspSource) return;
        auto descs = dspSource->config_descriptors();
        configValues.clear();
        for (const auto& desc : descs)
            configValues.push_back({desc, desc.default_value});
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
        for (auto& [desc, val] : configValues)
            dspSource->set_config(desc.name, val);
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

        // Envelope: output only; ADSR params are config values shown in Properties
        if (typeName == NT_ENVELOPE) {
            outputs.emplace_back("out", PinKind::Output);
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
// paramMap entries as loaded from JSON, captured verbatim. The UI node graph
// models a paramMap target only when it resolves to an input pin, so it cannot
// represent curve entries ({target, curve}) or targets that are configs (e.g.
// Envelope.sustainLevel — a config, not a param pin). To avoid silently
// stripping those on load→save, save_patch carries forward the original entry
// verbatim for any Parameter name whose loaded entry carried a curve. Full
// visual editing of curves is deferred (see dsp BACKLOG 3b "Later"). Reset on
// new/clear/load.
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
static void update_node_dsp(GraphNode& node) {
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
                if (pin.multi && node.dspSource)
                    node.dspSource->add_param(pin.name, srcNode->dspSource);
                else
                    node.wire_pin(pin.name, srcNode->dspSource);
            }
        }
    }

    // Envelope: apply config values (ADSREnvelope rebuilds internally)
    if (node.typeName == NT_ENVELOPE) {
        node.apply_config();
    }
}

// Update ALL nodes' DSP (call after link changes)
// Automatically wraps shared sources in RefSource for secondary consumers.
static void update_all_dsp() {
    try {
        // First: run standard per-node wiring (primary sources)
        for (auto& node : s_nodes)
            update_node_dsp(node);

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
// and buffer playback own their data (shared_ptrs / member buffer) and
// survive structural edits.
static void stop_streams();

static void delete_node(int nodeId) {
    stop_streams();
    s_graphDirty = true;
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
        break;
    }
    if (s_listenTapNode == nodeId) s_listenTapNode = -1;
    s_nodes.erase(
        std::remove_if(s_nodes.begin(), s_nodes.end(),
            [nodeId](const GraphNode& n) { return n.id == nodeId; }),
        s_nodes.end());
}

static void delete_link(int linkId) {
    s_graphDirty = true;
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
                                                                : "Linear";
        stages.push_back({
            {"percent",  s.percent},
            {"startVal", s.ramp.startVal},
            {"endVal",   s.ramp.endVal},
            {"type",     t},
            {"power",    s.ramp.power},
            {"holdPct",  s.ramp.holdPct},
            {"minSec",   s.minSec},
            {"maxSec",   s.maxSec},
        });
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
        s.ramp.holdPct  = sj.value("holdPct",  0.0f);
        std::string t   = sj.value("type", std::string("Linear"));
        s.ramp.type = (t == "Expo")        ? RampType::Expo
                    : (t == "InverseExpo") ? RampType::InverseExpo
                    : (t == "Sine")        ? RampType::Sine
                                            : RampType::Linear;
        s.percent = sj.value("percent", 0.0f);
        s.minSec  = sj.value("minSec",  0.0f);
        s.maxSec  = sj.value("maxSec",  0.0f);
        env.add_stage(s);
    }
}

// Defined with the transport helpers below — loads report unknown node
// types (patch authored against an engine branch this build lacks) in the
// transport status line instead of silently creating inert nodes.
static void transport_set_status(const char* msg, bool isError);

static void load_graph_from_path(const std::string& path) {
    using json = nlohmann::json;

    if (path.empty()) return;

    std::ifstream f(path);
    if (!f) return;
    json root = json::parse(f);
    stop_playback();  // stream paths hold raw pointers into s_nodes' DSP
    s_currentFilePath = path;
    s_graphDirty = false;

    s_nodes.clear();
    s_links.clear();
    s_groups.clear();
    s_groupPath.clear();
    s_groupListen.clear();
    s_listenTapNode = -1;
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

    // First pass: create all nodes
    std::vector<std::string> unknownTypes;
    for (const auto& jnode : nodes) {
        std::string id = jnode["id"].get<std::string>();
        std::string type = jnode["type"].get<std::string>();

        // Skip Formants owned by a FormantSpectrum — they'll live inside its row table.
        if (ownedFormants.count(id)) continue;

        // Track types this build's engine doesn't know. The node is still
        // created (inert, no dspSource, one bare "out" pin) so the graph
        // shape survives a load→save, but params/links through it are lost —
        // surface that instead of failing silently.
        if (!is_special_ui_type(type) && type != NT_ENVELOPE &&
            type != "FormantSpectrum" && !SourceRegistry::instance().has(type) &&
            std::find(unknownTypes.begin(), unknownTypes.end(), type) == unknownTypes.end())
            unknownTypes.push_back(type);

        s_nodes.emplace_back(type);
        GraphNode& gn = s_nodes.back();
        gn.label = id;  // use JSON id as label
        nodeMap[id] = &gn;

        // Map output pin
        if (!gn.outputs.empty())
            outputPinMap[id] = gn.outputs[0].id;

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
                    // sustain slots now that set_config recognizes the adsr
                    // shape (it was a silent no-op before — 3n).
                    for (auto& [desc, val] : gn.configValues)
                        if (std::string_view(desc.name) == "sustainLevel")
                            val = env->get_config("sustainLevel");
                }
            }

            // Restore config values
            for (auto& [desc, val] : gn.configValues) {
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
            for (auto& [desc, val] : gn.configValues)
                val = gn.dspSource->get_config(desc.name);

            // Capture every params key the passes above did NOT consume —
            // carried verbatim through save (GraphNode::jsonExtras, 3n).
            {
                auto is_pin = [&](const std::string& k) {
                    for (auto& pin : gn.inputs) if (pin.name == k) return true;
                    return false;
                };
                auto is_config = [&](const std::string& k) {
                    for (auto& [desc, v] : gn.configValues)
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
                    if (!refObj.is_object() || !refObj.contains("ref")) continue;
                    std::string refId = refObj["ref"].get<std::string>();
                    auto outIt = outputPinMap.find(refId);
                    if (outIt != outputPinMap.end())
                        s_links.emplace_back(outIt->second, inIt->second);
                }
                continue;
            }

            if (!val.is_object() || !val.contains("ref")) continue;
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
        // edited in the Mappings dialog / Curves tab, show as green badges on
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

            if (positions.contains(key) && !s_headless) {
                float x = positions[key][0].get<float>();
                float y = positions[key][1].get<float>();
                ImNodes::SetNodeGridSpacePos(node.id, ImVec2(x, y));
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
                        if (sp.id == srcPinId)
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

// Serialized id = the node's label, made safe: ids embed in "node.pin"
// paramMap targets (no '.'), "__" is reserved for synthesized keys
// (__output, __param_*, FormantSpectrum "__fN" children), and duplicates
// from a hand-edited file must not collapse two nodes into one id.
static std::string sanitize_unique_id(const std::string& want,
                                      std::unordered_set<std::string>& used,
                                      const std::string& typeName) {
    std::string base = want;
    std::replace(base.begin(), base.end(), '.', '_');
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
    }
    if (oldName != newName) {
        // Group membership stores labels — follow the rename.
        for (auto& g : s_groups)
            for (auto& m : g.members)
                if (m == oldName) m = newName;
    }
    s_graphDirty = true;
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
    s_graphDirty = true;
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
        nodeIds[node.id] = sanitize_unique_id(node.label, usedIds, node.typeName);
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

        json jnode;
        jnode["id"] = nodeIds[node.id];
        jnode["type"] = node.typeName;

        json params = json::object();
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
                    if (srcNode && srcPin && srcPin->kind == PinKind::Output && nodeIds.count(srcNode->id))
                        refs.push_back(json{{"ref", nodeIds[srcNode->id]}});
                }
                if (!refs.empty()) params[pin.name] = refs;
            } else {
                GraphNode* src = find_source_node(pin.id);
                if (pin.inputOnly) {
                    if (src && nodeIds.count(src->id))
                        params[pin.name] = json{{"ref", nodeIds[src->id]}};
                    else if (!src && pin.hasConstant)
                        params[pin.name] = pin.defaultValue;
                } else if (src) {
                    if (src->typeName == NT_PARAMETER)
                        params[pin.name] = pin.defaultValue;
                    else if (nodeIds.count(src->id))
                        params[pin.name] = json{{"ref", nodeIds[src->id]}};
                } else {
                    params[pin.name] = pin.defaultValue;
                }
            }
        }
        if (!params.empty()) jnode["params"] = params;

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
        for (auto& [desc, val] : node.configValues) {
            // Envelope sustain lives INSIDE the emitted stages (slot
            // values); writing the config too made reload rewrite the slots
            // through set_config's [0,1] clamp, breaking >1.0 test
            // envelopes (_envacc_test) — and 0.0 artifacts of the old
            // get_config broke three library patches (3n).
            if (node.typeName == NT_ENVELOPE &&
                std::string_view(desc.name) == "sustainLevel")
                continue;
            if (!jnode.contains("params")) jnode["params"] = json::object();
            if (desc.type == ConfigType::Bool)
                jnode["params"][desc.name] = (val != 0.0f);
            else if (desc.type == ConfigType::Int)
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
    root["graph"]["nodes"] = nodes;
    root["graph"]["output"] = outputId;

    if (outputNode) {
        // Fields the UI doesn't model (release, volume, ...) pass through
        // verbatim; UI-owned keys below overwrite.
        for (auto it = s_loadedInstrumentExtras.begin();
             it != s_loadedInstrumentExtras.end(); ++it)
            root["instrument"][it.key()] = it.value();
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
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodePtr->id);
            positions[nodeIds[nodePtr->id]] = {pos.x, pos.y};
        }
        if (outputNode) {
            ImVec2 pos = ImNodes::GetNodeGridSpacePos(outputNode->id);
            positions["__output"] = {pos.x, pos.y};
        }
        root["ui"]["positions"] = positions;
        ImVec2 pan = ImNodes::EditorContextGetPanning();
        root["ui"]["panning"] = {pan.x, pan.y};
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
        nodeIds[node.id] = sanitize_unique_id(node.label, usedIds, node.typeName);

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
                        refs.push_back(json{{"ref", nodeIds[srcNode->id]}});
                }
                if (!refs.empty()) params[pin.name] = refs;
            } else if (pin.inputOnly) {
                if (src)
                    params[pin.name] = json{{"ref", nodeIds[src->id]}};
                else if (pin.hasConstant)
                    params[pin.name] = pin.defaultValue;
            } else {
                if (src)
                    params[pin.name] = json{{"ref", nodeIds[src->id]}};
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

        for (auto& [desc, val] : node.configValues) {
            // Envelope sustain lives INSIDE the emitted stages (slot
            // values); writing the config too made reload rewrite the slots
            // through set_config's [0,1] clamp, breaking >1.0 test
            // envelopes (_envacc_test) — and 0.0 artifacts of the old
            // get_config broke three library patches (3n).
            if (node.typeName == NT_ENVELOPE &&
                std::string_view(desc.name) == "sustainLevel")
                continue;
            if (!jnode.contains("params")) jnode["params"] = json::object();
            if (desc.type == ConfigType::Bool)
                jnode["params"][desc.name] = (val != 0.0f);
            else if (desc.type == ConfigType::Int)
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
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodePtr->id);
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
static int g_waveColumns = 1;     // number of columns for waveform tiling
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
// / g_bufferPlayback must be serialized with the callback's reads via g_audioMutex.
static std::unique_ptr<RtAudio> g_audio;
static std::mutex g_audioMutex;

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
};
static Voice g_voices[MAX_VOICES];

static void voice_schedule(std::shared_ptr<InstrumentPatch> patch,
                           std::shared_ptr<ValueSource> source,
                           int totalSamples, float gain, int midiNote,
                           bool held = false,
                           std::vector<mforce::Envelope*> envs = {}) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    // Find a free voice, or steal the one closest to done
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (!g_voices[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        int best = 0;
        for (int i = 1; i < MAX_VOICES; ++i)
            if (g_voices[i].samplesRemaining < g_voices[best].samplesRemaining) best = i;
        slot = best;
    }
    g_voices[slot].patch = std::move(patch);
    g_voices[slot].source = std::move(source);
    g_voices[slot].samplesRemaining = totalSamples;
    g_voices[slot].gain = gain;
    g_voices[slot].held = held;
    g_voices[slot].envs = std::move(envs);
    g_voices[slot].midiNote = midiNote;
    g_voices[slot].active = true;
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
            voiceSum += voice.source->next() * voice.gain;
            voice.samplesRemaining--;
            if (voice.samplesRemaining <= 0) {
                voice.active = false;
                // Do NOT reset() the source/patch shared_ptrs here — dropping
                // the last ref would destruct the whole DSP graph on the audio
                // thread, hitting the Windows heap lock and causing glitches.
                // voice_gc() on the UI thread does the actual destruction.
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

        g_waveScrollPos = 0;
        g_waveZoom = std::max(1, frames / 800);
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

        // Reset waveform view to show the full authoritative buffer.
        g_waveScrollPos = 0;
        g_waveZoom = std::max(1, frames / 800);
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
// ConstantSource; config targets go through set_config (and refresh the
// node's cached configValues so Properties shows the per-note value).
// Replaces the old Parameter-node scan, which pushed RAW frequency into
// curve-bearing pin targets — the UI render ignored curves entirely.
static float eval_map_curve(const nlohmann::json& curve, float freq) {
    auto x = [&](size_t i) { return curve[i][0].get<float>(); };
    auto y = [&](size_t i) { return curve[i][1].get<float>(); };
    size_t n = curve.size();
    if (n == 0) return freq;
    if (freq <= x(0))     return y(0);
    if (freq >= x(n - 1)) return y(n - 1);
    for (size_t i = 1; i < n; ++i) {
        if (freq <= x(i)) {
            float lf = std::log(freq / x(i - 1)) / std::log(x(i) / x(i - 1));
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
            if (e.contains("curve"))  v = eval_map_curve(e["curve"], freq);
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
                n.dspSource->set_config(pname, v);
                for (auto& [desc, val] : n.configValues)
                    if (pname == desc.name) val = n.dspSource->get_config(desc.name);
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

    // Reset zoom to fit entire waveform, reset scroll
    g_waveScrollPos = 0;
    g_waveZoom = std::max(1, samples / 800);

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
static void note_played(float noteNum);

static void play_note(float noteNum, float velocity, float durationSeconds) {
    if (s_graphMode != GraphMode::PatchGraph) return;
    note_played(noteNum);

    // (Waveform-display render intentionally skipped here — live keyboard
    // mode prioritizes audio responsiveness over visual feedback. Use the
    // Render button / offline render path when you want to see the waveform.)

    // Audio path: load a fresh instrument from the current UI state (synced
    // to a temp file if dirty) and play_note on it, so Multiplex clones
    // retune correctly via paramMap fan-out.
    std::string path = get_playback_patch_path();
    if (path.empty()) return;

    try {
        // Wrap the loaded patch in a shared_ptr so a Voice slot can keep the
        // whole DSP graph alive for the lifetime of the note. (InstrumentPatch
        // owns the unique_ptr<PitchedInstrument>, so the shared wrapper keeps
        // both the instrument and the voicePool source graphs from being
        // destroyed mid-play.)
        auto ip = std::make_shared<InstrumentPatch>(load_instrument_patch(path));
        auto* pitched = ip->instrument.get();
        if (!pitched) return;

        // Prepare the voice (set frequency, prep the source) but DON'T render —
        // streaming voice mixer will pull samples on demand in fill_audio_buffer.
        // sv.gain carries the patch's pre-clip volume (calibrated gain staging).
        auto sv = pitched->prepare_voice(noteNum, velocity, durationSeconds);

        // Note-contained sound (2026-08-13): the voice lives exactly
        // durSamples — release is inside the note, no tail window.
        voice_schedule(ip, sv.source, sv.durSamples, sv.gain, int(noteNum));
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
    std::string path = get_playback_patch_path();
    if (path.empty()) return;
    try {
        auto ip = std::make_shared<InstrumentPatch>(load_instrument_patch(path));
        auto* pitched = ip->instrument.get();
        if (!pitched) return;
        std::vector<mforce::Envelope*> envs;
        std::vector<mforce::ValueSource*> seen;
        bool gateable = true;
        for (auto& vg : pitched->voicePool)
            gateable = collect_envelopes(vg.source.get(), envs, seen) && gateable;
        if (!gateable || envs.empty()) {
            play_note(noteNum, velocity, nominalSeconds);   // scheduled fallback
            return;
        }
        note_played(noteNum);
        for (auto* e : envs) e->set_gated(true);
        auto sv = pitched->prepare_voice(noteNum, velocity, nominalSeconds);
        voice_schedule(ip, sv.source, INT_MAX / 2, sv.gain, int(noteNum),
                       true, std::move(envs));
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
        g_voices[i].active = false;
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

// QWERTY-to-chromatic-offset mapping (from legacy LBKeyboard.cs)
struct QwertyMapping { ImGuiKey key; int offset; const char* label; };
static const QwertyMapping s_qwertyMap[] = {
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
// Curves window — visual editor for paramMap frequency→value curves.
//
// Curve-bearing paramMap entries ({"target": "node.paramOrConfig", "curve":
// [[hz, value], ...]}) live verbatim in s_loadedParamMap (the UI node graph
// cannot model them — see the stash comment near its declaration). This
// window edits that stash directly. Because save_patch_graph carries the
// stash forward (with node-id remapping) and get_playback_patch_path
// re-serializes the patch through save_patch_graph whenever the graph is
// dirty, every edit here is immediately audible on keyboard playback and
// lands in the file on Save. All edits set s_graphDirty.
//
// Engine semantics (ParamSlot::map in engine/include/mforce/render/
// instrument.h): breakpoints are ascending (hz, value) pairs, >= 2 of them;
// evaluation is linear in log-frequency, clamped past the ends; curves are
// evaluated per note-on from the note frequency, so only entries under the
// "frequency" paramMap name have any effect at present.
// ===========================================================================

// Mirror of ParamSlot::map for the plot preview.
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
    s_graphDirty = true;
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
    if (changed || needSort) s_graphDirty = true;

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

// Eligible mapping targets on a node: pins the loader can resolve to a
// ConstantSource (value pins not wired to a source) plus all scalar
// configs (delivered via set_config). Shared by the Curves tab's Add-curve
// section and the Parameter-mappings dialog.
struct TargetOpt { std::string name; float current; bool isConfig; };
static std::vector<TargetOpt> eligible_targets(GraphNode* tn) {
    std::vector<TargetOpt> opts;
    for (auto& pin : tn->inputs) {
        if (pin.inputOnly || pin.kind != PinKind::Input) continue;
        if (pin.name.substr(0, 3) == "ch ") continue;
        if (is_pin_connected(pin.id)) continue;  // loader rejects ref-wired targets
        opts.push_back({pin.name, pin.defaultValue, false});
    }
    for (auto& [desc, val] : tn->configValues)
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
// go through the same append rules as the Curves tab.
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

    // --- Existing bindings ---
    struct DeleteReq { std::string param; int subIdx; };  // -1 = whole entry
    std::vector<DeleteReq> deletes;
    bool focusCurves = false;
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
                char lbl[48];
                snprintf(lbl, sizeof(lbl), "%d pts##c%s%d",
                         (int)e["curve"].size(), pname.c_str(), subIdx);
                if (ImGui::SmallButton(lbl)) focusCurves = true;
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Edit the shape in the Curves window");
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
        s_graphDirty = true;
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
        ImGui::TextDisabled("Selected node has no mappable params/configs.");
        ImGui::End();
        return;
    }
    selTarget = std::clamp(selTarget, 0, (int)opts.size() - 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("Param / config##map", opts[selTarget].name.c_str())) {
        for (int i = 0; i < (int)opts.size(); ++i) {
            char lbl[160];
            snprintf(lbl, sizeof(lbl), "%s%s##mt%d", opts[i].name.c_str(),
                     opts[i].isConfig ? "  (config)" : "", i);
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
        s_graphDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Add with curve##mapcurve")) {
        curves_add_entry("frequency", target, opts[selTarget].current);
        s_graphDirty = true;
        focusCurves = true;
    }
    ImGui::EndDisabled();
    if (exists) {
        ImGui::SameLine();
        ImGui::TextDisabled("(already bound)");
    }

    if (focusCurves) ImGui::SetWindowFocus("Curves");
    ImGui::End();
}

static void draw_curves_window() {
    ImGui::Begin("Curves", nullptr, ImGuiWindowFlags_NoCollapse);

    if (s_graphMode != GraphMode::PatchGraph) {
        ImGui::TextDisabled("Curves apply to instrument patches only\n(no instrument block in this graph).");
        ImGui::End();
        return;
    }

    // Logical mapping names come from the stash itself (the model);
    // "frequency" is always offered — it is the only name the instrument
    // evaluates at note-on.
    std::vector<std::string> paramNames{"frequency"};
    if (s_loadedParamMap.is_object())
        for (auto& [k, v] : s_loadedParamMap.items())
            if (k != "frequency") paramNames.push_back(k);

    // --- Selected-node filter (Matt 2026-08-12): with exactly one node
    // selected in the editor, show only curves targeting that node's
    // attributes. No/multi selection = show everything.
    std::string filterLabel;
    if (ImNodes::NumSelectedNodes() == 1) {
        int selId = -1;
        ImNodes::GetSelectedNodes(&selId);
        for (auto& n : s_nodes)
            if (n.id == selId && !n.label.empty()) { filterLabel = n.label; break; }
    }
    auto entry_passes_filter = [&](const nlohmann::json& e) {
        if (filterLabel.empty()) return true;
        if (!e.is_object() || !e.contains("target") || !e["target"].is_string())
            return true;   // malformed/legacy — never hide silently
        const std::string t = e["target"].get<std::string>();
        return t.rfind(filterLabel + ".", 0) == 0;
    };
    if (!filterLabel.empty())
        ImGui::TextDisabled("Filtered to node '%s' (deselect to show all)",
                            filterLabel.c_str());

    // --- Existing curve entries ---
    // Structural deletes are deferred to after iteration.
    struct DeleteReq { std::string param; int subIdx; };  // subIdx -1 = entry itself is the object
    std::vector<DeleteReq> deletes;
    bool anyCurve = false;
    bool anyShown = false;
    for (auto& [pname, entry] : s_loadedParamMap.items()) {
        ImGui::PushID(pname.c_str());
        if (entry.is_object() && entry.contains("curve")) {
            anyCurve = true;
            if (entry_passes_filter(entry)) {
                anyShown = true;
                if (draw_one_curve(pname, entry)) deletes.push_back({pname, -1});
            }
        } else if (entry.is_array()) {
            for (int i = 0; i < (int)entry.size(); ++i) {
                if (!entry[i].is_object() || !entry[i].contains("curve")) continue;
                anyCurve = true;
                if (!entry_passes_filter(entry[i])) continue;
                anyShown = true;
                ImGui::PushID(i);
                if (draw_one_curve(pname, entry[i])) deletes.push_back({pname, i});
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    if (!anyCurve)
        ImGui::TextDisabled("No curves in this patch.");
    else if (!anyShown)
        ImGui::TextDisabled("No curves on node '%s'.", filterLabel.c_str());

    for (const auto& d : deletes) {
        nlohmann::json& entry = s_loadedParamMap[d.param];
        if (d.subIdx < 0) {
            // Entry was a bare {target, curve} object: deleting the curve
            // deletes the BINDING (spec §2 — the widget on the target's
            // Properties row comes back; a curveless binding would push raw
            // note frequency into a non-frequency target, which is never
            // what the delete meant).
            s_loadedParamMap.erase(d.param);
        } else if (entry.is_array() && d.subIdx < (int)entry.size()) {
            entry.erase(entry.begin() + d.subIdx);
            if (entry.size() == 1 && entry[0].is_string())
                s_loadedParamMap[d.param] = entry[0];
        }
        s_graphDirty = true;
    }

    // --- Add a new curve ---
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Add curve");

    static int selParam = 0, selNode = 0, selTarget = 0;

    // Parameter combo (which paramMap name the curve is evaluated under).
    // Only "frequency" is evaluated at note-on today; still list all names.
    selParam = std::clamp(selParam, 0, (int)paramNames.size() - 1);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("Parameter", paramNames[selParam].c_str())) {
        for (int i = 0; i < (int)paramNames.size(); ++i)
            if (ImGui::Selectable(paramNames[i].c_str(), i == selParam)) selParam = i;
        ImGui::EndCombo();
    }

    // Target node combo (any non-special node in the graph).
    std::vector<GraphNode*> targetNodes;
    for (auto& n : s_nodes)
        if (!is_special_ui_type(n.typeName) && !n.label.empty())
            targetNodes.push_back(&n);
    if (targetNodes.empty()) {
        ImGui::TextDisabled("No target nodes in graph.");
        ImGui::End();
        return;
    }
    selNode = std::clamp(selNode, 0, (int)targetNodes.size() - 1);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("Node", targetNodes[selNode]->label.c_str())) {
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

    // Param/config combo for the chosen node (shared with the Mappings
    // dialog — see eligible_targets above draw_curves_window).
    GraphNode* tn = targetNodes[selNode];
    std::vector<TargetOpt> opts = eligible_targets(tn);
    if (opts.empty()) {
        ImGui::TextDisabled("Selected node has no curve-able params/configs.");
        ImGui::End();
        return;
    }
    selTarget = std::clamp(selTarget, 0, (int)opts.size() - 1);
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("Param / config", opts[selTarget].name.c_str())) {
        for (int i = 0; i < (int)opts.size(); ++i) {
            const bool hasCurve = curve_exists_for(
                paramNames[selParam], tn->label + "." + opts[i].name);
            char lbl[160];
            snprintf(lbl, sizeof(lbl), "%s%s%s##%d", opts[i].name.c_str(),
                     opts[i].isConfig ? "  (config)" : "",
                     hasCurve ? "  (has curve)" : "", i);
            if (ImGui::Selectable(lbl, i == selTarget)) selTarget = i;
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Add##addcurve")) {
        std::string target = tn->label + "." + opts[selTarget].name;
        curves_add_entry(paramNames[selParam], target, opts[selTarget].current);
    }
    if (paramNames[selParam] != "frequency") {
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1),
            "Note: only 'frequency' curves are evaluated at note-on today.");
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
                s_qwertyHeldNote[i] = absNote;
                play_note_held(float(absNote), g_transport.velocity, g_keyboard.duration);
            }
            if (ImGui::IsKeyReleased(s_qwertyMap[i].key) && s_qwertyHeldNote[i] >= 0) {
                release_note_held(s_qwertyHeldNote[i]);
                s_qwertyHeldNote[i] = -1;
            }
        }
        // Action keys
        if (ImGui::IsKeyPressed(ImGuiKey_G, false))
            g_keyboard.octave = std::max(0, g_keyboard.octave - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
            g_keyboard.octave = std::min(20, g_keyboard.octave + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            g_keyboard.duration = std::max(0.05f, g_keyboard.duration * 0.5f);
        if (ImGui::IsKeyPressed(ImGuiKey_B, false))
            g_keyboard.duration = std::min(30.0f, g_keyboard.duration * 2.0f);
    }

    // --- Piano keyboard rendering via ImDrawList ---
    const int NUM_OCTAVES = 4;
    const int WHITE_KEYS_PER_OCT = 7;
    const int TOTAL_WHITE = NUM_OCTAVES * WHITE_KEYS_PER_OCT;

    float availW = ImGui::GetContentRegionAvail().x;
    float availH = ImGui::GetContentRegionAvail().y;
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

    // Draw white keys
    for (int oct = 0; oct < NUM_OCTAVES; ++oct) {
        for (int w = 0; w < WHITE_KEYS_PER_OCT; ++w) {
            int idx = oct * WHITE_KEYS_PER_OCT + w;
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
            for (int oct = 0; oct < NUM_OCTAVES; ++oct) {
                for (int w = 0; w < WHITE_KEYS_PER_OCT; ++w) {
                    int idx = oct * WHITE_KEYS_PER_OCT + w;
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
                if (hitNote >= 0) break;
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

    g_waveScrollPos = 0;
    g_waveZoom = std::max(1, totalSamples / 800);
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

        g_waveScrollPos = 0;
        g_waveZoom = std::max(1, uiFrames / 800);
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

        g_waveScrollPos = 0;
        g_waveZoom = std::max(1, frames / 800);
    } catch (const std::exception& e) {
        transport_set_status(e.what(), true);
    }
}

// Last note played (Play/Generate, QWERTY, on-screen keys). Displayed
// right-aligned in the transport bar; label uses the HOUSE octave
// convention (name = midi%12, octave = midi/12 — comp REVIEW item 19).
static int g_lastNoteMidi = -1;
static void note_played(float noteNum) { g_lastNoteMidi = int(noteNum + 0.5f); }

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
                note_played(noteNum);
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
        char lastBuf[64];
        snprintf(lastBuf, sizeof(lastBuf), "Last note: %d  %s%d  %.1fHz",
                 g_lastNoteMidi, kNames[g_lastNoteMidi % 12],
                 g_lastNoteMidi / 12, note_to_freq(float(g_lastNoteMidi)));
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
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleCol);
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, bgCol);
    ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, bgCol);
    ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, bgCol);

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

    // Output pins
    for (auto& pin : node.outputs) {
        ImNodes::BeginOutputAttribute(pin.id);
        float nodeWidth = 150.0f;
        float textWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
        ImGui::Indent(nodeWidth - textWidth - 20);
        ImGui::TextUnformatted(pin.name.c_str());
        ImNodes::EndOutputAttribute();
    }

    ImNodes::EndNode();
    ImNodes::PopColorStyle(); // NodeBackgroundSelected
    ImNodes::PopColorStyle(); // NodeBackgroundHovered
    ImNodes::PopColorStyle(); // NodeBackground
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
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, titleCol);
    ImNodes::PushColorStyle(ImNodesCol_NodeBackground, IM_COL32(45, 38, 55, 255));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, IM_COL32(52, 44, 64, 255));
    ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundSelected, IM_COL32(52, 44, 64, 255));

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
        ImNodes::BeginInputAttribute(g.inPinIds[i]);
        ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s",
                           ins[i].label.c_str());
        ImNodes::EndInputAttribute();
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

    // Header
    ImGui::TextColored(ImColor(node_title_color(node->typeName)).Value, "%s", node->label.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "(%s)", node_display_name(node->typeName).c_str());

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
                ImGui::SetTooltip("Driven per note by the paramMap — edit in\nEdit > Parameter mapping (curve shapes in Curves).");
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
                s_graphDirty = true;
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
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "(not connected)");
        }
    }

    // Config values. Int configs that are logically enums (e.g.
    // CombinedSource.operation) render as a dropdown using labels
    // declared on the ConfigDescriptor.
    if (!node->configValues.empty()) {
        if (hasParams) { ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Settings");

        for (auto& [desc, val] : node->configValues) {
            ImGui::Text("%s", desc.name);
            ImGui::SameLine(labelW);
            if (const char* badge = mapping_badge(node->label, desc.name)) {
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "%s", badge);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Driven per note by the paramMap — edit in\nEdit > Parameter mapping (curve shapes in Curves).");
                continue;
            }
            ImGui::PushItemWidth(widgetW);
            char cfgLabel[64];
            snprintf(cfgLabel, sizeof(cfgLabel), "##pcfg_%s_%d", desc.name, node->id);
            bool changed = false;
            if (desc.type == ConfigType::Bool) {
                bool b = (val != 0.0f);
                if (ImGui::Checkbox(cfgLabel, &b)) { val = b ? 1.0f : 0.0f; changed = true; }
            } else if (desc.type == ConfigType::Int && desc.enum_labels) {
                // Count labels (null-terminated).
                int count = 0;
                while (desc.enum_labels[count]) ++count;
                int iv = std::clamp(int(val), 0, count - 1);
                if (ImGui::Combo(cfgLabel, &iv, desc.enum_labels, count)) {
                    val = float(iv); changed = true;
                }
            } else if (desc.type == ConfigType::Int) {
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
                node->dspSource->set_config(desc.name, val);
                node->jsonExtras.erase(desc.name);  // edit wins over carried value
                // set_config may have mutated internal arrays (e.g. ExplicitPartials
                // mirrors _1 → _2 when evolve flips off). Re-pull cached values.
                for (auto& [d, v] : node->arrayValues)
                    v = node->dspSource->get_array(d.name);
                s_graphDirty = true;
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
                for (auto& [cdesc, cval] : node->configValues) {
                    if (std::string_view(cdesc.name) == "evolve") {
                        evolveOff = (cval == 0.0f);
                        break;
                    }
                }
            }

            if (!grouped) {
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
                if (changed) { node->push_array(d.name); s_graphDirty = true; }
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
                    s_graphDirty = true;
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

        if (changed) { node->rebuild_formant_spectrum(); s_graphDirty = true; }

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

    // Inline stage table (bare Envelope)
    if (node->typeName == NT_ENVELOPE) {
        auto* env = dynamic_cast<Envelope*>(node->dspSource.get());
        if (env) {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Stages");
            bool absTime = env->absolute_time;
            if (ImGui::Checkbox("seconds (timeMode)", &absTime)) {
                env->absolute_time = absTime;
                s_graphDirty = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("off: stage Pct is a fraction of note duration\n"
                                  "on:  stage Pct is literal seconds (timeMode=seconds)");

            const char* typeNames[] = { "Linear", "Expo", "InverseExpo", "Sine" };
            bool changed = false;
            int  removeIdx = -1;

            ImGui::BeginGroup();
            if (ImGui::BeginTable("stages", 9,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("Pct");
                ImGui::TableSetupColumn("Start");
                ImGui::TableSetupColumn("End");
                ImGui::TableSetupColumn("Curve");
                ImGui::TableSetupColumn("Power");
                ImGui::TableSetupColumn("Hold");
                ImGui::TableSetupColumn("Min Secs");
                ImGui::TableSetupColumn("Max Secs");
                ImGui::TableSetupColumn("");

                // Centered header row (replaces TableHeadersRow's left-aligned labels)
                {
                    static const char* hdrs[] = {
                        "Pct", "Start", "End", "Curve", "Power", "Hold", "Min Secs", "Max Secs", ""
                    };
                    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
                    for (int c = 0; c < 9; ++c) {
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
                    changed |= ImGui::DragFloat("##hold", &s.ramp.holdPct, 0.005f, 0.0f, 1.0f, "%.3f");
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
            ImVec2 leftSize = ImGui::GetItemRectSize();

            // Curve preview to the right of the table
            ImGui::SameLine();

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
                             vmin - vpad, vmax + vpad, ImVec2(plotW, leftSize.y));

            if (changed) s_graphDirty = true;
        }
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

    if (!node.configValues.empty()) {
        json cfg = json::object();
        for (const auto& [desc, val] : node.configValues) cfg[desc.name] = val;
        j["configs"] = cfg;
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

    if (j.contains("configs")) {
        for (auto& [desc, val] : gn.configValues) {
            if (j["configs"].contains(desc.name))
                val = j["configs"][desc.name].get<float>();
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
        for (auto& [desc, val] : gn.configValues)
            val = gn.dspSource->get_config(desc.name);
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
    s_graphDirty = true;

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
    s_graphDirty = true;
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
    s_graphDirty = true;
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
        s_graphDirty = true;
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

    // --- Top level quick access ---
    menu_source("Sine", "SineSource");
    menu_source("Var", "VarSource");
    menu_source("Range", "RangeSource");
    menu_source("Red Noise", "RedNoiseSource");

    // Parameter nodes exist only in NodeGraph mode (keyboard playability);
    // instrument patches bind via the Parameter-mapping dialog instead.
    if (s_graphMode == GraphMode::NodeGraph && ImGui::MenuItem("Parameter")) {
        s_nodes.emplace_back(std::string(NT_PARAMETER), "frequency");
        ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
    }

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

    ImGui::Separator();

    // --- Generators ---
    if (ImGui::BeginMenu("Generators")) {
        menu_source("Sine", "SineSource");
        menu_source("Saw", "SawSource");
        menu_source("Pulse", "PulseSource");
        menu_source("Triangle", "TriangleSource");
        menu_sep();
        menu_source("FM", "FMSource");
        menu_source("Distorted", "DistortedSource");
        menu_source("Hybrid KS", "HybridKSSource");
        menu_source("KS Piano String", "KSPianoString");
        menu_source("Allpass Resonator", "AllpassResonator");
        menu_sep();
        menu_source("Phased", "PhasedValueSource");
        menu_source("Repeating", "RepeatingSource");
        ImGui::EndMenu();
    }

    // --- Combiner ---
    if (ImGui::BeginMenu("Combiner")) {
        menu_source("Combined", "CombinedSource");
        menu_source("Crossfade", "CrossfadeSource");
        menu_source("Multi", "MultiSource");
        menu_source("Multiplex", "MultiplexSource");
        ImGui::EndMenu();
    }

    // --- Wavetable ---
    if (ImGui::BeginMenu("Wavetable")) {
        menu_source("Wavetable", "WavetableSource");
        menu_sep();
        menu_source("EKS Evolution", "EKSEvolution");
        menu_source("Pluck Evolution", "PluckEvolution");
        menu_source("Averaging Evolution", "AveragingEvolution");
        ImGui::EndMenu();
    }

    // --- Physical (continuous-excitation WaveEvolutions) ---
    if (ImGui::BeginMenu("Physical")) {
        menu_source("Reed (clarinet)", "ReedEvolution");
        menu_source("Bowed String", "BowedStringEvolution");
        menu_source("Brass (lip-reed)", "BrassEvolution");
        ImGui::EndMenu();
    }

    // --- Algorithmic (abstract WaveEvolutions, not physical models) ---
    if (ImGui::BeginMenu("Algorithmic")) {
        menu_source("Reaction-Diffusion (Gray-Scott)", "ReactionDiffusionEvolution");
        menu_source("Sort Erosion (->saw)", "SortErosionEvolution");
        menu_source("Cellular Automaton (Wolfram)", "CellularAutomatonEvolution");
        menu_source("Histogram Equalize", "HistogramEqualizeEvolution");
        menu_source("Bezier Pull (->curve)", "BezierPullEvolution");
        menu_source("Bit Rotate (glitch)", "BitRotateEvolution");
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
        menu_sep();
        menu_source("Formant", "Formant");
        menu_source("Formant Sequence", "FormantSequence");
        menu_source("Formant Spectrum", "FormantSpectrum");
        menu_source("Band Spectrum", "BandSpectrum");
        menu_source("Fixed Spectrum", "FixedSpectrum");
        menu_sep();
        menu_source("Expand Rule", "ExpandRule");
        ImGui::EndMenu();
    }

    // --- Noise ---
    if (ImGui::BeginMenu("Noise")) {
        menu_source("White", "WhiteNoiseSource");
        menu_source("Pink", "PinkNoiseSource");
        menu_source("Red", "RedNoiseSource");
        menu_source("Layered Red", "LayeredRedNoiseSource");
        menu_source("Blue", "BlueNoiseSource");
        menu_source("Violet", "VioletNoiseSource");
        menu_sep();
        menu_source("Velvet", "VelvetNoiseSource");
        menu_source("Perlin", "PerlinNoiseSource");
        menu_source("Crackle", "CrackleNoiseSource");
        menu_source("Murmuration", "MurmurationNoiseSource");
        menu_sep();
        menu_source("Segment", "SegmentSource");
        menu_source("Wander 1", "WanderNoiseSource");
        menu_source("Wander 2", "WanderNoise2Source");
        menu_source("Wander 3", "WanderNoise3Source");
        ImGui::EndMenu();
    }

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
        menu_sep();
        menu_source("BW Bandpass", "BWBandpassFilter");
        menu_source("BW Lowpass", "BWLowpassFilter");
        menu_source("BW Highpass", "BWHighpassFilter");
        ImGui::EndMenu();
    }

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
                    s_graphDirty = true;
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
                s_graphDirty = true;
            }
            if (!hasMixer) {
                if (ImGui::MenuItem("Mixer")) {
                    s_nodes.emplace_back(std::string(NT_STEREO_MIXER));
                    ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
                    s_graphDirty = true;
                }
            } else {
                menu_placeholder("Mixer (exists)");
            }
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
    s_graphDirty = true;
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
    s_graphDirty = true;
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
        s_graphDirty = true;
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
        for (int i = 0; i < (int)node->configValues.size() && i < (int)dup.configValues.size(); ++i) {
            dup.configValues[i].second = node->configValues[i].second;
            if (dup.dspSource)
                dup.dspSource->set_config(dup.configValues[i].first.name, dup.configValues[i].second);
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
    float ro1 = ipt->get_config("rolloff1");
    float ro2 = ipt->get_config("rolloff2");
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
    // TEMP DEBUG: dump the curve-target options the Curves window would list
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
            printf("node '%s' (%s): dsp=%s configValues=%zu\n",
                   n.label.c_str(), n.typeName.c_str(),
                   n.dspSource ? n.dspSource->type_name() : "NULL",
                   n.configValues.size());
            for (auto& pin : n.inputs) {
                if (pin.inputOnly || pin.kind != PinKind::Input) continue;
                if (pin.name.substr(0, 3) == "ch ") continue;
                if (is_pin_connected(pin.id)) { printf("    [pin skipped: connected] %s\n", pin.name.c_str()); continue; }
                printf("    pin  %s = %g%s\n", pin.name.c_str(), pin.defaultValue,
                       curve_exists_for("frequency", n.label + "." + pin.name) ? "  (has curve)" : "");
            }
            for (auto& [desc, val] : n.configValues)
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
                for (int i = 0; i < sv.durSamples; ++i)
                    mono[i] = soft_clip(sv.source->next() * sv.gain);
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
            ImGui::DockBuilderDockWindow("Properties", dockRight);  // docked first → selected tab
            ImGui::DockBuilderDockWindow("Curves", dockRight);
            ImGui::DockBuilderDockWindow("Waveforms", dockBottom);  // docked first → selected tab
            ImGui::DockBuilderDockWindow("Spectrum",  dockBottom);
            ImGui::DockBuilderDockWindow("Keyboard",  dockBottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }

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

        ImNodes::BeginNodeEditor();

        s_groupProj = GroupProjection{};
        for (auto& node : s_nodes)
            if (visible_at_path(node.label)) draw_node(node);
        for (auto& g : s_groups)
            if (visible_at_path(g.name)) draw_group_node(g);

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
            if (a >= 0 && b >= 0 && a != b)
                ImNodes::Link(link.id, a, b);
        }

        bool editorHovered = ImNodes::IsEditorHovered();

        ImNodes::EndNodeEditor();

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
                if (ImNodes::IsNodeHovered(&hovered) &&
                    prevSelection.count(hovered)) {
                    ImNodes::ClearNodeSelection(hovered);
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
            if (startPin && endPin && startPin->kind != endPin->kind) {
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
                bool accept = true;
                if (outNode && inNode && inPinDesc) {
                    const char* err = pin_type_compat_error(
                        inNode->typeName, inPinDesc->name, outNode->typeName);
                    if (err) {
                        transport_set_status(err, true);
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
                    s_graphDirty = true;
                }
            }
        }

        // Detached link
        {
            int destroyedLinkId;
            if (ImNodes::IsLinkDestroyed(&destroyedLinkId)) {
                s_links.erase(
                    std::remove_if(s_links.begin(), s_links.end(),
                        [destroyedLinkId](const Link& l) { return l.id == destroyedLinkId; }),
                    s_links.end());
                update_all_dsp();
                s_graphDirty = true;
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
                for (int lid : sel) delete_link(lid);
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
        // Curves window (paramMap frequency→value curve editor)
        // =================================================================
        draw_curves_window();
        draw_mappings_dialog();

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

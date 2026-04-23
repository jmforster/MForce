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
#include <nlohmann/json.hpp>
#include "mforce/render/patch_loader.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/core/source_registry.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/envelope.h"      // needed for Envelope::make_adsr
#include "mforce/core/envelope_presets.h" // ADSREnvelope for NT_ENVELOPE nodes
#include "mforce/core/var_source.h"     // needed for VarSource constructor
#include "mforce/core/range_source.h"   // needed for RangeSource constructor
#include "mforce/source/additive/formant.h" // needed for FormantSpectrum inline table
#include "mforce/render/instrument.h"
#include "mforce/render/mixer.h"
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
};

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

    // Offline-rendered waveform samples for display
    std::vector<float> waveformData;

    // Parameter-specific
    std::string paramName;
    char paramNameBuf[32]{};

    GraphNode(const std::string& type) : id(next_id()), typeName(type), label(node_display_name(type)) {
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
            dspSource = std::make_shared<ADSREnvelope>(DSP_SAMPLE_RATE);
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
        if (!reg.has(typeName)) return;

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
static std::string s_currentFilePath;
// True when the UI graph has been edited since last save/load. Playback paths
// sync to a temp file before loading the instrument so MultiplexSource (and
// anything else that bakes state at patch-load time) picks up current edits.
static bool s_graphDirty = false;

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

static void delete_node(int nodeId) {
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

static void new_graph(GraphMode mode) {
    s_currentFilePath.clear();
    s_nodes.clear();
    s_links.clear();
    s_graphMode = mode;
    s_nextId = 1;
    g_selectedNodeId = -1;

    if (mode == GraphMode::PatchGraph) {
        s_nodes.emplace_back(std::string(NT_PARAMETER), "frequency");
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

static std::string save_file_dialog() {
    char filename[MAX_PATH] = "patch.json";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "json";
    if (GetSaveFileNameA(&ofn))
        return filename;
    return "";
}

static std::string open_file_dialog() {
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "JSON Files\0*.json\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn))
        return filename;
    return "";
}

// ===========================================================================
// JSON import
// ===========================================================================

// json_type_to_node removed — typeName strings are used directly

static bool s_needsLayout = false;

static void load_graph_from_path(const std::string& path) {
    using json = nlohmann::json;

    if (path.empty()) return;

    std::ifstream f(path);
    if (!f) return;
    json root = json::parse(f);
    s_currentFilePath = path;
    s_graphDirty = false;

    s_nodes.clear();
    s_links.clear();
    s_nextId = 1;

    bool hasInstrument = root.contains("instrument");
    s_graphMode = hasInstrument ? GraphMode::PatchGraph : GraphMode::NodeGraph;

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
            rows.push_back(row);
            ids.push_back(fid);
        }
        if (canConsume) {
            specRows[jnode["id"].get<std::string>()] = std::move(rows);
            for (auto& fid : ids) ownedFormants[fid] = true;
        }
    }

    // First pass: create all nodes
    for (const auto& jnode : nodes) {
        std::string id = jnode["id"].get<std::string>();
        std::string type = jnode["type"].get<std::string>();

        // Skip Formants owned by a FormantSpectrum — they'll live inside its row table.
        if (ownedFormants.count(id)) continue;

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
            for (auto& pin : gn.inputs) {
                if (!params.contains(pin.name)) continue;
                const auto& val = params[pin.name];
                if (val.is_number()) {
                    pin.defaultValue = val.get<float>();
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

            // Restore config values
            for (auto& [desc, val] : gn.configValues) {
                if (!params.contains(desc.name)) continue;
                const auto& jval = params[desc.name];
                if (jval.is_boolean()) val = jval.get<bool>() ? 1.0f : 0.0f;
                else if (jval.is_number()) val = jval.get<float>();
            }
            gn.apply_config();
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

        // Create Parameter nodes from paramMap
        if (root["instrument"].contains("paramMap")) {
            for (auto& [paramName, targetStr] : root["instrument"]["paramMap"].items()) {
                std::string target = targetStr.get<std::string>();

                s_nodes.emplace_back(std::string(NT_PARAMETER), paramName);
                GraphNode& pn = s_nodes.back();

                // Find the target input pin and get its current default value
                auto inIt = inputPinMap.find(target);
                if (inIt != inputPinMap.end()) {
                    Pin* targetPin = find_pin(inIt->second);
                    if (targetPin) {
                        pn.inputs[0].defaultValue = targetPin->defaultValue;
                        if (pn.inputs[0].constantSrc)
                            pn.inputs[0].constantSrc->set(targetPin->defaultValue);
                    }

                    // Wire parameter output to target input
                    s_links.emplace_back(pn.outputs[0].id, inIt->second);
                }
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
            else if (node.typeName == NT_PARAMETER)
                key = "__param_" + node.paramName;
            else
                key = node.label;  // label was set to the JSON id

            if (positions.contains(key)) {
                float x = positions[key][0].get<float>();
                float y = positions[key][1].get<float>();
                ImNodes::SetNodeGridSpacePos(node.id, ImVec2(x, y));
            }
        }
    } else {
        s_needsLayout = true;
    }

    // Wire all DSP connections (including RefSource for shared sources)
    update_all_dsp();
}

// Dialog-driven wrapper for load_graph_from_path.
static void load_graph() {
    std::string path = open_file_dialog();
    if (path.empty()) return;
    load_graph_from_path(path);
}

// ===========================================================================
// JSON export
// ===========================================================================

// node_type_to_json removed — typeName strings are used directly

// Generate a short prefix from a type name for JSON node IDs
static std::string type_prefix(const std::string& typeName) {
    if (typeName == NT_SOUND_CHANNEL) return "ch";
    if (typeName == NT_STEREO_MIXER)  return "mix";
    if (typeName == NT_ENVELOPE)      return "env";
    // Strip "Source" suffix for shorter IDs
    std::string name = typeName;
    if (name.size() > 6 && name.substr(name.size() - 6) == "Source")
        name = name.substr(0, name.size() - 6);
    return name;
}

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

static void save_patch_graph(const std::string& path) {
    using json = nlohmann::json;

    // Assign string IDs to nodes
    std::unordered_map<int, std::string> nodeIds;
    std::unordered_map<std::string, int> typeCounts;
    for (auto& node : s_nodes) {
        if (node.typeName == NT_PATCH_OUTPUT || node.typeName == NT_PARAMETER)
            continue;
        int& count = typeCounts[node.typeName];
        count++;
        nodeIds[node.id] = type_prefix(node.typeName) + std::to_string(count);
    }

    // Find Output and Parameter nodes
    GraphNode* outputNode = nullptr;
    std::vector<GraphNode*> paramNodes;
    for (auto& node : s_nodes) {
        if (node.typeName == NT_PATCH_OUTPUT) outputNode = &node;
        if (node.typeName == NT_PARAMETER) paramNodes.push_back(&node);
    }

    // Determine graph output: what's connected to Output's source pin
    std::string outputId;
    if (outputNode && !outputNode->inputs.empty()) {
        GraphNode* src = find_source_node(outputNode->inputs[0].id);
        if (src) outputId = nodeIds[src->id];
    }

    // Build paramMap: trace each Parameter node's output to find target
    json paramMap = json::object();
    for (auto* pn : paramNodes) {
        if (pn->outputs.empty() || pn->paramName.empty()) continue;
        int outPinId = pn->outputs[0].id;
        // Find what this output connects to
        for (auto& link : s_links) {
            int targetPinId = -1;
            if (link.startPinId == outPinId) targetPinId = link.endPinId;
            if (link.endPinId == outPinId) targetPinId = link.startPinId;
            if (targetPinId < 0) continue;

            // Find the target node and pin name
            for (auto& node : s_nodes) {
                for (auto& pin : node.inputs) {
                    if (pin.id == targetPinId && nodeIds.count(node.id)) {
                        paramMap[pn->paramName] = nodeIds[node.id] + "." + pin.name;
                    }
                }
            }
        }
    }

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

        // Envelope: add preset
        if (node.typeName == NT_ENVELOPE) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["preset"] = "adsr";
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
                std::string fid = specId + "__f" + std::to_string(i);
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

        nodes.push_back(jnode);
    }

    // Build final JSON
    json root;
    root["sampleRate"] = 48000;
    root["graph"]["nodes"] = nodes;
    root["graph"]["output"] = outputId;

    if (outputNode) {
        root["instrument"]["polyphony"] = outputNode->polyphony;
        if (!paramMap.empty())
            root["instrument"]["paramMap"] = paramMap;
    }

    // Default score + duration so the CLI can render this patch standalone.
    // Conservative defaults; user can edit JSON to customize.
    root["seconds"] = 3.0f;
    root["score"] = json::array({
        json{
            {"note",     60},
            {"velocity", 0.8f},
            {"time",     0.0f},
            {"duration", 2.0f}
        }
    });

    // Save UI layout
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
    for (auto* pn : paramNodes) {
        if (pn->paramName.empty()) continue;
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(pn->id);
        positions["__param_" + pn->paramName] = {pos.x, pos.y};
    }
    root["ui"]["positions"] = positions;

    std::ofstream f(path);
    f << root.dump(2);
    f.close();
}

static void save_node_graph(const std::string& path) {
    using json = nlohmann::json;

    // Assign string IDs
    std::unordered_map<int, std::string> nodeIds;
    std::unordered_map<std::string, int> typeCounts;
    for (auto& node : s_nodes) {
        int& count = typeCounts[node.typeName];
        count++;
        nodeIds[node.id] = type_prefix(node.typeName) + std::to_string(count);
    }

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
            } else {
                if (src)
                    params[pin.name] = json{{"ref", nodeIds[src->id]}};
                else
                    params[pin.name] = pin.defaultValue;
            }
        }
        if (!params.empty()) jnode["params"] = params;

        if (node.typeName == NT_ENVELOPE) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["preset"] = "adsr";
        }

        if (node.typeName == "FormantSpectrum" && !node.formantRows.empty()) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            json refs = json::array();
            const std::string& specId = nodeIds[node.id];
            for (size_t i = 0; i < node.formantRows.size(); ++i) {
                const auto& row = node.formantRows[i];
                std::string fid = specId + "__f" + std::to_string(i);
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

        nodes.push_back(jnode);
    }

    json root;
    root["sampleRate"] = 48000;
    root["seconds"] = 5;
    root["graph"]["nodes"] = nodes;
    root["graph"]["output"] = outputId;

    // Save UI layout
    json positions = json::object();
    for (auto* nodePtr : sorted) {
        ImVec2 pos = ImNodes::GetNodeGridSpacePos(nodePtr->id);
        positions[nodeIds[nodePtr->id]] = {pos.x, pos.y};
    }
    root["ui"]["positions"] = positions;

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
}

// Returns the path to a patch file reflecting current UI state — either
// s_currentFilePath (if no edits pending) or a temp file we sync-write to.
// Playback paths should use this instead of s_currentFilePath directly so
// MultiplexSource and other load-time-baked constructs see current edits.
static std::string get_playback_patch_path() {
    // Use the saved file if one exists and the graph hasn't been edited.
    if (!s_graphDirty && !s_currentFilePath.empty()) return s_currentFilePath;
    // Otherwise (edited since last save, or never saved) write current state
    // to a temp file and return that path. User's explicit save file is
    // untouched until they explicitly Save.
    std::string tmp = (std::filesystem::temp_directory_path() / "mforce_playback.json").string();
    if (s_graphMode == GraphMode::PatchGraph) save_patch_graph(tmp);
    else save_node_graph(tmp);
    s_graphDirty = false;
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
static constexpr int AUDIO_CHUNK = 1024;
static constexpr int NUM_AUDIO_BUFS = 4;

// ===========================================================================
// Offline waveform display buffers
// ===========================================================================
static std::vector<float> g_outputWaveform;  // final output waveform
static int g_waveformSamples = 0;            // number of samples in waveform buffers

static int g_waveZoom = 1;        // samples per pixel (1 = most zoomed in)
static int g_waveScrollPos = 0;   // starting sample offset into available data
static int g_waveColumns = 1;     // number of columns for waveform tiling

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

static HWAVEOUT g_waveOut = nullptr;
static WAVEHDR  g_waveHdrs[NUM_AUDIO_BUFS] = {};
static int16_t  g_audioBufs[NUM_AUDIO_BUFS][AUDIO_CHUNK * 2] = {};

static ValueSource* g_streamSource = nullptr;
static int g_streamRemaining = 0;
static float g_streamVelocity = 0.5f;

// Buffer playback: stream from a pre-rendered buffer (e.g. g_outputWaveform)
static const float* g_bufferPlayback = nullptr;
static int g_bufferPlaybackPos = 0;
static int g_bufferPlaybackLen = 0;

// Polyphonic voice pool: pre-rendered note buffers that mix together
static constexpr int MAX_VOICES = 16;
struct Voice {
    std::vector<float> samples;
    int pos = 0;
    bool active = false;
    int midiNote = 0;  // for keyboard highlight
};
static Voice g_voices[MAX_VOICES];

static void voice_play(std::vector<float>&& buf, int midiNote = 0) {
    // Find a free voice, or steal the oldest
    int slot = -1;
    for (int i = 0; i < MAX_VOICES; ++i) {
        if (!g_voices[i].active) { slot = i; break; }
    }
    if (slot < 0) {
        // Steal voice with most progress
        int best = 0;
        for (int i = 1; i < MAX_VOICES; ++i)
            if (g_voices[i].pos > g_voices[best].pos) best = i;
        slot = best;
    }
    g_voices[slot].samples = std::move(buf);
    g_voices[slot].pos = 0;
    g_voices[slot].active = true;
    g_voices[slot].midiNote = midiNote;
}

static bool any_voice_active() {
    for (int i = 0; i < MAX_VOICES; ++i)
        if (g_voices[i].active) return true;
    return false;
}

static bool is_playing() {
    return g_streamSource != nullptr || g_bufferPlayback != nullptr || any_voice_active();
}

static void fill_audio_buffer(int bufIdx) {
    auto* out = g_audioBufs[bufIdx];
    auto* hdr = &g_waveHdrs[bufIdx];

    for (int i = 0; i < AUDIO_CHUNK; ++i) {
        float s = 0.0f;

        // Mix active voices
        for (int v = 0; v < MAX_VOICES; ++v) {
            auto& voice = g_voices[v];
            if (voice.active) {
                s += voice.samples[voice.pos++];
                if (voice.pos >= (int)voice.samples.size())
                    voice.active = false;
            }
        }

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

        s = std::clamp(s, -1.0f, 1.0f);

        int16_t v = int16_t(s < 0 ? s * 32768.0f : s * 32767.0f);
        out[i * 2]     = v;
        out[i * 2 + 1] = v;
    }

    hdr->lpData = reinterpret_cast<LPSTR>(out);
    hdr->dwBufferLength = AUDIO_CHUNK * 2 * sizeof(int16_t);
    hdr->dwFlags = 0;

    waveOutPrepareHeader(g_waveOut, hdr, sizeof(WAVEHDR));
    waveOutWrite(g_waveOut, hdr, sizeof(WAVEHDR));
}

// Called each frame from the main loop — refill any completed buffers
static void pump_audio() {
    if (!g_waveOut) return;

    for (int i = 0; i < NUM_AUDIO_BUFS; ++i) {
        if (g_waveHdrs[i].dwFlags & WHDR_DONE) {
            waveOutUnprepareHeader(g_waveOut, &g_waveHdrs[i], sizeof(WAVEHDR));
            fill_audio_buffer(i);
        }
    }
}

static bool init_audio() {
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = AUDIO_SAMPLE_RATE;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    // No callback — we poll from the main loop
    MMRESULT res = waveOutOpen(&g_waveOut, WAVE_MAPPER, &fmt, 0, 0, CALLBACK_NULL);
    if (res != MMSYSERR_NOERROR) return false;

    // Prime all buffers
    for (int i = 0; i < NUM_AUDIO_BUFS; ++i) {
        g_waveHdrs[i] = {};
        fill_audio_buffer(i);
    }

    return true;
}

static void shutdown_audio() {
    g_streamSource = nullptr;
    if (g_waveOut) {
        waveOutReset(g_waveOut);
        for (int i = 0; i < NUM_AUDIO_BUFS; ++i) {
            if (g_waveHdrs[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(g_waveOut, &g_waveHdrs[i], sizeof(WAVEHDR));
        }
        waveOutClose(g_waveOut);
        g_waveOut = nullptr;
    }
}

// Find the DSP source connected to the Output node
static ValueSource* find_output_source() {
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

// Offline render: populate per-node waveformData and g_outputWaveform for display
// Overwrite g_outputWaveform with authoritative audio for a passage (note
// sequence) via load_instrument_patch + PitchedInstrument. Per-node
// waveform data still comes from render_passage_waveforms' UI DSP pass.
static void render_passage_output_authoritative(
    const std::vector<ParsedNote>& notes, float velocity)
{
    if (notes.empty()) return;
    std::string path = get_playback_patch_path();
    if (path.empty()) return;

    try {
        auto ip = load_instrument_patch(path);
        auto* pitched = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        if (!pitched) return;

        ip.instrument->volume = 1.0f;
        float timeCursor = 0.0f;
        for (const auto& pn : notes) {
            pitched->play_note(pn.noteNumber, velocity, pn.durationSeconds, timeCursor);
            timeCursor += pn.durationSeconds;
        }

        float totalSeconds = timeCursor + 0.5f;
        int frames = int(totalSeconds * float(ip.sampleRate));
        g_outputWaveform.assign(frames, 0.0f);
        g_waveformSamples = frames;
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, g_outputWaveform.data(), frames);

        g_waveScrollPos = 0;
        g_waveZoom = std::max(1, frames / 800);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "render_passage_output_authoritative failed: %s\n", e.what());
    }
}

// Overwrite g_outputWaveform with authoritative audio produced via
// load_instrument_patch — the same path CLI `mforce_cli` uses, so
// MultiplexSource fan-out (and any other load-time constructs) apply.
// Called after render_waveforms during Generate so per-node displays still
// use the UI DSP tree for per-node waveforms, but the main g_outputWaveform
// and Play path reflect what the patch will really sound like.
static void render_output_authoritative(float noteNum, float velocity,
                                        float durationSeconds) {
    std::string path = get_playback_patch_path();
    if (path.empty()) return;

    try {
        auto ip = load_instrument_patch(path);
        auto* pitched = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        if (!pitched) return;

        ip.instrument->volume = 1.0f;
        pitched->play_note(noteNum, velocity, durationSeconds, 0.0f);

        int frames = int((durationSeconds + 0.5f) * float(ip.sampleRate));
        g_outputWaveform.assign(frames, 0.0f);
        g_waveformSamples = frames;
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, g_outputWaveform.data(), frames);

        // Reset waveform view to show the full authoritative buffer.
        g_waveScrollPos = 0;
        g_waveZoom = std::max(1, frames / 800);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "render_output_authoritative failed: %s\n", e.what());
    }
}

static void render_waveforms(float noteNum, float velocity, float durationSeconds) {
    ValueSource* src = find_output_source();
    if (!src) return;

    // Set frequency on Parameter nodes named "frequency"
    float freq = note_to_freq(noteNum);
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

    // Render the full note offline
    for (int i = 0; i < samples; ++i) {
        float s = src->next();
        g_outputWaveform[i] = s * velocity;

        // Capture each node's current output
        for (auto& n : s_nodes) {
            if (!n.waveformData.empty())
                n.waveformData[i] = n.dspSource->current();
        }
    }

    // Reset zoom to fit entire waveform, reset scroll
    g_waveScrollPos = 0;
    g_waveZoom = std::max(1, samples / 800);
}

// Play a note: render offline into a voice buffer for polyphonic mixing.
// Also updates the waveform display with the most recent note.
//
// Audio path routes through load_instrument_patch(temp) so the CLI loader's
// voice-pool + paramMap machinery (including MultiplexSource fan-out into
// internal clones) applies to live-keyboard playback. The UI's in-memory
// DSP tree is used only for the waveform display — that path shows the
// UI's solo-preview state without the fan-out, which is fine for a visual.
static void play_note(float noteNum, float velocity, float durationSeconds) {
    if (s_graphMode != GraphMode::PatchGraph) return;

    // Render for waveform display (shows last-played note via UI DSP tree).
    render_waveforms(noteNum, velocity, durationSeconds);

    // Audio path: load a fresh instrument from the current UI state (synced
    // to a temp file if dirty) and play_note on it, so Multiplex clones
    // retune correctly via paramMap fan-out.
    std::string path = get_playback_patch_path();
    if (path.empty()) return;

    try {
        auto ip = load_instrument_patch(path);
        auto* pitched = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        if (!pitched) return;  // non-pitched instruments not supported here

        ip.instrument->volume = 1.0f;
        pitched->play_note(noteNum, velocity, durationSeconds, 0.0f);

        // +0.5s tail so a patch's own release/decay has room past the note's
        // nominal duration. Matches the feel of a sustained keyboard press.
        int frames = int((durationSeconds + 0.5f) * float(ip.sampleRate));
        std::vector<float> mono(frames, 0.0f);
        RenderContext ctx{ip.sampleRate};
        ip.instrument->render(ctx, mono.data(), frames);

        voice_play(std::move(mono), int(noteNum));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "play_note failed: %s\n", e.what());
    }
}

// Start continuous streaming — uses a long fixed duration for Envelope compat
static void play_continuous(float velocity) {
    if (s_graphMode != GraphMode::PatchGraph) return;

    ValueSource* src = find_output_source();
    if (!src) return;

    g_streamSource = nullptr;

    int samples = AUDIO_SAMPLE_RATE * 30;
    prepare_graph(samples);

    g_streamVelocity = velocity;
    g_streamRemaining = -1;
    g_streamSource = src;
}

static void stop_playback() {
    g_streamSource = nullptr;
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

static void draw_keyboard_panel() {
    ImGui::Begin("Keyboard", nullptr, ImGuiWindowFlags_NoCollapse);

    // --- Header bar ---
    ImGui::Text("Octave");
    ImGui::SameLine();
    spinner_int("kb_oct", &g_keyboard.octave, 1, 0, 8);

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    ImGui::Text("Duration");
    ImGui::SameLine();
    spinner_float("kb_dur", &g_keyboard.duration, 0.05f, 0.05f, 8.0f, "%.2f");

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    ImGui::Text("Velocity");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderFloat("##vel", &g_keyboard.velocity, 0.0f, 1.0f, "%.2f");

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    if (ImGui::Checkbox("Sustain", &g_keyboard.sustain)) {}

    ImGui::SameLine();
    ImGui::Spacing(); ImGui::SameLine();

    // Wire to Transport's noteMode
    ImGui::Checkbox("PC Keyboard##kb", &g_transport.noteMode);

    // --- QWERTY input (gated by note mode and not typing in text field) ---
    if (g_transport.noteMode && !ImGui::GetIO().WantTextInput) {
        for (int i = 0; i < QWERTY_MAP_COUNT; ++i) {
            if (ImGui::IsKeyPressed(s_qwertyMap[i].key, false)) {
                int absNote = (g_keyboard.octave + 1) * 12 + s_qwertyMap[i].offset;
                play_note(float(absNote), g_keyboard.velocity, g_keyboard.duration);
            }
        }
        // Action keys
        if (ImGui::IsKeyPressed(ImGuiKey_G, false))
            g_keyboard.octave = std::max(0, g_keyboard.octave - 1);
        if (ImGui::IsKeyPressed(ImGuiKey_H, false))
            g_keyboard.octave = std::min(8, g_keyboard.octave + 1);
        if (ImGui::IsKeyPressed(ImGuiKey_V, false))
            g_keyboard.duration = std::max(0.05f, g_keyboard.duration * 0.5f);
        if (ImGui::IsKeyPressed(ImGuiKey_B, false))
            g_keyboard.duration = std::min(8.0f, g_keyboard.duration * 2.0f);
    }

    // --- Piano keyboard rendering via ImDrawList ---
    const int NUM_OCTAVES = 2;
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

    int baseNote = (g_keyboard.octave + 1) * 12;

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

            if (g_transport.noteMode) {
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

            if (g_transport.noteMode) {
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
        float hitVelocity = g_keyboard.velocity;

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

// Forward declaration (defined after transport_generate)
static void transport_set_status(const char* msg, bool isError);

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

        // Peak-normalize to prevent distortion from overlapping notes
        float peak = 0.0f;
        for (int i = 0; i < frames; ++i) {
            float a = std::fabs(mono[i]);
            if (a > peak) peak = a;
        }
        if (peak > 1.0f) {
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

static void transport_set_status(const char* msg, bool isError) {
    snprintf(g_transport.statusMsg, sizeof(g_transport.statusMsg), "%s", msg);
    g_transport.statusIsError = isError;
}

static void transport_generate() {
    g_transport.statusMsg[0] = '\0';
    g_transport.statusIsError = false;

    switch (g_transport.mode) {
        case PlayMode::Note: {
            if (!find_output_source()) {
                transport_set_status("No patch loaded — open a patch first", true);
                break;
            }
            float noteNum = parse_note_input(g_transport.noteStr);
            // Per-node waveforms from UI DSP tree (fast, for inspector views).
            render_waveforms(noteNum, g_transport.velocity, g_transport.duration);
            // Authoritative audio into g_outputWaveform (slow for fat Multiplex;
            // UI blocks here until done — that's the visible feedback that
            // generation is running. Play then just streams the buffer.)
            render_output_authoritative(noteNum, g_transport.velocity,
                                        g_transport.duration);
            transport_set_status("Generated note", false);
            break;
        }
        case PlayMode::Passage: {
            if (!find_output_source()) {
                transport_set_status("No patch loaded — open a patch first", true);
                break;
            }
            try {
                auto notes = parse_passage(g_transport.passageStr, g_transport.octave, g_transport.bpm);
                if (notes.empty()) {
                    transport_set_status("No notes parsed from passage string", true);
                } else {
                    render_passage_waveforms(notes, g_transport.velocity);
                    render_passage_output_authoritative(notes, g_transport.velocity);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Generated %d notes", (int)notes.size());
                    transport_set_status(buf, false);
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
    if (s_graphMode != GraphMode::PatchGraph) return;

    switch (g_transport.mode) {
        case PlayMode::Note: {
            // Stream the pre-rendered buffer produced during Generate. If the
            // user hasn't Generated yet (or changed the note since last Gen),
            // generate now so Play reflects current settings.
            if (g_outputWaveform.empty() || g_waveformSamples == 0)
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
    ofn.lpstrInitialDir = "renders";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = "wav";
    if (GetSaveFileNameA(&ofn))
        return filename;
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
        bool noteMode = g_transport.noteMode;
        if (noteMode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.6f, 0.3f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.7f, 0.4f, 1.0f));
        }
        if (ImGui::Button("PC Keyboard")) {
            g_transport.noteMode = !g_transport.noteMode;
        }
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

// ===========================================================================
// Properties panel — full editing UI for selected node
// ===========================================================================

static void draw_properties_panel() {
    ImGui::Begin("Properties", nullptr,
                 ImGuiWindowFlags_NoCollapse);

    GraphNode* node = find_selected_node();
    if (!node) {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Select a node to edit");
        ImGui::End();
        return;
    }

    // Header
    ImGui::TextColored(ImColor(node_title_color(node->typeName)).Value, "%s", node->label.c_str());
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "(%s)", node_display_name(node->typeName).c_str());
    ImGui::Separator();

    // Layout: label on left (120px), widget on right
    float labelW = 120.0f;
    float widgetW = ImGui::GetContentRegionAvail().x - labelW;

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
        if (connected) {
            GraphNode* srcNode = find_source_node(pin.id);
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1), "<-- %s", srcNode ? srcNode->label.c_str() : "?");
        } else {
            ImGui::PushItemWidth(widgetW);
            char label[64];
            snprintf(label, sizeof(label), "##prop%d", pin.id);
            // Step = 1% of current value (min 0.001), fast step = 10x
            float step = std::max(0.001f, std::abs(pin.defaultValue) * 0.01f);
            if (ImGui::InputFloat(label, &pin.defaultValue, step, step * 10.0f, "%.4f")) {
                if (pin.constantSrc) pin.constantSrc->set(pin.defaultValue);
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

        // Column headers
        ImGui::Text("  Freq       Gain       Width      Power");

        bool changed = false;
        int removeIdx = -1;
        for (int i = 0; i < (int)node->formantRows.size(); ++i) {
            auto& row = node->formantRows[i];
            ImGui::PushID(i);

            ImGui::PushItemWidth(70);
            changed |= ImGui::DragFloat("##freq", &row.frequency, 10.0f, 1.0f, 20000.0f, "%.0f");
            ImGui::SameLine();
            changed |= ImGui::DragFloat("##gain", &row.gain, 0.01f, 0.0f, 10.0f, "%.2f");
            ImGui::SameLine();
            changed |= ImGui::DragFloat("##wid", &row.width, 10.0f, 1.0f, 10000.0f, "%.0f");
            ImGui::SameLine();
            changed |= ImGui::DragFloat("##pow", &row.power, 0.05f, 0.01f, 10.0f, "%.2f");
            ImGui::PopItemWidth();

            ImGui::SameLine();
            if (ImGui::SmallButton(" x ")) removeIdx = i;

            ImGui::PopID();
        }

        if (removeIdx >= 0 && node->formantRows.size() > 1) {
            node->formantRows.erase(node->formantRows.begin() + removeIdx);
            changed = true;
        }
        if (ImGui::SmallButton(" + Add formant ")) {
            node->formantRows.push_back({1000.0f, 1.0f, 500.0f, 2.0f});
            changed = true;
        }
        if (changed) { node->rebuild_formant_spectrum(); s_graphDirty = true; }
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

    if (ImGui::MenuItem("Parameter")) {
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

    // --- Exotic ---
    if (ImGui::BeginMenu("Exotic")) {
        menu_source("Sort Oscillator", "SortOscillator");
        menu_source("Gray-Scott", "GrayScottSource");
        menu_source("FitzHugh-Nagumo", "FitzhughNagumoSource");
        menu_sep();
        menu_source("Markov ODE", "MarkovOdeSource");
        menu_source("Mass-Spring", "MassSpringSource");
        menu_source("Self-Rewriting AST", "SelfRewritingASTSource");
        menu_sep();
        menu_source("SAT/DPLL", "SatDpllSource");
        menu_source("Micro-NN", "MicroNNSource");
        menu_source("Self-Avoiding Walk", "SelfAvoidingWalkSource");
        menu_source("Homotopy", "HomotopySource");
        menu_source("LDPC Decoder", "LDPCSource");
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
        menu_source("Blown Tube (flute)", "BlownTubeEvolution");
        menu_source("Reed (clarinet)", "ReedEvolution");
        menu_source("Bowed String", "BowedStringEvolution");
        menu_source("Brass (lip-reed)", "BrassEvolution");
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
        menu_source("BW Bandpass", "BWBandpassFilter");
        menu_source("BW Lowpass", "BWLowpassFilter");
        menu_source("BW Highpass", "BWHighpassFilter");
        ImGui::EndMenu();
    }

    ImGui::EndPopup();
}

// Node context menu (right-click on existing node)
static void show_node_context_menu() {
    if (!ImGui::BeginPopup("NodeContextMenu")) return;

    GraphNode* node = nullptr;
    for (auto& n : s_nodes) if (n.id == s_contextNodeId) { node = &n; break; }

    if (!node) { ImGui::EndPopup(); return; }

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

    ImGui::EndPopup();
}

// ===========================================================================
// Waveform display
// ===========================================================================

static constexpr float WAVE_MARGIN_W = 45.0f;  // left margin for peak values

static void draw_waveform(const char* label, const float* buf, int sampleCount,
                          ImU32 waveColor,
                          float x, float y, float width, float height) {
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
            int sampleStart = g_waveScrollPos + px * g_waveZoom;
            if (sampleStart >= sampleCount) break;

            float minVal = 1.0f, maxVal = -1.0f;
            int sampleEnd = std::min(sampleStart + g_waveZoom, sampleCount);
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
    if (ImGui::ArrowButton("##zo", ImGuiDir_Left)) g_waveZoom = std::min(4096, g_waveZoom * 2);
    ImGui::SameLine(0, 2);
    ImGui::Text("%dx", g_waveZoom);
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##zi", ImGuiDir_Right)) g_waveZoom = std::max(1, g_waveZoom / 2);
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

    // Calculate strip heights from remaining space
    int dspNodeCount = 0;
    for (auto& n : s_nodes)
        if (!n.waveformData.empty()) dspNodeCount++;

    float remainH = ImGui::GetContentRegionAvail().y;
    int totalStrips = 1 + dspNodeCount;  // output + per-node
    int totalRows = (totalStrips + g_waveColumns - 1) / g_waveColumns;
    float stripH = std::max(25.0f, (remainH - totalRows * 2.0f) / float(totalRows));

    // Scrollable waveform area
    ImGui::BeginChild("WaveScroll", ImVec2(0, 0), false);

    ImVec2 cursor = ImGui::GetCursorScreenPos();
    float yOff = 0.0f;

    // Collect all strips: output first, then per-node
    struct WaveStrip { const char* label; const float* buf; int count; ImU32 color; };
    std::vector<WaveStrip> strips;
    static const ImU32 audioColor = IM_COL32(80, 220, 80, 255);     // bright green
    static const ImU32 envelopeColor = IM_COL32(220, 220, 60, 255); // yellow

    strips.push_back({"Output",
                      g_outputWaveform.empty() ? nullptr : g_outputWaveform.data(),
                      g_waveformSamples, audioColor});

    auto& reg = SourceRegistry::instance();
    for (auto& n : s_nodes) {
        if (n.waveformData.empty()) continue;
        SourceCategory cat = reg.has(n.typeName) ? reg.get_category(n.typeName) : SourceCategory::Utility;
        ImU32 col = (cat == SourceCategory::Envelope) ? envelopeColor : audioColor;
        strips.push_back({n.label.c_str(),
                          n.waveformData.data(), (int)n.waveformData.size(),
                          col});
    }

    // Layout strips in g_waveColumns columns
    int N = g_waveColumns;
    float gap = 4.0f;
    float colW = (waveAreaW - (N - 1) * gap) / float(N);

    for (int i = 0; i < (int)strips.size(); ++i) {
        int col = i % N;
        int row = i / N;
        float xPos = cursor.x + col * (colW + gap);
        float yPos = cursor.y + row * (stripH + 2.0f);
        draw_waveform(strips[i].label, strips[i].buf, strips[i].count,
                      strips[i].color, xPos, yPos, colW, stripH);
    }

    int actualRows = ((int)strips.size() + N - 1) / N;
    yOff = actualRows * (stripH + 2.0f);
    ImGui::Dummy(ImVec2(waveAreaW, yOff));

    // Mouse wheel zoom
    if (ImGui::IsWindowHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel > 0.0f && g_waveZoom > 1)
            g_waveZoom = std::max(1, g_waveZoom / 2);
        else if (wheel < 0.0f && g_waveZoom < 4096)
            g_waveZoom = std::min(4096, g_waveZoom * 2);
    }

    // Click-drag scroll
    if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        float dx = ImGui::GetIO().MouseDelta.x;
        g_waveScrollPos -= (int)(dx * g_waveZoom);
        g_waveScrollPos = std::clamp(g_waveScrollPos, 0, std::max(1, maxScroll));
    }

    ImGui::EndChild();
    ImGui::End(); // Waveforms
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char** argv) {
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1400, 900, "MForce - Patch Editor", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImNodes::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
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

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Init audio
    if (!init_audio()) {
        // Non-fatal — UI works without audio
    }

    // Initialize the source registry before creating any nodes
    register_all_sources();

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
                char buf[512];
                snprintf(buf, sizeof(buf), "Loaded: %s", patchArg.c_str());
                transport_set_status(buf, false);
            } catch (const std::exception& e) {
                char buf[512];
                snprintf(buf, sizeof(buf), "Failed to load %s: %s",
                         patchArg.c_str(), e.what());
                transport_set_status(buf, true);
            }
        }
    }
    bool s_firstFrame = true;
    bool s_dockLayoutInitialized = false;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

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

        // Layout nodes (first frame or after load)
        if (s_firstFrame || s_needsLayout) {
            float x = 50, y = 80;
            for (int i = 0; i < (int)s_nodes.size(); ++i) {
                ImNodes::SetNodeScreenSpacePos(s_nodes[i].id, ImVec2(x, y));
                x += 220;
                if (x > 900) { x = 50; y += 250; }
            }
            s_firstFrame = false;
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
        glfwSetWindowTitle(window, titleBuf);

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
                        s_firstFrame = true;
                    }
                    if (ImGui::MenuItem("Node Graph")) {
                        new_graph(GraphMode::NodeGraph);
                        s_firstFrame = true;
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Open", "Ctrl+O")) {
                    load_graph();
                }
                if (ImGui::MenuItem("Save", "Ctrl+S")) {
                    save_graph();
                }
                if (ImGui::MenuItem("Save As...")) {
                    save_graph_as();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
                ImGui::EndMenu();
            }

            // Play/Stop in menu bar (wired through transport state)
            if (s_graphMode == GraphMode::PatchGraph) {
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

            // Split bottom: waveforms on left, keyboard on right
            ImGuiID dockBottomLeft;
            ImGuiID dockBottomRight;
            ImGui::DockBuilderSplitNode(dockBottom, ImGuiDir_Right, 0.4f, &dockBottomRight, &dockBottomLeft);

            // Dock windows
            ImGui::DockBuilderDockWindow("Transport", dockTransport);
            ImGui::DockBuilderDockWindow("Node Editor", dockCenter);
            ImGui::DockBuilderDockWindow("Properties", dockRight);
            ImGui::DockBuilderDockWindow("Waveforms", dockBottomLeft);
            ImGui::DockBuilderDockWindow("Keyboard", dockBottomRight);

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

        ImNodes::BeginNodeEditor();

        for (auto& node : s_nodes)
            draw_node(node);
        for (auto& link : s_links)
            ImNodes::Link(link.id, link.startPinId, link.endPinId);

        bool editorHovered = ImNodes::IsEditorHovered();

        ImNodes::EndNodeEditor();

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

        // Arrow key panning (when not editing text)
        if (!ImGui::GetIO().WantTextInput) {
            float panSpeed = ImGui::GetIO().KeyShift ? 200.0f : 50.0f;
            auto p = ImNodes::EditorContextGetPanning();
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  ImNodes::EditorContextResetPanning(ImVec2(p.x + panSpeed, p.y));
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) ImNodes::EditorContextResetPanning(ImVec2(p.x - panSpeed, p.y));
            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    ImNodes::EditorContextResetPanning(ImVec2(p.x, p.y + panSpeed));
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  ImNodes::EditorContextResetPanning(ImVec2(p.x, p.y - panSpeed));
        }

        if (!ImGui::GetIO().WantTextInput &&
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
        // Waveform display window
        // =================================================================
        draw_waveform_window();

        // =================================================================
        // Keyboard panel
        // =================================================================
        draw_keyboard_panel();

        // Pump audio: refill any completed waveOut buffers
        pump_audio();

        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

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
}

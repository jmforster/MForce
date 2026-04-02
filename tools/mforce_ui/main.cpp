#include "imgui.h"
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
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp_wave_source.h"
#include "mforce/source/sine_source.h"
#include "mforce/source/saw_source.h"
#include "mforce/source/triangle_source.h"
#include "mforce/source/pulse_source.h"
#include "mforce/source/white_noise_source.h"
#include "mforce/source/red_noise_source.h"
#include "mforce/core/envelope.h"
#include "mforce/core/var_source.h"
#include "mforce/core/range_source.h"
#include "mforce/render/mixer.h"

using namespace mforce;

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <cstdio>
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
    std::shared_ptr<ConstantSource> constantSrc;  // holds editable value for unconnected pins

    Pin(const std::string& n, PinKind k, float def = 0.0f)
        : id(next_id()), name(n), kind(k), defaultValue(def)
        , constantSrc(std::make_shared<ConstantSource>(def)) {}
};

enum class NodeType {
    SineSource, SawSource, TriangleSource, PulseSource,
    WhiteNoiseSource, RedNoiseSource,
    Envelope,
    VarSource, RangeSource,
    SoundChannel, StereoMixer,
    PatchOutput,
    Parameter
};

static const char* node_type_name(NodeType t) {
    switch (t) {
        case NodeType::SineSource:       return "Sine";
        case NodeType::SawSource:        return "Saw";
        case NodeType::TriangleSource:   return "Triangle";
        case NodeType::PulseSource:      return "Pulse";
        case NodeType::WhiteNoiseSource: return "White Noise";
        case NodeType::RedNoiseSource:   return "Red Noise";
        case NodeType::Envelope:         return "Envelope";
        case NodeType::VarSource:        return "Var";
        case NodeType::RangeSource:      return "Range";
        case NodeType::SoundChannel:     return "Channel";
        case NodeType::StereoMixer:      return "Mixer";
        case NodeType::PatchOutput:      return "Output";
        case NodeType::Parameter:        return "Parameter";
    }
    return "?";
}

static ImU32 node_title_color(NodeType t) {
    switch (t) {
        case NodeType::SineSource:
        case NodeType::SawSource:
        case NodeType::TriangleSource:
        case NodeType::PulseSource:
            return IM_COL32(70, 100, 180, 255);   // blue — oscillators
        case NodeType::WhiteNoiseSource:
        case NodeType::RedNoiseSource:
            return IM_COL32(140, 80, 80, 255);    // red-brown — noise
        case NodeType::Envelope:
            return IM_COL32(80, 140, 80, 255);    // green — envelope
        case NodeType::VarSource:
        case NodeType::RangeSource:
            return IM_COL32(140, 120, 60, 255);   // gold — utility
        case NodeType::SoundChannel:
        case NodeType::StereoMixer:
            return IM_COL32(100, 100, 100, 255);  // gray — mixer/channel
        case NodeType::PatchOutput:
            return IM_COL32(60, 150, 150, 255);   // teal — output
        case NodeType::Parameter:
            return IM_COL32(180, 100, 180, 255);  // purple — parameter
    }
    return IM_COL32(128, 128, 128, 255);
}

static constexpr int DSP_SAMPLE_RATE = 48000;

struct GraphNode {
    int id;
    NodeType type;
    std::string label;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    // Live DSP object
    std::shared_ptr<ValueSource> dspSource;

    // PatchOutput-specific
    int polyphony{4};

    // Parameter-specific
    std::string paramName;
    char paramNameBuf[32]{};

    GraphNode(NodeType t) : id(next_id()), type(t), label(node_type_name(t)) {
        build_pins();
        create_dsp();
    }

    GraphNode(NodeType t, const std::string& name) : GraphNode(t) {
        if (t == NodeType::Parameter) {
            paramName = name;
            label = name;
            snprintf(paramNameBuf, sizeof(paramNameBuf), "%s", name.c_str());
        }
    }

    // Create the DSP object and wire pins' ConstantSources as defaults
    void create_dsp() {
        switch (type) {
            case NodeType::SineSource: {
                auto s = std::make_shared<SineSource>(DSP_SAMPLE_RATE);
                wire_wave_defaults(s);
                dspSource = s;
                break;
            }
            case NodeType::SawSource: {
                auto s = std::make_shared<SawSource>(DSP_SAMPLE_RATE);
                wire_wave_defaults(s);
                dspSource = s;
                break;
            }
            case NodeType::TriangleSource: {
                auto s = std::make_shared<TriangleSource>(DSP_SAMPLE_RATE);
                wire_wave_defaults(s);
                if (auto* p = find_input("bias"))
                    s->set_bias(p->constantSrc);
                dspSource = s;
                break;
            }
            case NodeType::PulseSource: {
                auto s = std::make_shared<PulseSource>(DSP_SAMPLE_RATE);
                wire_wave_defaults(s);
                if (auto* p = find_input("dutyCycle"))
                    s->set_duty_cycle(p->constantSrc);
                if (auto* p = find_input("bend"))
                    s->set_bend(p->constantSrc);
                dspSource = s;
                break;
            }
            case NodeType::WhiteNoiseSource: {
                auto s = std::make_shared<WhiteNoiseSource>(DSP_SAMPLE_RATE);
                dspSource = s;
                break;
            }
            case NodeType::RedNoiseSource: {
                auto s = std::make_shared<RedNoiseSource>(DSP_SAMPLE_RATE);
                wire_wave_defaults(s);
                if (auto* p = find_input("density"))       s->density = p->constantSrc;
                if (auto* p = find_input("smoothness"))    s->smoothness = p->constantSrc;
                if (auto* p = find_input("rampVariation")) s->rampVariation = p->constantSrc;
                if (auto* p = find_input("boost"))         s->boost = p->constantSrc;
                if (auto* p = find_input("continuity"))    s->continuity = p->constantSrc;
                if (auto* p = find_input("zeroCrossTend")) s->zeroCrossTendency = p->constantSrc;
                dspSource = s;
                break;
            }
            case NodeType::Envelope: {
                auto s = std::make_shared<Envelope>(
                    Envelope::make_adsr(DSP_SAMPLE_RATE, 0.01f, 0.1f, 0.6f, 0.0f));
                dspSource = s;
                break;
            }
            case NodeType::VarSource: {
                auto* val = find_input("val");
                auto* var = find_input("var");
                auto* pct = find_input("varPct");
                auto s = std::make_shared<VarSource>(
                    val ? val->constantSrc : std::make_shared<ConstantSource>(1.0f),
                    var ? var->constantSrc : std::make_shared<ConstantSource>(0.0f),
                    pct ? pct->constantSrc : std::make_shared<ConstantSource>(0.0f));
                dspSource = s;
                break;
            }
            case NodeType::RangeSource: {
                auto* mn = find_input("min");
                auto* mx = find_input("max");
                auto* vr = find_input("var");
                auto s = std::make_shared<RangeSource>(
                    mn ? mn->constantSrc : std::make_shared<ConstantSource>(0.0f),
                    mx ? mx->constantSrc : std::make_shared<ConstantSource>(1.0f),
                    vr ? vr->constantSrc : std::make_shared<ConstantSource>(0.5f));
                dspSource = s;
                break;
            }
            case NodeType::Parameter: {
                // Parameter node's DSP is just its constantSrc from the "default" pin
                if (auto* p = find_input("default"))
                    dspSource = p->constantSrc;
                break;
            }
            default:
                break;
        }
    }

    // Rewire a specific input pin's DSP connection
    void wire_pin(const std::string& pinName, std::shared_ptr<ValueSource> src) {
        if (!src || !dspSource) return;
        auto ws = std::dynamic_pointer_cast<WaveSource>(dspSource);
        if (ws) {
            if (pinName == "frequency")  { ws->set_frequency(src); return; }
            if (pinName == "amplitude")  { ws->set_amplitude(src); return; }
            if (pinName == "phase")      { ws->set_phase(src); return; }
        }
        auto tri = std::dynamic_pointer_cast<TriangleSource>(dspSource);
        if (tri && pinName == "bias") { tri->set_bias(src); return; }

        auto pulse = std::dynamic_pointer_cast<PulseSource>(dspSource);
        if (pulse) {
            if (pinName == "dutyCycle") { pulse->set_duty_cycle(src); return; }
            if (pinName == "bend")     { pulse->set_bend(src); return; }
        }
        auto rn = std::dynamic_pointer_cast<RedNoiseSource>(dspSource);
        if (rn) {
            if (pinName == "density")       { rn->density = src; return; }
            if (pinName == "smoothness")    { rn->smoothness = src; return; }
            if (pinName == "rampVariation") { rn->rampVariation = src; return; }
            if (pinName == "boost")         { rn->boost = src; return; }
            if (pinName == "continuity")    { rn->continuity = src; return; }
            if (pinName == "zeroCrossTend") { rn->zeroCrossTendency = src; return; }
        }
        auto range = std::dynamic_pointer_cast<RangeSource>(dspSource);
        if (range) {
            if (pinName == "min") { range->set_min(src); return; }
            if (pinName == "max") { range->set_max(src); return; }
            if (pinName == "var") { range->set_var(src); return; }
        }
        auto var = std::dynamic_pointer_cast<VarSource>(dspSource);
        if (var) {
            if (pinName == "val")    { var->set_val(src); return; }
            if (pinName == "var")    { var->set_var(src); return; }
            if (pinName == "varPct") { var->set_var_pct(src); return; }
        }
    }

    Pin* find_input(const std::string& name) {
        for (auto& p : inputs) if (p.name == name) return &p;
        return nullptr;
    }

    void wire_wave_defaults(std::shared_ptr<WaveSource> ws) {
        if (auto* p = find_input("frequency"))  ws->set_frequency(p->constantSrc);
        if (auto* p = find_input("amplitude"))  ws->set_amplitude(p->constantSrc);
        if (auto* p = find_input("phase"))      ws->set_phase(p->constantSrc);
    }

    void build_pins() {
        switch (type) {
            case NodeType::SineSource:
                inputs.emplace_back("frequency", PinKind::Input, 440.0f);
                inputs.emplace_back("amplitude", PinKind::Input, 1.0f);
                inputs.emplace_back("phase",     PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::SawSource:
                inputs.emplace_back("frequency", PinKind::Input, 440.0f);
                inputs.emplace_back("amplitude", PinKind::Input, 1.0f);
                inputs.emplace_back("phase",     PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::TriangleSource:
                inputs.emplace_back("frequency", PinKind::Input, 440.0f);
                inputs.emplace_back("amplitude", PinKind::Input, 1.0f);
                inputs.emplace_back("phase",     PinKind::Input, 0.0f);
                inputs.emplace_back("bias",      PinKind::Input, 0.5f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::PulseSource:
                inputs.emplace_back("frequency",  PinKind::Input, 440.0f);
                inputs.emplace_back("amplitude",  PinKind::Input, 1.0f);
                inputs.emplace_back("phase",      PinKind::Input, 0.0f);
                inputs.emplace_back("dutyCycle",  PinKind::Input, 0.5f);
                inputs.emplace_back("bend",       PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::WhiteNoiseSource:
                inputs.emplace_back("amplitude", PinKind::Input, 1.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::RedNoiseSource:
                inputs.emplace_back("frequency",       PinKind::Input, 400.0f);
                inputs.emplace_back("amplitude",       PinKind::Input, 1.0f);
                inputs.emplace_back("density",         PinKind::Input, 1.0f);
                inputs.emplace_back("smoothness",      PinKind::Input, 0.0f);
                inputs.emplace_back("rampVariation",   PinKind::Input, 0.5f);
                inputs.emplace_back("boost",           PinKind::Input, 0.0f);
                inputs.emplace_back("continuity",      PinKind::Input, 0.0f);
                inputs.emplace_back("zeroCrossTend",   PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::Envelope:
                inputs.emplace_back("attack",       PinKind::Input, 0.01f);
                inputs.emplace_back("decay",        PinKind::Input, 0.1f);
                inputs.emplace_back("sustainLevel", PinKind::Input, 0.6f);
                inputs.emplace_back("release",      PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::VarSource:
                inputs.emplace_back("val",    PinKind::Input, 1.0f);
                inputs.emplace_back("var",    PinKind::Input, 0.0f);
                inputs.emplace_back("varPct", PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::RangeSource:
                inputs.emplace_back("min", PinKind::Input, 0.0f);
                inputs.emplace_back("max", PinKind::Input, 1.0f);
                inputs.emplace_back("var", PinKind::Input, 0.5f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::SoundChannel:
                inputs.emplace_back("source", PinKind::Input, 0.0f);
                inputs.emplace_back("volume", PinKind::Input, 1.0f);
                inputs.emplace_back("pan",    PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::StereoMixer:
                inputs.emplace_back("ch 1", PinKind::Input, 0.0f);
                inputs.emplace_back("gainL", PinKind::Input, 1.0f);
                inputs.emplace_back("gainR", PinKind::Input, 1.0f);
                outputs.emplace_back("outL", PinKind::Output);
                outputs.emplace_back("outR", PinKind::Output);
                break;
            case NodeType::PatchOutput:
                inputs.emplace_back("source", PinKind::Input, 0.0f);
                break;
            case NodeType::Parameter:
                inputs.emplace_back("default", PinKind::Input, 440.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
        }
    }

    void add_channel_input() {
        if (type != NodeType::StereoMixer) return;
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

    // First pass: wire all pins to their ConstantSource defaults
    for (auto& pin : node.inputs) {
        if (pin.kind == PinKind::Input && pin.constantSrc)
            node.wire_pin(pin.name, pin.constantSrc);
    }

    // Second pass: override with connected sources
    for (auto& pin : node.inputs) {
        for (auto& link : s_links) {
            int otherPinId = -1;
            if (link.endPinId == pin.id) otherPinId = link.startPinId;
            if (link.startPinId == pin.id) otherPinId = link.endPinId;
            if (otherPinId < 0) continue;

            GraphNode* srcNode = find_node_for_pin(otherPinId);
            Pin* otherPin = find_pin(otherPinId);
            if (srcNode && otherPin && otherPin->kind == PinKind::Output && srcNode->dspSource)
                node.wire_pin(pin.name, srcNode->dspSource);
        }
    }

    // Envelope: recreate DSP with current param values (not streamable params)
    if (node.type == NodeType::Envelope) {
        auto* att = node.find_input("attack");
        auto* dec = node.find_input("decay");
        auto* sus = node.find_input("sustainLevel");
        auto* rel = node.find_input("release");
        float a = att ? att->defaultValue : 0.01f;
        float d = dec ? dec->defaultValue : 0.1f;
        float s = sus ? sus->defaultValue : 0.6f;
        float r = rel ? rel->defaultValue : 0.0f;

        auto newEnv = std::make_shared<Envelope>(
            Envelope::make_adsr(DSP_SAMPLE_RATE, a, d, s, r));
        node.dspSource = newEnv;

        // Re-wire any nodes that reference this envelope
        for (auto& link : s_links) {
            for (auto& outPin : node.outputs) {
                int targetPinId = -1;
                if (link.startPinId == outPin.id) targetPinId = link.endPinId;
                if (link.endPinId == outPin.id) targetPinId = link.startPinId;
                if (targetPinId < 0) continue;

                GraphNode* dstNode = find_node_for_pin(targetPinId);
                Pin* dstPin = find_pin(targetPinId);
                if (dstNode && dstPin && dstPin->kind == PinKind::Input)
                    dstNode->wire_pin(dstPin->name, node.dspSource);
            }
        }
    }
}

// Update ALL nodes' DSP (call after link changes)
static void update_all_dsp() {
    try {
        for (auto& node : s_nodes)
            update_node_dsp(node);
    } catch (...) {
        // Don't crash the UI on DSP wiring errors
    }
}

static bool is_pin_connected(int pinId) {
    for (auto& link : s_links)
        if (link.startPinId == pinId || link.endPinId == pinId) return true;
    return false;
}

static void delete_node(int nodeId) {
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

    if (mode == GraphMode::PatchGraph) {
        s_nodes.emplace_back(NodeType::Parameter, "frequency");
        s_nodes.emplace_back(NodeType::SineSource);
        s_nodes.emplace_back(NodeType::Envelope);
        s_nodes.emplace_back(NodeType::PatchOutput);
    } else {
        s_nodes.emplace_back(NodeType::SineSource);
        s_nodes.emplace_back(NodeType::Envelope);
        s_nodes.emplace_back(NodeType::SoundChannel);
        s_nodes.emplace_back(NodeType::StereoMixer);
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

static NodeType json_type_to_node(const std::string& t) {
    if (t == "SineSource")      return NodeType::SineSource;
    if (t == "SawSource")       return NodeType::SawSource;
    if (t == "TriangleSource")  return NodeType::TriangleSource;
    if (t == "PulseSource")     return NodeType::PulseSource;
    if (t == "WhiteNoiseSource")return NodeType::WhiteNoiseSource;
    if (t == "RedNoiseSource")  return NodeType::RedNoiseSource;
    if (t == "Envelope")        return NodeType::Envelope;
    if (t == "VarSource")       return NodeType::VarSource;
    if (t == "RangeSource")     return NodeType::RangeSource;
    if (t == "SoundChannel")    return NodeType::SoundChannel;
    if (t == "StereoMixer")     return NodeType::StereoMixer;
    return NodeType::SineSource;
}

static bool s_needsLayout = false;

static void load_graph() {
    using json = nlohmann::json;

    std::string path = open_file_dialog();
    if (path.empty()) return;

    std::ifstream f(path);
    if (!f) return;
    json root = json::parse(f);
    s_currentFilePath = path;

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

    // First pass: create all nodes
    for (const auto& jnode : nodes) {
        std::string id = jnode["id"].get<std::string>();
        std::string type = jnode["type"].get<std::string>();

        s_nodes.emplace_back(json_type_to_node(type));
        GraphNode& gn = s_nodes.back();
        gn.label = id;
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
                }
                // refs handled in second pass
            }
        }

        // Mixer: add extra channel inputs if needed
        if (gn.type == NodeType::StereoMixer && jnode.contains("inputs") &&
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
        s_nodes.emplace_back(NodeType::PatchOutput);
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

                s_nodes.emplace_back(NodeType::Parameter, paramName);
                GraphNode& pn = s_nodes.back();

                // Find the target input pin and get its current default value
                auto inIt = inputPinMap.find(target);
                if (inIt != inputPinMap.end()) {
                    Pin* targetPin = find_pin(inIt->second);
                    if (targetPin)
                        pn.inputs[0].defaultValue = targetPin->defaultValue;

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
            if (node.type == NodeType::PatchOutput)
                key = "__output";
            else if (node.type == NodeType::Parameter)
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
}

// ===========================================================================
// JSON export
// ===========================================================================

static const char* node_type_to_json(NodeType t) {
    switch (t) {
        case NodeType::SineSource:       return "SineSource";
        case NodeType::SawSource:        return "SawSource";
        case NodeType::TriangleSource:   return "TriangleSource";
        case NodeType::PulseSource:      return "PulseSource";
        case NodeType::WhiteNoiseSource: return "WhiteNoiseSource";
        case NodeType::RedNoiseSource:   return "RedNoiseSource";
        case NodeType::Envelope:         return "Envelope";
        case NodeType::VarSource:        return "VarSource";
        case NodeType::RangeSource:      return "RangeSource";
        case NodeType::SoundChannel:     return "SoundChannel";
        case NodeType::StereoMixer:      return "StereoMixer";
        default: return "";
    }
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
    std::unordered_map<NodeType, int> typeCounts;
    for (auto& node : s_nodes) {
        if (node.type == NodeType::PatchOutput || node.type == NodeType::Parameter)
            continue;
        int& count = typeCounts[node.type];
        count++;
        std::string prefix;
        switch (node.type) {
            case NodeType::SineSource:       prefix = "sine"; break;
            case NodeType::SawSource:        prefix = "saw"; break;
            case NodeType::TriangleSource:   prefix = "tri"; break;
            case NodeType::PulseSource:      prefix = "pulse"; break;
            case NodeType::WhiteNoiseSource: prefix = "wn"; break;
            case NodeType::RedNoiseSource:   prefix = "rn"; break;
            case NodeType::Envelope:         prefix = "env"; break;
            case NodeType::VarSource:        prefix = "var"; break;
            case NodeType::RangeSource:      prefix = "range"; break;
            default: prefix = "node"; break;
        }
        nodeIds[node.id] = prefix + std::to_string(count);
    }

    // Find Output and Parameter nodes
    GraphNode* outputNode = nullptr;
    std::vector<GraphNode*> paramNodes;
    for (auto& node : s_nodes) {
        if (node.type == NodeType::PatchOutput) outputNode = &node;
        if (node.type == NodeType::Parameter) paramNodes.push_back(&node);
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
        if (node.type == NodeType::PatchOutput || node.type == NodeType::Parameter)
            continue;

        json jnode;
        jnode["id"] = nodeIds[node.id];
        jnode["type"] = node_type_to_json(node.type);

        json params = json::object();
        for (auto& pin : node.inputs) {
            bool isSourcePin = (pin.name == "source" || pin.name.substr(0, 2) == "ch");
            if (isSourcePin) continue;

            GraphNode* src = find_source_node(pin.id);
            if (src) {
                if (src->type == NodeType::Parameter) {
                    // Parameter node: emit default value (the instrument will set it)
                    params[pin.name] = pin.defaultValue;
                } else {
                    params[pin.name] = json{{"ref", nodeIds[src->id]}};
                }
            } else {
                params[pin.name] = pin.defaultValue;
            }
        }
        if (!params.empty()) jnode["params"] = params;

        // Envelope: add preset
        if (node.type == NodeType::Envelope) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["preset"] = "adsr";
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

    // Save UI layout
    json positions = json::object();
    for (auto* nodePtr : sorted) {
        if (nodePtr->type == NodeType::PatchOutput || nodePtr->type == NodeType::Parameter)
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
    std::unordered_map<NodeType, int> typeCounts;
    for (auto& node : s_nodes) {
        int& count = typeCounts[node.type];
        count++;
        std::string prefix;
        switch (node.type) {
            case NodeType::SineSource:       prefix = "sine"; break;
            case NodeType::SawSource:        prefix = "saw"; break;
            case NodeType::TriangleSource:   prefix = "tri"; break;
            case NodeType::PulseSource:      prefix = "pulse"; break;
            case NodeType::WhiteNoiseSource: prefix = "wn"; break;
            case NodeType::RedNoiseSource:   prefix = "rn"; break;
            case NodeType::Envelope:         prefix = "env"; break;
            case NodeType::VarSource:        prefix = "var"; break;
            case NodeType::RangeSource:      prefix = "range"; break;
            case NodeType::SoundChannel:     prefix = "ch"; break;
            case NodeType::StereoMixer:      prefix = "mix"; break;
            default: prefix = "node"; break;
        }
        nodeIds[node.id] = prefix + std::to_string(count);
    }

    // Find mixer for output
    std::string outputId;
    for (auto& node : s_nodes)
        if (node.type == NodeType::StereoMixer)
            outputId = nodeIds[node.id];

    // Build nodes (topologically sorted)
    auto sorted = topo_sort();
    json nodes = json::array();
    for (auto* nodePtr : sorted) {
        auto& node = *nodePtr;
        json jnode;
        jnode["id"] = nodeIds[node.id];
        jnode["type"] = node_type_to_json(node.type);

        json params = json::object();
        for (auto& pin : node.inputs) {
            bool isSourcePin = (pin.name == "source" || pin.name.substr(0, 2) == "ch");

            GraphNode* src = find_source_node(pin.id);

            if (isSourcePin) {
                if (src && node.type == NodeType::SoundChannel) {
                    jnode["inputs"]["source"] = nodeIds[src->id];
                }
                if (src && node.type == NodeType::StereoMixer) {
                    if (!jnode.contains("inputs") || !jnode["inputs"].contains("channels"))
                        jnode["inputs"]["channels"] = json::array();
                    jnode["inputs"]["channels"].push_back(nodeIds[src->id]);
                }
            } else {
                if (src)
                    params[pin.name] = json{{"ref", nodeIds[src->id]}};
                else
                    params[pin.name] = pin.defaultValue;
            }
        }
        if (!params.empty()) jnode["params"] = params;

        if (node.type == NodeType::Envelope) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["preset"] = "adsr";
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

static HWAVEOUT g_waveOut = nullptr;
static WAVEHDR  g_waveHdrs[NUM_AUDIO_BUFS] = {};
static int16_t  g_audioBufs[NUM_AUDIO_BUFS][AUDIO_CHUNK * 2] = {};

static ValueSource* g_streamSource = nullptr;
static int g_streamRemaining = 0;
static float g_streamVelocity = 0.5f;

static void fill_audio_buffer(int bufIdx) {
    auto* out = g_audioBufs[bufIdx];
    auto* hdr = &g_waveHdrs[bufIdx];

    for (int i = 0; i < AUDIO_CHUNK; ++i) {
        float s = 0.0f;

        if (g_streamSource && g_streamRemaining != 0) {
            s = g_streamSource->next() * g_streamVelocity;
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
        if (n.type != NodeType::PatchOutput) continue;
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
    for (auto& n : s_nodes) {
        if (n.dspSource) n.dspSource->prepare(samples);
    }
}

// Play a note: set frequency param, prepare the graph, start streaming
static void play_note(float noteNum, float velocity, float durationSeconds) {
    if (s_graphMode != GraphMode::PatchGraph) return;

    ValueSource* src = find_output_source();
    if (!src) return;

    // Stop any current stream first
    g_streamSource = nullptr;

    // Set frequency on Parameter nodes named "frequency"
    float freq = note_to_freq(noteNum);
    for (auto& n : s_nodes) {
        if (n.type == NodeType::Parameter && n.paramName == "frequency") {
            if (auto* p = n.find_input("default"))
                p->constantSrc->set(freq);
        }
    }

    int samples = int(durationSeconds * float(AUDIO_SAMPLE_RATE));
    prepare_graph(samples);

    g_streamVelocity = velocity;
    g_streamRemaining = samples;
    g_streamSource = src;
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
}

// ===========================================================================
// Node rendering
// ===========================================================================

static void draw_node(GraphNode& node) {
    ImNodes::PushColorStyle(ImNodesCol_TitleBar, node_title_color(node.type));

    ImNodes::BeginNode(node.id);

    // Title bar
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(node.label.c_str());

    if (node.type == NodeType::StereoMixer) {
        ImGui::SameLine();
        char btnLabel[32];
        snprintf(btnLabel, sizeof(btnLabel), " + ##addch%d", node.id);
        if (ImGui::SmallButton(btnLabel))
            node.add_channel_input();
    }

    ImNodes::EndNodeTitleBar();

    // Input pins
    for (auto& pin : node.inputs) {
        ImNodes::BeginInputAttribute(pin.id);

        if (is_pin_connected(pin.id)) {
            ImGui::TextUnformatted(pin.name.c_str());
        } else {
            bool isSourcePin = (pin.name == "source" || pin.name.substr(0, 2) == "ch");
            if (isSourcePin) {
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", pin.name.c_str());
            } else {
                ImGui::PushItemWidth(80);
                char label[64];
                snprintf(label, sizeof(label), "##%d", pin.id);
                if (ImGui::DragFloat(label, &pin.defaultValue, 0.01f, 0.0f, 0.0f, "%.3f")) {
                    if (pin.constantSrc) pin.constantSrc->set(pin.defaultValue);
                    update_node_dsp(node);
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                ImGui::TextUnformatted(pin.name.c_str());
            }
        }

        ImNodes::EndInputAttribute();
    }

    // PatchOutput: polyphony (paramMap is derived from Parameter nodes)
    if (node.type == NodeType::PatchOutput) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::PushItemWidth(60);
        char polyLabel[32];
        snprintf(polyLabel, sizeof(polyLabel), "##poly%d", node.id);
        ImGui::DragInt(polyLabel, &node.polyphony, 0.1f, 1, 32);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Text("polyphony");
    }

    // Parameter: editable name
    if (node.type == NodeType::Parameter) {
        ImGui::Spacing();
        ImGui::PushItemWidth(100);
        char nameLabel[32];
        snprintf(nameLabel, sizeof(nameLabel), "##pname%d", node.id);
        if (ImGui::InputText(nameLabel, node.paramNameBuf, sizeof(node.paramNameBuf))) {
            node.paramName = node.paramNameBuf;
            node.label = node.paramName.empty() ? "Parameter" : node.paramName;
        }
        ImGui::PopItemWidth();
    }

    // Output pins
    for (auto& pin : node.outputs) {
        ImNodes::BeginOutputAttribute(pin.id);
        float nodeWidth = 180.0f;
        float textWidth = ImGui::CalcTextSize(pin.name.c_str()).x;
        ImGui::Indent(nodeWidth - textWidth - 20);
        ImGui::TextUnformatted(pin.name.c_str());
        ImNodes::EndOutputAttribute();
    }

    ImNodes::EndNode();
    ImNodes::PopColorStyle();
}

// ===========================================================================
// Context menu
// ===========================================================================

static bool s_wantCreateMenu = false;
static ImVec2 s_createMenuPos;

static void show_create_menu() {
    if (!ImGui::BeginPopup("CreateNodeMenu")) return;

    ImGui::SeparatorText("Oscillators");
    if (ImGui::MenuItem("Sine"))      { s_nodes.emplace_back(NodeType::SineSource);      ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
    if (ImGui::MenuItem("Saw"))       { s_nodes.emplace_back(NodeType::SawSource);        ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
    if (ImGui::MenuItem("Triangle"))  { s_nodes.emplace_back(NodeType::TriangleSource);   ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
    if (ImGui::MenuItem("Pulse"))     { s_nodes.emplace_back(NodeType::PulseSource);      ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }

    ImGui::SeparatorText("Noise");
    if (ImGui::MenuItem("White Noise")) { s_nodes.emplace_back(NodeType::WhiteNoiseSource); ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
    if (ImGui::MenuItem("Red Noise"))   { s_nodes.emplace_back(NodeType::RedNoiseSource);   ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }

    ImGui::SeparatorText("Modifiers");
    if (ImGui::MenuItem("Envelope")) { s_nodes.emplace_back(NodeType::Envelope);  ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
    if (ImGui::MenuItem("Var"))      { s_nodes.emplace_back(NodeType::VarSource);  ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
    if (ImGui::MenuItem("Range"))    { s_nodes.emplace_back(NodeType::RangeSource); ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }

    if (s_graphMode == GraphMode::PatchGraph) {
        ImGui::SeparatorText("Instrument");
        if (ImGui::MenuItem("Parameter")) {
            s_nodes.emplace_back(NodeType::Parameter, "param");
            ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
        }

        bool hasOutput = false;
        for (auto& n : s_nodes) if (n.type == NodeType::PatchOutput) hasOutput = true;
        if (!hasOutput) {
            if (ImGui::MenuItem("Output")) { s_nodes.emplace_back(NodeType::PatchOutput); ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Output (exists)");
        }
    } else {
        ImGui::SeparatorText("Output");
        if (ImGui::MenuItem("Channel")) { s_nodes.emplace_back(NodeType::SoundChannel); ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
        if (ImGui::MenuItem("Mixer"))   { s_nodes.emplace_back(NodeType::StereoMixer);  ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos); }
    }

    ImGui::EndPopup();
}

// ===========================================================================
// Main
// ===========================================================================

int main(int, char**) {
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

    ImGui::StyleColorsDark();

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

    // Start with a Patch Graph
    new_graph(GraphMode::PatchGraph);
    bool firstFrame = true;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Layout nodes (first frame or after load)
        if (firstFrame || s_needsLayout) {
            float x = 50, y = 80;
            for (int i = 0; i < (int)s_nodes.size(); ++i) {
                ImNodes::SetNodeScreenSpacePos(s_nodes[i].id, ImVec2(x, y));
                x += 220;
                if (x > 900) { x = 50; y += 250; }
            }
            firstFrame = false;
            s_needsLayout = false;
        }

        // Full-window editor
        const char* modeLabel = s_graphMode == GraphMode::PatchGraph ? "Patch Graph" : "Node Graph";
        char titleBuf[64];
        if (s_currentFilePath.empty())
            snprintf(titleBuf, sizeof(titleBuf), "MForce - %s (unsaved)", modeLabel);
        else {
            // Show just the filename, not full path
            const char* fname = s_currentFilePath.c_str();
            const char* slash = strrchr(fname, '/');
            const char* bslash = strrchr(fname, '\\');
            if (bslash && (!slash || bslash > slash)) slash = bslash;
            if (slash) fname = slash + 1;
            snprintf(titleBuf, sizeof(titleBuf), "MForce - %s - %s", fname, modeLabel);
        }
        glfwSetWindowTitle(window, titleBuf);

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Editor", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_MenuBar);

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::BeginMenu("New")) {
                    if (ImGui::MenuItem("Patch Graph")) {
                        new_graph(GraphMode::PatchGraph);
                        firstFrame = true;
                    }
                    if (ImGui::MenuItem("Node Graph")) {
                        new_graph(GraphMode::NodeGraph);
                        firstFrame = true;
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

            // Play/Stop in menu bar
            if (s_graphMode == GraphMode::PatchGraph) {
                ImGui::Separator();
                bool isPlaying = g_streamSource != nullptr;
                if (!isPlaying) {
                    if (ImGui::MenuItem("Play", "Space"))
                        play_note(60.0f, 0.8f, 2.0f);
                    if (ImGui::MenuItem("Stream", "S"))
                        play_continuous(0.5f);
                } else {
                    if (ImGui::MenuItem("Stop", "Space"))
                        stop_playback();
                }
            }

            ImGui::EndMenuBar();
        }

        ImNodes::BeginNodeEditor();

        for (auto& node : s_nodes)
            draw_node(node);
        for (auto& link : s_links)
            ImNodes::Link(link.id, link.startPinId, link.endPinId);

        bool editorHovered = ImNodes::IsEditorHovered();

        ImNodes::EndNodeEditor();

        // New links
        int startAttr, endAttr;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
            Pin* startPin = find_pin(startAttr);
            Pin* endPin = find_pin(endAttr);
            if (startPin && endPin && startPin->kind != endPin->kind) {
                // Ensure output→input order
                int outPin = (startPin->kind == PinKind::Output) ? startAttr : endAttr;
                int inPin  = (startPin->kind == PinKind::Input)  ? startAttr : endAttr;
                s_links.emplace_back(outPin, inPin);
                update_all_dsp();
            }
        }

        // Ctrl+S save, Ctrl+O open, Space play
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
            save_graph();
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
            load_graph();
        if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::GetIO().WantTextInput) {
            if (g_streamSource != nullptr)
                stop_playback();
            else
                play_note(60.0f, 0.8f, 2.0f);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_S) && !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().WantTextInput) {
            if (g_streamSource != nullptr)
                stop_playback();
            else
                play_continuous(0.5f);
        }

        // Delete
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

        // Right-click
        if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            s_createMenuPos = ImGui::GetMousePos();
            s_wantCreateMenu = true;
        }
        if (s_wantCreateMenu) {
            ImGui::OpenPopup("CreateNodeMenu");
            s_wantCreateMenu = false;
        }
        show_create_menu();

        // Status bar
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - 30);
        ImGui::Separator();
        ImGui::Text("%s  |  Nodes: %d  Links: %d  |  Right-click: add  |  Del: remove  |  Space: play",
                     modeLabel, (int)s_nodes.size(), (int)s_links.size());

        ImGui::End();

        // Render
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

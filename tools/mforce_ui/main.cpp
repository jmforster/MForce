#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imnodes.h"

#include <GLFW/glfw3.h>

#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <cstdio>

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

    Pin(const std::string& n, PinKind k, float def = 0.0f)
        : id(next_id()), name(n), kind(k), defaultValue(def) {}
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

struct GraphNode {
    int id;
    NodeType type;
    std::string label;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    // PatchOutput-specific
    int polyphony{4};

    // Parameter-specific
    std::string paramName;  // editable name, e.g. "frequency"
    char paramNameBuf[32]{};

    GraphNode(NodeType t) : id(next_id()), type(t), label(node_type_name(t)) {
        build_pins();
    }

    GraphNode(NodeType t, const std::string& name) : GraphNode(t) {
        if (t == NodeType::Parameter) {
            paramName = name;
            label = name;
            snprintf(paramNameBuf, sizeof(paramNameBuf), "%s", name.c_str());
        }
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

static Pin* find_pin(int pinId) {
    for (auto& node : s_nodes) {
        for (auto& pin : node.inputs)  if (pin.id == pinId) return &pin;
        for (auto& pin : node.outputs) if (pin.id == pinId) return &pin;
    }
    return nullptr;
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
}

static void new_graph(GraphMode mode) {
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
                ImGui::DragFloat(label, &pin.defaultValue, 0.01f, 0.0f, 0.0f, "%.3f");
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

    // Start with a Patch Graph
    new_graph(GraphMode::PatchGraph);
    bool firstFrame = true;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (firstFrame) {
            if (s_graphMode == GraphMode::PatchGraph) {
                ImNodes::SetNodeScreenSpacePos(s_nodes[0].id, ImVec2(50, 80));   // frequency param
                ImNodes::SetNodeScreenSpacePos(s_nodes[1].id, ImVec2(250, 80));  // sine
                ImNodes::SetNodeScreenSpacePos(s_nodes[2].id, ImVec2(50, 300));  // envelope
                ImNodes::SetNodeScreenSpacePos(s_nodes[3].id, ImVec2(500, 150)); // output
            } else {
                ImNodes::SetNodeScreenSpacePos(s_nodes[0].id, ImVec2(50, 80));
                ImNodes::SetNodeScreenSpacePos(s_nodes[1].id, ImVec2(50, 350));
                ImNodes::SetNodeScreenSpacePos(s_nodes[2].id, ImVec2(350, 150));
                ImNodes::SetNodeScreenSpacePos(s_nodes[3].id, ImVec2(600, 150));
            }
            firstFrame = false;
        }

        // Full-window editor
        const char* modeLabel = s_graphMode == GraphMode::PatchGraph ? "Patch Graph" : "Node Graph";
        char titleBuf[64];
        snprintf(titleBuf, sizeof(titleBuf), "MForce - %s", modeLabel);
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
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    glfwSetWindowShouldClose(window, GLFW_TRUE);
                }
                ImGui::EndMenu();
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
            if (startPin && endPin && startPin->kind != endPin->kind)
                s_links.emplace_back(startAttr, endAttr);
        }

        // Delete
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
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
        ImGui::Text("%s  |  Nodes: %d  Links: %d  |  Right-click: add  |  Delete: remove",
                     modeLabel, (int)s_nodes.size(), (int)s_links.size());

        ImGui::End();

        // Render
        ImGui::Render();
        int displayW, displayH;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImNodes::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}

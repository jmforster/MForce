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
    SoundChannel, StereoMixer
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
    }
    return "?";
}

struct GraphNode {
    int id;
    NodeType type;
    std::string label;
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;

    GraphNode(NodeType t) : id(next_id()), type(t), label(node_type_name(t)) {
        build_pins();
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
                // Starts with one channel input; more can be added
                inputs.emplace_back("ch 1", PinKind::Input, 0.0f);
                inputs.emplace_back("gainL", PinKind::Input, 1.0f);
                inputs.emplace_back("gainR", PinKind::Input, 1.0f);
                outputs.emplace_back("outL", PinKind::Output);
                outputs.emplace_back("outR", PinKind::Output);
                break;
        }
    }

    // Add a channel input to mixer
    void add_channel_input() {
        if (type != NodeType::StereoMixer) return;
        int chNum = 0;
        for (auto& p : inputs)
            if (p.name.substr(0, 2) == "ch") chNum++;
        char name[16];
        snprintf(name, sizeof(name), "ch %d", chNum + 1);
        // Insert before gainL/gainR (last 2 inputs)
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

static GraphNode* find_node_for_pin(int pinId) {
    for (auto& node : s_nodes) {
        for (auto& pin : node.inputs)  if (pin.id == pinId) return &node;
        for (auto& pin : node.outputs) if (pin.id == pinId) return &node;
    }
    return nullptr;
}

static bool is_pin_connected(int pinId) {
    for (auto& link : s_links)
        if (link.startPinId == pinId || link.endPinId == pinId) return true;
    return false;
}

static void delete_node(int nodeId) {
    // Remove links connected to this node
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

// ===========================================================================
// Node rendering
// ===========================================================================

static void draw_node(GraphNode& node) {
    // Set title bar color based on type
    ImNodes::PushColorStyle(ImNodesCol_TitleBar,
        node.type <= NodeType::PulseSource      ? IM_COL32(70, 100, 180, 255) :
        node.type <= NodeType::RedNoiseSource    ? IM_COL32(140, 80, 80, 255) :
        node.type == NodeType::Envelope          ? IM_COL32(80, 140, 80, 255) :
        node.type <= NodeType::RangeSource       ? IM_COL32(140, 120, 60, 255) :
                                                   IM_COL32(100, 100, 100, 255));

    ImNodes::BeginNode(node.id);

    // Title bar
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(node.label.c_str());

    // Mixer: add channel button in title bar
    if (node.type == NodeType::StereoMixer) {
        ImGui::SameLine();
        char btnLabel[32];
        snprintf(btnLabel, sizeof(btnLabel), " + ##addch%d", node.id);
        if (ImGui::SmallButton(btnLabel)) {
            node.add_channel_input();
        }
    }

    ImNodes::EndNodeTitleBar();

    // Input pins
    for (auto& pin : node.inputs) {
        ImNodes::BeginInputAttribute(pin.id);

        if (is_pin_connected(pin.id)) {
            ImGui::TextUnformatted(pin.name.c_str());
        } else {
            // Source-type inputs (like "source" on Channel, "ch N" on Mixer)
            // don't need editable values — just show label
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

    // Output pins
    for (auto& pin : node.outputs) {
        ImNodes::BeginOutputAttribute(pin.id);
        float nodeWidth = 160.0f;
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
    struct Entry { const char* label; const char* name; NodeType type; };
    static const Entry entries[] = {
        {"Oscillators",  nullptr,        NodeType::SineSource},
        {"  Sine",       "Sine",         NodeType::SineSource},
        {"  Saw",        "Saw",          NodeType::SawSource},
        {"  Triangle",   "Triangle",     NodeType::TriangleSource},
        {"  Pulse",      "Pulse",        NodeType::PulseSource},
        {"Noise",        nullptr,        NodeType::WhiteNoiseSource},
        {"  White Noise","White Noise",  NodeType::WhiteNoiseSource},
        {"  Red Noise",  "Red Noise",    NodeType::RedNoiseSource},
        {"Modifiers",    nullptr,        NodeType::Envelope},
        {"  Envelope",   "Envelope",     NodeType::Envelope},
        {"  Var",        "Var",          NodeType::VarSource},
        {"  Range",      "Range",        NodeType::RangeSource},
        {"Output",       nullptr,        NodeType::SoundChannel},
        {"  Channel",    "Channel",      NodeType::SoundChannel},
        {"  Mixer",      "Mixer",        NodeType::StereoMixer},
    };

    if (ImGui::BeginPopup("CreateNodeMenu")) {
        for (auto& e : entries) {
            if (e.name == nullptr) {
                // Section header
                ImGui::SeparatorText(e.label);
            } else {
                if (ImGui::MenuItem(e.label + 2)) {  // skip "  " indent
                    s_nodes.emplace_back(e.type);
                    ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, s_createMenuPos);
                }
            }
        }
        ImGui::EndPopup();
    }
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

    // Node editor style
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8, 8);
    style.PinCircleRadius = 4.0f;
    style.LinkThickness = 2.5f;
    style.Flags |= ImNodesStyleFlags_GridLines;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Create starter nodes
    s_nodes.emplace_back(NodeType::SineSource);
    s_nodes.emplace_back(NodeType::Envelope);
    s_nodes.emplace_back(NodeType::SoundChannel);
    s_nodes.emplace_back(NodeType::StereoMixer);

    bool firstFrame = true;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Position starter nodes on first frame
        if (firstFrame) {
            ImNodes::SetNodeScreenSpacePos(s_nodes[0].id, ImVec2(50, 80));
            ImNodes::SetNodeScreenSpacePos(s_nodes[1].id, ImVec2(50, 350));
            ImNodes::SetNodeScreenSpacePos(s_nodes[2].id, ImVec2(350, 150));
            ImNodes::SetNodeScreenSpacePos(s_nodes[3].id, ImVec2(600, 150));
            firstFrame = false;
        }

        // Full-window editor
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Patch Editor", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_MenuBar);

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New")) {
                    s_nodes.clear();
                    s_links.clear();
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

        // Draw nodes and links
        for (auto& node : s_nodes)
            draw_node(node);
        for (auto& link : s_links)
            ImNodes::Link(link.id, link.startPinId, link.endPinId);

        // Right-click: open create menu
        // Must be checked INSIDE the node editor scope
        bool editorHovered = ImNodes::IsEditorHovered();

        ImNodes::EndNodeEditor();

        // Handle new links (after EndNodeEditor)
        int startAttr, endAttr;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
            Pin* startPin = find_pin(startAttr);
            Pin* endPin = find_pin(endAttr);
            if (startPin && endPin && startPin->kind != endPin->kind) {
                s_links.emplace_back(startAttr, endAttr);
            }
        }

        // Handle link drop on empty space (after EndNodeEditor)
        int droppedPin;
        if (ImNodes::IsLinkDropped(&droppedPin, false)) {
            // Could auto-create a node here in the future
        }

        // Delete selected nodes/links with Delete key
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
            int numSelectedNodes = ImNodes::NumSelectedNodes();
            int numSelectedLinks = ImNodes::NumSelectedLinks();

            if (numSelectedLinks > 0) {
                std::vector<int> selectedLinks(numSelectedLinks);
                ImNodes::GetSelectedLinks(selectedLinks.data());
                for (int lid : selectedLinks)
                    delete_link(lid);
            }

            if (numSelectedNodes > 0) {
                std::vector<int> selectedNodes(numSelectedNodes);
                ImNodes::GetSelectedNodes(selectedNodes.data());
                for (int nid : selectedNodes)
                    delete_node(nid);
            }

            ImNodes::ClearNodeSelection();
            ImNodes::ClearLinkSelection();
        }

        // Right-click context menu (outside node editor scope to avoid event conflicts)
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
        ImGui::Text("Nodes: %d  Links: %d  |  Right-click: add node  |  Delete: remove selected",
                     (int)s_nodes.size(), (int)s_links.size());

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

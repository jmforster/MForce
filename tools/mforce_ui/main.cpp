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
// ID scheme: nodes get IDs 1000+, pins get unique IDs via counter
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
            case NodeType::SawSource:
            case NodeType::TriangleSource:
                inputs.emplace_back("frequency", PinKind::Input, 440.0f);
                inputs.emplace_back("amplitude", PinKind::Input, 1.0f);
                inputs.emplace_back("phase",     PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::PulseSource:
                inputs.emplace_back("frequency",  PinKind::Input, 440.0f);
                inputs.emplace_back("amplitude",  PinKind::Input, 1.0f);
                inputs.emplace_back("phase",      PinKind::Input, 0.0f);
                inputs.emplace_back("pulseWidth", PinKind::Input, 0.5f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::WhiteNoiseSource:
                inputs.emplace_back("amplitude", PinKind::Input, 1.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::RedNoiseSource:
                inputs.emplace_back("frequency",  PinKind::Input, 400.0f);
                inputs.emplace_back("amplitude",  PinKind::Input, 1.0f);
                inputs.emplace_back("density",    PinKind::Input, 1.0f);
                inputs.emplace_back("smoothness", PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::Envelope:
                inputs.emplace_back("attack",  PinKind::Input, 0.01f);
                inputs.emplace_back("decay",   PinKind::Input, 0.1f);
                inputs.emplace_back("sustain", PinKind::Input, 0.6f);
                inputs.emplace_back("release", PinKind::Input, 0.0f);
                outputs.emplace_back("out", PinKind::Output);
                break;
            case NodeType::VarSource:
                inputs.emplace_back("value", PinKind::Input, 1.0f);
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
                inputs.emplace_back("channel", PinKind::Input, 0.0f);
                inputs.emplace_back("gainL",   PinKind::Input, 1.0f);
                inputs.emplace_back("gainR",   PinKind::Input, 1.0f);
                outputs.emplace_back("outL", PinKind::Output);
                outputs.emplace_back("outR", PinKind::Output);
                break;
        }
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

// ===========================================================================
// Node rendering
// ===========================================================================

static void draw_node(GraphNode& node) {
    ImNodes::BeginNode(node.id);

    // Title bar
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(node.label.c_str());
    ImNodes::EndNodeTitleBar();

    // Input pins
    for (auto& pin : node.inputs) {
        ImNodes::BeginInputAttribute(pin.id);

        if (is_pin_connected(pin.id)) {
            // Connected: just show label
            ImGui::TextUnformatted(pin.name.c_str());
        } else {
            // Unconnected: editable value + label
            ImGui::PushItemWidth(80);
            char label[64];
            snprintf(label, sizeof(label), "##%d", pin.id);
            ImGui::DragFloat(label, &pin.defaultValue, 0.1f, 0.0f, 0.0f, "%.2f");
            ImGui::PopItemWidth();
            ImGui::SameLine();
            ImGui::TextUnformatted(pin.name.c_str());
        }

        ImNodes::EndInputAttribute();
    }

    // Output pins
    for (auto& pin : node.outputs) {
        ImNodes::BeginOutputAttribute(pin.id);
        ImGui::Indent(100);
        ImGui::TextUnformatted(pin.name.c_str());
        ImNodes::EndOutputAttribute();
    }

    ImNodes::EndNode();
}

// ===========================================================================
// Context menu
// ===========================================================================

static void show_create_menu() {
    struct Entry { const char* name; NodeType type; };
    static const Entry entries[] = {
        {"Sine",        NodeType::SineSource},
        {"Saw",         NodeType::SawSource},
        {"Triangle",    NodeType::TriangleSource},
        {"Pulse",       NodeType::PulseSource},
        {"White Noise", NodeType::WhiteNoiseSource},
        {"Red Noise",   NodeType::RedNoiseSource},
        {"Envelope",    NodeType::Envelope},
        {"Var",         NodeType::VarSource},
        {"Range",       NodeType::RangeSource},
        {"Channel",     NodeType::SoundChannel},
        {"Mixer",       NodeType::StereoMixer},
    };

    if (ImGui::BeginPopup("CreateNode")) {
        ImGui::TextUnformatted("Add Node");
        ImGui::Separator();

        for (auto& e : entries) {
            if (ImGui::MenuItem(e.name)) {
                s_nodes.emplace_back(e.type);
                ImNodes::SetNodeScreenSpacePos(s_nodes.back().id, ImGui::GetMousePos());
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

    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    ImGui::StyleColorsDark();

    // Style the node editor
    ImNodesStyle& style = ImNodes::GetStyle();
    style.NodeCornerRounding = 4.0f;
    style.NodePadding = ImVec2(8, 8);
    style.PinCircleRadius = 4.0f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Create starter nodes
    s_nodes.emplace_back(NodeType::SineSource);
    s_nodes.emplace_back(NodeType::Envelope);
    s_nodes.emplace_back(NodeType::SoundChannel);
    s_nodes.emplace_back(NodeType::StereoMixer);

    ImNodes::SetNodeScreenSpacePos(s_nodes[0].id, ImVec2(100, 100));
    ImNodes::SetNodeScreenSpacePos(s_nodes[1].id, ImVec2(100, 300));
    ImNodes::SetNodeScreenSpacePos(s_nodes[2].id, ImVec2(400, 200));
    ImNodes::SetNodeScreenSpacePos(s_nodes[3].id, ImVec2(650, 200));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // --- Node Editor ---
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Patch Editor", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImNodes::BeginNodeEditor();

        // Draw all nodes
        for (auto& node : s_nodes)
            draw_node(node);

        // Draw all links
        for (auto& link : s_links)
            ImNodes::Link(link.id, link.startPinId, link.endPinId);

        ImNodes::EndNodeEditor();

        // Handle new links
        int startAttr, endAttr;
        if (ImNodes::IsLinkCreated(&startAttr, &endAttr)) {
            // Validate: one must be output, one input
            Pin* startPin = find_pin(startAttr);
            Pin* endPin = find_pin(endAttr);
            if (startPin && endPin && startPin->kind != endPin->kind) {
                s_links.emplace_back(startAttr, endAttr);
            }
        }

        // Handle deleted links
        int linkId;
        if (ImNodes::IsLinkDestroyed(&linkId)) {
            s_links.erase(
                std::remove_if(s_links.begin(), s_links.end(),
                    [linkId](const Link& l) { return l.id == linkId; }),
                s_links.end());
        }

        // Right-click context menu
        if (ImNodes::IsEditorHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("CreateNode");
        }
        show_create_menu();

        ImGui::End();

        // --- Render ---
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

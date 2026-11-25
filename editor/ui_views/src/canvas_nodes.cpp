// The canvas's drawing: each node, face, group and the Output node from the
// table, a Wormhole's small face, and each link.
#include <algorithm>
#include <string>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_model/groups.h"
#include "mforce/ui_views/node_canvas.h"
#include "canvas_look.h"

namespace mforce::ui {

using namespace canvas_look;   // the canvas's colours and conversions by their short names

namespace {

using Look = CanvasPin::Look;

constexpr ImVec4 kGrey(0.55f, 0.55f, 0.55f, 1.0f);
constexpr ImVec4 kNumber(0.55f, 0.75f, 0.95f, 1.0f);
constexpr ImVec4 kText(0.9f, 0.7f, 0.9f, 1.0f);
constexpr ImVec4 kTapText(0.35f, 0.65f, 0.7f, 1.0f);
constexpr ImVec4 kDynamicText(0.85f, 0.72f, 0.30f, 1.0f);
constexpr float kOutputIndent = 110.0f;            // an output pin's text sits at the node's right side
constexpr ImVec4 kListenMark(0.5f, 0.8f, 0.5f, 1.0f);   // "<)))" after the title of the node listened to
constexpr ImU32 kGroupTitle = IM_COL32(90, 60, 120, 255);    // a group: purple
constexpr ImU32 kGroupBody = IM_COL32(45, 38, 55, 255);
constexpr ImVec4 kGroupCount(0.6f, 0.6f, 0.6f, 1.0f);
constexpr ImVec4 kGroupPinText(0.85f, 0.85f, 0.85f, 1.0f);
constexpr float kGroupOutIndent = 130.0f;          // a group's out pin's text: 150 less 20, less its width
constexpr ImU32 kWormholeBody = IM_COL32(52, 46, 74, 255);   // a Wormhole: dim violet, no title
constexpr ImVec4 kWormholeText(0.74f, 0.68f, 0.94f, 1.0f);
constexpr ImVec2 kWormholePadding(7.0f, 4.0f);
constexpr float kWormholeRounding = 9.0f;
constexpr float kPinSpot = 3.0f;                   // half the side of the box noted around a pin

const Node* node_named(const Patch& p, const std::string& id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

// The pin's place on screen, noted as "pin:<owner>.<label>": at the node's
// left edge for an input, its right edge (as last drawn) for an output, at
// the height of the row just drawn.
void note_pin(Places& places, int node, const std::string& owner, const std::string& label, bool input) {
  const float y = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
  const ImVec2 corner = ImNodes::GetNodeScreenSpacePos(node);
  const float x = input ? corner.x : corner.x + ImNodes::GetNodeDimensions(node).x;
  places.note("pin:" + owner + "." + label, Area{Vec2{x - kPinSpot, y - kPinSpot}, Vec2{x + kPinSpot, y + kPinSpot}});
}

// An input row: its name, and on a pin holding a number with nothing feeding
// it, the number.
void input_row(const Node* n, const CanvasPin& pin, const ImVec4& textColour) {
  ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
  if (pin.look == Look::Setting) {
    ImNodes::PushColorStyle(ImNodesCol_Pin, kDynamicColour);
    ImNodes::BeginInputAttribute(pin.id, pin.fed ? ImNodesPinShape_QuadFilled : ImNodesPinShape_Quad);
    ImGui::TextColored(kDynamicText, "%s", pin.label.c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", pin_tooltip(pin).c_str());
    ImNodes::EndInputAttribute();
    ImNodes::PopColorStyle();
  } else {
    ImNodes::BeginInputAttribute(pin.id);
    const float* number = nullptr;
    if (n)
      if (const auto held = n->values.find(pin.label); held != n->values.end()) number = &held->second;
    if (pin.look == Look::InputOnly && !pin.fed) {
      ImGui::TextColored(kGrey, "%s", pin.label.c_str());
    } else if (!pin.fed && number) {
      ImGui::TextColored(textColour, "%s", pin.label.c_str());
      ImGui::SameLine();
      ImGui::TextColored(kNumber, "%.3g", double(*number));
    } else {
      ImGui::TextColored(textColour, "%s", pin.label.c_str());
    }
    ImNodes::EndInputAttribute();
  }
  ImNodes::PopAttributeFlag();
}

void output_row(const CanvasPin& pin, float indent) {
  const bool tap = pin.look == Look::Tap;
  if (tap) ImNodes::PushColorStyle(ImNodesCol_Pin, kTapColour);
  ImNodes::BeginOutputAttribute(pin.id, tap ? ImNodesPinShape_Quad : ImNodesPinShape_CircleFilled);
  ImGui::Indent(std::max(1.0f, indent - ImGui::CalcTextSize(pin.label.c_str()).x));
  if (tap) ImGui::TextColored(kTapText, "%s", pin.label.c_str());
  else ImGui::TextUnformatted(pin.label.c_str());
  ImNodes::EndOutputAttribute();
  if (tap) ImNodes::PopColorStyle();
}

void push_node_colours(ImU32 title, ImU32 body) {
  ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
  ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, lighter(title, 12));
  ImNodes::PushColorStyle(ImNodesCol_NodeBackground, body);
  ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, lighter(body, 6));
}

void pop_node_colours() {
  for (int k = 0; k < 4; ++k) ImNodes::PopColorStyle();
}

// The title; with `listened`, the mark after it, noted as "mark:<listened>".
void title(const std::string& text, const std::string& listened, Places& places) {
  ImNodes::BeginNodeTitleBar();
  ImGui::PushStyleColor(ImGuiCol_Text, kTitleText);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopStyleColor();
  if (!listened.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(kListenMark, "<)))");
    places.note_item("mark:" + listened);
  }
  ImNodes::EndNodeTitleBar();
}

// A Wormhole: no title, one row: its input pin holding its name, then its
// output pin as a one-point mark.
void draw_wormhole(const Session& s, const CanvasNode& c, Places& places) {
  ImNodes::PushColorStyle(ImNodesCol_NodeBackground, kWormholeBody);
  ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, lighter(kWormholeBody, 14));
  ImNodes::PushStyleVar(ImNodesStyleVar_NodePadding, kWormholePadding);
  ImNodes::PushStyleVar(ImNodesStyleVar_NodeCornerRounding, kWormholeRounding);
  ImNodes::BeginNode(c.id);
  if (!c.inputs.empty()) {
    ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
    ImNodes::BeginInputAttribute(c.inputs[0].id);
    ImGui::TextColored(kWormholeText, "%s", c.name.c_str());
    if (s.listening == c.name) {
      ImGui::SameLine();
      ImGui::TextColored(kListenMark, "<)))");
      places.note_item("mark:" + c.name);
    }
    ImNodes::EndInputAttribute();
    ImNodes::PopAttributeFlag();
    note_pin(places, c.id, c.name, c.inputs[0].label, true);
  }
  ImGui::SameLine();
  ImNodes::BeginOutputAttribute(c.outputs[0].id);
  ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight()));
  ImNodes::EndOutputAttribute();
  note_pin(places, c.id, c.name, c.outputs[0].label, false);
  ImNodes::EndNode();
  ImNodes::PopStyleVar(2);
  ImNodes::PopColorStyle();
  ImNodes::PopColorStyle();
}

// A group: its name (and the mark while its output is listened to), how
// many nodes are under it, a pin for each wire in, one for each member read
// from outside.
void draw_group(const Session& s, const CanvasNode& c, Places& places) {
  push_node_colours(kGroupTitle, kGroupBody);
  ImNodes::BeginNode(c.id);
  const std::string out = s.listening.empty() ? std::string() : output_node(s.open.patch, c.name);
  title(c.name, !out.empty() && out == s.listening ? c.name : std::string(), places);
  ImGui::TextColored(kGroupCount, "%d nodes  (double-click)", c.under);
  for (const auto& pin : c.inputs) {
    input_row(nullptr, pin, kGroupPinText);
    note_pin(places, c.id, c.name, pin.label, true);
  }
  for (const auto& pin : c.outputs) {
    output_row(pin, kGroupOutIndent);
    note_pin(places, c.id, c.name, pin.label, false);
  }
  ImNodes::EndNode();
  pop_node_colours();
}

void draw_one(const Session& s, const CanvasNode& c, Places& places) {
  if (c.kind == CanvasNode::Kind::Group) return draw_group(s, c, places);
  if (c.kind == CanvasNode::Kind::Graph && c.type == "Wormhole") return draw_wormhole(s, c, places);
  const Patch& p = s.open.patch;
  const bool graph = c.kind == CanvasNode::Kind::Graph;
  const ImU32 colour = graph ? title_colour(c.type) : c.kind == CanvasNode::Kind::Face ? kNoteColour : kOutputColour;
  push_node_colours(colour, body_colour(colour));
  ImNodes::BeginNode(c.id);
  const std::string listened = graph && s.listening == c.name ? c.name : std::string();
  if (graph) title(c.name + "  (" + c.type + ")", listened, places);
  else title(c.kind == CanvasNode::Kind::Face ? c.name + "  (Note)" : std::string("Output"), {}, places);
  if (graph && !type_declarations(c.type)) ImGui::TextDisabled("type not registered");
  for (const auto& text : c.texts) ImGui::TextColored(kText, "%s", text.c_str());
  const Node* n = graph ? node_named(p, c.name) : nullptr;
  const std::string owner = c.kind == CanvasNode::Kind::Output ? std::string("(Output)") : c.name;
  for (const auto& pin : c.inputs) {
    input_row(n, pin, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    note_pin(places, c.id, owner, pin.label, true);
  }
  for (const auto& pin : c.outputs) {
    output_row(pin, kOutputIndent);
    note_pin(places, c.id, owner, pin.label, false);
  }
  ImNodes::EndNode();
  pop_node_colours();
}

}  // namespace

void NodeCanvas::draw_nodes(const Session& s, const CanvasTable& t) {
  for (const auto& c : t.nodes) draw_one(s, c, places_);
}

void NodeCanvas::draw_links(const CanvasTable& t) {
  for (const auto& l : t.links) {
    const bool coloured = l.look != CanvasLink::Look::Plain;
    if (coloured) ImNodes::PushColorStyle(ImNodesCol_Link, l.look == CanvasLink::Look::Tap ? kTapColour : kDynamicColour);
    ImNodes::Link(l.id, l.from, l.to);
    if (coloured) ImNodes::PopColorStyle();
  }
}

}  // namespace mforce::ui

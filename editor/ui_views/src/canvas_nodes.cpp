// The canvas's drawing: each node, face and the Output node from the table,
// and each link.
#include <algorithm>
#include <string>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/patch/type_declarations.h"
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

const Node* node_named(const Patch& p, const std::string& id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

// An input row: its name, and on a pin holding a number with nothing feeding
// it, the number.
void input_row(const Node* n, const CanvasPin& pin) {
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
      ImGui::TextUnformatted(pin.label.c_str());
      ImGui::SameLine();
      ImGui::TextColored(kNumber, "%.3g", double(*number));
    } else {
      ImGui::TextUnformatted(pin.label.c_str());
    }
    ImNodes::EndInputAttribute();
  }
  ImNodes::PopAttributeFlag();
}

void output_row(const CanvasPin& pin) {
  const bool tap = pin.look == Look::Tap;
  if (tap) ImNodes::PushColorStyle(ImNodesCol_Pin, kTapColour);
  ImNodes::BeginOutputAttribute(pin.id, tap ? ImNodesPinShape_Quad : ImNodesPinShape_CircleFilled);
  ImGui::Indent(std::max(1.0f, kOutputIndent - ImGui::CalcTextSize(pin.label.c_str()).x));
  if (tap) ImGui::TextColored(kTapText, "%s", pin.label.c_str());
  else ImGui::TextUnformatted(pin.label.c_str());
  ImNodes::EndOutputAttribute();
  if (tap) ImNodes::PopColorStyle();
}

void push_node_colours(ImU32 title) {
  ImNodes::PushColorStyle(ImNodesCol_TitleBar, title);
  ImNodes::PushColorStyle(ImNodesCol_TitleBarHovered, lighter(title, 12));
  ImNodes::PushColorStyle(ImNodesCol_NodeBackground, body_colour(title));
  ImNodes::PushColorStyle(ImNodesCol_NodeBackgroundHovered, lighter(body_colour(title), 6));
}

void pop_node_colours() {
  for (int k = 0; k < 4; ++k) ImNodes::PopColorStyle();
}

void title(const std::string& text) {
  ImNodes::BeginNodeTitleBar();
  ImGui::PushStyleColor(ImGuiCol_Text, kTitleText);
  ImGui::TextUnformatted(text.c_str());
  ImGui::PopStyleColor();
  ImNodes::EndNodeTitleBar();
}

void draw_one(const Patch& p, const CanvasNode& c) {
  const bool graph = c.kind == CanvasNode::Kind::Graph;
  const ImU32 colour = graph ? title_colour(c.type) : c.kind == CanvasNode::Kind::Face ? kNoteColour : kOutputColour;
  push_node_colours(colour);
  ImNodes::BeginNode(c.id);
  if (graph) title(c.name + "  (" + c.type + ")");
  else title(c.kind == CanvasNode::Kind::Face ? c.name + "  (Note)" : std::string("Output"));
  if (graph && !type_declarations(c.type)) ImGui::TextDisabled("type not registered");
  for (const auto& text : c.texts) ImGui::TextColored(kText, "%s", text.c_str());
  const Node* n = graph ? node_named(p, c.name) : nullptr;
  for (const auto& pin : c.inputs) input_row(n, pin);
  for (const auto& pin : c.outputs) output_row(pin);
  ImNodes::EndNode();
  pop_node_colours();
}

}  // namespace

void NodeCanvas::draw_nodes(const Session& s, const CanvasTable& t) {
  for (const auto& c : t.nodes) draw_one(s.open.patch, c);
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

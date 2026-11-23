#include "mforce/ui_views/node_canvas.h"
#include <algorithm>
#include <cstdio>
#include <set>
#include <string_view>
#include <vector>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_model/canvas_view.h"

namespace mforce::ui {

namespace {

constexpr int kOutputNode = 0;
constexpr int kPinsPerNode = 1024;
constexpr int kFeedsPerPin = 256;
constexpr unsigned int kTapColour = IM_COL32(230, 160, 60, 255);
constexpr unsigned int kDynamicColour = IM_COL32(110, 200, 120, 255);

int pin_int(int node, int row) { return node * kPinsPerNode + row; }
int link_int(int readerPin, int feed) { return readerPin * kFeedsPerPin + feed; }
ImVec2 im(Vec2 v) { return ImVec2(v.x, v.y); }
Vec2 vec(ImVec2 v) { return Vec2{v.x, v.y}; }

// A node's input rows, in order: each declared Param, each declared Input,
// then each setting a dynamic pin feeds. A pin's row in the node is its
// place here plus one (row 0 is the output).
std::vector<std::string> rows(const Node& n, const TypeDeclarations* d) {
  std::vector<std::string> names;
  if (d) {
    for (const auto& p : d->params) names.push_back(p.name);
    for (const auto& in : d->inputs) names.push_back(in.name);
  }
  for (const auto& [setting, source] : n.dynamicPins) names.push_back(setting);
  return names;
}

int row_of(const std::vector<std::string>& names, std::string_view name) {
  const auto it = std::find(names.begin(), names.end(), name);
  return it == names.end() ? -1 : int(it - names.begin()) + 1;
}

// A row's words: the pin's name, and its number when it holds one.
std::string row_text(const Node& n, const std::string& name) {
  const auto v = n.values.find(name);
  if (v == n.values.end()) return name;
  char number[32];
  std::snprintf(number, sizeof number, "%g", double(v->second));
  return name + "  " + number;
}

bool mouse_held() { return ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle); }

}  // namespace

int NodeCanvas::node_int(const std::string& id) {
  const auto [it, fresh] = ints_.try_emplace(id, nextInt_);
  if (fresh) ++nextInt_;
  return it->second;
}

std::string NodeCanvas::id_of(int nodeInt) const {
  for (const auto& [id, n] : ints_)
    if (n == nodeInt) return id;
  return {};
}

// The record's positions and panning, unless the user is moving them now.
void NodeCanvas::show_record(const Session& s) {
  const Layout& l = s.open.patch.layout;
  shownPanning_ = canvas_panning(s.open.patch);
  if (gesture_) return;
  ImNodes::EditorContextResetPanning(im(shownPanning_));
  for (const auto& n : s.open.patch.graph.nodes)
    if (auto p = l.positions.find(n.id); p != l.positions.end()) ImNodes::SetNodeGridSpacePos(node_int(n.id), im(p->second));
  if (l.outputPos) ImNodes::SetNodeGridSpacePos(kOutputNode, im(*l.outputPos));
}

void NodeCanvas::draw_nodes(const Session& s) {
  for (const Node& n : s.open.patch.graph.nodes) {
    const int id = node_int(n.id);
    const TypeDeclarations* d = type_declarations(n.type);
    ImNodes::BeginNode(id);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted((n.id + "  (" + n.type + ")").c_str());
    ImNodes::EndNodeTitleBar();
    if (!d) ImGui::TextDisabled("type not registered");
    const auto names = rows(n, d);
    for (size_t k = 0; k < names.size(); ++k) {
      ImNodes::BeginInputAttribute(pin_int(id, int(k) + 1));
      ImGui::TextUnformatted(row_text(n, names[k]).c_str());
      ImNodes::EndInputAttribute();
    }
    ImNodes::BeginOutputAttribute(pin_int(id, 0));
    ImGui::TextUnformatted("out");
    ImNodes::EndOutputAttribute();
    ImNodes::EndNode();
  }
}

void NodeCanvas::draw_output(const Session& s) {
  ImNodes::BeginNode(kOutputNode);
  ImNodes::BeginNodeTitleBar();
  ImGui::TextUnformatted("Output");
  ImNodes::EndNodeTitleBar();
  ImNodes::BeginInputAttribute(pin_int(kOutputNode, 1));
  ImGui::TextUnformatted(s.open.patch.graph.output.c_str());
  ImNodes::EndInputAttribute();
  ImNodes::EndNode();
}

void NodeCanvas::draw_links(const Session& s) {
  const Graph& g = s.open.patch.graph;
  std::set<std::string, std::less<>> present;
  for (const auto& n : g.nodes) present.insert(n.id);
  for (const Node& n : g.nodes) {
    const auto names = rows(n, type_declarations(n.type));
    const int reader = node_int(n.id);
    for (const auto& [pin, feeds] : n.wires) {
      const int row = row_of(names, pin);
      for (size_t f = 0; row > 0 && f < feeds.size(); ++f) {
        if (!present.count(feeds[f].source)) continue;
        if (feeds[f].tap) ImNodes::PushColorStyle(ImNodesCol_Link, kTapColour);
        ImNodes::Link(link_int(pin_int(reader, row), int(f)), pin_int(node_int(feeds[f].source), 0),
                      pin_int(reader, row));
        if (feeds[f].tap) ImNodes::PopColorStyle();
      }
    }
    for (const auto& [setting, source] : n.dynamicPins) {
      if (!present.count(source)) continue;
      ImNodes::PushColorStyle(ImNodesCol_Link, kDynamicColour);
      const int pin = pin_int(reader, row_of(names, setting));
      ImNodes::Link(link_int(pin, 0), pin_int(node_int(source), 0), pin);
      ImNodes::PopColorStyle();
    }
  }
  if (present.count(g.output))
    ImNodes::Link(link_int(pin_int(kOutputNode, 1), 0), pin_int(node_int(g.output), 0), pin_int(kOutputNode, 1));
}

// A position or the panning imnodes holds that the record does not: while
// the mouse is down the user is still moving it; once it is up, one edit
// carries it to the record.
void NodeCanvas::carry_moves(Session& s) {
  const bool held = mouse_held();
  bool moved = false;
  for (const auto& n : s.open.patch.graph.nodes) {
    const Vec2 now = vec(ImNodes::GetNodeGridSpacePos(node_int(n.id)));
    const auto p = s.open.patch.layout.positions.find(n.id);
    if (p == s.open.patch.layout.positions.end() || p->second == now) continue;   // a node the record places nowhere is not moved
    moved = true;
    if (!held) edit_position(s, n.id, now);
  }
  const Vec2 output = vec(ImNodes::GetNodeGridSpacePos(kOutputNode));
  if (s.open.patch.layout.outputPos && *s.open.patch.layout.outputPos != output) {
    moved = true;
    if (!held) edit_output_position(s, output);
  }
  const Vec2 panning = vec(ImNodes::EditorContextGetPanning());
  if (!(panning == shownPanning_)) {
    moved = true;
    if (!held) pan_canvas(s, panning);
  }
  gesture_ = held && (moved || gesture_);
}

// A click on a node selects it, on the empty canvas selects nothing; the
// record's selection is what imnodes shows.
void NodeCanvas::carry_selection(Session& s) {
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImNodes::IsEditorHovered()) {
    int hovered = -1;
    select_node(s, ImNodes::IsNodeHovered(&hovered) ? id_of(hovered) : std::string());
  }
  ImNodes::ClearNodeSelection();
  if (!s.selected.empty() && ints_.count(s.selected)) {
    const auto& nodes = s.open.patch.graph.nodes;
    if (std::any_of(nodes.begin(), nodes.end(), [&](const Node& n) { return n.id == s.selected; }))
      ImNodes::SelectNode(ints_.at(s.selected));
  }
}

void NodeCanvas::draw(Session& s) {
  ImNodes::BeginNodeEditor();
  show_record(s);
  draw_nodes(s);
  draw_output(s);
  draw_links(s);
  ImNodes::EndNodeEditor();
  carry_moves(s);
  carry_selection(s);
}

}  // namespace mforce::ui

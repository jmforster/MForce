// The canvas's selection, kept in step with imnodes', and a double-click.
#include <algorithm>
#include <string>
#include <vector>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_control/grouping.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_model/groups.h"
#include "mforce/ui_views/node_canvas.h"

namespace mforce::ui {

namespace {

// The name a drawn thing's integer stands for; empty for the Output node
// and for an integer the table does not have.
std::string name_of(const CanvasTable& t, int id) {
  const CanvasNode* n = t.node(id);
  return n && n->kind != CanvasNode::Kind::Output ? n->name : std::string();
}

bool holds(const std::vector<std::string>& names, const std::string& name) {
  return std::find(names.begin(), names.end(), name) != names.end();
}

// What imnodes holds selected, by name.
std::vector<std::string> imnodes_selection(const CanvasTable& t) {
  std::vector<int> ids(size_t(std::max(0, ImNodes::NumSelectedNodes())));
  if (!ids.empty()) ImNodes::GetSelectedNodes(ids.data());
  std::vector<std::string> names;
  for (int id : ids)
    if (const std::string n = name_of(t, id); !n.empty()) names.push_back(n);
  return names;
}

}  // namespace

// While the left button is down imnodes owns the selection: a click, Ctrl
// and a click, a box, a drag of all it holds. On the frame the button is let
// go after going down on the canvas, the record takes imnodes' selection,
// with a Ctrl and a click on a pin (which imnodes takes for the start of a
// wire, near the pin's hover radius) toggling the pin's owner; a Ctrl and a
// drag out of a pin is a wire and toggles nothing. On every
// other frame with the button up, imnodes is given the record's names shown
// here.
void NodeCanvas::carry_selection(Session& s, const CanvasTable& t) {
  int hovered = -1;
  const bool overNode = ImNodes::IsNodeHovered(&hovered);
  hoveredNode_ = overNode ? name_of(t, hovered) : std::string();
  if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overCanvas_) {
    pressedHere_ = true;
    ctrlPin_.clear();
    ctrlDeselect_ = false;
    int pin = -1;
    if (ImGui::GetIO().KeyCtrl) {
      const std::string thing = overNode ? hoveredNode_
                                : ImNodes::IsPinHovered(&pin) ? name_of(t, pin / CanvasNames::kPinsPerNode)
                                                              : std::string();
      if (!overNode) ctrlPin_ = thing;
      ctrlDeselect_ = !thing.empty() && holds(s.selections, thing);
    }
  }
  if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && pressedHere_) {
    pressedHere_ = false;
    const ImGuiIO& io = ImGui::GetIO();
    if (!ctrlPin_.empty() && io.MouseDragMaxDistanceSqr[0] > io.MouseDragThreshold * io.MouseDragThreshold) {
      ctrlPin_.clear();                             // a drag out of the pin: a wire, not a toggle
      ctrlDeselect_ = false;
    }
    std::vector<std::string> names = imnodes_selection(t);
    if (!ctrlPin_.empty()) {
      const auto it = std::find(names.begin(), names.end(), ctrlPin_);
      if (it == names.end()) names.push_back(ctrlPin_);
      else names.erase(it);
    }
    select_nodes(s, names);
    if (ctrlDeselect_) s.status = "deselected";
    ctrlPin_.clear();
  }
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) return;
  ImNodes::ClearNodeSelection();
  for (const auto& n : t.nodes)
    if (holds(s.selections, n.name) && n.kind != CanvasNode::Kind::Output) ImNodes::SelectNode(n.id);
}

// A double-click on a group goes in; on a Wormhole with a twin, to the
// twin's level, the twin centred and selected. Either way the press that
// made it gives no selection of its own when the button is let go.
void NodeCanvas::carry_double_click(Session& s, const CanvasTable& t) {
  if (!overCanvas_ || !ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) return;
  int hovered = -1;
  if (!ImNodes::IsNodeHovered(&hovered)) return;
  const CanvasNode* n = t.node(hovered);
  if (!n) return;
  if (n->kind == CanvasNode::Kind::Group) {
    open_group(s, n->name);
    pressedHere_ = false;
    return;
  }
  if (n->kind != CanvasNode::Kind::Graph || n->type != "Wormhole") return;
  const std::string twin = wormhole_twin(s.open.patch, n->name);
  if (twin.empty()) return;
  go_to(s, twin, size_);
  pressedHere_ = false;
}

}  // namespace mforce::ui

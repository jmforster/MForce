// What the canvas shows of a Wormhole pair, whose span between the halves
// is never drawn: the span's ghost while a half is selected or under the
// mouse, and the tooltip naming the twin.
#include <algorithm>
#include <string>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_model/groups.h"
#include "mforce/ui_views/node_canvas.h"

namespace mforce::ui {

namespace {

constexpr ImU32 kGhostLine = IM_COL32(140, 170, 200, 110);
constexpr ImU32 kGhostBox = IM_COL32(140, 170, 200, 200);
constexpr float kGhostWidth = 2.0f;
constexpr float kGhostRounding = 6.0f;

// The drawn thing that stands for `name` at this level; null when none does.
const CanvasNode* drawn_for(const Session& s, const CanvasTable& t, const std::string& name) {
  const std::string rep = shown_for(s.open.patch, s.groupPath, name);
  if (rep.empty()) return nullptr;
  for (const auto& n : t.nodes)
    if (n.name == rep && n.kind != CanvasNode::Kind::Output) return &n;
  return nullptr;
}

ImVec2 centre_of(int id) {
  const ImVec2 lo = ImNodes::GetNodeScreenSpacePos(id), size = ImNodes::GetNodeDimensions(id);
  return ImVec2(lo.x + size.x * 0.5f, lo.y + size.y * 0.5f);
}

}  // namespace

// Drawn inside the editor, in the channel its wires are drawn in: over the
// grid, under the nodes. A half counts as under the mouse by the frame
// before's answer, the one known while the editor draws. A freshly wired
// pair stays visibly connected while selected, so pairing never looks like
// a wire's deletion.
void NodeCanvas::draw_ghosts(const Session& s, const CanvasTable& t) {
  ImDrawList* list = ImGui::GetWindowDrawList();
  list->ChannelsSetCurrent(0);
  for (const auto& half : t.nodes) {
    if (half.kind != CanvasNode::Kind::Graph || half.type != "Wormhole") continue;
    const bool picked = std::find(s.selections.begin(), s.selections.end(), half.name) != s.selections.end();
    if (!picked && hoveredNode_ != half.name) continue;
    const CanvasNode* twin = drawn_for(s, t, wormhole_twin(s.open.patch, half.name));
    if (!twin) continue;
    list->AddLine(centre_of(half.id), centre_of(twin->id), kGhostLine, kGhostWidth);
    const ImVec2 lo = ImNodes::GetNodeScreenSpacePos(twin->id), size = ImNodes::GetNodeDimensions(twin->id);
    list->AddRect(lo, ImVec2(lo.x + size.x, lo.y + size.y), kGhostBox, kGhostRounding, 0, kGhostWidth);
    places_.note("ghost:" + half.name, Area{Vec2{lo.x, lo.y}, Vec2{lo.x + size.x, lo.y + size.y}});
  }
}

// While a Wormhole is under the mouse: whether it has a twin, which, and
// where the twin is.
void NodeCanvas::wormhole_tooltip(const Session& s, const CanvasTable& t) {
  int hovered = -1;
  if (!ImNodes::IsNodeHovered(&hovered)) return;
  const CanvasNode* half = t.node(hovered);
  if (!half || half->kind != CanvasNode::Kind::Graph || half->type != "Wormhole") return;
  const std::string twin = wormhole_twin(s.open.patch, half->name);
  if (twin.empty()) {
    ImGui::SetTooltip("unpaired wormhole — wire it to another Wormhole to open the far end");
    return;
  }
  const std::string holder = group_holding(s.open.patch, twin);
  const std::string where = holder.empty() ? std::string("in main") : "inside '" + holder + "'";
  ImGui::SetTooltip("twin: %s — %s\ndouble-click: jump", twin.c_str(), where.c_str());
}

}  // namespace mforce::ui

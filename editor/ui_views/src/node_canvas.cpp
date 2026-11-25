// The canvas's frame: the table made for the level shown, the record shown,
// the nodes and links drawn, then what the person did carried to ui_control.
#include "mforce/ui_views/node_canvas.h"
#include <algorithm>
#include <optional>
#include <string>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/grouping.h"
#include "canvas_look.h"

namespace mforce::ui {

using namespace canvas_look;   // the canvas's colours and conversions by their short names

namespace {

constexpr float kMiniMapShare = 0.2f;

// The same link: its integer, its two pins, the end it feeds and the feed's number.
bool same_links(const std::vector<CanvasLink>& a, const std::vector<CanvasLink>& b) {
  return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](const CanvasLink& x, const CanvasLink& y) {
    return x.id == y.id && x.from == y.from && x.to == y.to && x.feed == y.feed && x.reader.kind == y.reader.kind &&
           x.reader.node == y.reader.node && x.reader.name == y.reader.name;
  });
}

bool mouse_held() { return ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle); }

// Where the record places a drawn thing; none for one it places nowhere.
std::optional<Vec2> record_pos(const Patch& p, const CanvasNode& n) {
  const Layout& l = p.layout;
  if (n.kind == CanvasNode::Kind::Output) return l.outputPos;
  if (n.kind == CanvasNode::Kind::Face) {
    for (const auto& f : l.faces)
      if (f.label == n.name) return f.pos;
    return std::nullopt;
  }
  if (n.kind == CanvasNode::Kind::Group) {
    for (const auto& g : l.groups)
      if (g.name == n.name) return g.pos;
    return std::nullopt;
  }
  const auto it = l.positions.find(n.name);
  return it == l.positions.end() ? std::nullopt : std::optional<Vec2>(it->second);
}

}  // namespace

NodeCanvas::NodeCanvas(Clipboard& clipboard) : clipboard_(clipboard) {}

// The record's positions and the level's panning, unless the user is moving them now.
void NodeCanvas::show_record(const Session& s, const CanvasTable& t) {
  shownPanning_ = shown_panning(s);
  if (gesture_) return;
  ImNodes::EditorContextResetPanning(im(shownPanning_));
  for (const auto& n : t.nodes)
    if (const auto pos = record_pos(s.open.patch, n)) ImNodes::SetNodeGridSpacePos(n.id, im(*pos));
}

// A position or the panning imnodes holds that the record does not: while
// the mouse is down the user is still moving it; once it is up, one edit
// per thing moved carries it to the record, all one step, and the step ends.
void NodeCanvas::carry_moves(Session& s, const CanvasTable& t) {
  const bool held = mouse_held();
  bool moved = false, carried = false;
  for (const auto& n : t.nodes) {
    const auto pos = record_pos(s.open.patch, n);
    const Vec2 now = vec(ImNodes::GetNodeGridSpacePos(n.id));
    if (!pos || *pos == now) continue;                 // a thing the record places nowhere is not moved
    moved = true;
    if (held) continue;
    carried = true;
    if (n.kind == CanvasNode::Kind::Output) edit_output_position(s, now);
    else edit_position(s, n.name, now);
  }
  if (carried) end_gesture(s);
  const Vec2 panning = vec(ImNodes::EditorContextGetPanning());
  if (!(panning == shownPanning_)) {
    moved = true;
    if (!held) pan_canvas(s, panning);
  }
  gesture_ = held && (moved || gesture_);
}

// Each drawn thing's box, by its kind's name.
void NodeCanvas::note_places(const CanvasTable& t) {
  for (const auto& n : t.nodes) {
    const ImVec2 lo = ImNodes::GetNodeScreenSpacePos(n.id), size = ImNodes::GetNodeDimensions(n.id);
    const std::string name = n.kind == CanvasNode::Kind::Group    ? "group:" + n.name
                             : n.kind == CanvasNode::Kind::Output ? std::string("node:(Output)")
                                                                  : "node:" + n.name;
    places_.note(name, Area{vec(lo), Vec2{lo.x + size.x, lo.y + size.y}});
  }
}

void NodeCanvas::draw(Session& s) {
  places_.clear();
  if (seenOpened_ != s.opened) {                    // another patch: its things get integers afresh
    names_ = CanvasNames{};
    gesture_ = false;
    seenOpened_ = s.opened;
    ImNodes::ClearLinkSelection();
  }
  const CanvasTable table = canvas_table(s.open.patch, s.groupPath, s.offered, names_);
  if (!same_links(table.links, drawnLinks_)) ImNodes::ClearLinkSelection();   // a selected slot may now be another wire
  drawnLinks_ = table.links;
  origin_ = vec(ImGui::GetCursorScreenPos());
  size_ = vec(ImGui::GetContentRegionAvail());
  ImNodes::BeginNodeEditor();
  // Asked here, of the editor's own window: once EndNodeEditor has closed it
  // the window is the one around it, which the mouse is never directly over.
  overCanvas_ = ImGui::IsWindowHovered();
  show_record(s, table);
  draw_nodes(s, table);
  draw_ghosts(s, table);
  draw_links(table);
  ImNodes::MiniMap(kMiniMapShare, ImNodesMiniMapLocation_BottomRight);
  ImNodes::EndNodeEditor();
  note_places(table);
  wormhole_tooltip(s, table);
  open_menus(s, table);
  carry_wiring(s, table);
  carry_moves(s, table);
  carry_selection(s, table);
  carry_double_click(s, table);
  carry_keys(s, table);
  carry_wheel(s);
  draw_menus(s);
}

}  // namespace mforce::ui

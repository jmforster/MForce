// The canvas's menus: a right click let go over a thing opens its menu
// (canvas_thing_menu.cpp), over a wire routes it through a pair of
// Wormholes, over empty canvas opens the create menu: Paste, then
// create_menu()'s.
#include <cstddef>
#include <string>
#include <vector>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/clipboard.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_views/node_canvas.h"
#include "canvas_look.h"
#include "canvas_popups.h"

namespace mforce::ui {

using namespace canvas_look;   // the canvas's colours and conversions by their short names

namespace {

// A family's items as a submenu; the type chosen, or none.
const CreateItem* family_menu(const CreateFamily& f) {
  const CreateItem* chosen = nullptr;
  if (f.separatorBefore) ImGui::Separator();
  if (!ImGui::BeginMenu(f.name.c_str())) return nullptr;
  for (const auto& item : f.items)
    if (ImGui::MenuItem(item.label.c_str())) chosen = &item;
  ImGui::EndMenu();
  return chosen;
}

}  // namespace

namespace canvas_popups {

const CreateItem* families_menu(const std::vector<CreateFamily>& families) {
  const CreateItem* chosen = nullptr;
  for (const auto& f : families)
    if (const CreateItem* c = family_menu(f)) chosen = c;
  return chosen;
}

}  // namespace canvas_popups

// A right click let go over a thing (never the Output node) opens its menu,
// the thing alone selected first when it is not selected; over a wire, the
// wire is routed through wormholes at once; over empty canvas, the create
// menu, its point kept in the record's coordinates: the screen point less
// the canvas's origin and its panning.
void NodeCanvas::open_menus(Session& s, const CanvasTable& t) {
  if (!overCanvas_ || !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) return;
  int hovered = -1;
  if (ImNodes::IsNodeHovered(&hovered)) {
    const CanvasNode* n = t.node(hovered);
    if (!n || n->kind == CanvasNode::Kind::Output) return;
    menuNode_ = n->name;
    menuKind_ = n->kind;
    bool chosen = false;
    for (const auto& name : s.selections) chosen = chosen || name == n->name;
    if (!chosen) select_node(s, n->name);
    ImGui::OpenPopup(n->kind == CanvasNode::Kind::Group ? canvas_popups::kGroup : canvas_popups::kThing);
    return;
  }
  if (ImNodes::IsLinkHovered(&hovered)) {
    if (const CanvasLink* l = t.link(hovered)) route_wire(s, l->reader, l->feed);
    return;
  }
  if (ImNodes::IsPinHovered(&hovered)) return;
  const ImVec2 mouse = ImGui::GetMousePos();
  const Vec2 panning = vec(ImNodes::EditorContextGetPanning());
  menuAt_ = Vec2{mouse.x - origin_.x - panning.x, mouse.y - origin_.y - panning.y};
  ImGui::OpenPopup(canvas_popups::kCreate);
}

// The create menu: Paste (at the click), then the favourites, the families
// and the Experimental shelf.
void NodeCanvas::create_items(Session& s) {
  const bool paste = ImGui::MenuItem("Paste", "Ctrl+V");
  places_.note_item("menu:Paste");
  ImGui::Separator();
  const CreateItem* chosen = nullptr;
  for (size_t k = 0; k < menu_.favourites.size(); ++k) {
    if (k == 1) ImGui::Separator();
    const CreateItem& item = menu_.favourites[k];
    if (ImGui::MenuItem(item.label.c_str(), nullptr, false, can_create(item, s.open.patch))) chosen = &item;
    places_.note_item("menu:" + item.label);
  }
  ImGui::Separator();
  if (const CreateItem* c = canvas_popups::families_menu(menu_.main)) chosen = c;
  if (!menu_.experimental.empty()) {
    ImGui::Separator();
    if (ImGui::BeginMenu("Experimental")) {
      if (const CreateItem* c = canvas_popups::families_menu(menu_.experimental)) chosen = c;
      ImGui::EndMenu();
    }
  }
  ImGui::EndPopup();
  if (paste) paste_text(s, clipboard_.text(), menuAt_);
  else if (chosen && chosen->type.empty()) create_note(s, menuAt_);
  else if (chosen) create_node(s, chosen->type, menuAt_);
}

void NodeCanvas::draw_menus(Session& s) {
  if (ImGui::BeginPopup(canvas_popups::kThing)) thing_menu(s);
  if (ImGui::BeginPopup(canvas_popups::kGroup)) group_menu(s);
  if (ImGui::BeginPopup(canvas_popups::kCreate)) create_items(s);
}

}  // namespace mforce::ui

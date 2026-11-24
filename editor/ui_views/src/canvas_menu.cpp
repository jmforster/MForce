// The canvas's create menu: opened by a right click on empty canvas, at the
// mouse; what it holds is create_menu()'s.
#include <cstddef>
#include <vector>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_views/node_canvas.h"
#include "canvas_look.h"

namespace mforce::ui {

using namespace canvas_look;   // the canvas's colours and conversions by their short names

namespace {

constexpr const char* kPopup = "create";

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

const CreateItem* families_menu(const std::vector<CreateFamily>& families) {
  const CreateItem* chosen = nullptr;
  for (const auto& f : families)
    if (const CreateItem* c = family_menu(f)) chosen = c;
  return chosen;
}

}  // namespace

// A right click let go over empty canvas opens the menu; the point is kept
// in the record's coordinates: the screen point less the canvas's origin and
// its panning.
void NodeCanvas::open_create_menu() {
  if (!ImNodes::IsEditorHovered() || !ImGui::IsMouseReleased(ImGuiMouseButton_Right)) return;
  int hovered = -1;
  if (ImNodes::IsNodeHovered(&hovered) || ImNodes::IsLinkHovered(&hovered) || ImNodes::IsPinHovered(&hovered)) return;
  const ImVec2 mouse = ImGui::GetMousePos();
  const Vec2 panning = vec(ImNodes::EditorContextGetPanning());
  menuAt_ = Vec2{mouse.x - origin_.x - panning.x, mouse.y - origin_.y - panning.y};
  ImGui::OpenPopup(kPopup);
}

void NodeCanvas::draw_create_menu(Session& s) {
  if (!ImGui::BeginPopup(kPopup)) return;
  const CreateItem* chosen = nullptr;
  for (size_t k = 0; k < menu_.favourites.size(); ++k) {
    if (k == 1) ImGui::Separator();
    if (ImGui::MenuItem(menu_.favourites[k].label.c_str())) chosen = &menu_.favourites[k];
  }
  ImGui::Separator();
  if (const CreateItem* c = families_menu(menu_.main)) chosen = c;
  if (!menu_.experimental.empty()) {
    ImGui::Separator();
    if (ImGui::BeginMenu("Experimental")) {
      if (const CreateItem* c = families_menu(menu_.experimental)) chosen = c;
      ImGui::EndMenu();
    }
  }
  ImGui::EndPopup();
  if (!chosen) return;
  if (chosen->type.empty()) create_note(s, menuAt_);
  else create_node(s, chosen->type, menuAt_);
}

}  // namespace mforce::ui

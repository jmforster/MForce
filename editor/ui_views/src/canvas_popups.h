#pragma once
#include <vector>
#include "mforce/ui_views/create_menu.h"

// What the canvas's two menu files share: the menus' names and the families'
// submenus. Included only by .cpp files of ui_views.
namespace mforce::ui::canvas_popups {

constexpr const char* kCreate = "create";
constexpr const char* kThing = "node";
constexpr const char* kGroup = "group";

// Each family's items as a submenu; the item chosen, or none.
const CreateItem* families_menu(const std::vector<CreateFamily>& families);

}  // namespace mforce::ui::canvas_popups

#include "mforce/ui_views/places.h"
#include <imgui.h>

namespace mforce::ui {

void Places::note_item(const std::string& name) {
  const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
  areas_[name] = Area{Vec2{lo.x, lo.y}, Vec2{hi.x, hi.y}};
}

std::optional<Area> Places::area(const std::string& name) const {
  const auto it = areas_.find(name);
  if (it == areas_.end()) return std::nullopt;
  return it->second;
}

}  // namespace mforce::ui

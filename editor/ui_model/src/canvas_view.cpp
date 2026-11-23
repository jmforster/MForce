#include "mforce/ui_model/canvas_view.h"
#include <algorithm>

namespace mforce::ui {

namespace {

constexpr float kMargin = 40.0f;

}  // namespace

Vec2 canvas_panning(const Patch& patch) {
  const Layout& l = patch.layout;
  if (l.panning) return *l.panning;
  bool any = false;
  Vec2 least;
  const auto take = [&](Vec2 p) {
    least = any ? Vec2{std::min(least.x, p.x), std::min(least.y, p.y)} : p;
    any = true;
  };
  for (const auto& [id, pos] : l.positions) take(pos);
  if (l.outputPos) take(*l.outputPos);
  return any ? Vec2{kMargin - least.x, kMargin - least.y} : Vec2{kMargin, kMargin};
}

}  // namespace mforce::ui

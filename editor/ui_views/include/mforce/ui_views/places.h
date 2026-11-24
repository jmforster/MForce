#pragma once
#include <map>
#include <optional>
#include <string>
#include "mforce/patch/layout.h"

namespace mforce::ui {

// A rectangle on screen.
struct Area {
  Vec2 lo, hi;
  Vec2 centre() const { return Vec2{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f}; }
};

// Where a view drew each thing a test or a picture names, by name, from the
// last frame: drawing state, kept by the view and cleared as it begins a frame.
class Places {
public:
  void clear() { areas_.clear(); }
  void note(const std::string& name, Area a) { areas_[name] = a; }
  void note_item(const std::string& name);         // the item ImGui drew last
  std::optional<Area> area(const std::string& name) const;

private:
  std::map<std::string, Area> areas_;
};

}  // namespace mforce::ui

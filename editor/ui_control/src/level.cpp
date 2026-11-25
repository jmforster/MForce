#include "level.h"
#include <algorithm>
#include <optional>
#include <utility>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/grouping.h"
#include "mforce/ui_model/groups.h"

namespace mforce::ui::level {

namespace {

constexpr float kFrameMargin = 80.0f;

void show_panning(Session& s, Vec2 v) {
  if (s.groupPath.empty()) (void)set_panning(s.open.patch, v);
  else s.levelPanning = v;
}

}  // namespace

std::optional<Vec2> place_of(const Patch& p, const std::string& name) {
  if (const auto it = p.layout.positions.find(name); it != p.layout.positions.end()) return it->second;
  for (const auto& f : p.layout.faces)
    if (f.label == name) return f.pos;
  for (const auto& g : p.layout.groups)
    if (g.name == name) return g.pos;
  return std::nullopt;
}

Vec2 framed(const Patch& p, const std::vector<std::string>& path) {
  bool any = false;
  Vec2 least{};
  const auto take = [&](Vec2 at) {
    least = any ? Vec2{std::min(least.x, at.x), std::min(least.y, at.y)} : at;
    any = true;
  };
  for (const auto& name : shown_at(p, path))
    if (const auto at = place_of(p, name)) take(*at);
  if (path.empty() && p.layout.outputPos) take(*p.layout.outputPos);
  return any ? Vec2{kFrameMargin - least.x, kFrameMargin - least.y} : Vec2{kFrameMargin, kFrameMargin};
}

bool set_path(Session& s, std::vector<std::string> to) {
  if (to == s.groupPath) return false;
  const std::vector<std::string> from = s.groupPath;
  const bool deeper = to.size() > from.size() && std::equal(from.begin(), from.end(), to.begin());
  s.selections.clear();
  if (deeper) {
    s.pans.resize(from.size() + 1);
    s.pans[from.size()] = shown_panning(s);
    s.groupPath = std::move(to);
    show_panning(s, framed(s.open.patch, s.groupPath));
    return false;
  }
  s.groupPath = std::move(to);
  if (s.groupPath.size() < s.pans.size()) {
    show_panning(s, s.pans[s.groupPath.size()]);
    s.pans.resize(s.groupPath.size());
  } else {
    show_panning(s, framed(s.open.patch, s.groupPath));
  }
  if (s.listening.empty()) return false;
  s.listening.clear();
  return true;
}

bool follow_path(Session& s) {
  const std::size_t depth = valid_depth(s.open.patch, s.groupPath);
  if (depth == s.groupPath.size()) return false;
  return set_path(s, std::vector<std::string>(s.groupPath.begin(), s.groupPath.begin() + std::ptrdiff_t(depth)));
}

}  // namespace mforce::ui::level

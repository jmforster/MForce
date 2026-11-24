#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_model/placement.h"
#include <algorithm>
#include <set>
#include <string>

namespace mforce::ui {

namespace {

constexpr float kMargin = 40.0f;

std::set<std::string> face_nodes(const Layout& l) {
  std::set<std::string> ids;
  for (const auto& f : l.faces)
    for (const auto& [field, id] : f.fields) ids.insert(id);
  return ids;
}

}  // namespace

Vec2 fit_panning(const Patch& patch) {
  const Layout& l = patch.layout;
  const std::set<std::string> named = face_nodes(l);
  bool any = false;
  Vec2 least;
  const auto take = [&](Vec2 p) {
    least = any ? Vec2{std::min(least.x, p.x), std::min(least.y, p.y)} : p;
    any = true;
  };
  for (const auto& [id, pos] : l.positions)
    if (!named.count(id)) take(pos);
  for (const auto& f : l.faces)
    if (f.pos) take(*f.pos);
  if (l.outputPos) take(*l.outputPos);
  return any ? Vec2{kMargin - least.x, kMargin - least.y} : Vec2{kMargin, kMargin};
}

Vec2 centred_panning(const Patch& patch, Vec2 canvasSize) {
  const Layout& l = patch.layout;
  const std::set<std::string> named = face_nodes(l);
  bool any = false;
  Vec2 lo, hi;
  const auto take = [&](Vec2 p, Vec2 size) {
    lo = any ? Vec2{std::min(lo.x, p.x), std::min(lo.y, p.y)} : p;
    hi = any ? Vec2{std::max(hi.x, p.x + size.x), std::max(hi.y, p.y + size.y)} : Vec2{p.x + size.x, p.y + size.y};
    any = true;
  };
  for (const auto& n : patch.graph.nodes)
    if (auto p = l.positions.find(n.id); p != l.positions.end() && !named.count(n.id)) take(p->second, node_slot(n));
  for (const auto& f : l.faces)
    if (f.pos) take(*f.pos, face_slot());
  if (l.outputPos) take(*l.outputPos, output_slot());
  if (!any) return fit_panning(patch);
  return Vec2{canvasSize.x * 0.5f - (lo.x + hi.x) * 0.5f, canvasSize.y * 0.5f - (lo.y + hi.y) * 0.5f};
}

Vec2 canvas_panning(const Patch& patch) {
  return patch.layout.panning ? *patch.layout.panning : fit_panning(patch);
}

CanvasCounts canvas_counts(const Patch& patch) {
  const std::set<std::string> named = face_nodes(patch.layout);
  CanvasCounts c;
  for (const auto& n : patch.graph.nodes) {
    if (!named.count(n.id)) ++c.nodes;
    for (const auto& [pin, feeds] : n.wires) c.wires += int(feeds.size());
    c.wires += int(n.dynamicPins.size());
  }
  c.nodes += int(patch.layout.faces.size());
  if (!patch.graph.output.empty()) ++c.wires;
  return c;
}

}  // namespace mforce::ui

#include "stepping.h"
#include <algorithm>
#include <iterator>

namespace mforce::ui::stepping {

bool has_node(const Patch& p, std::string_view id) {
  return std::any_of(p.graph.nodes.begin(), p.graph.nodes.end(), [&](const Node& n) { return n.id == id; });
}

bool has_face(const Patch& p, std::string_view label) {
  return std::any_of(p.layout.faces.begin(), p.layout.faces.end(), [&](const NoteFace& f) { return f.label == label; });
}

void forget_what_went(Session& s) {
  const Patch& p = s.open.patch;
  if (!s.selected.empty() && !has_node(p, s.selected) && !has_face(p, s.selected)) s.selected.clear();
  std::erase_if(s.offered, [&](const auto& offer) { return !has_node(p, offer.first); });
}

}  // namespace mforce::ui::stepping

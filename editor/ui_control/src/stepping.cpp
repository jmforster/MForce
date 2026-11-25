#include "stepping.h"
#include "level.h"
#include <algorithm>
#include <iterator>
#include <string>
#include <vector>
#include "mforce/ui_model/groups.h"

namespace mforce::ui::stepping {

bool has_node(const Patch& p, std::string_view id) {
  return std::any_of(p.graph.nodes.begin(), p.graph.nodes.end(), [&](const Node& n) { return n.id == id; });
}

bool has_face(const Patch& p, std::string_view label) {
  return std::any_of(p.layout.faces.begin(), p.layout.faces.end(), [&](const NoteFace& f) { return f.label == label; });
}

bool has_group(const Patch& p, std::string_view name) {
  return std::any_of(p.layout.groups.begin(), p.layout.groups.end(), [&](const Group& g) { return g.name == name; });
}

bool forget_what_went(Session& s) {
  const Patch& p = s.open.patch;
  std::erase_if(s.offered, [&](const auto& offer) { return !has_node(p, offer.first); });
  bool ended = false;
  if (!s.listening.empty() && !has_node(p, s.listening)) {
    s.listening.clear();
    ended = true;
  }
  ended = level::follow_path(s) || ended;
  const std::vector<std::string> shown = shown_at(p, s.groupPath);
  std::erase_if(s.selections, [&](const std::string& name) {
    return std::find(shown.begin(), shown.end(), name) == shown.end();
  });
  return ended;
}

}  // namespace mforce::ui::stepping

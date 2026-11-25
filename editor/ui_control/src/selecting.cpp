#include "mforce/ui_control/selecting.h"
#include <algorithm>
#include <string>
#include <utility>
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

const std::string kNone;   // what selected_one answers when not exactly one thing is selected

}  // namespace

void select_node(Session& s, std::string_view name) {
  s.selections.clear();
  if (!name.empty()) s.selections.emplace_back(name);
}

void select_nodes(Session& s, std::vector<std::string> names) {
  s.selections.clear();
  for (auto& n : names)
    if (!n.empty() && std::find(s.selections.begin(), s.selections.end(), n) == s.selections.end())
      s.selections.push_back(std::move(n));
}

void toggle_selected(Session& s, std::string_view name) {
  if (name.empty()) return;
  const auto it = std::find(s.selections.begin(), s.selections.end(), name);
  if (it == s.selections.end()) s.selections.emplace_back(name);
  else s.selections.erase(it);
}

const std::string& selected_one(const Session& s) { return s.selections.size() == 1 ? s.selections.front() : kNone; }

}  // namespace mforce::ui

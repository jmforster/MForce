#include "mforce/ui_control/grouping.h"
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_model/groups.h"
#include "level.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

const Vec2 kNoPlace{200, 200};   // where a group of things with no position goes

// The group the canvas shows; empty at the top.
std::string open_one(const Session& s) { return s.groupPath.empty() ? std::string() : s.groupPath.back(); }

// Whether a note face names node `id` for a field.
bool face_node(const Patch& p, const std::string& id) {
  for (const auto& f : p.layout.faces)
    for (const auto& [field, node] : f.fields)
      if (node == id) return true;
  return false;
}

// The selected things a group can hold, in the order chosen: groups, faces
// and nodes that are no face's node.
std::vector<std::string> groupable(const Session& s) {
  const Patch& p = s.open.patch;
  std::vector<std::string> names;
  for (const auto& n : s.selections)
    if (has_group(p, n) || has_face(p, n) || (has_node(p, n) && !face_node(p, n))) names.push_back(n);
  return names;
}

// The selected nodes and faces, which join and leave groups (a selected
// group does not).
std::vector<std::string> targets(const Session& s) {
  std::vector<std::string> names;
  for (const auto& n : groupable(s))
    if (!has_group(s.open.patch, n)) names.push_back(n);
  return names;
}

Vec2 centroid(const Patch& p, const std::vector<std::string>& names) {
  Vec2 sum{};
  int count = 0;
  for (const auto& n : names)
    if (const auto at = level::place_of(p, n)) {
      sum = Vec2{sum.x + at->x, sum.y + at->y};
      ++count;
    }
  return count ? Vec2{sum.x / float(count), sum.y / float(count)} : kNoPlace;
}

// After a group step: what went is forgotten, and the sound follows when
// that ended listening.
void after_step(Session& s) {
  if (forget_what_went(s)) sound_follows(s);
}

}  // namespace

Vec2 shown_panning(const Session& s) { return s.groupPath.empty() ? canvas_panning(s.open.patch) : s.levelPanning; }

void group_selection(Session& s) {
  const std::vector<std::string> members = groupable(s);
  const std::string why = s.selections.empty() ? "nothing selected"
                          : members.empty()      ? "nothing groupable selected"
                                                 : group_refusal(s.open.patch, members);
  if (!why.empty()) {
    s.status = "Group refused: " + why;
    return;
  }
  const std::string name = fresh_group_name(s.open.patch);
  const Vec2 at = centroid(s.open.patch, members);
  const std::string parent = open_one(s);
  if (!step(s, "", [&](Patch& p) { return add_group(p, name, members, at, parent); })) return;
  after_step(s);
  select_node(s, name);
  s.status = "Grouped as '" + name + "' — double-click to open, rename in Properties";
}

void add_to_group(Session& s, std::string_view group) {
  const std::vector<std::string> moving = targets(s);
  if (moving.empty()) {
    s.status = "Add refused: nothing groupable selected";
    return;
  }
  std::vector<std::string> trial;
  for (const auto& g : s.open.patch.layout.groups)
    if (g.name == group) trial = g.members;
  for (const auto& m : moving)
    if (std::find(trial.begin(), trial.end(), m) == trial.end()) trial.push_back(m);
  const std::vector<std::string> outs = group_outputs(s.open.patch, trial);
  if (outs.size() > 1) {
    std::string words = "Add refused: '" + std::string(group) + "' would have " + std::to_string(outs.size()) + " outputs:";
    for (const auto& o : outs) words += " " + o;
    s.status = words;
    return;
  }
  if (!step(s, "", [&](Patch& p) {
        for (const auto& m : moving) (void)regroup(p, m, group);
        return Change::Layout;
      }))
    return;
  after_step(s);
  s.status = std::to_string(moving.size()) + " node(s) -> '" + std::string(group) + "'";
}

void remove_from_group(Session& s) {
  const std::vector<std::string> moving = targets(s);
  if (!step(s, "", [&](Patch& p) {
        for (const auto& m : moving) {
          const std::string holder = group_holding(p, m);
          if (!holder.empty()) (void)regroup(p, m, group_holding(p, holder));
        }
        return Change::Layout;
      }))
    return;
  after_step(s);
  s.status = "removed from group";
}

void ungroup(Session& s, std::string_view group) {
  if (step(s, "", [&](Patch& p) { return remove_group(p, group); })) after_step(s);
}

void open_group(Session& s, std::string_view group) {
  if (!has_group(s.open.patch, group) || group_holding(s.open.patch, group) != open_one(s)) {
    s.status = "'" + std::string(group) + "' is not a group shown here";
    return;
  }
  std::vector<std::string> to = s.groupPath;
  to.emplace_back(group);
  (void)level::set_path(s, std::move(to));
}

void go_up(Session& s, std::size_t depth) {
  if (depth >= s.groupPath.size()) return;
  std::vector<std::string> to(s.groupPath.begin(), s.groupPath.begin() + std::ptrdiff_t(depth));
  if (level::set_path(s, std::move(to))) sound_follows(s);
}

void go_to(Session& s, std::string_view thing, std::optional<Vec2> canvasSize) {
  const Patch& p = s.open.patch;
  if (!has_node(p, thing) && !has_face(p, thing) && !has_group(p, thing)) {
    s.status = "'" + std::string(thing) + "' names nothing to go to";
    return;
  }
  std::string shown(thing);
  for (const auto& f : p.layout.faces)
    for (const auto& [field, id] : f.fields)
      if (id == thing) shown = f.label;
  if (level::set_path(s, path_to(p, shown))) sound_follows(s);
  select_node(s, shown);
  const auto at = level::place_of(s.open.patch, shown);
  if (!canvasSize || !at) return;
  const Vec2 centred{canvasSize->x * 0.5f - at->x, canvasSize->y * 0.5f - at->y};
  pan_canvas(s, centred);
}

void listen_at_group(Session& s, std::string_view group) {
  const std::string out = output_node(s.open.patch, group);
  if (out.empty()) {
    s.status = "group '" + std::string(group) + "' has no one output to listen to";
    return;
  }
  listen_at(s, out);
}

}  // namespace mforce::ui

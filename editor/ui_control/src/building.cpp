#include "mforce/ui_control/building.h"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_model/groups.h"
#include "copying.h"
#include "level.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

// The editor's rules for a name a person types, in its words; empty when
// `to` passes them. Its own name again passes. A group's words differ: a
// node or a face holding the name is "in use by a node".
std::string name_refused(const Patch& p, std::string_view from, const std::string& to) {
  const bool group = has_group(p, from);
  if (to.empty()) return "name is empty";
  if (to.find('.') != std::string::npos) return group ? "'.' not allowed" : "'.' not allowed (a pin is named node.pin)";
  if (to.rfind("__", 0) == 0) return "'__' prefix is reserved";
  if (to == from) return {};
  if (group && (has_node(p, to) || has_face(p, to))) return "name already in use by a node: " + to;
  if (group) return has_group(p, to) ? "name already in use: " + to : std::string();
  if (has_node(p, to) || has_face(p, to)) return "name already in use: " + to;
  if (has_group(p, to)) return "name already in use by a group: " + to;
  return {};
}

// What a group holds, through the groups it holds: its nodes (no face's
// node), its faces and its groups, itself included.
void things_under(const Patch& p, const std::string& group, std::vector<std::string>& nodes,
                  std::vector<std::string>& faces, std::vector<std::string>& groups) {
  for (const auto& g : p.layout.groups) {
    if (g.name != group || std::find(groups.begin(), groups.end(), group) != groups.end()) continue;
    groups.push_back(group);
    for (const auto& m : g.members) {
      if (has_group(p, m)) things_under(p, m, nodes, faces, groups);
      else if (has_face(p, m)) faces.push_back(m);
      else nodes.push_back(m);
    }
  }
}

// The names to delete with every group among them opened into what it
// holds: faces first, then nodes, then the groups, innermost last.
std::vector<std::string> opened_names(const Patch& p, const std::vector<std::string>& names, bool& anyGroup) {
  std::vector<std::string> nodes, faces, groups;
  for (const auto& n : names) {
    if (has_group(p, n)) {
      anyGroup = true;
      things_under(p, n, nodes, faces, groups);
    } else if (has_face(p, n)) {
      faces.push_back(n);
    } else {
      nodes.push_back(n);
    }
  }
  std::vector<std::string> all = faces;
  all.insert(all.end(), nodes.begin(), nodes.end());
  all.insert(all.end(), groups.rbegin(), groups.rend());
  return all;
}

// Every (node, setting) a dynamic pin drives.
std::set<std::pair<std::string, std::string>> driven(const Patch& p) {
  std::set<std::pair<std::string, std::string>> pins;
  for (const auto& n : p.graph.nodes)
    for (const auto& [setting, source] : n.dynamicPins) pins.insert({n.id, setting});
  return pins;
}

// The wires of `wires` in the order they go: by reader, the highest feed first.
std::vector<std::pair<PinEnd, std::size_t>> highest_first(std::vector<std::pair<PinEnd, std::size_t>> wires) {
  std::stable_sort(wires.begin(), wires.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
  return wires;
}

}  // namespace

void create_node(Session& s, std::string_view type, Vec2 pos) {
  const std::string id = fresh_id(s.open.patch, type);
  const std::uint32_t seed = s.seeds();
  if (!step(s, "", [&](Patch& p) {
        const Change made = add_node(p, type, id, seed, pos);
        copying::join_open(p, s, id);
        return made;
      })) return;
  select_node(s, id);
  sound_follows(s);
}

void create_note(Session& s, Vec2 pos) {
  if (!s.open.patch.instrument) {
    s.status = "a plain sound has no note to read";
    return;
  }
  const std::string label = fresh_face_label(s.open.patch);
  if (step(s, "", [&](Patch& p) {
        const Change made = add_face(p, label, pos);
        copying::join_open(p, s, label);
        return made;
      })) select_node(s, label);
}

void delete_these(Session& s, const std::vector<std::pair<PinEnd, std::size_t>>& wires,
                  const std::vector<std::string>& nodes) {
  const auto ordered = highest_first(wires);
  const auto drivenBefore = driven(s.open.patch);
  bool anyGroup = false;
  const std::vector<std::string> names = opened_names(s.open.patch, nodes, anyGroup);
  const int drawnBefore = canvas_counts(s.open.patch).nodes;
  const auto done = step(s, "", [&](Patch& p) {
    for (const auto& [reader, feed] : ordered) (void)unwire_in(p, reader, feed);
    for (const auto& name : names) {
      if (has_node(p, name)) (void)remove_node(p, name);
      else if (has_face(p, name)) (void)remove_face(p, name);
      else if (has_group(p, name)) (void)remove_group(p, name);
    }
    return Change::Structure;
  });
  if (!done) return;
  // A setting whose drive went, on a node still there, keeps its pin shown,
  // unwired: whether its wire was chosen or the node driving it was deleted.
  const auto drivenAfter = driven(s.open.patch);
  for (const auto& pin : drivenBefore)
    if (!drivenAfter.count(pin) && has_node(s.open.patch, pin.first)) s.offered.insert(pin);
  (void)forget_what_went(s);
  sound_follows(s);
  if (!anyGroup) return;
  const std::string went = "Deleted " + std::to_string(drawnBefore - canvas_counts(s.open.patch).nodes) + " node(s)";
  s.status = s.status.empty() ? went : went + "; " + s.status;   // and why there is no sound, when there is none
}

void rename(Session& s, std::string_view nodeOrFace, std::string to) {
  const std::string refused = name_refused(s.open.patch, nodeOrFace, to);
  if (!refused.empty()) {
    s.status = refused;
    return;
  }
  const bool face = has_face(s.open.patch, nodeOrFace);
  const bool group = has_group(s.open.patch, nodeOrFace);
  const auto done = step(s, "", [&](Patch& p) {
    if (group) return rename_group(p, nodeOrFace, to);
    return face ? rename_face(p, nodeOrFace, to) : rename_node(p, nodeOrFace, to);
  });
  if (!done) return;
  for (auto& name : s.selections)
    if (name == nodeOrFace) name = to;
  for (auto& name : s.groupPath)
    if (name == nodeOrFace) name = to;
  if (s.listening == nodeOrFace) s.listening = to;
  for (auto& id : s.take.stripIds)                // the strip, and its pop-outs, under the new name
    if (id == nodeOrFace) id = to;
  for (auto& p : s.popOuts)
    if (p.node == nodeOrFace) p.node = to;
  std::set<std::pair<std::string, std::string>> offered;
  for (const auto& [node, setting] : s.offered) offered.insert({node == nodeOrFace ? to : node, setting});
  s.offered = std::move(offered);
  if (*done == Change::Structure) sound_follows(s);
}

std::vector<std::string> onsets_from_text(std::string_view text) {
  std::vector<std::string> names;
  std::size_t start = 0;
  while (start <= text.size()) {
    const std::size_t comma = std::min(text.find(',', start), text.size());
    std::string_view name = text.substr(start, comma - start);
    const std::size_t first = name.find_first_not_of(" \t");
    if (first != std::string_view::npos) {
      name = name.substr(first, name.find_last_not_of(" \t") - first + 1);
      names.emplace_back(name);
    }
    start = comma + 1;
  }
  return names;
}

std::string onsets_text(const std::vector<std::string>& onsets) {
  std::string text;
  for (const auto& name : onsets) text += (text.empty() ? "" : ", ") + name;
  return text;
}

void edit_instrument(Session& s, const InstrumentBlock& block) {
  if (!s.open.patch.instrument) {
    s.status = "a plain sound has no instrument block: its settings cannot be edited";
    return;
  }
  if (step(s, "", [&](Patch& p) { return set_instrument(p, block); })) sound_follows(s);
}

// A pin is offered only where a feed could later be wired, and whether one
// could is check()'s to say (a structural setting, a plain sound). So the
// offer feeds the setting on a COPY of the patch from a probe: a VarSource
// added to the copy alone, a signal that reads nothing, so it can close no
// loop and is refused for nothing of its own. The feed's refusal, if any, is
// the engine's own sentence; the copy and its probe are then thrown away and
// the open patch is never touched.
void offer_setting_pin(Session& s, std::string_view node, std::string_view setting) {
  Patch trial = s.open.patch;
  Node probe;
  probe.id = fresh_id(trial, "VarSource");
  probe.type = "VarSource";
  trial.graph.nodes.push_back(probe);
  try {
    (void)feed_setting(trial, node, setting, probe.id);
  } catch (const std::exception& e) {
    s.status = e.what();
    return;
  }
  s.offered.insert({std::string(node), std::string(setting)});
}

void withdraw_setting_pin(Session& s, std::string_view node, std::string_view setting) {
  s.offered.erase({std::string(node), std::string(setting)});
  const auto fed = [&](const Patch& p) {
    for (const auto& n : p.graph.nodes)
      if (n.id == node) return n.dynamicPins.count(std::string(setting)) != 0;
    return false;
  };
  if (!fed(s.open.patch)) return;
  if (step(s, "", [&](Patch& p) { return unfeed_setting(p, node, setting); })) sound_follows(s);
}

// The end of a drag of a number is when a running plain sound hears it: not
// every tick of the drag, which would start it again sixty times a second.
void end_gesture(Session& s) {
  const bool value = s.gesture.rfind("value:", 0) == 0;
  s.gesture.clear();
  if (value) plain_follows(s);
}

void fit_canvas(Session& s, Vec2 canvasSize) {
  if (!s.groupPath.empty()) s.levelPanning = level::framed(s.open.patch, s.groupPath);
  else (void)set_panning(s.open.patch, centred_panning(s.open.patch, canvasSize));
}

void clear_recent(Session& s) { s.recent.paths.clear(); }

}  // namespace mforce::ui

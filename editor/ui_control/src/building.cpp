#include "mforce/ui_control/building.h"
#include <algorithm>
#include <cstdint>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/canvas_view.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

bool has_group(const Patch& p, std::string_view name) {
  return std::any_of(p.layout.groups.begin(), p.layout.groups.end(), [&](const Group& g) { return g.name == name; });
}

// The editor's rules for a name a person types, in its words; empty when
// `to` passes them. Its own name again passes.
std::string name_refused(const Patch& p, std::string_view from, const std::string& to) {
  if (to.empty()) return "name is empty";
  if (to.find('.') != std::string::npos) return "'.' not allowed (a pin is named node.pin)";
  if (to.rfind("__", 0) == 0) return "'__' prefix is reserved";
  if (to == from) return {};
  if (has_node(p, to) || has_face(p, to)) return "name already in use: " + to;
  if (has_group(p, to)) return "name already in use by a group: " + to;
  return {};
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
  if (!step(s, "", [&](Patch& p) { return add_node(p, type, id, seed, pos); })) return;
  s.selected = id;
  sound_follows(s);
}

void create_note(Session& s, Vec2 pos) {
  if (!s.open.patch.instrument) {
    s.status = "a plain sound has no note to read";
    return;
  }
  const std::string label = fresh_face_label(s.open.patch);
  if (step(s, "", [&](Patch& p) { return add_face(p, label, pos); })) s.selected = label;
}

void delete_these(Session& s, const std::vector<std::pair<PinEnd, std::size_t>>& wires,
                  const std::vector<std::string>& nodes) {
  const auto ordered = highest_first(wires);
  const auto done = step(s, "", [&](Patch& p) {
    for (const auto& [reader, feed] : ordered) (void)unwire_in(p, reader, feed);
    for (const auto& name : nodes) {
      if (has_node(p, name)) (void)remove_node(p, name);
      else if (has_face(p, name)) (void)remove_face(p, name);
    }
    return Change::Structure;
  });
  if (!done) return;
  for (const auto& [reader, feed] : ordered)
    if (reader.kind == PinEnd::Kind::SettingPin) s.offered.insert({reader.node, reader.name});
  forget_what_went(s);
  sound_follows(s);
}

void rename(Session& s, std::string_view nodeOrFace, std::string to) {
  const std::string refused = name_refused(s.open.patch, nodeOrFace, to);
  if (!refused.empty()) {
    s.status = refused;
    return;
  }
  const bool face = has_face(s.open.patch, nodeOrFace);
  const auto done = step(s, "", [&](Patch& p) {
    return face ? rename_face(p, nodeOrFace, to) : rename_node(p, nodeOrFace, to);
  });
  if (!done) return;
  if (s.selected == nodeOrFace) s.selected = to;
  std::set<std::pair<std::string, std::string>> offered;
  for (const auto& [node, setting] : s.offered) offered.insert({node == nodeOrFace ? to : node, setting});
  s.offered = std::move(offered);
  if (*done == Change::Structure) sound_follows(s);
}

void edit_instrument(Session& s, const InstrumentBlock& block) {
  if (!s.open.patch.instrument) {
    s.status = "a plain sound has no instrument block: its settings cannot be edited";
    return;
  }
  if (step(s, "", [&](Patch& p) { return set_instrument(p, block); })) sound_follows(s);
}

// The offer is tried on a copy of the patch with a signal that reads nothing
// driving the setting, so it is refused exactly as feeding it would be.
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

void end_gesture(Session& s) { s.gesture.clear(); }

void fit_canvas(Session& s) { (void)set_panning(s.open.patch, fit_panning(s.open.patch)); }

void clear_recent(Session& s) { s.recent.paths.clear(); }

}  // namespace mforce::ui

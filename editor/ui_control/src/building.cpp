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
  const auto drivenBefore = driven(s.open.patch);
  const auto done = step(s, "", [&](Patch& p) {
    for (const auto& [reader, feed] : ordered) (void)unwire_in(p, reader, feed);
    for (const auto& name : nodes) {
      if (has_node(p, name)) (void)remove_node(p, name);
      else if (has_face(p, name)) (void)remove_face(p, name);
    }
    return Change::Structure;
  });
  if (!done) return;
  // A setting whose drive went, on a node still there, keeps its pin shown,
  // unwired: whether its wire was chosen or the node driving it was deleted.
  const auto drivenAfter = driven(s.open.patch);
  for (const auto& pin : drivenBefore)
    if (!drivenAfter.count(pin) && has_node(s.open.patch, pin.first)) s.offered.insert(pin);
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
  (void)set_panning(s.open.patch, centred_panning(s.open.patch, canvasSize));
}

void clear_recent(Session& s) { s.recent.paths.clear(); }

}  // namespace mforce::ui

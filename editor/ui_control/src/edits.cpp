#include "mforce/ui_control/edits.h"
#include <exception>
#include <map>
#include <optional>
#include <utility>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/placement.h"

namespace mforce::ui {

namespace {

// The first node with id `id`, as the edits read it; null when none has it.
const Node* node_named(const Patch& p, std::string_view id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

// What `map` holds under `name`, when it holds anything.
template <class T>
std::optional<T> held(const std::map<std::string, T>& map, std::string_view name) {
  auto it = map.find(std::string(name));
  return it == map.end() ? std::nullopt : std::optional<T>(it->second);
}

// Runs the engine's edit; a refusal's sentence goes to status. A template
// function takes any callable, as a C# method takes an Action.
template <class Edit>
bool applied(Session& s, Edit edit) {
  try {
    edit();
    return true;
  } catch (const std::exception& e) {
    s.status = e.what();
    return false;
  }
}

void tell(Session& s, const std::string& trouble) {
  if (!trouble.empty()) s.status = trouble;
}

// An edit applied that changed something: the patch is unsaved, and the
// status line no longer shows what an earlier edit was refused for.
void changed(Session& s) {
  s.open.dirty = true;
  s.status.clear();
}

}  // namespace

void edit_value(Session& s, std::string_view node, std::string_view pin, float v) {
  const Node* n = node_named(s.open.patch, node);
  const bool same = n && held(n->values, pin) == v;
  if (!applied(s, [&] { set_value(s.open.patch, node, pin, v); }) || same) return;
  changed(s);
  tell(s, s.live.value_changed(s.open.patch, node, pin, v));
}

void edit_setting(Session& s, std::string_view node, std::string_view name, float v) {
  const Node* n = node_named(s.open.patch, node);
  const bool same = n && held(n->settings, name) == v;
  if (!applied(s, [&] { set_setting(s.open.patch, node, name, v); }) || same) return;
  changed(s);
  tell(s, s.live.rebuild(s.open.patch));
}

void edit_string(Session& s, std::string_view node, std::string_view name, std::string v) {
  const Node* n = node_named(s.open.patch, node);
  const bool same = n && held(n->strings, name) == v;
  if (!applied(s, [&] { set_string(s.open.patch, node, name, std::move(v)); }) || same) return;
  changed(s);
  tell(s, s.live.rebuild(s.open.patch));
}

void edit_position(Session& s, std::string_view node, Vec2 pos) {
  const bool same = held(s.open.patch.layout.positions, node) == pos;
  if (!applied(s, [&] { move_node(s.open.patch, node, pos); }) || same) return;
  changed(s);
}

void edit_output_position(Session& s, Vec2 pos) {
  const bool same = s.open.patch.layout.outputPos == pos;
  if (!applied(s, [&] { move_output(s.open.patch, pos); }) || same) return;
  changed(s);
}

void pan_canvas(Session& s, Vec2 pos) {
  (void)applied(s, [&] { set_panning(s.open.patch, pos); });
}

void select_node(Session& s, std::string_view node) { s.selected = std::string(node); }

void arrange(Session& s) {
  Patch arranged = s.open.patch;
  (void)arrange_nodes(arranged);
  if (arranged == s.open.patch) return;
  s.open.patch = std::move(arranged);
  changed(s);
}

}  // namespace mforce::ui

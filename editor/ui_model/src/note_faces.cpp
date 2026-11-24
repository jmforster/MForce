#include "mforce/ui_model/note_faces.h"
#include <optional>
#include <set>
#include <string>
#include <vector>
#include "mforce/patch/edit.h"
#include "mforce/patch/type_declarations.h"

namespace mforce::ui {

namespace {

// The field a PerformNode reads, as its `field` setting's label; empty when
// its setting names none.
std::string field_of(const Node& n) {
  const TypeDeclarations* d = type_declarations(n.type);
  const auto* field = d ? d->setting("field") : nullptr;
  if (!field) return {};
  const auto held = n.settings.find("field");
  const int index = int(held == n.settings.end() ? field->defaultValue : held->second);
  return index >= 0 && index < int(field->labels.size()) ? field->labels[size_t(index)] : std::string();
}

// The index of the first face whose slot for `field` is free, or -1.
int free_slot(const Layout& l, const std::string& field) {
  for (size_t f = 0; f < l.faces.size(); ++f)
    if (!l.faces[f].fields.count(field)) return int(f);
  return -1;
}

// True when a feed, a dynamic pin or the output reads `id`.
bool is_read(const Patch& p, const std::string& id) {
  if (p.graph.output == id) return true;
  for (const auto& n : p.graph.nodes) {
    for (const auto& [pin, feeds] : n.wires)
      for (const auto& w : feeds)
        if (w.source == id) return true;
    for (const auto& [setting, source] : n.dynamicPins)
      if (source == id) return true;
  }
  return false;
}

}  // namespace

int drop_unread_face_nodes(Patch& patch) {
  std::vector<std::string> unread;
  for (const auto& f : patch.layout.faces)
    for (const auto& [field, id] : f.fields)
      if (!is_read(patch, id)) unread.push_back(id);
  int dropped = 0;
  for (const auto& id : unread) {
    (void)remove_node(patch, id);
    ++dropped;
  }
  return dropped;
}

int gather_note_faces(Patch& patch) {
  std::set<std::string> named;
  for (const auto& f : patch.layout.faces)
    for (const auto& [field, id] : f.fields) named.insert(id);
  int made = 0;
  for (const Node& n : patch.graph.nodes) {
    if (n.type != "PerformNode" || named.count(n.id)) continue;
    const std::string field = field_of(n);
    if (field.empty()) continue;
    Layout& l = patch.layout;
    int home = free_slot(l, field);
    if (home < 0) {
      const auto at = l.positions.find(n.id);
      const std::optional<Vec2> pos = at == l.positions.end() ? std::nullopt : std::optional<Vec2>(at->second);
      l.faces.push_back(NoteFace{fresh_face_label(patch), {}, pos});
      home = int(l.faces.size()) - 1;
      ++made;
    }
    l.faces[size_t(home)].fields[field] = n.id;
    l.positions.erase(n.id);
    named.insert(n.id);
  }
  return made;
}

}  // namespace mforce::ui

// The edits of note faces: a face, where it sits, its label, and the
// PerformNode it names for each field of the note.
#include "mforce/patch/edit.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>
#include "mforce/patch/type_declarations.h"
#include "mforce/source/modulator/perform_node.h"
#include "edit_support.h"
#include "node_complete.h"
#include "sentence.h"

namespace mforce {

using namespace edit_detail;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

// The face labelled `label`; refused, ending with `after`, when none is.
NoteFace& face_named(Patch& p, std::string_view label, const std::string& after) {
  for (auto& f : p.layout.faces)
    if (f.label == label) return f;
  throw std::runtime_error(in_quotes(label) + " names no face: " + after);
}

std::string face_opening(std::string_view label) { return "face " + in_quotes(label) + ": "; }

// The note's fields, as PerformNode labels its `field` setting.
std::vector<std::string> field_labels() {
  std::vector<std::string> labels;
  for (int k = 0; PerformNode::kFieldLabels[k]; ++k) labels.emplace_back(PerformNode::kFieldLabels[k]);
  return labels;
}

// The index of `field` among the note's fields; refused in the reader's
// words for a label the setting does not have.
int field_index(std::string_view label, std::string_view field) {
  const std::vector<std::string> labels = field_labels();
  const auto it = std::find(labels.begin(), labels.end(), field);
  if (it == labels.end())
    throw std::runtime_error(face_opening(label) + "'field' holds \"" + std::string(field) +
                             "\", which is not one of its labels (" + comma_list(labels) + ") or an index into them");
  return int(it - labels.begin());
}

}  // namespace

Change add_face(Patch& p, std::string_view label, Vec2 pos) {
  require_finite(pos, face_opening(label) + "its position");
  Patch copy = p;
  copy.layout.faces.push_back(NoteFace{std::string(label), {}, pos});
  return adopt(p, copy, Change::Layout);
}

Change move_face(Patch& p, std::string_view label, Vec2 pos) {
  Patch copy = p;
  NoteFace& face = face_named(copy, label, "there is nothing to move");
  require_finite(pos, face_opening(label) + "its position");
  face.pos = pos;
  return adopt(p, copy, Change::Layout);
}

Change rename_face(Patch& p, std::string_view label, std::string_view to) {
  Patch copy = p;
  NoteFace& face = face_named(copy, label, "there is nothing to rename");
  if (face.label == to) return Change::Layout;
  const std::string from = face.label;
  face.label = std::string(to);
  for (auto& g : copy.layout.groups)
    for (auto& m : g.members)
      if (m == from) m = std::string(to);
  return adopt(p, copy, Change::Layout);
}

Change remove_face(Patch& p, std::string_view label) {
  Patch copy = p;
  const NoteFace face = face_named(copy, label, "there is nothing to remove");
  std::erase_if(copy.layout.faces, [&](const NoteFace& f) { return f.label == label; });
  for (auto& g : copy.layout.groups) std::erase(g.members, face.label);
  std::vector<std::string> wereRead;
  for (const auto& [field, id] : face.fields)
    if (const auto index = index_of(copy, id)) {
      const std::vector<std::string> read = detach_node(copy, *index);
      wereRead.insert(wereRead.end(), read.begin(), read.end());
    }
  drop_unread_face_nodes(copy, wereRead);
  return adopt(p, copy, Change::Structure);
}

std::string face_field(Patch& p, std::string_view label, std::string_view field) {
  Patch copy = p;
  NoteFace& face = face_named(copy, label, "it has no field " + in_quotes(field));
  const int index = field_index(label, field);
  const std::string key(field);
  if (auto named = face.fields.find(key); named != face.fields.end()) return named->second;
  Node n;
  n.id = free_name(copy, "__perf_" + key, true);
  n.type = "PerformNode";
  n.settings["field"] = float(index);
  if (const TypeDeclarations* d = type_declarations(n.type)) complete(n, *d);
  face.fields[key] = n.id;
  const std::string id = n.id;
  copy.graph.nodes.push_back(std::move(n));
  (void)adopt(p, copy, Change::Structure);
  return id;
}

} // namespace mforce

// The properties panel's frame: the chooser, and what is selected (a node,
// a face, or nothing); and the two fields every part of it types into.
#include "mforce/ui_views/properties_panel.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <imgui.h>
#include <imgui_stdlib.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/selecting.h"

namespace mforce::ui {

namespace {

const Node* node_named(const Patch& p, const std::string& id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

const NoteFace* face_named(const Patch& p, const std::string& label) {
  for (const auto& f : p.layout.faces)
    if (f.label == label) return &f;
  return nullptr;
}

std::set<std::string> face_nodes(const Patch& p) {
  std::set<std::string> ids;
  for (const auto& f : p.layout.faces)
    for (const auto& [field, id] : f.fields) ids.insert(id);
  return ids;
}

// A drag's speed: a two-hundredth of the range, or of the number's size.
float speed_for(float lo, float hi, float v) {
  if (lo < hi) return (hi - lo) / 200.0f;
  return std::max(0.001f, std::fabs(v) / 100.0f);
}

}  // namespace

bool PropertiesPanel::text_field(const char* label, const std::string& key, const std::string& now, std::string& out) {
  std::string text = typedKey_ == key ? typed_ : now;
  ImGui::InputText(label, &text);                  // ImGui's std::string field: any length
  if (ImGui::IsItemActive()) {
    typedKey_ = key;
    typed_ = text;
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    typedKey_.clear();
    out = text;
    return true;
  }
  if (!ImGui::IsItemActive() && typedKey_ == key) typedKey_.clear();
  return false;
}

bool PropertiesPanel::number_field(const char* label, const std::string& key, float now, float lo, float hi,
                                   bool isInt, float& out) {
  float v = draggedKey_ == key ? dragged_ : now;
  if (isInt) {
    int i = int(std::lround(v));
    ImGui::DragInt(label, &i, std::max(0.05f, (hi - lo) / 200.0f), int(lo), int(hi));
    v = float(i);
  } else {
    ImGui::DragFloat(label, &v, speed_for(lo, hi, v), lo, hi, "%.4g");
  }
  if (ImGui::IsItemActive()) {
    draggedKey_ = key;
    dragged_ = v;
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    draggedKey_.clear();
    out = v;
    return true;
  }
  if (!ImGui::IsItemActive() && draggedKey_ == key) draggedKey_.clear();
  return false;
}

// What the canvas draws, by name, in its order: the nodes no face names,
// then the faces. A thing another hides on the canvas can be chosen here.
void PropertiesPanel::choose_node(Session& s) {
  const Patch& p = s.open.patch;
  const Node* n = node_named(p, selected_one(s));
  const NoteFace* f = face_named(p, selected_one(s));
  const std::string shown = n ? n->id + "  (" + n->type + ")" : f ? f->label + "  (Note)" : std::string("(nothing)");
  if (!ImGui::BeginCombo("node", shown.c_str())) return;
  if (ImGui::Selectable("(nothing)", selected_one(s).empty())) select_node(s, "");
  const std::set<std::string> named = face_nodes(p);
  for (const auto& node : p.graph.nodes)
    if (!named.count(node.id) && ImGui::Selectable((node.id + "  (" + node.type + ")").c_str(), node.id == selected_one(s)))
      select_node(s, node.id);
  for (const auto& face : p.layout.faces)
    if (ImGui::Selectable((face.label + "  (Note)").c_str(), face.label == selected_one(s))) select_node(s, face.label);
  ImGui::EndCombo();
}

// The name of the node or face selected: typed, then renamed by the editor's rules.
void PropertiesPanel::name_field(Session& s, const std::string& name) {
  std::string to;
  if (text_field("name", "name/" + name, name, to) && to != name) rename(s, name, to);
}

// A face: its name, and each of the note's fields with the node that reads it.
void PropertiesPanel::face_panel(Session& s, const NoteFace& face) {
  const std::string label = face.label;            // the face may be renamed below
  name_field(s, label);
  ImGui::TextDisabled("a note: each field an output to wire");
  ImGui::Separator();
  const TypeDeclarations* d = type_declarations("PerformNode");
  const auto* field = d ? d->setting("field") : nullptr;
  if (!field) return;
  for (const auto& name : field->labels) {
    const auto read = face.fields.find(name);
    if (read == face.fields.end()) ImGui::TextDisabled("%s", name.c_str());
    else ImGui::Text("%s  -> read (%s)", name.c_str(), read->second.c_str());
  }
}

void PropertiesPanel::draw(Session& s) {
  if (seenOpened_ != s.opened) {                    // another patch: nothing in flight belongs to it
    draggedKey_.clear();
    typedKey_.clear();
    seenOpened_ = s.opened;
  }
  if (!s.open.patch.graph.nodes.empty() || !s.open.patch.layout.faces.empty()) choose_node(s);
  // An edit below may change the patch; each part copies what it names first.
  if (const Node* n = node_named(s.open.patch, selected_one(s))) {
    const Node node = *n;
    node_panel(s, node);
  } else if (const NoteFace* f = face_named(s.open.patch, selected_one(s))) {
    const NoteFace face = *f;
    face_panel(s, face);
  } else if (s.open.patch.instrument) {
    const InstrumentBlock block = *s.open.patch.instrument;
    instrument_fields(s, block);
  } else {
    patch_facts(s);
  }
}

}  // namespace mforce::ui

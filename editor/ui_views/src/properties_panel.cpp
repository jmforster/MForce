// The properties panel's frame: the chooser, and what is selected (a node,
// a face, or nothing); and the two fields every part of it types into.
#include "mforce/ui_views/properties_panel.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <set>
#include <imgui.h>
#include <imgui_stdlib.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/grouping.h"
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

constexpr ImVec4 kGroupName(0.75f, 0.6f, 0.9f, 1.0f);   // a group's name: lavender
constexpr ImVec4 kGroupWord(0.5f, 0.5f, 0.5f, 1.0f);
constexpr ImVec4 kRefusal(0.9f, 0.4f, 0.4f, 1.0f);
constexpr ImVec4 kMembersWord(0.7f, 0.7f, 0.7f, 1.0f);
constexpr float kGroupFieldWidth = 180.0f;
constexpr std::size_t kGroupNameLimit = 63;        // a name of at most 63 characters

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
  const std::string& one = selected_one(s);
  const Node* n = node_named(p, one);
  const NoteFace* f = face_named(p, one);
  const std::string shown = n ? n->id + "  (" + n->type + ")" : f ? f->label + "  (Note)" : !one.empty() ? one + "  (Group)"
                                                                                                          : std::string("(nothing)");
  const bool open = ImGui::BeginCombo("node", shown.c_str());
  places_.note_item("list:node");
  if (!open) return;
  std::string chosen;
  bool none = ImGui::Selectable("(nothing)", one.empty());
  const std::set<std::string> named = face_nodes(p);
  for (const auto& node : p.graph.nodes)
    if (!named.count(node.id)) {
      if (ImGui::Selectable((node.id + "  (" + node.type + ")").c_str(), node.id == one)) chosen = node.id;
      places_.note_item("list:" + node.id);
    }
  for (const auto& face : p.layout.faces) {
    if (ImGui::Selectable((face.label + "  (Note)").c_str(), face.label == one)) chosen = face.label;
    places_.note_item("list:" + face.label);
  }
  for (const auto& group : p.layout.groups) {
    if (ImGui::Selectable((group.name + "  (Group)").c_str(), group.name == one)) chosen = group.name;
    places_.note_item("list:" + group.name);
  }
  ImGui::EndCombo();
  if (none) select_node(s, "");
  else if (!chosen.empty()) go_to(s, chosen, std::nullopt);
}

// A group: its name, a field to rename it (on Enter, the refusal under it),
// and its members.
void PropertiesPanel::group_panel(Session& s, const Group& group) {
  ImGui::TextColored(kGroupName, "%s", group.name.c_str());
  ImGui::SameLine();
  ImGui::TextColored(kGroupWord, "(Group)");
  if (groupTyped_ != group.name) {
    groupTyped_ = group.name;
    groupText_ = group.name;
    groupRefused_.clear();
  }
  ImGui::SetNextItemWidth(kGroupFieldWidth);
  const bool entered = ImGui::InputText("##groupName", &groupText_, ImGuiInputTextFlags_EnterReturnsTrue);
  places_.note_item("field:group name");
  if (entered) {
    rename(s, group.name, groupText_.substr(0, kGroupNameLimit));
    const bool kept = std::any_of(s.open.patch.layout.groups.begin(), s.open.patch.layout.groups.end(),
                                  [&](const Group& g) { return g.name == group.name; });
    groupRefused_ = kept && groupText_ != group.name ? s.status : std::string();
  }
  if (!groupRefused_.empty()) ImGui::TextColored(kRefusal, "%s", groupRefused_.c_str());
  ImGui::Separator();
  ImGui::TextColored(kMembersWord, "Members");
  for (const auto& m : group.members) {
    const bool nested = std::any_of(s.open.patch.layout.groups.begin(), s.open.patch.layout.groups.end(),
                                    [&](const Group& g) { return g.name == m; });
    ImGui::BulletText("%s%s", m.c_str(), nested ? "  (group)" : "");
    places_.note_item("member:" + m);
  }
}

// The name of the node or face selected: typed, then renamed by the editor's rules.
void PropertiesPanel::name_field(Session& s, const std::string& name) {
  std::string to;
  const bool applied = text_field("name", "name/" + name, name, to);
  places_.note_item("field:name");
  if (applied && to != name) rename(s, name, to);
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
  places_.clear();
  if (seenOpened_ != s.opened) {                    // another patch: nothing in flight belongs to it
    draggedKey_.clear();
    typedKey_.clear();
    seenOpened_ = s.opened;
  }
  if (!s.open.patch.graph.nodes.empty() || !s.open.patch.layout.faces.empty()) choose_node(s);
  // An edit below may change the patch; each part copies what it names first.
  if (s.selections.size() > 1) {
    ImGui::Text("%d selected", int(s.selections.size()));
    return;
  }
  for (const auto& g : s.open.patch.layout.groups)
    if (g.name == selected_one(s)) {
      const Group group = g;
      group_panel(s, group);
      return;
    }
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

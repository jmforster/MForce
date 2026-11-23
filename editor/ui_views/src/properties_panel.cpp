#include "mforce/ui_views/properties_panel.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>
#include <imgui_stdlib.h>
#include "mforce/ui_control/edits.h"

namespace mforce::ui {

namespace {

const Node* selected_node(const Session& s) {
  for (const auto& n : s.open.patch.graph.nodes)
    if (n.id == s.selected) return &n;
  return nullptr;
}

// A drag's speed: a two-hundredth of the range, or of the number's size.
float speed_for(float lo, float hi, float v) {
  if (lo < hi) return (hi - lo) / 200.0f;
  return std::max(0.001f, std::fabs(v) / 100.0f);
}

std::string feeds_text(const std::vector<Wire>& feeds) {
  std::string text;
  for (const auto& w : feeds) text += (text.empty() ? "" : ", ") + w.source + (w.tap ? " (tap)" : "");
  return text;
}

void heading(const Node& n) {
  ImGui::TextUnformatted(n.id.c_str());
  ImGui::TextDisabled("%s", n.type.c_str());
  if (!n.comment.empty()) ImGui::TextWrapped("%s", n.comment.c_str());
  ImGui::Separator();
}

}  // namespace

void PropertiesPanel::patch_facts(const Session& s) {
  const Patch& p = s.open.patch;
  if (s.open.path.empty() && p.graph.nodes.empty()) {
    ImGui::TextUnformatted("No patch is open: File > Open (Ctrl+O).");
    return;
  }
  ImGui::TextUnformatted(s.open.path.empty() ? "(no file)" : s.open.path.c_str());
  ImGui::Text("rate %d Hz, %zu nodes", p.sampleRate, p.graph.nodes.size());
  if (!p.instrument) {
    ImGui::TextUnformatted("a plain sound");
    return;
  }
  const InstrumentBlock& b = *p.instrument;
  ImGui::TextUnformatted("an instrument");
  ImGui::Text("polyphony %d, volume %g", b.polyphony, double(b.volume));
  ImGui::Text("%s, glide %g ms", b.sustaining ? "sustaining" : "struck", double(b.glideMs));
  ImGui::Text("%zu onsets", b.onsets.size());
}

void PropertiesPanel::pin_row(Session& s, const Node& n, const std::string& name, float lo, float hi, bool isParam) {
  if (auto fed = n.wires.find(name); fed != n.wires.end() && !fed->second.empty()) {
    ImGui::Text("%s  <- %s", name.c_str(), feeds_text(fed->second).c_str());
    return;
  }
  const auto held = n.values.find(name);
  if (held == n.values.end() && !isParam) {
    ImGui::TextDisabled("%s  (nothing)", name.c_str());
    return;
  }
  const TypeDeclarations* d = type_declarations(n.type);
  const auto* p = d ? d->param(name) : nullptr;
  float v = held != n.values.end() ? held->second : (p ? p->defaultValue : 0.0f);
  if (ImGui::DragFloat(name.c_str(), &v, speed_for(lo, hi, v), lo < hi ? lo : 0.0f, lo < hi ? hi : 0.0f, "%.4g"))
    edit_value(s, n.id, name, v);
}

void PropertiesPanel::pins(Session& s, const Node& n, const TypeDeclarations& d) {
  for (const auto& p : d.params) pin_row(s, n, p.name, p.min, p.max, true);
  for (const auto& in : d.inputs) {
    if (!in.slot) {
      pin_row(s, n, in.name, 0.0f, 0.0f, false);
      continue;
    }
    const auto fed = n.wires.find(in.name);
    ImGui::Text("%s  <- %s", in.name.c_str(), fed == n.wires.end() ? "(no part)" : feeds_text(fed->second).c_str());
  }
}

// A float or an int: the dragged number is drawn until the drag ends or the
// typed number is entered, then applied once.
void PropertiesPanel::number_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st, float v) {
  const std::string key = n.id + "/" + st.name;
  if (draggedKey_ == key) v = dragged_;
  const float lo = st.min < st.max ? st.min : 0.0f, hi = st.min < st.max ? st.max : 0.0f;
  if (st.type == SettingType::Int) {
    int i = int(std::lround(v));
    ImGui::DragInt(st.name.c_str(), &i, std::max(0.05f, (hi - lo) / 200.0f), int(lo), int(hi));
    v = float(i);
  } else {
    ImGui::DragFloat(st.name.c_str(), &v, speed_for(lo, hi, v), lo, hi, "%.4g");
  }
  if (ImGui::IsItemActive()) {
    draggedKey_ = key;
    dragged_ = v;
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    draggedKey_.clear();
    edit_setting(s, n.id, st.name, v);
  } else if (!ImGui::IsItemActive() && draggedKey_ == key) {
    draggedKey_.clear();
  }
}

void PropertiesPanel::settings(Session& s, const Node& n, const TypeDeclarations& d) {
  for (const auto& st : d.settings) {
    if (st.type == SettingType::String) {
      string_setting(s, n, st);
      continue;
    }
    const auto held = n.settings.find(st.name);
    const float v = held == n.settings.end() ? st.defaultValue : held->second;
    if (st.type == SettingType::Bool) {
      bool on = v != 0.0f;
      if (ImGui::Checkbox(st.name.c_str(), &on)) edit_setting(s, n.id, st.name, on ? 1.0f : 0.0f);
    } else if (!st.labels.empty()) {
      const int index = int(v);
      const char* shown = index >= 0 && index < int(st.labels.size()) ? st.labels[size_t(index)].c_str() : "?";
      if (ImGui::BeginCombo(st.name.c_str(), shown)) {
        for (size_t k = 0; k < st.labels.size(); ++k)
          if (ImGui::Selectable(st.labels[k].c_str(), int(k) == index)) edit_setting(s, n.id, st.name, float(k));
        ImGui::EndCombo();
      }
    } else {
      number_setting(s, n, st, v);
    }
    if (auto pin = n.dynamicPins.find(st.name); pin != n.dynamicPins.end())
      ImGui::TextDisabled("    set per note by %s", pin->second.c_str());
  }
}

void PropertiesPanel::string_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st) {
  const std::string key = n.id + "/" + st.name;
  std::string text = typed_;
  if (typedKey_ != key) {
    const auto held = n.strings.find(st.name);
    text = held == n.strings.end() ? st.defaultText : held->second;
  }
  ImGui::InputText(st.name.c_str(), &text);        // ImGui's std::string field: any length
  if (ImGui::IsItemActive()) {
    typedKey_ = key;
    typed_ = text;
  }
  if (ImGui::IsItemDeactivatedAfterEdit()) {
    typedKey_.clear();
    edit_string(s, n.id, st.name, text);
  } else if (!ImGui::IsItemActive() && typedKey_ == key) {
    typedKey_.clear();
  }
}

void PropertiesPanel::arrays(const Node& n, const TypeDeclarations& d) {
  for (const auto& a : d.arrays) {
    const auto held = n.arrays.find(a.name);
    ImGui::TextDisabled("%s: %zu rows", a.name.c_str(), held == n.arrays.end() ? size_t(0) : held->second.size());
  }
}

// Every node of the patch by id and type, in the graph's order: a node
// another hides on the canvas can be chosen here.
void PropertiesPanel::choose_node(Session& s) {
  const Node* current = selected_node(s);
  const std::string shown = current ? current->id + "  (" + current->type + ")" : std::string("(no node)");
  if (!ImGui::BeginCombo("node", shown.c_str())) return;
  if (ImGui::Selectable("(no node)", !current)) select_node(s, "");
  for (const auto& n : s.open.patch.graph.nodes)
    if (ImGui::Selectable((n.id + "  (" + n.type + ")").c_str(), current == &n)) select_node(s, n.id);
  ImGui::EndCombo();
}

void PropertiesPanel::draw(Session& s) {
  if (seenOpened_ != s.opened) {                    // another file: nothing in flight belongs to it
    draggedKey_.clear();
    typedKey_.clear();
    seenOpened_ = s.opened;
  }
  if (!s.open.patch.graph.nodes.empty()) choose_node(s);
  const Node* n = selected_node(s);
  if (!n) {
    patch_facts(s);
    return;
  }
  // An edit below adds to the node's maps and leaves the node where it is,
  // so the reference stays good for the rest of the frame.
  const Node& node = *n;
  heading(node);
  const TypeDeclarations* d = type_declarations(node.type);
  if (!d) {
    ImGui::TextDisabled("its type is not registered");
    return;
  }
  ImGui::PushID(node.id.c_str());
  pins(s, node, *d);
  settings(s, node, *d);
  arrays(node, *d);
  ImGui::PopID();
}

}  // namespace mforce::ui

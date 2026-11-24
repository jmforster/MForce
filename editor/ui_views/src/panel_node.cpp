// The properties panel for a node: its name, its type, its pins with their
// hints, its settings with their offer marks, its arrays' sizes.
#include <algorithm>
#include <cmath>
#include <string>
#include <imgui.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_views/properties_panel.h"
#include "canvas_look.h"

namespace mforce::ui {

namespace {

std::string feeds_text(const std::vector<Wire>& feeds) {
  std::string text;
  for (const auto& w : feeds) text += (text.empty() ? "" : ", ") + w.source + (w.tap ? " (tap)" : "");
  return text;
}

// A pin's name, then its hint in grey brackets when it has one.
void pin_name(const std::string& name, const std::string& hint) {
  ImGui::TextUnformatted(name.c_str());
  if (hint.empty()) return;
  ImGui::SameLine(0.0f, 6.0f);
  ImGui::TextDisabled("(%s)", hint.c_str());
}

}  // namespace

void PropertiesPanel::node_panel(Session& s, const Node& n) {
  name_field(s, n.id);
  ImGui::PushStyleColor(ImGuiCol_Text, canvas_look::title_colour(n.type));
  ImGui::TextUnformatted(n.type.c_str());
  ImGui::PopStyleColor();
  if (!n.comment.empty()) ImGui::TextWrapped("%s", n.comment.c_str());
  ImGui::Separator();
  const TypeDeclarations* d = type_declarations(n.type);
  if (!d) {
    ImGui::TextDisabled("its type is not registered");
    return;
  }
  ImGui::PushID(n.id.c_str());
  pins(s, n, *d);
  settings(s, n, *d);
  for (const auto& a : d->arrays) {
    const auto held = n.arrays.find(a.name);
    ImGui::TextDisabled("%s: %zu rows", a.name.c_str(), held == n.arrays.end() ? size_t(0) : held->second.size());
  }
  ImGui::PopID();
}

// A pin fed by a wire names what feeds it; one holding a number, or a Param,
// is a drag field that follows the drag, one undo step until it is let go.
void PropertiesPanel::pin_row(Session& s, const Node& n, const std::string& name, const std::string& hint, float lo,
                              float hi, bool isParam) {
  if (auto fed = n.wires.find(name); fed != n.wires.end() && !fed->second.empty()) {
    pin_name(name, hint);
    ImGui::SameLine();
    ImGui::TextDisabled("<- %s", feeds_text(fed->second).c_str());
    return;
  }
  const auto held = n.values.find(name);
  if (held == n.values.end() && !isParam) {
    pin_name(name, hint);
    ImGui::SameLine();
    ImGui::TextDisabled("(nothing)");
    return;
  }
  const TypeDeclarations* d = type_declarations(n.type);
  const auto* p = d ? d->param(name) : nullptr;
  float v = held != n.values.end() ? held->second : (p ? p->defaultValue : 0.0f);
  const float speed = lo < hi ? (hi - lo) / 200.0f : std::max(0.001f, std::fabs(v) / 100.0f);
  ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);
  if (ImGui::DragFloat(("##" + name).c_str(), &v, speed, lo < hi ? lo : 0.0f, lo < hi ? hi : 0.0f, "%.4g"))
    edit_value(s, n.id, name, v);
  if (ImGui::IsItemDeactivated()) end_gesture(s);
  ImGui::SameLine();
  pin_name(name, hint);
}

void PropertiesPanel::pins(Session& s, const Node& n, const TypeDeclarations& d) {
  for (const auto& p : d.params) pin_row(s, n, p.name, p.hint, p.min, p.max, true);
  for (const auto& in : d.inputs) {
    if (!in.slot) {
      pin_row(s, n, in.name, in.hint, 0.0f, 0.0f, false);
      continue;
    }
    const auto fed = n.wires.find(in.name);
    pin_name(in.name, in.hint);
    ImGui::SameLine();
    ImGui::TextDisabled("<- %s", fed == n.wires.end() ? "(no part)" : feeds_text(fed->second).c_str());
  }
}

// The mark beside a setting that can be driven once a note: on while its pin
// is offered on the node or fed.
void PropertiesPanel::offer_mark(Session& s, const Node& n, const TypeDeclarations::Setting& st) {
  if (st.structural) {
    ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
  } else {
    bool on = s.offered.count({n.id, st.name}) || n.dynamicPins.count(st.name);
    if (ImGui::Checkbox(("##pin" + st.name).c_str(), &on)) {
      if (on) offer_setting_pin(s, n.id, st.name);
      else withdraw_setting_pin(s, n.id, st.name);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("its pin on the node: wire a signal to set it once a note");
  }
  ImGui::SameLine();
}

void PropertiesPanel::settings(Session& s, const Node& n, const TypeDeclarations& d) {
  for (const auto& st : d.settings) {
    offer_mark(s, n, st);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.45f);   // room for the name to its right
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

// A float or an int: the dragged number is drawn until the drag ends or the
// typed number is entered, then applied once.
void PropertiesPanel::number_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st, float v) {
  const bool ranged = st.min < st.max;
  float applied = v;
  if (number_field(st.name.c_str(), n.id + "/" + st.name, v, ranged ? st.min : 0.0f, ranged ? st.max : 0.0f,
                   st.type == SettingType::Int, applied))
    edit_setting(s, n.id, st.name, applied);
}

void PropertiesPanel::string_setting(Session& s, const Node& n, const TypeDeclarations::Setting& st) {
  const auto held = n.strings.find(st.name);
  std::string text;
  if (text_field(st.name.c_str(), n.id + "/" + st.name, held == n.strings.end() ? st.defaultText : held->second, text))
    edit_string(s, n.id, st.name, text);
}

}  // namespace mforce::ui

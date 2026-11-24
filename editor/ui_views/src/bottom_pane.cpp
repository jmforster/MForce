#include "mforce/ui_views/bottom_pane.h"
#include <imgui.h>
#include "mforce/ui_control/pane.h"

namespace mforce::ui {

namespace {

constexpr float kShare = 0.28f;          // of the room under the menu bar
constexpr float kCollapsedExtra = 10.0f; // collapsed: a frame's height and this

}  // namespace

float BottomPane::height(const Session& s, float room) {
  return s.pane.collapsed ? ImGui::GetFrameHeight() + kCollapsedExtra : room * kShare;
}

void BottomPane::begin_frame() {
  places_.clear();
  waves_.begin_frame();
  spectrum_.begin_frame();
}

std::optional<Area> BottomPane::area(const std::string& name) const {
  if (auto a = places_.area(name)) return a;
  if (auto a = waves_.places().area(name)) return a;
  return spectrum_.places().area(name);
}

// The tab shown is the session's: a tab clicked sets it, and one set
// elsewhere (the command line, a test) selects its tab. Collapsed, no tab's
// content is drawn.
void BottomPane::tabs(Session& s) {
  static constexpr struct { const char* label; Pane::Tab tab; } kTabs[] = {
      {"Waveforms", Pane::Tab::Waveforms}, {"Spectrum", Pane::Tab::Spectrum}, {"Keyboard", Pane::Tab::Keyboard}};
  if (!ImGui::BeginTabBar("##pane_tabs")) return;
  for (const auto& tab : kTabs) {
    const bool shown = s.pane.tab == tab.tab;
    const bool open = ImGui::BeginTabItem(tab.label, nullptr, shown ? ImGuiTabItemFlags_SetSelected : 0);
    places_.note_item(std::string("tab:") + tab.label);
    if (ImGui::IsItemClicked() && !shown) show_pane_tab(s, tab.tab);
    if (!open) continue;
    if (!s.pane.collapsed && shown) {
      if (tab.tab == Pane::Tab::Waveforms) waves_.draw(s);
      else if (tab.tab == Pane::Tab::Spectrum) spectrum_.draw(s);
      else keyboard_.draw(s);
    }
    ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
}

// The arrow at the tab bar's right end: down while the pane is open, up
// while it is collapsed.
void BottomPane::chevron(Session& s, Vec2 barEnd) {
  ImGui::SetCursorScreenPos(ImVec2(barEnd.x - ImGui::GetFrameHeight(), barEnd.y));
  if (ImGui::ArrowButton("##paneChevron", s.pane.collapsed ? ImGuiDir_Up : ImGuiDir_Down))
    collapse_pane(s, !s.pane.collapsed);
  places_.note_item("chevron");
  if (ImGui::IsItemHovered()) ImGui::SetTooltip(s.pane.collapsed ? "Restore pane height" : "Collapse pane");
}

void BottomPane::draw(Session& s) {
  keyboard_.read_keys(s);
  const ImVec2 top = ImGui::GetCursorScreenPos();
  const Vec2 barEnd{top.x + ImGui::GetContentRegionAvail().x, top.y};
  tabs(s);
  chevron(s, barEnd);
}

}  // namespace mforce::ui

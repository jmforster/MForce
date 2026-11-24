// The pane's Waveforms tab: the toolbar and the strips in their columns.
#include "mforce/ui_views/waveforms_view.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <imgui.h>
#include "wave_strip.h"

namespace mforce::ui {

namespace {

constexpr float kGap = 4.0f;             // between columns
constexpr float kRowGap = 2.0f;          // between rows

Area item_area() {
  const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
  return Area{Vec2{lo.x, lo.y}, Vec2{hi.x, hi.y}};
}

// The columns spinner: an arrow each side of the number, sized to "4".
void columns_spinner(Session& s, Places& places) {
  ImGui::PushID("wcol");
  if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) set_columns(s.waves, s.waves.columns - 1);
  places.note_item("columns -");
  ImGui::SameLine(0, 2);
  const float w = ImGui::CalcTextSize("4").x + ImGui::GetStyle().FramePadding.x * 2 + 4;
  ImGui::SetNextItemWidth(std::max(w, 20.0f));
  int shown = s.waves.columns;
  if (ImGui::InputInt("##v", &shown, 0, 0)) set_columns(s.waves, shown);
  ImGui::SameLine(0, 2);
  if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) set_columns(s.waves, s.waves.columns + 1);
  places.note_item("columns +");
  ImGui::PopID();
}

}  // namespace

void WaveformsView::begin_frame() {
  places_.clear();
  wasOverPane_ = overPane_;
  overPane_ = false;
  wasOverPop_ = overPop_;
  overPop_.clear();
}

bool WaveformsView::over() const {
  return overPane_ || std::any_of(overPop_.begin(), overPop_.end(), [](const auto& p) { return p.second; });
}

void WaveformsView::toolbar(Session& s, float width) {
  const int total = int(s.take.mono.size());
  ImGui::TextUnformatted("Zoom");
  ImGui::SameLine();
  ImGui::PushID("wz");
  if (ImGui::ArrowButton("##zdec", ImGuiDir_Left)) zoom_in(s.waves);
  places_.note_item("zoom in");
  ImGui::SameLine(0, 2);
  ImGui::Text("%dx", s.waves.zoom);
  ImGui::SameLine(0, 2);
  if (ImGui::ArrowButton("##zinc", ImGuiDir_Right)) zoom_out(s.waves);
  places_.note_item("zoom out");
  ImGui::PopID();
  ImGui::SameLine();
  if (ImGui::SmallButton("Fit")) fit(s.waves, total, plotWidth_);
  places_.note_item("Fit");
  ImGui::SameLine();
  ImGui::Spacing();
  ImGui::SameLine();
  ImGui::TextUnformatted("Columns");
  ImGui::SameLine();
  columns_spinner(s, places_);
  ImGui::SameLine();
  ImGui::Spacing();
  ImGui::SameLine();
  bool envelopes = s.waves.envelopes;
  if (ImGui::Checkbox("Envelopes", &envelopes)) show_envelopes(s.waves, envelopes);
  places_.note_item("Envelopes");
  const int most = max_scroll(s.waves, total, plotWidth_);
  if (most <= 0) return;
  ImGui::PushItemWidth(width);
  int at = s.waves.scroll;
  if (ImGui::SliderInt("##hscroll", &at, 0, most, "")) scroll_to(s.waves, at, total, plotWidth_);
  places_.note_item("scroll");
  ImGui::PopItemWidth();
}

// The strips tiled row by row, each row as tall as shares the room, never
// less than its three lines of text: past that the strips scroll. Under them all, one invisible
// button takes a left drag; each strip's arrow, drawn over it, pops it out.
void WaveformsView::strips(Session& s) {
  const std::vector<Strip> all = strips_of(s);
  const int n = s.waves.columns, rows = std::max(1, (int(all.size()) + n - 1) / n);
  const float room = ImGui::GetContentRegionAvail().y;
  const float h = std::max(wave_strip::least_height(), (room - float(rows) * kRowGap) / float(rows));
  ImGui::BeginChild("WaveScroll", ImVec2(0, 0), ImGuiChildFlags_None,
                    ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysVerticalScrollbar);   // one width, scrolling or not
  const float width = ImGui::GetContentRegionAvail().x, colW = (width - float(n - 1) * kGap) / float(n);
  plotWidth_ = wave_strip::plot_width(colW);
  const int total = int(s.take.mono.size());
  first_fit(s.waves, total, plotWidth_);
  const ImVec2 at = ImGui::GetCursorScreenPos();
  ImGui::SetNextItemAllowOverlap();
  ImGui::InvisibleButton("##strips", ImVec2(std::max(1.0f, width), float(rows) * (h + kRowGap)));
  const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
  overPane_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  for (size_t i = 0; i < all.size(); ++i) {
    const float x = at.x + float(int(i) % n) * (colW + kGap), y = at.y + float(int(i) / n) * (h + kRowGap);
    const Area a{Vec2{x, y}, Vec2{x + colW, y + h}};
    wave_strip::draw(dl, all[i], a, s.takes, s.waves, cache_, columns_);
    places_.note("strip:" + all[i].label, Area{Vec2{x + float(kStripMargin), y}, a.hi});
    ImGui::SetCursorScreenPos(ImVec2(x + colW - 20, y + 2));
    ImGui::PushID("wpop");
    ImGui::PushID(int(i));
    if (ImGui::ArrowButton("##pop", ImGuiDir_Up)) open_pop_out(s, all[i].node);
    places_.note_item("arrow:" + all[i].label);
    ImGui::PopID();
    ImGui::PopID();
  }
  wave_strip::carry(s.waves, overPane_, dragging, wasOverPane_, total, plotWidth_);
  ImGui::EndChild();
}

void WaveformsView::draw(Session& s) {
  toolbar(s, ImGui::GetContentRegionAvail().x);
  strips(s);
}

}  // namespace mforce::ui

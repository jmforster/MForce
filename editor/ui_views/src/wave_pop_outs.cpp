// Each strip popped out: a window of its own, named by its number, with its
// own zoom, Fit and scroll, and the pane's gestures.
#include <algorithm>
#include <string>
#include <vector>
#include <imgui.h>
#include "mforce/ui_views/waveforms_view.h"
#include "wave_strip.h"

namespace mforce::ui {

namespace {

constexpr ImVec2 kFirstSize(720, 260);
constexpr float kFirstCorner = 80.0f, kCascade = 30.0f;   // where a new one first opens, each a little further on
constexpr int kCascadeSteps = 10;

// Where ImGui puts a window's close button: the title bar's right end, a
// font's size square, in from the frame's padding.
Area close_button() {
  const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float side = ImGui::GetFontSize();
  const Vec2 lo{pos.x + size.x - style.FramePadding.x - side, pos.y + style.FramePadding.y};
  return Area{lo, Vec2{lo.x + side, lo.y + side}};
}

}  // namespace

// Its toolbar, its scroll slider, then the strip in the rest, an invisible
// button under it taking a left drag.
bool WaveformsView::pop_out(Session& s, PopOut& p, int order) {
  const Strip strip = strip_of(s, p.node);
  const std::string name = "popout " + std::to_string(p.number);
  const std::string title = "Waveform — " + strip.label + "###wavepop_" + std::to_string(p.number);
  const float corner = kFirstCorner + kCascade * float(order % kCascadeSteps);
  ImGui::SetNextWindowPos(ImVec2(corner, corner), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(kFirstSize, ImGuiCond_FirstUseEver);
  bool open = true;
  if (ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar)) {
    places_.note(name + ":close", close_button());
    const int total = int(strip.samples.size());
    ImGui::TextUnformatted("Zoom");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##zdec", ImGuiDir_Left)) zoom_in(p.view);
    places_.note_item(name + ":zoom in");
    ImGui::SameLine(0, 2);
    ImGui::Text("%dx", p.view.zoom);
    ImGui::SameLine(0, 2);
    if (ImGui::ArrowButton("##zinc", ImGuiDir_Right)) zoom_out(p.view);
    places_.note_item(name + ":zoom out");
    ImGui::SameLine();
    const bool fitAsked = ImGui::SmallButton("Fit");
    places_.note_item(name + ":Fit");
    const float waveW = ImGui::GetContentRegionAvail().x;
    const int plot = wave_strip::plot_width(waveW);
    first_fit(p.view, total, plot);
    if (fitAsked) fit(p.view, total, plot);
    if (const int most = max_scroll(p.view, total, plot); most > 0) {
      ImGui::PushItemWidth(waveW);
      int at = p.view.scroll;
      if (ImGui::SliderInt("##pop_hscroll", &at, 0, most, "")) scroll_to(p.view, at, total, plot);
      ImGui::PopItemWidth();
    }
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float h = std::max(1.0f, ImGui::GetContentRegionAvail().y);
    ImGui::InvisibleButton("##strip", ImVec2(std::max(1.0f, waveW), h));
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    const Area a{Vec2{at.x, at.y}, Vec2{at.x + waveW, at.y + h}};
    wave_strip::draw(ImGui::GetWindowDrawList(), strip, a, s.takes, p.view, cache_, columns_);
    places_.note(name + ":strip", Area{Vec2{a.lo.x + float(kStripMargin), a.lo.y}, a.hi});
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    overPop_[p.number] = hovered;
    const auto was = wasOverPop_.find(p.number);
    wave_strip::carry(p.view, hovered, dragging, was != wasOverPop_.end() && was->second, total, plot);
  }
  ImGui::End();
  return open;
}

void WaveformsView::draw_pop_outs(Session& s) {
  std::vector<int> closed;
  for (size_t i = 0; i < s.popOuts.size(); ++i)
    if (!pop_out(s, s.popOuts[i], s.popOuts[i].number - 1)) closed.push_back(s.popOuts[i].number);
  for (int number : closed) close_pop_out(s, number);
}

}  // namespace mforce::ui

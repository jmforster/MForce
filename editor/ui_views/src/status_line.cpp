#include "mforce/ui_views/status_line.h"
#include <algorithm>
#include <string>
#include <imgui.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/grouping.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_model/canvas_view.h"

namespace mforce::ui {

namespace {

constexpr float kPanStep = 100.0f;                 // the pan buttons' step
constexpr float kRightPart = 330.0f;               // room for the counts and the buttons

void pan_by(Session& s, float x, float y) {
  const Vec2 p = shown_panning(s);
  pan_canvas(s, Vec2{p.x + x, p.y + y});
}

}  // namespace

void StatusLine::draw(Session& s, Vec2 canvasSize) {
  ImGui::TextUnformatted(s.status.c_str());
  const std::string trouble = sound_trouble(s);
  if (!trouble.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f), "   %s", trouble.c_str());
  }
  ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowWidth() - kRightPart));
  const CanvasCounts c = canvas_counts(s.open.patch);
  ImGui::Text("%d nodes, %d wires", c.nodes, c.wires);
  ImGui::SameLine();
  if (ImGui::SmallButton("<")) pan_by(s, kPanStep, 0.0f);
  ImGui::SameLine();
  if (ImGui::SmallButton(">")) pan_by(s, -kPanStep, 0.0f);
  ImGui::SameLine();
  if (ImGui::SmallButton("^")) pan_by(s, 0.0f, kPanStep);
  ImGui::SameLine();
  if (ImGui::SmallButton("v")) pan_by(s, 0.0f, -kPanStep);
  ImGui::SameLine();
  if (ImGui::SmallButton("Fit")) fit_canvas(s, canvasSize);
}

}  // namespace mforce::ui

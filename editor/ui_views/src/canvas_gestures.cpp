// What a person does on the canvas, each carried to one call of ui_control:
// a wire made or dragged off, Delete, the wheel, Shift with an arrow, F.
#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/wiring.h"
#include "mforce/ui_views/node_canvas.h"
#include "canvas_look.h"

namespace mforce::ui {

using namespace canvas_look;   // the canvas's colours and conversions by their short names

namespace {

constexpr float kWheelStep = 40.0f;    // the canvas's pan per notch of the wheel
constexpr float kArrowStep = 20.0f;    // per frame while Shift and an arrow are held

// The selected links, as the wires delete_these takes.
std::vector<std::pair<PinEnd, std::size_t>> selected_wires(const CanvasTable& t) {
  std::vector<int> ids(size_t(std::max(0, ImNodes::NumSelectedLinks())));
  if (!ids.empty()) ImNodes::GetSelectedLinks(ids.data());
  std::vector<std::pair<PinEnd, std::size_t>> wires;
  for (int id : ids)
    if (const CanvasLink* l = t.link(id)) wires.push_back({l->reader, l->feed});
  return wires;
}

}  // namespace

// imnodes reports a link made between two pins (its giving pin first) and a
// link dragged off its pin; each is decoded through the table.
void NodeCanvas::carry_wiring(Session& s, const CanvasTable& t) {
  int from = 0, to = 0;
  if (ImNodes::IsLinkCreated(&from, &to)) {
    const PinEnd* a = t.pin(from);
    const PinEnd* b = t.pin(to);
    if (a && b) wire(s, *a, *b);
  }
  int link = 0;
  if (ImNodes::IsLinkDestroyed(&link))
    if (const CanvasLink* l = t.link(link)) unwire(s, l->reader, l->feed);
}

// Delete or Backspace, with the canvas holding the keyboard and no text field
// active: the selected wires, then the selected node or face (never the
// Output node). The links then differ, and the next frame clears imnodes'
// link selection. F, wherever the mouse is and with no Ctrl: fit.
// Shift with an arrow, held: the canvas pans.
void NodeCanvas::carry_keys(Session& s, const CanvasTable& t) {
  const ImGuiIO& io = ImGui::GetIO();
  if (io.WantTextInput) return;
  const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
  if (focused && (ImGui::IsKeyPressed(ImGuiKey_Delete, false) || ImGui::IsKeyPressed(ImGuiKey_Backspace, false))) {
    std::vector<std::string> names;
    if (!s.selected.empty()) names.push_back(s.selected);
    delete_these(s, selected_wires(t), names);
  }
  if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) fit_canvas(s, size_);
  if (!io.KeyShift) return;
  Vec2 by{};
  if (ImGui::IsKeyDown(ImGuiKey_LeftArrow)) by.x += kArrowStep;
  if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) by.x -= kArrowStep;
  if (ImGui::IsKeyDown(ImGuiKey_UpArrow)) by.y += kArrowStep;
  if (ImGui::IsKeyDown(ImGuiKey_DownArrow)) by.y -= kArrowStep;
  if (by.x != 0.0f || by.y != 0.0f) {
    const Vec2 p = vec(ImNodes::EditorContextGetPanning());
    pan_canvas(s, Vec2{p.x + by.x, p.y + by.y});
  }
}

// The wheel, over the canvas: vertical; Shift or a horizontal wheel, across.
void NodeCanvas::carry_wheel(Session& s) {
  const ImGuiIO& io = ImGui::GetIO();
  if (!overCanvas_ || io.WantTextInput) return;
  float across = io.MouseWheelH, down = io.MouseWheel;
  if (io.KeyShift && across == 0.0f) std::swap(across, down);
  if (across == 0.0f && down == 0.0f) return;
  const Vec2 p = vec(ImNodes::EditorContextGetPanning());
  pan_canvas(s, Vec2{p.x + across * kWheelStep, p.y + down * kWheelStep});
}

}  // namespace mforce::ui

#include "wave_strip.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mforce::ui::wave_strip {

namespace {

constexpr ImU32 kBackground = IM_COL32(20, 20, 25, 255);
constexpr ImU32 kCentreLine = IM_COL32(60, 60, 60, 255);
constexpr ImU32 kLabel = IM_COL32(150, 150, 150, 255);
constexpr ImU32 kPeak = IM_COL32(100, 100, 100, 255);
constexpr ImU32 kBorder = IM_COL32(80, 80, 80, 255);
constexpr float kLeast = 0.0001f;        // a peak below this draws the strip unscaled
constexpr float kReach = 0.45f;          // a full-scale sample reaches this share of the height from the middle
constexpr float kUpperPeakAt = 0.15f, kLowerPeakAt = 0.75f;

// The label at the top; the upper peak at 15% of the height and the lower
// at 75%, each moved down to clear the line above it.
void margin(ImDrawList* dl, const Strip& strip, Area a, Extent peaks, bool samples) {
  const float h = a.hi.y - a.lo.y, line = ImGui::GetTextLineHeight();
  dl->AddText(ImVec2(a.lo.x + 2, a.lo.y + 2), kLabel, strip.label.c_str());
  if (!samples) return;
  const float upper = std::max(h * kUpperPeakAt, 2.0f + line), lower = std::max(h * kLowerPeakAt, upper + line);
  char text[16];
  std::snprintf(text, sizeof text, "%.3f", double(peaks.hi));
  dl->AddText(ImVec2(a.lo.x + 2, a.lo.y + upper), kPeak, text);
  std::snprintf(text, sizeof text, "%.3f", double(peaks.lo));
  dl->AddText(ImVec2(a.lo.x + 2, a.lo.y + lower), kPeak, text);
}

}  // namespace

void draw(ImDrawList* dl, const Strip& strip, Area a, int take, const WaveView& v, ExtentCache& cache,
          std::vector<Extent>& columns) {
  const float x0 = a.lo.x + float(kStripMargin), h = a.hi.y - a.lo.y, mid = a.lo.y + h * 0.5f;
  const int plot = plot_width(a.hi.x - a.lo.x);
  dl->AddRectFilled(ImVec2(a.lo.x, a.lo.y), ImVec2(a.hi.x, a.hi.y), kBackground);
  dl->AddLine(ImVec2(x0, mid), ImVec2(x0 + float(plot), mid), kCentreLine);
  const bool samples = !strip.samples.empty();
  const Extent pk = peaks(strip, take, cache);
  margin(dl, strip, a, pk, samples);
  if (samples && plot > 0) {
    const float most = std::max(std::fabs(pk.hi), std::fabs(pk.lo));
    const float scale = (most > kLeast ? 1.0f / most : 1.0f) * h * kReach;
    columns.resize(size_t(plot));
    const int n = extents(strip, take, v.zoom, v.scroll, columns, cache);
    const ImU32 colour = strip.envelope ? kEnvelope : kSignal;
    for (int px = 0; px < n; ++px) {
      float y0 = mid - columns[size_t(px)].hi * scale, y1 = mid - columns[size_t(px)].lo * scale;
      if (y1 - y0 < 1.0f) {
        y0 -= 0.5f;
        y1 += 0.5f;
      }
      dl->AddLine(ImVec2(x0 + float(px), y0), ImVec2(x0 + float(px), y1), colour);
    }
  }
  dl->AddRect(ImVec2(a.lo.x, a.lo.y), ImVec2(a.hi.x, a.hi.y), kBorder);
  dl->AddLine(ImVec2(x0, a.lo.y), ImVec2(x0, a.hi.y), kCentreLine);
}

void carry(WaveView& v, bool hovered, bool dragging, bool hasKeys, int total, int plotWidth) {
  const ImGuiIO& io = ImGui::GetIO();
  if (hovered && io.MouseWheel != 0.0f) wheel(v, io.MouseWheel, io.KeyCtrl, total, plotWidth);
  if (dragging && io.MouseDelta.x != 0.0f) drag(v, io.MouseDelta.x, total, plotWidth);
  if (!hasKeys || io.WantTextInput) return;
  const auto key = [&](ImGuiKey k, bool repeat, WaveKey what) {
    if (ImGui::IsKeyPressed(k, repeat)) wave_key(v, what, io.KeyShift, total, plotWidth);
  };
  key(ImGuiKey_LeftArrow, true, WaveKey::Left);
  key(ImGuiKey_RightArrow, true, WaveKey::Right);
  key(ImGuiKey_Home, false, WaveKey::Home);
  key(ImGuiKey_End, false, WaveKey::End);
  key(ImGuiKey_Equal, true, WaveKey::ZoomIn);
  key(ImGuiKey_KeypadAdd, true, WaveKey::ZoomIn);
  key(ImGuiKey_Minus, true, WaveKey::ZoomOut);
  key(ImGuiKey_KeypadSubtract, true, WaveKey::ZoomOut);
  key(ImGuiKey_0, false, WaveKey::Fit);
}

}  // namespace mforce::ui::wave_strip

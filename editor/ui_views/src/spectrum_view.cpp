#include "mforce/ui_views/spectrum_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <imgui.h>

namespace mforce::ui {

namespace {

constexpr ImU32 kBackground = IM_COL32(20, 20, 25, 255);
constexpr ImU32 kBorder = IM_COL32(80, 80, 80, 255);
constexpr ImU32 kGrid = IM_COL32(50, 50, 55, 255);
constexpr ImU32 kMajor = IM_COL32(70, 70, 80, 255);
constexpr ImU32 kLabel = IM_COL32(120, 120, 120, 255);
constexpr ImU32 kEmpty = IM_COL32(140, 140, 140, 255);
constexpr ImU32 kLine = IM_COL32(60, 200, 200, 255);
constexpr ImU32 kCross = IM_COL32(180, 180, 180, 140);
constexpr ImU32 kReadoutBox = IM_COL32(0, 0, 0, 200);
constexpr ImU32 kReadoutText = IM_COL32(255, 255, 200, 255);
constexpr float kLeastRoom = 60.0f, kMarginLeft = 36.0f, kMarginBottom = 18.0f, kInset = 4.0f;
constexpr float kLowest = 20.0f, kDbLow = -100.0f, kDbHigh = 6.0f;
constexpr int kDbStep = 12;

// The plot's axes: a log frequency across, dB up.
struct Axes {
  float x0, x1, y0, y1, logLow, logHigh;
  float x_of(float hz) const { return x0 + (std::log10(std::max(hz, kLowest)) - logLow) / (logHigh - logLow) * (x1 - x0); }
  float y_of(float db) const { return y1 - (std::clamp(db, kDbLow, kDbHigh) - kDbLow) / (kDbHigh - kDbLow) * (y1 - y0); }
  float hz_at(float x) const { return std::pow(10.0f, logLow + (x - x0) / (x1 - x0) * (logHigh - logLow)); }
};

int bin_of(const Spectrum& sp, float hz) {
  const int levels = int(sp.dB.size());
  return std::clamp(int(std::round(hz * float(sp.size) / float(sp.sampleRate))), 1, levels - 1);
}

// Lines at 1, 2 and 5 of each decade from 10 to 100,000 inside the axis,
// labelled at 1 and 2; lines every 12 dB from +6 down, and at 0 dB.
void grid(ImDrawList* dl, const Axes& a, float nyquist, float left) {
  for (int dec = 1; dec <= 5; ++dec)
    for (float m : {1.0f, 2.0f, 5.0f}) {
      const float hz = std::pow(10.0f, float(dec)) * m;
      if (hz < kLowest || hz > nyquist) continue;
      const float x = a.x_of(hz);
      dl->AddLine(ImVec2(x, a.y0), ImVec2(x, a.y1), m == 1.0f ? kMajor : kGrid);
      if (m == 5.0f) continue;
      char text[16];
      if (hz >= 1000.0f) std::snprintf(text, sizeof text, "%gk", double(hz / 1000.0f));
      else std::snprintf(text, sizeof text, "%g", double(hz));
      dl->AddText(ImVec2(x + 2, a.y1 + 2), kLabel, text);
    }
  for (int db = int(kDbHigh); db >= int(kDbLow); db -= kDbStep) {
    const float y = a.y_of(float(db));
    dl->AddLine(ImVec2(a.x0, y), ImVec2(a.x1, y), kGrid);
    char text[16];
    std::snprintf(text, sizeof text, "%d", db);
    dl->AddText(ImVec2(left + 2, y - 6), kLabel, text);
  }
  dl->AddLine(ImVec2(a.x0, a.y_of(0.0f)), ImVec2(a.x1, a.y_of(0.0f)), kMajor);
}

// The line, one segment a pixel, each the loudest bin since the one before.
void line(ImDrawList* dl, const Axes& a, const Spectrum& sp) {
  int prevBin = 1;
  float prevX = a.x_of(float(sp.sampleRate) / float(sp.size)), prevY = a.y_of(sp.dB[1]);
  for (float px = a.x0 + 1.0f; px <= a.x1; px += 1.0f) {
    const int bin = bin_of(sp, a.hz_at(px));
    float db = kDbLow;
    for (int b = std::min(prevBin + 1, bin); b <= bin; ++b) db = std::max(db, sp.dB[size_t(b)]);
    const float y = a.y_of(db);
    dl->AddLine(ImVec2(prevX, prevY), ImVec2(px, y), kLine, 1.5f);
    prevX = px;
    prevY = y;
    prevBin = bin;
  }
}

// The crosshair and the words for the point under the mouse, kept within the plot.
std::string readout_at(ImDrawList* dl, const Axes& a, const Spectrum& sp, ImVec2 m) {
  const float hz = a.hz_at(m.x), db = sp.dB[size_t(bin_of(sp, hz))];
  dl->AddLine(ImVec2(m.x, a.y0), ImVec2(m.x, a.y1), kCross);
  dl->AddLine(ImVec2(a.x0, a.y_of(db)), ImVec2(a.x1, a.y_of(db)), kCross);
  char text[64];
  if (hz >= 1000.0f) std::snprintf(text, sizeof text, "%.2f kHz   %.1f dB", double(hz / 1000.0f), double(db));
  else std::snprintf(text, sizeof text, "%.1f Hz   %.1f dB", double(hz), double(db));
  const ImVec2 size = ImGui::CalcTextSize(text);
  float x = m.x + 8.0f, y = m.y - 16.0f;
  if (x + size.x > a.x1 - 4.0f) x = m.x - 8.0f - size.x;
  if (y < a.y0 + 2.0f) y = m.y + 8.0f;
  dl->AddRectFilled(ImVec2(x - 3, y - 1), ImVec2(x + size.x + 3, y + size.y + 1), kReadoutBox);
  dl->AddText(ImVec2(x, y), kReadoutText, text);
  return text;
}

}  // namespace

void SpectrumView::begin_frame() {
  places_.clear();
  readout_.clear();
}

void SpectrumView::draw(const Session& s) {
  if (madeFor_ != s.takes) {
    spectrum_ = spectrum_of(s.take);
    madeFor_ = s.takes;
  }
  const ImVec2 room = ImGui::GetContentRegionAvail();
  if (room.x < kLeastRoom || room.y < kLeastRoom) return;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1(p0.x + room.x, p0.y + room.y);
  dl->AddRectFilled(p0, p1, kBackground);
  const float nyquist = float(spectrum_.sampleRate) * 0.5f;
  const Axes a{p0.x + kMarginLeft, p1.x - kInset, p0.y + kInset, p1.y - kMarginBottom,
               std::log10(kLowest), std::log10(std::max(nyquist, kLowest * 2.0f))};
  places_.note("spectrum", Area{Vec2{a.x0, a.y0}, Vec2{a.x1, a.y1}});
  if (spectrum_.dB.size() < 2) {
    dl->AddText(ImVec2(a.x0 + 8, a.y0 + 8), kEmpty, "No render yet — hit Generate.");
  } else {
    grid(dl, a, nyquist, p0.x);
    line(dl, a, spectrum_);
    const ImVec2 m = ImGui::GetMousePos();
    if (ImGui::IsWindowHovered() && m.x >= a.x0 && m.x <= a.x1 && m.y >= a.y0 && m.y <= a.y1)
      readout_ = readout_at(dl, a, spectrum_, m);
  }
  dl->AddRect(ImVec2(a.x0, a.y0), ImVec2(a.x1, a.y1), kBorder);
  ImGui::Dummy(room);
}

}  // namespace mforce::ui

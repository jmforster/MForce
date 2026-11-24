#pragma once
// One strip drawn, and the hand's gestures over strips reported to the
// control, shared by the pane's strips and the pop-outs. Included only by
// .cpp files of ui_views, which may use ImGui.
#include <vector>
#include <imgui.h>
#include "mforce/ui_control/waveforms.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui::wave_strip {

constexpr ImU32 kSignal = IM_COL32(80, 220, 80, 255);
constexpr ImU32 kEnvelope = IM_COL32(220, 220, 60, 255);

// `strip` in `a` (its margin and its plot), as `v` looks at it: the
// background, the centre line, the label and the two peaks in the margin,
// one line a pixel column, each strip to its own peak, the border.
// `columns` is the caller's room for the columns, reused frame to frame.
void draw(ImDrawList* dl, const Strip& strip, Area a, int take, const WaveView& v, ExtentCache& cache,
          std::vector<Extent>& columns);

// The least height a strip is drawn at: its label and its two peaks, a line each.
inline float least_height() { return 3.0f * ImGui::GetTextLineHeight() + 6.0f; }

// The plot's width of a strip `width` wide.
inline int plot_width(float width) { return width > float(kStripMargin) ? int(width) - kStripMargin : 0; }

// What the hand did over strips this frame, each to the control: the wheel
// (and Ctrl with it) while `hovered`, a left drag while `dragging`, and the
// strips' keys while `hasKeys` (the mouse was over these strips last frame).
void carry(WaveView& v, bool hovered, bool dragging, bool hasKeys, int total, int plotWidth);

}  // namespace mforce::ui::wave_strip

// What each gesture does to a view of the strips, reckoned in the plot's
// width: the zoom, Fit, the scroll, the wheel, a drag, the keys.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "mforce/ui_control/waveforms.h"

namespace mforce::ui {

namespace {

constexpr int kMostColumns = 4;
constexpr double kNotchShare = 0.1;      // a notch of the wheel, and a key's step: a tenth of the plot's samples
constexpr int kStepsPerPage = 10;

// The samples one plot shows at the view's zoom, at least 1.
std::int64_t page(const WaveView& v, int plotWidth) {
  return std::max<std::int64_t>(1, std::int64_t(std::max(0, plotWidth)) * v.zoom);
}

void keep_in(WaveView& v, std::int64_t at, int total, int plotWidth) {
  v.scroll = int(std::clamp<std::int64_t>(at, 0, max_scroll(v, total, plotWidth)));
}

}  // namespace

void zoom_in(WaveView& v) { v.zoom = std::max(1, v.zoom / 2); }

void zoom_out(WaveView& v) {
  if (v.zoom < kMostZoom) v.zoom = std::min(kMostZoom, v.zoom * 2);
}

void fit(WaveView& v, int total, int plotWidth) {
  if (total > 0) v.zoom = std::max(1, total / std::max(1, plotWidth));
  v.scroll = 0;
}

void first_fit(WaveView& v, int total, int plotWidth) {
  if (v.fitted || total <= 0 || plotWidth <= 0) return;
  fit(v, total, plotWidth);
  v.fitted = true;
}

int max_scroll(const WaveView& v, int total, int plotWidth) {
  return int(std::max<std::int64_t>(0, std::int64_t(total) - std::int64_t(std::max(0, plotWidth)) * v.zoom));
}

void scroll_by(WaveView& v, int samples, int total, int plotWidth) {
  keep_in(v, std::int64_t(v.scroll) + samples, total, plotWidth);
}

void scroll_to(WaveView& v, int sample, int total, int plotWidth) { keep_in(v, sample, total, plotWidth); }

void wheel(WaveView& v, float notches, bool ctrl, int total, int plotWidth) {
  if (ctrl) {
    if (notches > 0.0f) zoom_in(v);
    else if (notches < 0.0f) zoom_out(v);
  } else {
    const double by = -double(notches) * double(page(v, plotWidth)) * kNotchShare;
    keep_in(v, std::int64_t(v.scroll) + std::llround(by), total, plotWidth);
  }
  keep_in(v, v.scroll, total, plotWidth);
}

void drag(WaveView& v, float pixels, int total, int plotWidth) {
  keep_in(v, std::int64_t(v.scroll) - std::llround(double(pixels) * v.zoom), total, plotWidth);
}

void wave_key(WaveView& v, WaveKey key, bool shift, int total, int plotWidth) {
  const std::int64_t whole = page(v, plotWidth);
  const std::int64_t move = shift ? whole : std::max<std::int64_t>(1, whole / kStepsPerPage);
  switch (key) {
    case WaveKey::Left:    keep_in(v, std::int64_t(v.scroll) - move, total, plotWidth); break;
    case WaveKey::Right:   keep_in(v, std::int64_t(v.scroll) + move, total, plotWidth); break;
    case WaveKey::Home:    v.scroll = 0; break;
    case WaveKey::End:     keep_in(v, max_scroll(v, total, plotWidth), total, plotWidth); break;
    case WaveKey::ZoomIn:  zoom_in(v); break;
    case WaveKey::ZoomOut: zoom_out(v); break;
    case WaveKey::Fit:     fit(v, total, plotWidth); break;
  }
  keep_in(v, v.scroll, total, plotWidth);
}

void set_columns(WaveView& v, int columns) { v.columns = std::clamp(columns, 1, kMostColumns); }

void show_envelopes(WaveView& v, bool shown) { v.envelopes = shown; }

}  // namespace mforce::ui

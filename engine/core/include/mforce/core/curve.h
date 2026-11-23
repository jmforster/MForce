#pragma once
#include "mforce/core/ramp.h"
#include "mforce/core/smoothness_interpolator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace mforce {

// Shared piecewise-curve math (docs/superpowers/specs/2026-09-05-curve-
// morph-design.md §2). A curve is breakpoints + an interpolation policy;
// hosts keep their own storage and quirks (CurveNode expressions mode,
// SegmentSource deltas) and delegate evaluation here. UI copy says
// "points", never "knots".
//
// Parity note: the old CurveNode Linear path had no w > 0 guard — a curve
// with duplicate knot x's divided by zero; eval_core returns the right
// endpoint instead. No shipped patch has duplicate x's (null gate
// arbitrates). The Linear-domain body otherwise carries ParamSlot::vmap
// semantics forward verbatim; LogX/LogLog carry ParamSlot::map.
struct Curve {
  enum class Domain : uint8_t { Linear, LogX, LogLog };

  // Per-segment connection override ("how this point connects to the
  // next"), stored on the segment's left point. overridden=false =>
  // host default (SmoothnessInterpolator at the given smoothness; 0.5
  // is exact lerp, which is CurveNode's fixed behavior).
  struct Seg {
    bool overridden{false};
    RampType type{RampType::Linear};
    float power{2.0f};
  };

  static float eval_seg(float y0, float y1, float t, const Seg& seg,
                        float smoothness) {
    if (!seg.overridden)
      return SmoothnessInterpolator(smoothness, false).interpolate(y0, y1, t);
    Ramp r{y0, y1, seg.type, seg.power};
    return r.value(t);
  }

  template <class PX, class PY, class SG>
  static float eval_core(size_t n, PX px, PY py, SG seg_at, Domain domain,
                         float smoothness, float x) {
    if (n == 0) return x;                       // identity (both hosts)
    if (x <= px(0))     return py(0);
    if (x >= px(n - 1)) return py(n - 1);
    for (size_t i = 1; i < n; ++i) {
      if (x <= px(i)) {
        if (domain == Domain::Linear) {
          const float w = px(i) - px(i - 1);
          const float pos = w > 0.0f
              ? std::clamp((x - px(i - 1)) / w, 0.0f, 1.0f) : 1.0f;
          return eval_seg(py(i - 1), py(i), pos, seg_at(i - 1), smoothness);
        }
        const float lf = std::log(x / px(i - 1)) /
                         std::log(px(i) / px(i - 1));
        if (domain == Domain::LogLog && py(i - 1) > 0.0f && py(i) > 0.0f)
          return py(i - 1) * std::pow(py(i) / py(i - 1), lf);
        return py(i - 1) + (py(i) - py(i - 1)) * lf;
      }
    }
    return py(n - 1);
  }

  static float eval(std::span<const std::pair<float, float>> pts,
                    std::span<const Seg> segs, Domain domain,
                    float smoothness, float x) {
    return eval_core(pts.size(),
        [&](size_t i) { return pts[i].first; },
        [&](size_t i) { return pts[i].second; },
        [&](size_t i) { return i < segs.size() ? segs[i] : Seg{}; },
        domain, smoothness, x);
  }

  static float eval_flat(std::span<const float> xy,
                         std::span<const Seg> segs, Domain domain,
                         float smoothness, float x) {
    return eval_core(xy.size() / 2,
        [&](size_t i) { return xy[i * 2]; },
        [&](size_t i) { return xy[i * 2 + 1]; },
        [&](size_t i) { return i < segs.size() ? segs[i] : Seg{}; },
        domain, smoothness, x);
  }
};

} // namespace mforce

#pragma once
#include "mforce/core/curve.h"
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace mforce {

// Transfer node: y = curve(x), knots sorted by x, clamped at the end knots.
// The runtime (pullable) home of the paramMap curve/vcurve data
// (docs/perform_source_design.md §2.2). Interp modes:
//   Linear — y linear in x               (legacy ParamSlot::vmap, verbatim)
//   LogX   — y linear in log(x)          (legacy ParamSlot::map,  verbatim)
//   LogLog — log(y) linear in log(x): a 2-knot segment is exactly y = k*x^n
// Empty knots = identity (matches ParamSlot's empty-curve behavior).
//
// Expressions mode (2026-09-01, Matt's design): each knot's VALUE is a small
// formula of the input — Linear a*x+b or Power a*x^p — instead of a literal.
// Semantics: evaluate the two bracketing knots' formulas at the CURRENT x,
// then interpolate between the results (a literal is Linear a=0), so equal
// formulas on adjacent knots pin the formula exactly over that span and
// differing ones morph smoothly. Outside the end knots the edge formula
// keeps evaluating at x — live extrapolation, so a ONE-knot Linear(a,0)
// curve is global keytrack and the old always-end-at-16000 convention is
// unnecessary. For literal values extrapolation equals the old clamp, which
// is why points mode is byte-identical and stays the default.
struct CurveNode final : Generator {
  Param source{this, "source", 0.0f, -1.0e9f, 1.0e9f, "x"};

  enum class CurveInterp { Linear, LogX, LogLog };
  enum class KnotForm { Linear, Power };  // a*x + b  |  a*x^p
  struct ExprKnot {
    float x{100.0f};
    KnotForm form{KnotForm::Linear};
    float a{0.0f};
    float b{0.0f};  // offset for Linear, exponent p for Power
  };

  std::vector<std::pair<float, float>> knots;
  std::vector<ExprKnot> exprKnots;
  bool exprMode{false};
  CurveInterp interp{CurveInterp::Linear};

  void on_start(int) override {
    lastX_ = std::numeric_limits<float>::quiet_NaN();  // force re-eval
  }

  // Re-evaluates only when x moves; otherwise the held output stands.
  float compute() override {
    const float x = source.current();
    if (x != lastX_) { lastX_ = x; cur_ = map(x); }
    return cur_;
  }

  static float eval_expr(const ExprKnot& k, float x) {
    return k.form == KnotForm::Power ? k.a * std::pow(x, k.b)
                                     : k.a * x + k.b;
  }

  float map_expr(float x) const {
    if (exprKnots.empty()) return x;  // identity, matching points mode
    if (x <= exprKnots.front().x) return eval_expr(exprKnots.front(), x);
    if (x >= exprKnots.back().x)  return eval_expr(exprKnots.back(), x);
    for (size_t i = 1; i < exprKnots.size(); ++i) {
      if (x <= exprKnots[i].x) {
        const ExprKnot& k0 = exprKnots[i - 1];
        const ExprKnot& k1 = exprKnots[i];
        const float v0 = eval_expr(k0, x);
        const float v1 = eval_expr(k1, x);
        float t;
        if (interp == CurveInterp::Linear || k0.x <= 0.0f)
          t = (x - k0.x) / (k1.x - k0.x);
        else
          t = std::log(x / k0.x) / std::log(k1.x / k0.x);
        if (interp == CurveInterp::LogLog && v0 > 0.0f && v1 > 0.0f)
          return v0 * std::pow(v1 / v0, t);
        return v0 + (v1 - v0) * t;
      }
    }
    return eval_expr(exprKnots.back(), x);
  }

  // Delegates to the shared evaluator (curve.h) — its eval_core carries the
  // ParamSlot::vmap/map semantics forward; smoothness 0.5 is exact lerp.
  float map(float x) const {
    if (exprMode) return map_expr(x);
    const Curve::Domain d =
        interp == CurveInterp::LogLog ? Curve::Domain::LogLog
      : interp == CurveInterp::LogX   ? Curve::Domain::LogX
      :                                 Curve::Domain::Linear;
    return Curve::eval(knots, {}, d, 0.5f, x);
  }

  const char* type_name() const override { return "CurveNode"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

private:
  float lastX_{std::numeric_limits<float>::quiet_NaN()};
};

} // namespace mforce

#pragma once
#include "mforce/core/curve.h"
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace mforce {

// Transfer node: y = curve(x), knots sorted by x, clamped at the end knots.
// A patch's per-note frequency and velocity curves are these nodes, ordinary
// graph nodes reading the note: a pin they feed pulls them each sample, a
// dynamic pin steps them once at the note's Setup. Interp modes:
//   Linear — y linear in x
//   LogX   — y linear in log(x)
//   LogLog — log(y) linear in log(x): a 2-knot segment is exactly y = k*x^n
// Empty knots = identity.
//
// Expressions mode: each knot's VALUE is a small formula of the input —
// Linear a*x+b or Power a*x^p — instead of a literal.
// Semantics: evaluate the two bracketing knots' formulas at the CURRENT x,
// then interpolate between the results (a literal is Linear a=0), so equal
// formulas on adjacent knots pin the formula exactly over that span and
// differing ones morph smoothly. Outside the end knots the edge formula
// keeps evaluating at x — live extrapolation, so a ONE-knot Linear(a,0)
// curve is global keytrack. For literal values extrapolation equals the
// clamp of points mode.
//
// The node declares two settings, interp and mode, and two tables, the
// knots (x, y) and the expression rows (exprX, exprForm, exprA, exprB); the
// vectors map() evaluates are derived from those columns.
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

  static constexpr const char* const kInterpLabels[] = {"linear", "logx", "loglog", nullptr};
  static constexpr const char* const kModeLabels[] = {"points", "expressions", nullptr};
  static constexpr const char* const kFormLabels[] = {"linear", "power", nullptr};
  static constexpr int kPointsMode = 0, kExpressionsMode = 1;
  static_assert(int(CurveInterp::Linear) == 0 && int(CurveInterp::LogX) == 1 &&
                int(CurveInterp::LogLog) == 2, "kInterpLabels is in CurveInterp's order");
  static_assert(std::string_view(kModeLabels[kPointsMode]) == "points" &&
                std::string_view(kModeLabels[kExpressionsMode]) == "expressions",
                "kModeLabels is in the modes' order");
  static_assert(int(KnotForm::Linear) == 0 && int(KnotForm::Power) == 1,
                "kFormLabels is in KnotForm's order");

  Setting<int> interp{this, "interp", 0, 0, 2, kInterpLabels};
  Setting<int> mode{this, "mode", kPointsMode, kPointsMode, kExpressionsMode, kModeLabels};

  // The rows map() reads, in column order.
  const std::vector<std::pair<float, float>>& knots() const { return knots_; }
  const std::vector<ExprKnot>& expr_knots() const { return exprKnots_; }

  // For a caller that holds rows: writes them as the table's columns.
  void set_knots(std::span<const std::pair<float, float>> rows) {
    knotX.values.resize(rows.size());
    knotY.values.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
      knotX.values[i] = rows[i].first;
      knotY.values[i] = rows[i].second;
    }
    rebuild_();
  }
  void set_expr_knots(std::span<const ExprKnot> rows) {
    for (Array* c : {&exprX, &exprForm, &exprA, &exprB}) c->values.resize(rows.size());
    for (size_t i = 0; i < rows.size(); ++i) {
      exprX.values[i]    = rows[i].x;
      exprForm.values[i] = float(int(rows[i].form));
      exprA.values[i]    = rows[i].a;
      exprB.values[i]    = rows[i].b;
    }
    rebuild_();
  }

  void on_array_changed(std::string_view) override { rebuild_(); }

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
    if (exprKnots_.empty()) return x;  // identity, matching points mode
    if (x <= exprKnots_.front().x) return eval_expr(exprKnots_.front(), x);
    if (x >= exprKnots_.back().x)  return eval_expr(exprKnots_.back(), x);
    for (size_t i = 1; i < exprKnots_.size(); ++i) {
      if (x <= exprKnots_[i].x) {
        const ExprKnot& k0 = exprKnots_[i - 1];
        const ExprKnot& k1 = exprKnots_[i];
        const float v0 = eval_expr(k0, x);
        const float v1 = eval_expr(k1, x);
        float t;
        if (interp_() == CurveInterp::Linear || k0.x <= 0.0f)
          t = (x - k0.x) / (k1.x - k0.x);
        else
          t = std::log(x / k0.x) / std::log(k1.x / k0.x);
        if (interp_() == CurveInterp::LogLog && v0 > 0.0f && v1 > 0.0f)
          return v0 * std::pow(v1 / v0, t);
        return v0 + (v1 - v0) * t;
      }
    }
    return eval_expr(exprKnots_.back(), x);
  }

  // Delegates to the shared evaluator (curve.h, eval_core); smoothness 0.5
  // is exact lerp.
  float map(float x) const {
    if (expr_mode_()) return map_expr(x);
    const Curve::Domain d =
        interp_() == CurveInterp::LogLog ? Curve::Domain::LogLog
      : interp_() == CurveInterp::LogX   ? Curve::Domain::LogX
      :                                 Curve::Domain::Linear;
    return Curve::eval(knots_, {}, d, 0.5f, x);
  }

  const char* type_name() const override { return "CurveNode"; }

private:
  // The two tables: one row per knot, one column per field. The columns are
  // what the node holds and what a patch file and the editor see, through
  // set_array and get_array and the setters above; knots_ and exprKnots_ are
  // derived from them by rebuild_(). They are the last pins the node
  // declares, so they register after the settings.
  Array knotX{this, "x", "knots", 0.0f, -1.0e9f, 1.0e9f};
  Array knotY{this, "y", "knots", 0.0f, -1.0e9f, 1.0e9f};
  Array exprX{this, "exprX", "exprKnots", 100.0f, -1.0e9f, 1.0e9f};
  Array exprForm{this, "exprForm", "exprKnots", 0.0f, 0.0f, 1.0f, kFormLabels};
  Array exprA{this, "exprA", "exprKnots", 0.0f, -1.0e9f, 1.0e9f};
  Array exprB{this, "exprB", "exprKnots", 0.0f, -1.0e9f, 1.0e9f};

  // Total over the setting's int: a value outside the labels reads as Linear.
  CurveInterp interp_() const {
    return interp.value == 1 ? CurveInterp::LogX
         : interp.value == 2 ? CurveInterp::LogLog
         :                     CurveInterp::Linear;
  }
  bool expr_mode_() const { return mode.value == kExpressionsMode; }

  // The one derivation of the rows from the columns: each group has as many
  // rows as its shortest column has cells; a form cell is truncated and
  // clamped into KnotForm. Allocates only when a row count changes.
  void rebuild_() {
    const size_t nk = std::min(knotX.values.size(), knotY.values.size());
    knots_.resize(nk);
    for (size_t i = 0; i < nk; ++i) knots_[i] = {knotX.values[i], knotY.values[i]};
    const size_t ne = std::min({exprX.values.size(), exprForm.values.size(),
                                exprA.values.size(), exprB.values.size()});
    exprKnots_.resize(ne);
    for (size_t i = 0; i < ne; ++i) {
      ExprKnot& k = exprKnots_[i];
      k.x    = exprX.values[i];
      k.form = KnotForm(std::clamp(int(exprForm.values[i]), 0, 1));
      k.a    = exprA.values[i];
      k.b    = exprB.values[i];
    }
  }

  std::vector<std::pair<float, float>> knots_;   // derived from x, y: rebuild_()
  std::vector<ExprKnot> exprKnots_;              // derived from the expression columns: rebuild_()
  float lastX_{std::numeric_limits<float>::quiet_NaN()};
};

} // namespace mforce

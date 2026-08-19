#pragma once
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
struct CurveNode final : ValueSource {
  enum class CurveInterp { Linear, LogX, LogLog };

  std::vector<std::pair<float, float>> knots;
  CurveInterp interp{CurveInterp::Linear};

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    lastX_ = std::numeric_limits<float>::quiet_NaN();  // force re-eval
  }

  float next() override {
    float x = source_ ? (source_->next(), source_->current()) : 0.0f;
    if (x != lastX_) { lastX_ = x; cur_ = map(x); }
    return cur_;
  }
  float current() const override { return cur_; }

  float map(float x) const {
    if (knots.empty()) return x;  // identity
    if (x <= knots.front().first) return knots.front().second;
    if (x >= knots.back().first)  return knots.back().second;
    if (interp == CurveInterp::Linear) {
      // === ParamSlot::vmap body, verbatim (instrument.h) ===
      for (size_t i = 1; i < knots.size(); ++i) {
        if (x <= knots[i].first) {
          float t = (x - knots[i - 1].first) /
                    (knots[i].first - knots[i - 1].first);
          return knots[i - 1].second +
                 (knots[i].second - knots[i - 1].second) * t;
        }
      }
      return knots.back().second;
    }
    // === ParamSlot::map body, verbatim (instrument.h); LogLog is its
    // loglog branch ===
    for (size_t i = 1; i < knots.size(); ++i) {
      if (x <= knots[i].first) {
        float lf = std::log(x / knots[i - 1].first) /
                   std::log(knots[i].first / knots[i - 1].first);
        if (interp == CurveInterp::LogLog &&
            knots[i - 1].second > 0.0f && knots[i].second > 0.0f) {
          return knots[i - 1].second *
                 std::pow(knots[i].second / knots[i - 1].second, lf);
        }
        return knots[i - 1].second +
               (knots[i].second - knots[i - 1].second) * lf;
      }
    }
    return knots.back().second;
  }

  const char* type_name() const override { return "CurveNode"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"source", 0.0f, -1.0e9f, 1.0e9f, "x"},
    };
    return descs;
  }
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") source_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source") return source_;
    return nullptr;
  }

private:
  std::shared_ptr<ValueSource> source_;
  float cur_{0.0f};
  float lastX_{std::numeric_limits<float>::quiet_NaN()};
};

} // namespace mforce

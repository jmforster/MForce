#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/smoothness_interpolator.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace mforce {

// Per-sample drawn-curve transfer: y = curve(drive * x) — the junction
// nonlinearity for feedback loops (feedback_loop_design.md §3.5), and a
// free waveshaper open-loop. values[] holds ABSOLUTE (x, y) breakpoints
// ascending in x (NOT the SegmentSource delta form); evaluation is
// piecewise between breakpoints through SmoothnessInterpolator, clamped
// to the end values outside the drawn range. Default = identity.
struct ShaperSource final : ValueSource {
  ShaperSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      drive_(std::make_shared<ConstantSource>(1.0f)),
      smoothness_(std::make_shared<ConstantSource>(0.5f)),
      values_{-1.0f, -1.0f, 1.0f, 1.0f} {}

  const char* type_name() const override { return "Shaper"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"drive",      1.0f, 0.0f, 20.0f, "ratio"},
      {"smoothness", 0.5f, 0.0f, 1.0f, "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "drive")      { drive_ = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "drive")      return drive_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  std::span<const ArrayDescriptor> array_descriptors() const override {
    static constexpr ArrayDescriptor descs[] = {
      {"values", nullptr, 0.0f, -2.0f, 2.0f},
    };
    return descs;
  }
  void set_array(std::string_view name, std::vector<float> v) override {
    if (name == "values" && v.size() >= 4 && v.size() % 2 == 0)
      values_ = std::move(v);
  }
  std::vector<float> get_array(std::string_view name) const override {
    if (name == "values") return values_;
    return {};
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    drive_->prepare(ctx, frames);
    smoothness_->prepare(ctx, frames);
    cur_ = 0.0f;
  }

  float next() override {
    const float x = (source_ ? source_->next() : 0.0f) * drive_->next();
    smoothness_->next();
    interp_.setSmoothness(smoothness_->current());
    cur_ = map(x);
    return cur_;
  }

  float current() const override { return cur_; }

  // Public for the UI preview/editor overlay (mirrors CurveNode::map).
  float map(float x) {
    const size_t n = values_.size() / 2;
    auto px = [&](size_t i) { return values_[i * 2]; };
    auto py = [&](size_t i) { return values_[i * 2 + 1]; };
    if (n == 0) return x;
    if (x <= px(0))     return py(0);
    if (x >= px(n - 1)) return py(n - 1);
    for (size_t i = 1; i < n; ++i) {
      if (x <= px(i)) {
        const float w = px(i) - px(i - 1);
        const float pos = w > 0.0f
            ? std::clamp((x - px(i - 1)) / w, 0.0f, 1.0f) : 1.0f;
        return interp_.interpolate(py(i - 1), py(i), pos);
      }
    }
    return py(n - 1);
  }

private:
  std::shared_ptr<ValueSource> source_, drive_, smoothness_;
  std::vector<float> values_;
  SmoothnessInterpolator interp_{0.5f, false};
  float cur_{0.0f};
};

} // namespace mforce

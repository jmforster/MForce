#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace mforce {

// Smith (1986) memoryless bow friction curve, verbatim from STK BowTable
// (docs/superpowers/specs/2026-09-13-stk-bowed-port-design.md). Input is
// the differential velocity dv (bow minus string); output is the injected
// new velocity dv * rc(dv), with
//   rc(dv) = clamp( (|(dv + offset) * slope| + 0.75)^-4, min, max ).
// slope is the bow-pressure knob (STK: 5 - 4*pressure, i.e. high pressure
// = low slope = wide friction pulse); offset skews stick/slip asymmetry.
// Both are pins so pressure is playable per note or mid-note.
struct BowTableSource final : ValueSource {
  BowTableSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      slope_(std::make_shared<ConstantSource>(3.0f)),
      offset_(std::make_shared<ConstantSource>(0.0f)) {}

  const char* type_name() const override { return "BowTable"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"slope",  3.0f, 0.05f, 10.0f, "pressure"},
      {"offset", 0.0f, -1.0f, 1.0f},
    };
    return descs;
  }
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") { source_ = std::move(src); return; }
    if (name == "slope")  { slope_  = std::move(src); return; }
    if (name == "offset") { offset_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source") return source_;
    if (name == "slope")  return slope_;
    if (name == "offset") return offset_;
    return nullptr;
  }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"minOutput", SettingType::Float, 0.01f, 0.0f, 1.0f},
      {"maxOutput", SettingType::Float, 0.98f, 0.0f, 1.0f},
    };
    return descs;
  }
  void set_setting(std::string_view name, float v) override {
    if (name == "minOutput") minOut_ = v;
    else if (name == "maxOutput") maxOut_ = v;
  }
  float get_setting(std::string_view name) const override {
    if (name == "minOutput") return minOut_;
    if (name == "maxOutput") return maxOut_;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    slope_->prepare(ctx, frames);
    offset_->prepare(ctx, frames);
    cur_ = 0.0f;
  }

  float next() override {
    const float dv = source_ ? source_->next() : 0.0f;
    slope_->next();
    offset_->next();
    float s = std::fabs((dv + offset_->current()) * slope_->current()) + 0.75f;
    float rc = std::pow(s, -4.0f);
    rc = std::clamp(rc, minOut_, maxOut_);
    cur_ = dv * rc;
    return cur_;
  }
  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_, slope_, offset_;
  float minOut_{0.01f}, maxOut_{0.98f};
  float cur_{0.0f};
};

} // namespace mforce

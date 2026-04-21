#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.RangeSource
// Maps a modulator (var) into the range [min, max].
// Var in [0,1]: value = min + var * (max - min)
// Var in [-1,1] (unnormalized): value = min + (var+1)/2 * (max - min)
//
// In the C++ port we always use the normalized formula (var in [0,1] maps
// linearly to [min,max]).  WaveSources that output [-1,1] should use
// normalized=false so the [-1,1] -> [0,1] adjustment is applied.
struct RangeSource final : ValueSource {
  RangeSource(std::shared_ptr<ValueSource> min,
              std::shared_ptr<ValueSource> max,
              std::shared_ptr<ValueSource> var,
              bool varNormalized = true)
  : min_(std::move(min)), max_(std::move(max)),
    var_(std::move(var)), varNormalized_(varNormalized) {}

  void prepare(const RenderContext& ctx, int frames) override {
    min_->prepare(ctx, frames);
    max_->prepare(ctx, frames);
    var_->prepare(ctx, frames);
  }

  float next() override {
    min_->next();
    max_->next();
    var_->next();

    float v = var_->current();
    if (!varNormalized_) {
      // Legacy: WaveSource outputs [-1,1], remap to [0,1]
      v = (v + 1.0f) * 0.5f;
    }

    cur_ = min_->current() + v * (max_->current() - min_->current());
    return cur_;
  }

  float current() const override { return cur_; }

  void set_min(std::shared_ptr<ValueSource> s) { min_ = std::move(s); }
  void set_max(std::shared_ptr<ValueSource> s) { max_ = std::move(s); }
  void set_var(std::shared_ptr<ValueSource> s) { var_ = std::move(s); }

  const char* type_name() const override { return "RangeSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"min", 0.0f, -10000.0f, 10000.0f},
      {"max", 1.0f, -10000.0f, 10000.0f},
      {"var", 0.0f, -10000.0f, 10000.0f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "min") { set_min(std::move(src)); return; }
    if (name == "max") { set_max(std::move(src)); return; }
    if (name == "var") { set_var(std::move(src)); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "min") return min_;
    if (name == "max") return max_;
    if (name == "var") return var_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"normalized", ConfigType::Bool, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "normalized") { varNormalized_ = (value != 0.0f); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "normalized") return varNormalized_ ? 1.0f : 0.0f;
    return 0.0f;
  }

private:
  std::shared_ptr<ValueSource> min_;
  std::shared_ptr<ValueSource> max_;
  std::shared_ptr<ValueSource> var_;
  bool varNormalized_{true};
  float cur_{0.0f};
};

} // namespace mforce

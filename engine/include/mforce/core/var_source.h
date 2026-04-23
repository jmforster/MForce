#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.VarSource
// Varies around a center value by a percentage, driven by a modulator.
//   Absolute mode:  value = val * (1 + var * varPct)
//   Relative mode:  value = lastVal + delta * (1 + var * varPct)
struct VarSource final : ValueSource {
  VarSource(std::shared_ptr<ValueSource> val,
            std::shared_ptr<ValueSource> var,
            std::shared_ptr<ValueSource> varPct,
            bool absolute = true)
  : val_(std::move(val)), var_(std::move(var)),
    varPct_(std::move(varPct)), absolute_(absolute) {}

  void prepare(const RenderContext& ctx, int frames) override {
    val_->prepare(ctx, frames);
    var_->prepare(ctx, frames);
    varPct_->prepare(ctx, frames);
  }

  float next() override {
    val_->next();
    var_->next();
    varPct_->next();

    if (absolute_) {
      cur_ = val_->current() * (1.0f + var_->current() * varPct_->current());
    } else {
      float delta = val_->current() - lastVal_;
      cur_ = lastVal_ + delta * (1.0f + var_->current() * varPct_->current());
    }

    lastVal_ = cur_;
    return cur_;
  }

  float current() const override { return cur_; }

  void set_val(std::shared_ptr<ValueSource> s) { val_ = std::move(s); }
  void set_var(std::shared_ptr<ValueSource> s) { var_ = std::move(s); }
  void set_var_pct(std::shared_ptr<ValueSource> s) { varPct_ = std::move(s); }

  const char* type_name() const override { return "VarSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"val",    1.0f,  -10000.0f, 10000.0f},
      {"var",    0.0f,  -10000.0f, 10000.0f, "±1"},
      {"varPct", 0.0f,  0.0f,      1.0f,    "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "val")    { set_val(std::move(src)); return; }
    if (name == "var")    { set_var(std::move(src)); return; }
    if (name == "varPct") { set_var_pct(std::move(src)); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "val")    return val_;
    if (name == "var")    return var_;
    if (name == "varPct") return varPct_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"absolute", ConfigType::Bool, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "absolute") { absolute_ = (value != 0.0f); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "absolute") return absolute_ ? 1.0f : 0.0f;
    return 0.0f;
  }

private:
  std::shared_ptr<ValueSource> val_;
  std::shared_ptr<ValueSource> var_;
  std::shared_ptr<ValueSource> varPct_;
  bool absolute_{true};
  float cur_{0.0f};
  float lastVal_{0.0f};
};

} // namespace mforce

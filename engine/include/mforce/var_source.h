#pragma once
#include "dsp_value_source.h"
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

  void prepare(int frames) override {
    val_->prepare(frames);
    var_->prepare(frames);
    varPct_->prepare(frames);
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

private:
  std::shared_ptr<ValueSource> val_;
  std::shared_ptr<ValueSource> var_;
  std::shared_ptr<ValueSource> varPct_;
  bool absolute_{true};
  float cur_{0.0f};
  float lastVal_{0.0f};
};

} // namespace mforce

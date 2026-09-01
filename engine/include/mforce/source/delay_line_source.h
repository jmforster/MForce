#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <memory>
#include <vector>

namespace mforce {

// Pitch-tracked delay line — the resonator backbone for feedback loops
// (feedback_loop_design.md §3.4). No internal feedback; `amplitude` is a
// read-side loop-gain control (the KS loss factor — decays without the
// pitch bend cutoff modulation causes). Length = sampleRate/frequency*ratio
// samples, fractional read (linear interp), modulatable per sample (mild
// artifacts accepted in v1). Buffer sized at construction for a 20 Hz
// floor — no heap in the render loop; frequencies below floor clamp.
struct DelayLineSource final : ValueSource {
  int sampleRate{48000};

  explicit DelayLineSource(int sr)
    : sampleRate(sr),
      source_(std::make_shared<ConstantSource>(0.0f)),
      frequency_(std::make_shared<ConstantSource>(440.0f)),
      ratio_(std::make_shared<ConstantSource>(1.0f)),
      amplitude_(std::make_shared<ConstantSource>(1.0f)),
      buf_(size_t(sr / 20 + 4), 0.0f) {}

  const char* type_name() const override { return "DelayLine"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 440.0f, 20.0f, 20000.0f, "hz"},
      {"ratio",     1.0f, 0.05f, 20.0f, "ratio"},
      {"amplitude", 1.0f, 0.0f, 2.0f, "gain"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    { source_ = std::move(src); return; }
    if (name == "frequency") { frequency_ = std::move(src); return; }
    if (name == "ratio")     { ratio_ = std::move(src); return; }
    if (name == "amplitude") { amplitude_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "frequency") return frequency_;
    if (name == "ratio")     return ratio_;
    if (name == "amplitude") return amplitude_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    frequency_->prepare(ctx, frames);
    ratio_->prepare(ctx, frames);
    amplitude_->prepare(ctx, frames);
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    writeIdx_ = 0;
    cur_ = 0.0f;
  }

  float next() override {
    const float in = source_ ? source_->next() : 0.0f;
    frequency_->next();
    ratio_->next();
    amplitude_->next();
    const float f = std::max(frequency_->current(), 20.0f);
    const float len = std::clamp(float(sampleRate) / f * ratio_->current(),
                                 1.0f, float(buf_.size()) - 2.0f);
    buf_[size_t(writeIdx_)] = in;
    float rp = float(writeIdx_) - len;
    if (rp < 0.0f) rp += float(buf_.size());
    const int i0 = int(rp);
    const int i1 = (i0 + 1) % int(buf_.size());
    const float frac = rp - float(i0);
    // Read-side gain: the loop (and the listener) feel a closing envelope
    // immediately, not one recirculation later as a write-side gain would.
    cur_ = (buf_[size_t(i0)] * (1.0f - frac) + buf_[size_t(i1)] * frac)
           * amplitude_->current();
    writeIdx_ = (writeIdx_ + 1) % int(buf_.size());
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_, frequency_, ratio_, amplitude_;
  std::vector<float> buf_;
  int writeIdx_{0};
  float cur_{0.0f};
};

} // namespace mforce

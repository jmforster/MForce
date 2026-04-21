#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// SortOscillator — experimental time-domain oscillator.
// Runs a sorting algorithm on an internal buffer at audio rate; output is
// derived from the current state of the sort. When the buffer becomes sorted,
// it is reseeded (partially, by `perturb`) and the sort restarts.
//
// The fundamental pitch is roughly rate / O(N^2); the timbre is entirely a
// function of the algorithm's comparison/swap trajectory.
// ---------------------------------------------------------------------------
struct SortOscillator final : ValueSource {
  // Config fields
  int size{64};              // buffer length
  int algorithm{0};          // 0=insertion, 1=bubble, 2=gnome
  int outputMode{0};         // 0=compareDelta, 1=cursorValue, 2=positionRamp

  SortOscillator(int sampleRate, uint32_t seed = 0x5041'7201u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(1.0f)),
    rate_(std::make_shared<ConstantSource>(4000.0f)),
    perturb_(std::make_shared<ConstantSource>(0.3f)),
    spread_(std::make_shared<ConstantSource>(0.7f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    resize_and_reseed(size);
  }

  const char* type_name() const override { return "SortOscillator"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  // -------------------------------------------------------------------------
  // Connectable params
  // -------------------------------------------------------------------------
  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  1.0f,     0.0f,    10.0f},
      {"rate",       4000.0f,  0.0f,    500000.0f},
      {"perturb",    0.3f,     0.0f,    1.0f},
      {"spread",     0.7f,     0.0f,    1.0f},
      {"smoothness", 0.0f,     0.0f,    0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_  = std::move(src); return; }
    if (name == "rate")       { rate_       = std::move(src); return; }
    if (name == "perturb")    { perturb_    = std::move(src); return; }
    if (name == "spread")     { spread_     = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "rate")       return rate_;
    if (name == "perturb")    return perturb_;
    if (name == "spread")     return spread_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  // -------------------------------------------------------------------------
  // Non-connectable config (size, algorithm, output mode)
  // -------------------------------------------------------------------------
  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"size",       ConfigType::Int,  64.0f, 4.0f,  1024.0f},
      {"algorithm",  ConfigType::Int,  0.0f,  0.0f,  2.0f},
      {"outputMode", ConfigType::Int,  0.0f,  0.0f,  2.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "size") {
      int n = std::clamp(int(value), 4, 1024);
      if (n != size) { size = n; resize_and_reseed(size); }
      return;
    }
    if (name == "algorithm")  { algorithm  = std::clamp(int(value), 0, 2); reset_algo_state(); return; }
    if (name == "outputMode") { outputMode = std::clamp(int(value), 0, 2); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "size")       return float(size);
    if (name == "algorithm")  return float(algorithm);
    if (name == "outputMode") return float(outputMode);
    return 0.0f;
  }

  // -------------------------------------------------------------------------
  void prepare(const RenderContext& ctx, int frames) override {
    if (amplitude_)  amplitude_->prepare(ctx, frames);
    if (rate_)       rate_->prepare(ctx, frames);
    if (perturb_)    perturb_->prepare(ctx, frames);
    if (spread_)     spread_->prepare(ctx, frames);
    if (smoothness_) smoothness_->prepare(ctx, frames);
  }

  float next() override {
    amplitude_->next();
    rate_->next();
    perturb_->next();
    spread_->next();
    smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    // Execute integer number of sort ops this sample
    while (opCredits_ >= 1.0f) {
      step_once();
      opCredits_ -= 1.0f;
    }

    // Derive raw sample from current state
    float raw = 0.0f;
    switch (outputMode) {
      case 0: raw = lastCompareDelta_; break;
      case 1: raw = buffer_[cursor_ % buffer_.size()]; break;
      case 2: raw = 2.0f * float(cursor_) / float(std::max(1, int(buffer_.size()) - 1)) - 1.0f; break;
    }

    // One-pole lowpass for taming
    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // -------------------------------------------------------------------------
  // One sort operation. Advances algorithm state; when sort completes,
  // perturbs the buffer and restarts. Always updates cursor_ and
  // lastCompareDelta_ so outputs stay live.
  // -------------------------------------------------------------------------
  void step_once() {
    const int N = int(buffer_.size());
    if (N < 2) { cursor_ = 0; return; }

    switch (algorithm) {
      case 0: step_insertion(N); break;
      case 1: step_bubble(N);    break;
      case 2: step_gnome(N);     break;
    }
  }

  void step_insertion(int N) {
    // Compare a[j-1] vs a[j]. If out of order, swap, j--. Else advance i.
    if (i_ >= N) { reseed_and_restart(); return; }
    if (j_ <= 0) { i_++; j_ = i_; return; }
    float left  = buffer_[j_ - 1];
    float right = buffer_[j_];
    lastCompareDelta_ = left - right;
    cursor_ = j_;
    if (left > right) {
      buffer_[j_ - 1] = right;
      buffer_[j_] = left;
      j_--;
    } else {
      i_++;
      j_ = i_;
    }
  }

  void step_bubble(int N) {
    // Classic pass-by-pass bubble. i_ = inner index, pass_ = outer pass.
    if (pass_ >= N - 1) { reseed_and_restart(); return; }
    int inner_end = N - 1 - pass_;
    if (i_ >= inner_end) { pass_++; i_ = 0; return; }
    float left  = buffer_[i_];
    float right = buffer_[i_ + 1];
    lastCompareDelta_ = left - right;
    cursor_ = i_;
    if (left > right) {
      buffer_[i_] = right;
      buffer_[i_ + 1] = left;
    }
    i_++;
  }

  void step_gnome(int N) {
    // Gnome sort: single pointer. Walks forward if in order, back if not.
    if (i_ >= N) { reseed_and_restart(); return; }
    if (i_ == 0) { i_ = 1; cursor_ = 0; lastCompareDelta_ = 0.0f; return; }
    float left  = buffer_[i_ - 1];
    float right = buffer_[i_];
    lastCompareDelta_ = left - right;
    cursor_ = i_;
    if (left <= right) {
      i_++;
    } else {
      buffer_[i_ - 1] = right;
      buffer_[i_] = left;
      i_--;
    }
  }

  // -------------------------------------------------------------------------
  void resize_and_reseed(int n) {
    buffer_.assign(size_t(n), 0.0f);
    for (int k = 0; k < n; ++k) buffer_[k] = rng_.valuePN() * spread_->current();
    reset_algo_state();
  }

  void reseed_and_restart() {
    float p = std::clamp(perturb_->current(), 0.0f, 1.0f);
    float sp = std::clamp(spread_->current(), 0.0f, 1.0f);
    for (size_t k = 0; k < buffer_.size(); ++k) {
      if (rng_.decide(p)) buffer_[k] = rng_.valuePN() * sp;
      // else: keep existing (already sorted) value — near-sorted next pass
    }
    reset_algo_state();
  }

  void reset_algo_state() {
    i_ = 0; j_ = 0; pass_ = 0;
    if (algorithm == 0) { i_ = 1; j_ = 1; } // insertion starts at 1
    if (algorithm == 2) { i_ = 1; }         // gnome starts at 1
  }

  // -------------------------------------------------------------------------
  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> rate_;
  std::shared_ptr<ValueSource> perturb_;
  std::shared_ptr<ValueSource> spread_;
  std::shared_ptr<ValueSource> smoothness_;

  std::vector<float> buffer_;
  int i_{0}, j_{0}, pass_{0};
  int cursor_{0};
  float lastCompareDelta_{0.0f};
  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce

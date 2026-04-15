#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// MicroNNSource — a tiny 3-neuron single-layer neural network doing online
// SGD against slow sinusoidal targets. Each integration step runs ONE
// gradient update and emits a sample derived from the network state.
//
//   x_t = sin(2π * fInput * t)                    (plus optional noise)
//   y_i  = tanh(w_i * x_t + b_i)
//   tgt_i = sin(2π * fTarget_i * t + phase_i)
//   err_i = y_i - tgt_i
//   L    = 0.5 * Σ err_i²
//   ∂L/∂w_i = err_i * (1 - y_i²) * x_t
//   ∂L/∂b_i = err_i * (1 - y_i²)
//
// The network is perpetually chasing three slightly-different slow targets,
// so the weights wander in a bounded but non-trivial trajectory. Listening
// to the gradient norm, the mean activation, a single weight, or the error
// sum gives four qualitatively different sonic textures.
// ---------------------------------------------------------------------------
struct MicroNNSource final : ValueSource {
  int outputMode{0};  // 0 = grad norm, 1 = mean(y), 2 = w[0], 3 = Σ err_i
  int resetMode{0};   // 0 = never, 1 = reset weights when loss < 0.001

  MicroNNSource(int sampleRate, uint32_t seed = 0x17B4'0003u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(0.5f)),
    rate_(std::make_shared<ConstantSource>(48000.0f)),
    dt_(std::make_shared<ConstantSource>(1.0f / 48000.0f)),
    learningRate_(std::make_shared<ConstantSource>(0.01f)),
    fInput_(std::make_shared<ConstantSource>(0.3f)),
    fTarget0_(std::make_shared<ConstantSource>(1.0f)),
    fTarget1_(std::make_shared<ConstantSource>(1.5f)),
    fTarget2_(std::make_shared<ConstantSource>(2.3f)),
    noiseAmount_(std::make_shared<ConstantSource>(0.0f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    reseed_weights();
    // Give each neuron a different phase so targets aren't cophased.
    phase_[0] = 0.0f;
    phase_[1] = 1.0471975512f;  // 60°
    phase_[2] = 2.0943951024f;  // 120°
  }

  const char* type_name() const override { return "MicroNNSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",    0.5f,              0.0f,    10.0f},
      {"rate",         48000.0f,          0.0f,    480000.0f},
      {"dt",           1.0f / 48000.0f,   0.0f,    1.0f},
      {"learningRate", 0.01f,             0.0f,    1.0f},
      {"fInput",       0.3f,              0.0f,    100.0f},
      {"fTarget0",     1.0f,              0.0f,    100.0f},
      {"fTarget1",     1.5f,              0.0f,    100.0f},
      {"fTarget2",     2.3f,              0.0f,    100.0f},
      {"noiseAmount",  0.0f,              0.0f,    1.0f},
      {"smoothness",   0.0f,              0.0f,    0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")    { amplitude_    = std::move(src); return; }
    if (name == "rate")         { rate_         = std::move(src); return; }
    if (name == "dt")           { dt_           = std::move(src); return; }
    if (name == "learningRate") { learningRate_ = std::move(src); return; }
    if (name == "fInput")       { fInput_       = std::move(src); return; }
    if (name == "fTarget0")     { fTarget0_     = std::move(src); return; }
    if (name == "fTarget1")     { fTarget1_     = std::move(src); return; }
    if (name == "fTarget2")     { fTarget2_     = std::move(src); return; }
    if (name == "noiseAmount")  { noiseAmount_  = std::move(src); return; }
    if (name == "smoothness")   { smoothness_   = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")    return amplitude_;
    if (name == "rate")         return rate_;
    if (name == "dt")           return dt_;
    if (name == "learningRate") return learningRate_;
    if (name == "fInput")       return fInput_;
    if (name == "fTarget0")     return fTarget0_;
    if (name == "fTarget1")     return fTarget1_;
    if (name == "fTarget2")     return fTarget2_;
    if (name == "noiseAmount")  return noiseAmount_;
    if (name == "smoothness")   return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"outputMode", ConfigType::Int, 0.0f, 0.0f, 3.0f},
      {"resetMode",  ConfigType::Int, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "outputMode") { outputMode = std::clamp(int(value), 0, 3); return; }
    if (name == "resetMode")  { resetMode  = std::clamp(int(value), 0, 1); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "outputMode") return float(outputMode);
    if (name == "resetMode")  return float(resetMode);
    return 0.0f;
  }

  void prepare(int frames) override {
    if (amplitude_)    amplitude_->prepare(frames);
    if (rate_)         rate_->prepare(frames);
    if (dt_)           dt_->prepare(frames);
    if (learningRate_) learningRate_->prepare(frames);
    if (fInput_)       fInput_->prepare(frames);
    if (fTarget0_)     fTarget0_->prepare(frames);
    if (fTarget1_)     fTarget1_->prepare(frames);
    if (fTarget2_)     fTarget2_->prepare(frames);
    if (noiseAmount_)  noiseAmount_->prepare(frames);
    if (smoothness_)   smoothness_->prepare(frames);
  }

  float next() override {
    amplitude_->next(); rate_->next(); dt_->next();
    learningRate_->next();
    fInput_->next(); fTarget0_->next(); fTarget1_->next(); fTarget2_->next();
    noiseAmount_->next(); smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      integrate_one_step();
      opCredits_ -= 1.0f;
    }

    float raw = 0.0f;
    switch (outputMode) {
      case 0: raw = lastGradNorm_;    break;
      case 1: raw = lastMean_y_;      break;
      case 2: raw = w_[0];            break;
      case 3: raw = lastErrSum_;      break;
    }

    raw = std::clamp(raw, -5.0f, 5.0f);

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  static constexpr float kTwoPi = 6.28318530717958647692f;

  void integrate_one_step() {
    const float lr    = std::max(0.0f, learningRate_->current());
    const float dt    = dt_->current();
    const float fIn   = fInput_->current();
    const float fT0   = fTarget0_->current();
    const float fT1   = fTarget1_->current();
    const float fT2   = fTarget2_->current();
    const float noise = std::max(0.0f, noiseAmount_->current());
    const float fT[3] = { fT0, fT1, fT2 };

    float x_t = std::sin(kTwoPi * fIn * float(t_));
    if (noise > 0.0f) x_t += noise * rng_.valuePN();

    float gradSumSq = 0.0f;
    float meanY     = 0.0f;
    float errSum    = 0.0f;
    float loss      = 0.0f;

    for (int i = 0; i < 3; ++i) {
      float tgt = std::sin(kTwoPi * fT[i] * float(t_) + phase_[i]);
      float y   = std::tanh(w_[i] * x_t + b_[i]);
      float err = y - tgt;
      float dy  = 1.0f - y * y;  // tanh'(z) where y = tanh(z)
      float gw  = err * dy * x_t;
      float gb  = err * dy;

      w_[i] -= lr * gw;
      b_[i] -= lr * gb;

      // Clamp weights to keep the network bounded and audible.
      w_[i] = std::clamp(w_[i], -10.0f, 10.0f);
      b_[i] = std::clamp(b_[i], -10.0f, 10.0f);

      gradSumSq += gw * gw + gb * gb;
      meanY     += y;
      errSum    += err;
      loss      += 0.5f * err * err;
    }

    // NaN/inf guard.
    bool bad = false;
    for (int i = 0; i < 3; ++i) {
      if (!std::isfinite(w_[i]) || !std::isfinite(b_[i])) { bad = true; break; }
    }
    if (bad) {
      reseed_weights();
      gradSumSq = 0.0f;
      meanY     = 0.0f;
      errSum    = 0.0f;
      loss      = 0.0f;
    }

    lastGradNorm_ = std::sqrt(gradSumSq);
    lastMean_y_   = meanY / 3.0f;
    lastErrSum_   = errSum;

    if (resetMode == 1 && loss < 0.001f) {
      reseed_weights();
    }

    t_ += double(dt);
  }

  void reseed_weights() {
    for (int i = 0; i < 3; ++i) {
      w_[i] = rng_.valuePN() * 0.5f;
      b_[i] = 0.0f;
    }
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_, dt_;
  std::shared_ptr<ValueSource> learningRate_;
  std::shared_ptr<ValueSource> fInput_, fTarget0_, fTarget1_, fTarget2_;
  std::shared_ptr<ValueSource> noiseAmount_, smoothness_;

  float w_[3]{0.0f, 0.0f, 0.0f};
  float b_[3]{0.0f, 0.0f, 0.0f};
  float phase_[3]{0.0f, 0.0f, 0.0f};

  double t_{0.0};
  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};

  float lastGradNorm_{0.0f};
  float lastMean_y_{0.0f};
  float lastErrSum_{0.0f};
};

} // namespace mforce

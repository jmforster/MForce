#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// FitzhughNagumoSource — 1D coupled FitzHugh-Nagumo oscillators at audio rate.
//
//   dv/dt = v - v^3/3 - w + D * laplacian(v) + I_ext
//   dw/dt = eps * (v + a - b*w)
//
// A single FHN cell is a robust limit-cycle oscillator (triggered above a
// threshold of I_ext). On a 1D grid, diffusion couples neighbors: weak
// coupling gives beating/desync, strong coupling gives one big synced
// oscillator, intermediate gives travelling action potentials.
//
// dt is internal (0.1). The `rate` param = integration steps per second,
// which sets the limit-cycle frequency. Period in time-units ~ 2π / eps,
// so freq_Hz ~ rate * eps / (2π * 10)  [the 10 = 1/dt].
// ---------------------------------------------------------------------------
struct FitzhughNagumoSource final : ValueSource {
  int size{32};
  int seedMode{0};      // 0 = zero, 1 = random small, 2 = single spike
  int outputMode{0};    // 0 = v at probe, 1 = mean(v), 2 = v at probe - w at probe

  FitzhughNagumoSource(int sampleRate, uint32_t seed = 0xF417'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(0.5f)),
    rate_(std::make_shared<ConstantSource>(200000.0f)),
    a_(std::make_shared<ConstantSource>(0.7f)),
    b_(std::make_shared<ConstantSource>(0.8f)),
    eps_(std::make_shared<ConstantSource>(0.08f)),
    diffusion_(std::make_shared<ConstantSource>(0.2f)),
    I_(std::make_shared<ConstantSource>(0.5f)),
    detune_(std::make_shared<ConstantSource>(0.0f)),
    probe_(std::make_shared<ConstantSource>(0.5f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    resize_and_reseed(size);
  }

  const char* type_name() const override { return "FitzhughNagumoSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  0.5f,       0.0f,    10.0f},
      {"rate",       200000.0f,  0.0f,    2000000.0f},
      {"a",          0.7f,       0.0f,    2.0f},
      {"b",          0.8f,       0.0f,    2.0f},
      {"eps",        0.08f,      0.0f,    1.0f},
      {"diffusion",  0.2f,       0.0f,    5.0f},
      {"I",          0.5f,       -2.0f,   2.0f},
      {"detune",     0.0f,       0.0f,    1.0f},
      {"probe",      0.5f,       0.0f,    1.0f},
      {"smoothness", 0.0f,       0.0f,    0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_  = std::move(src); return; }
    if (name == "rate")       { rate_       = std::move(src); return; }
    if (name == "a")          { a_          = std::move(src); return; }
    if (name == "b")          { b_          = std::move(src); return; }
    if (name == "eps")        { eps_        = std::move(src); return; }
    if (name == "diffusion")  { diffusion_  = std::move(src); return; }
    if (name == "I")          { I_          = std::move(src); return; }
    if (name == "detune")     { detune_     = std::move(src); rebuild_detune(); return; }
    if (name == "probe")      { probe_      = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "rate")       return rate_;
    if (name == "a")          return a_;
    if (name == "b")          return b_;
    if (name == "eps")        return eps_;
    if (name == "diffusion")  return diffusion_;
    if (name == "I")          return I_;
    if (name == "detune")     return detune_;
    if (name == "probe")      return probe_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"size",       ConfigType::Int, 32.0f, 1.0f,  512.0f},
      {"seedMode",   ConfigType::Int, 0.0f,  0.0f,  2.0f},
      {"outputMode", ConfigType::Int, 0.0f,  0.0f,  2.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "size") {
      int n = std::clamp(int(value), 1, 512);
      if (n != size) { size = n; resize_and_reseed(size); }
      return;
    }
    if (name == "seedMode")   { seedMode   = std::clamp(int(value), 0, 2); reseed(); return; }
    if (name == "outputMode") { outputMode = std::clamp(int(value), 0, 2); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "size")       return float(size);
    if (name == "seedMode")   return float(seedMode);
    if (name == "outputMode") return float(outputMode);
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (amplitude_)  amplitude_->prepare(ctx, frames);
    if (rate_)       rate_->prepare(ctx, frames);
    if (a_)          a_->prepare(ctx, frames);
    if (b_)          b_->prepare(ctx, frames);
    if (eps_)        eps_->prepare(ctx, frames);
    if (diffusion_)  diffusion_->prepare(ctx, frames);
    if (I_)          I_->prepare(ctx, frames);
    if (detune_)     detune_->prepare(ctx, frames);
    if (probe_)      probe_->prepare(ctx, frames);
    if (smoothness_) smoothness_->prepare(ctx, frames);
  }

  float next() override {
    amplitude_->next(); rate_->next();
    a_->next(); b_->next(); eps_->next();
    diffusion_->next(); I_->next(); detune_->next();
    probe_->next(); smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      integrate_one_step();
      opCredits_ -= 1.0f;
    }

    const int N = int(v_.size());
    int probe = std::clamp(int(std::clamp(probe_->current(), 0.0f, 1.0f) * float(N - 1)), 0, N - 1);

    float raw = 0.0f;
    switch (outputMode) {
      case 0: raw = v_[probe]; break;
      case 1: {
        float s = 0.0f; for (float x : v_) s += x;
        raw = s / float(N); break;
      }
      case 2: raw = v_[probe] - w_[probe]; break;
    }

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  static constexpr float kDt = 0.1f;

  void integrate_one_step() {
    const int N = int(v_.size());
    const float a  = a_->current();
    const float b  = b_->current();
    const float eps = eps_->current();
    const float D  = std::max(0.0f, diffusion_->current());
    const float I  = I_->current();

    for (int i = 0; i < N; ++i) {
      float lap = 0.0f;
      if (N > 1) {
        int l = (i == 0) ? N - 1 : i - 1;
        int r = (i == N - 1) ? 0 : i + 1;
        lap = v_[l] + v_[r] - 2.0f * v_[i];
      }
      float v = v_[i];
      float w = w_[i];
      float Ii = I + detuneOffset_[i];
      float dv = v - (v*v*v)/3.0f - w + D * lap + Ii;
      float dw = eps * (v + a - b * w);
      vbuf_[i] = std::clamp(v + kDt * dv, -10.0f, 10.0f);
      wbuf_[i] = std::clamp(w + kDt * dw, -10.0f, 10.0f);
    }
    v_.swap(vbuf_);
    w_.swap(wbuf_);
  }

  void resize_and_reseed(int n) {
    v_.assign(size_t(n), 0.0f);
    w_.assign(size_t(n), 0.0f);
    vbuf_.assign(size_t(n), 0.0f);
    wbuf_.assign(size_t(n), 0.0f);
    detuneOffset_.assign(size_t(n), 0.0f);
    rebuild_detune();
    reseed();
  }

  void rebuild_detune() {
    if (!detune_) return;
    float d = std::clamp(detune_->current(), 0.0f, 1.0f);
    for (size_t i = 0; i < detuneOffset_.size(); ++i) {
      detuneOffset_[i] = rng_.valuePN() * d;
    }
  }

  void reseed() {
    const int N = int(v_.size());
    if (seedMode == 0) {
      std::fill(v_.begin(), v_.end(), 0.0f);
      std::fill(w_.begin(), w_.end(), 0.0f);
    } else if (seedMode == 1) {
      for (int i = 0; i < N; ++i) {
        v_[i] = rng_.valuePN() * 0.1f;
        w_[i] = rng_.valuePN() * 0.1f;
      }
    } else {
      std::fill(v_.begin(), v_.end(), 0.0f);
      std::fill(w_.begin(), w_.end(), 0.0f);
      v_[N / 2] = 1.5f;
    }
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_;
  std::shared_ptr<ValueSource> a_, b_, eps_, diffusion_, I_, detune_;
  std::shared_ptr<ValueSource> probe_, smoothness_;

  std::vector<float> v_, w_, vbuf_, wbuf_;
  std::vector<float> detuneOffset_;
  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce

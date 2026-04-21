#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// GrayScottSource — 1D Gray-Scott reaction-diffusion simulated at audio rate.
// Two scalar fields U, V on a periodic 1D grid; output is sampled at a
// configurable probe position.
//
//   dU/dt = Du * laplacian(U) - U*V^2 + F*(1 - U)
//   dV/dt = Dv * laplacian(V) + U*V^2 - (F + k)*V
//
// Different (F, k) regions give: extinction (silent), stripes/spots (tonal
// standing patterns), travelling waves (periodic → pitched), chaotic mixing
// (noisy). The `rate` param controls integration speed → perceived pitch.
// ---------------------------------------------------------------------------
struct GrayScottSource final : ValueSource {
  int size{256};
  int outputField{0};  // 0 = V at probe, 1 = U at probe, 2 = V - meanV (dc-blocked)
  int seedMode{0};     // 0 = center spike, 1 = random patches, 2 = uniform noise

  GrayScottSource(int sampleRate, uint32_t seed = 0x6783'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(1.0f)),
    rate_(std::make_shared<ConstantSource>(100000.0f)),
    feed_(std::make_shared<ConstantSource>(0.022f)),
    kill_(std::make_shared<ConstantSource>(0.051f)),
    diffusionU_(std::make_shared<ConstantSource>(0.16f)),
    diffusionV_(std::make_shared<ConstantSource>(0.08f)),
    probe_(std::make_shared<ConstantSource>(0.5f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    resize_and_reseed(size);
  }

  const char* type_name() const override { return "GrayScottSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  1.0f,       0.0f,    10.0f},
      {"rate",       100000.0f,  0.0f,    2000000.0f},
      {"feed",       0.022f,     0.0f,    0.15f},
      {"kill",       0.051f,     0.0f,    0.15f},
      {"diffusionU", 0.16f,      0.0f,    0.49f},
      {"diffusionV", 0.08f,      0.0f,    0.49f},
      {"probe",      0.5f,       0.0f,    1.0f},
      {"smoothness", 0.0f,       0.0f,    0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_  = std::move(src); return; }
    if (name == "rate")       { rate_       = std::move(src); return; }
    if (name == "feed")       { feed_       = std::move(src); return; }
    if (name == "kill")       { kill_       = std::move(src); return; }
    if (name == "diffusionU") { diffusionU_ = std::move(src); return; }
    if (name == "diffusionV") { diffusionV_ = std::move(src); return; }
    if (name == "probe")      { probe_      = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "rate")       return rate_;
    if (name == "feed")       return feed_;
    if (name == "kill")       return kill_;
    if (name == "diffusionU") return diffusionU_;
    if (name == "diffusionV") return diffusionV_;
    if (name == "probe")      return probe_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"size",        ConfigType::Int, 256.0f, 16.0f, 2048.0f},
      {"outputField", ConfigType::Int, 0.0f,   0.0f,  2.0f},
      {"seedMode",    ConfigType::Int, 0.0f,   0.0f,  2.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "size") {
      int n = std::clamp(int(value), 16, 2048);
      if (n != size) { size = n; resize_and_reseed(size); }
      return;
    }
    if (name == "outputField") { outputField = std::clamp(int(value), 0, 2); return; }
    if (name == "seedMode")    { seedMode    = std::clamp(int(value), 0, 2); reseed(); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "size")        return float(size);
    if (name == "outputField") return float(outputField);
    if (name == "seedMode")    return float(seedMode);
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (amplitude_)  amplitude_->prepare(ctx, frames);
    if (rate_)       rate_->prepare(ctx, frames);
    if (feed_)       feed_->prepare(ctx, frames);
    if (kill_)       kill_->prepare(ctx, frames);
    if (diffusionU_) diffusionU_->prepare(ctx, frames);
    if (diffusionV_) diffusionV_->prepare(ctx, frames);
    if (probe_)      probe_->prepare(ctx, frames);
    if (smoothness_) smoothness_->prepare(ctx, frames);
  }

  float next() override {
    amplitude_->next();
    rate_->next();
    feed_->next();
    kill_->next();
    diffusionU_->next();
    diffusionV_->next();
    probe_->next();
    smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      integrate_one_step();
      opCredits_ -= 1.0f;
    }

    int N = int(V_.size());
    int probe = std::clamp(int(std::clamp(probe_->current(), 0.0f, 1.0f) * float(N - 1)), 0, N - 1);

    float raw = 0.0f;
    switch (outputField) {
      case 0: raw = V_[probe] - 0.20f; break;
      case 1: raw = U_[probe] - 0.50f; break;
      case 2: raw = V_[probe] - meanV_; break;
    }

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  void integrate_one_step() {
    const int N = int(V_.size());
    const float F  = std::clamp(feed_->current(),       0.0f, 0.2f);
    const float k  = std::clamp(kill_->current(),       0.0f, 0.2f);
    const float Du = std::clamp(diffusionU_->current(), 0.0f, 0.49f);
    const float Dv = std::clamp(diffusionV_->current(), 0.0f, 0.49f);

    float sumV = 0.0f;
    for (int i = 0; i < N; ++i) {
      int l = (i == 0) ? N - 1 : i - 1;
      int r = (i == N - 1) ? 0 : i + 1;
      float u  = U_[i], v = V_[i];
      float lapU = U_[l] + U_[r] - 2.0f * u;
      float lapV = V_[l] + V_[r] - 2.0f * v;
      float uvv = u * v * v;
      float nu = u + Du * lapU - uvv + F * (1.0f - u);
      float nv = v + Dv * lapV + uvv - (F + k) * v;
      // Clamp to avoid explosion under pathological params
      Ubuf_[i] = std::clamp(nu, 0.0f, 1.5f);
      Vbuf_[i] = std::clamp(nv, 0.0f, 1.5f);
      sumV += Vbuf_[i];
    }
    U_.swap(Ubuf_);
    V_.swap(Vbuf_);
    meanV_ = sumV / float(N);
  }

  void resize_and_reseed(int n) {
    U_.assign(size_t(n), 1.0f);
    V_.assign(size_t(n), 0.0f);
    Ubuf_.assign(size_t(n), 0.0f);
    Vbuf_.assign(size_t(n), 0.0f);
    reseed();
  }

  void reseed() {
    const int N = int(V_.size());
    std::fill(U_.begin(), U_.end(), 1.0f);
    std::fill(V_.begin(), V_.end(), 0.0f);

    if (seedMode == 0) {
      // Center spike — classical Gray-Scott seed
      int c = N / 2;
      int halfWidth = std::max(2, N / 32);
      for (int i = c - halfWidth; i <= c + halfWidth; ++i) {
        int idx = (i + N) % N;
        U_[idx] = 0.50f;
        V_[idx] = 0.25f;
      }
    } else if (seedMode == 1) {
      // Random patches
      int patches = std::max(2, N / 40);
      int halfWidth = std::max(1, N / 64);
      for (int p = 0; p < patches; ++p) {
        int c = rng_.int_range(0, N - 1);
        for (int i = c - halfWidth; i <= c + halfWidth; ++i) {
          int idx = (i + N) % N;
          U_[idx] = 0.50f;
          V_[idx] = 0.25f + rng_.valuePN() * 0.1f;
        }
      }
    } else {
      // Uniform noise over the whole grid
      for (int i = 0; i < N; ++i) {
        U_[i] = 0.9f + rng_.valuePN() * 0.1f;
        V_[i] = 0.1f + rng_.value() * 0.2f;
      }
    }
    meanV_ = 0.0f;
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> rate_;
  std::shared_ptr<ValueSource> feed_;
  std::shared_ptr<ValueSource> kill_;
  std::shared_ptr<ValueSource> diffusionU_;
  std::shared_ptr<ValueSource> diffusionV_;
  std::shared_ptr<ValueSource> probe_;
  std::shared_ptr<ValueSource> smoothness_;

  std::vector<float> U_, V_, Ubuf_, Vbuf_;
  float opCredits_{0.0f};
  float meanV_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce

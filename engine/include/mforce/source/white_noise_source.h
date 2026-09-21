#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Sound.Source.WhiteNoiseSource
// Simple per-sample random noise (not a WaveSource — no phase accumulator needed).
//
// The legacy C# node carried four connectable inputs — Amplitude, Density,
// Boost, Continuity — used every sample:
//   if (Rand.Decide(Density)) {
//     Value = Rand.Range(Boost, 1) * Rand.Sign();
//     if (Continuity != 0) Value = Rand.Range(LastVal, Value, min(Continuity, 0.999));
//   } else Value = 0;
//   Value *= Amplitude;
// The C++ port originally shipped `amplitude` only (backlog 21). This restores
// density/boost/continuity and adds `zeroCrossTendency` (new — legacy WhiteNoise
// had none; modelled on RedNoiseSource's sign logic), all as ValueSource pins.
//
// BYTE-IDENTICAL DEFAULTS: at density=1, boost=0, continuity=0, zeroCrossTendency=0
// the node reproduces the historical single-draw output `valuePN() * amplitude`
// EXACTLY — same rng_ draw, same sequence — so every existing patch (none set
// these keys) renders bit-for-bit unchanged. The shaping logic only engages when
// a param leaves its inert default, which changes the draw structure by design.
struct WhiteNoiseSource final : ValueSource {
  explicit WhiteNoiseSource(uint32_t seed = 0x12345678u)
  : rng_(seed)
  , amplitude_(std::make_shared<ConstantSource>(1.0f))
  , density_(std::make_shared<ConstantSource>(1.0f))
  , boost_(std::make_shared<ConstantSource>(0.0f))
  , continuity_(std::make_shared<ConstantSource>(0.0f))
  , zeroCrossTendency_(std::make_shared<ConstantSource>(0.0f)) {}

  const char* type_name() const override { return "WhiteNoiseSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",         1.0f, 0.0f, 10.0f, "0-1"},
      {"density",           1.0f, 0.0f, 1.0f,  "0-1"},
      {"boost",             0.0f, 0.0f, 1.0f,  "0-1"},
      {"continuity",        0.0f, 0.0f, 1.0f,  "0-1"},
      {"zeroCrossTendency", 0.0f, 0.0f, 1.0f,  "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")         { amplitude_ = std::move(src); return; }
    if (name == "density")           { density_ = std::move(src); return; }
    if (name == "boost")             { boost_ = std::move(src); return; }
    if (name == "continuity")        { continuity_ = std::move(src); return; }
    if (name == "zeroCrossTendency") { zeroCrossTendency_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")         return amplitude_;
    if (name == "density")           return density_;
    if (name == "boost")             return boost_;
    if (name == "continuity")        return continuity_;
    if (name == "zeroCrossTendency") return zeroCrossTendency_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    amplitude_->prepare(ctx, frames);
    density_->prepare(ctx, frames);
    boost_->prepare(ctx, frames);
    continuity_->prepare(ctx, frames);
    zeroCrossTendency_->prepare(ctx, frames);
  }

  float next() override {
    amplitude_->next();
    density_->next();
    boost_->next();
    continuity_->next();
    zeroCrossTendency_->next();

    const float amp = amplitude_->current();
    const float dens = density_->current();
    const float bst  = boost_->current();
    const float cont = continuity_->current();
    const float zct  = zeroCrossTendency_->current();

    // Fast path — reproduce the historical output byte-for-byte when nothing
    // is shaping the noise. One rng_ draw, exactly as before.
    if (dens >= 1.0f && bst <= 0.0f && cont <= 0.0f && zct <= 0.0f) {
      cur_ = rng_.valuePN() * amp;
      lastVal_ = cur_;
      return cur_;
    }

    // Legacy-shaped path.
    float v;
    if (rng_.decide(dens)) {
      // Sign: random, or flipped from the last emitted value when
      // zeroCrossTendency draws (RedNoiseSource's rule; not in legacy WhiteNoise).
      if (rng_.decide(zct)) {
        lastSign_ = -lastSign_;
      } else {
        lastSign_ = float(rng_.sign());
        if (lastSign_ == 0.0f) lastSign_ = 1.0f;
      }
      // Magnitude in [boost, 1], signed. Boost excludes values near zero.
      const float b = std::clamp(bst, 0.0f, 1.0f);
      v = rng_.range(b, 1.0f) * lastSign_;
      // Continuity biases toward the previous output (min(cont, 0.999)).
      if (cont != 0.0f) {
        const float influence = std::min(cont, 0.999f);
        v = rng_.range(lastVal_, v, lastVal_, influence);
      }
    } else {
      v = 0.0f;
    }

    cur_ = v * amp;
    lastVal_ = cur_;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> density_;
  std::shared_ptr<ValueSource> boost_;
  std::shared_ptr<ValueSource> continuity_;
  std::shared_ptr<ValueSource> zeroCrossTendency_;
  float cur_{0.0f};
  float lastVal_{0.0f};
  float lastSign_{-1.0f};   // legacy starts non-zero to "get going"
};

} // namespace mforce

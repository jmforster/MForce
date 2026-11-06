#pragma once
// BrassEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/core/dsp/biquad.h"
#include "mforce/core/dsp/constants.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// BrassEvolution — lip-reed model for brass instruments (trumpet, trombone).
// The player's lips act as a pressure-controlled valve with inertia: bore
// pressure feeds back through a one-pole filter (modeling lip mass), creating
// a phase delay that enables self-oscillation at the tube resonance.
// Optional tanh nonlinearity models wave steepening in the bore — the source
// of the characteristic "brassy" bite at loud dynamics.
// ---------------------------------------------------------------------------
struct BrassEvolution final : WaveEvolution {
  // The two pins are advanced here, once per evolve() that has a loop to run
  // (an empty loop returns early and advances nothing), so they are Manual.
  Param brassiness{this, "brassiness", 1.5f, 0.0f, 10.0f, "exp", Pull::Manual};   // tanh drive → wave-steepening harmonics
  Input breath    {this, "breath", Input::Optional, "0-1", false, Pull::Manual};
  Setting<float> tubeLoss    {this, "tubeLoss",     0.01f, 0.0f, 0.5f};    // per-sample loop loss
  Setting<float> lipTension  {this, "lipTension",   0.92f, 0.0f, 0.99f};   // in-loop one-pole lowpass (0..0.99, high = dark)
  Setting<float> lipFreqRatio{this, "lipFreqRatio", 1.2f,  0.5f, 4.0f};    // out-of-loop biquad resonance, as multiple of played freq
  Setting<float> lipQ        {this, "lipQ",         3.0f,  0.5f, 15.0f};   // biquad Q (higher = sharper resonance peak)

  explicit BrassEvolution(uint32_t seed = 0xB8A5'5000u) : rng_(seed) {}

  const char* type_name() const override { return "BrassEvolution"; }

  void on_prepare(const RenderContext& ctx, int maxLen) override {
    sampleRate_ = ctx.sampleRate;
    rawLoop_.reserve(size_t(maxLen));
  }

  void adjust(float frequency) override {
    lipState_ = 0.0f;
    // Compute RBJ resonant lowpass coefficients for the out-of-loop biquad.
    // Out-of-loop means no feedback through it → no pitch detune.
    lip_.reset();
    float cutoff = std::max(20.0f, frequency * lipFreqRatio.value);
    float omega = 2.0f * dsp::kPi * cutoff / float(sampleRate_);
    if (omega > 3.0f) omega = 3.0f;
    float q = std::max(0.5f, lipQ.value);
    // Peak-normalized to unity so the biquad is a timbre shaper, not a gain stage.
    lip_.coefficients.set_resonant_lowpass(omega, q);
  }

  void shape_excitation(std::vector<float>& values) override {
    std::fill(values.begin(), values.end(), 0.0f);
    // rawLoop_ mirrors values' size; holds the unfiltered loop state so the biquad
    // sees it as input but never feeds back into the loop itself.
    rawLoop_.assign(values.size(), 0.0f);
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(rawLoop_.size());
    if (len == 0) { values[index] = 0.0f; return; }
    int next = (index + 1) % len;

    // Loop runs on internal rawLoop_; output buffer `values` is the biquad-filtered view.
    float avg = (rawLoop_[index] + rawLoop_[next]) * 0.5f;
    lipState_ = (1.0f - lipTension.value) * avg + lipTension.value * lipState_;
    float loopOut = lipState_ * (1.0f - tubeLoss.value);

    float drive = brassiness.next();
    if (drive > 0.0f) loopOut = std::tanh(drive * loopOut);

    float b = breath.next();
    float noise = rng_.valuePN();
    rawLoop_[index] = loopOut + b * noise;

    // Out-of-loop biquad for timbral shaping — colors the output without altering
    // the feedback loop, so pitch stays locked to sampleRate / tableLength.
    values[index] = lip_.process(rawLoop_[index]);
  }

private:
  Randomizer rng_;
  int sampleRate_{48000};
  float lipState_{0.0f};
  dsp::Biquad lip_;   // the out-of-loop resonant lowpass
  std::vector<float> rawLoop_;
};

} // namespace mforce

#pragma once
// BowedStringEvolution: a WaveEvolution.
#include "mforce/source/wavetable/wave_evolution.h"
#include "mforce/core/dsp/one_pole.h"
#include "mforce/core/dsp/bow_friction.h"
#include "mforce/core/dsp_wave_source.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// BowedStringEvolution — stick-slip friction model for bowed strings.
// The bow applies a friction force that depends on relative velocity between
// bow and string. At low relative velocity (stick phase), friction is high
// and drives the string. At high relative velocity (slip), friction drops.
// This produces the characteristic Helmholtz-motion sawtooth of bowed strings.
//
// Dual-waveguide bowed string (Smith/Serafin).
//   bridgeLine: carries signal through the round trip bow→bridge→bow
//   neckLine:   carries signal through the round trip bow→nut→bow
// Each buffer is 2×(one-way length) so the ring delay equals the full round
// trip; rigid-end reflection is modelled by sign-inversion on read. The two
// lines cross-couple at the bow junction (pass-through + bow impulse), which
// is what ties the combined oscillation to the fundamental period
// 2(Lb+Ln) = sampleRate / frequency.
// ---------------------------------------------------------------------------
struct BowedStringEvolution final : WaveEvolution {
  // The two pins are advanced here, once per evolve() that runs the lines (an
  // evolve() before adjust() has sized them returns early and advances
  // nothing), so they are Manual.
  Input bow         {this, "bow", Input::Optional, "0-1", false, Pull::Manual};   // bow pressure envelope (0..1)
  Param frictionGain{this, "frictionGain", 4.0f, 0.0f, 20.0f, "gain", Pull::Manual};   // slope of Friedlander bow table
  Setting<float> tubeLoss   {this, "tubeLoss",    0.005f, 0.0f,  0.5f};   // per-sample loop loss on each line
  Setting<float> bowSpeed   {this, "bowSpeed",    0.3f,   0.01f, 1.0f};   // bow velocity scale
  Setting<float> bowPosition{this, "bowPosition", 0.14f,  0.02f, 0.5f};   // bow along string (0=bridge, 0.5=middle)
  // In-loop damping: one-pole lowpass on each line's write (y += b*(x-y)),
  // the string/bridge loss filter of the reference model. Flat tubeLoss alone
  // recirculates 12 kHz as long as the fundamental, which measured as an
  // inter-harmonic noise floor AT/ABOVE harmonic energy >2 kHz (headphone
  // "sizzle") — both injected bow noise and the model's own low-note chaos
  // need frequency-dependent decay to die out. 1.0 = filter bypassed
  // entirely, byte-identical to the pre-brightness node (the discovery
  // patch's sound is locked to that). Applied per line write, so the loop
  // sees it twice per round trip.
  Setting<float> brightness {this, "brightness",  1.0f,   0.05f, 1.0f};

  const char* type_name() const override { return "BowedStringEvolution"; }

  void on_prepare(const RenderContext& ctx, int maxLen) override {
    sampleRate_ = ctx.sampleRate;
    // Each line is twice its one-way length and the two one-way lengths sum to
    // half the period, so neither buffer exceeds one period: maxLen.
    bridgeLine_.reserve(size_t(maxLen) + 2);
    neckLine_.reserve(size_t(maxLen) + 2);
  }

  void adjust(float frequency) override {
    int totalLen = int(std::lround(float(sampleRate_) / std::max(WaveSource::kMinTableHz, frequency)));
    if (totalLen < 4) totalLen = 4;
    int halfLen = std::max(2, totalLen / 2);
    int Lb = std::max(1, int(std::round(bowPosition.value * float(halfLen))));
    if (Lb >= halfLen) Lb = halfLen - 1;
    int Ln = halfLen - Lb;
    // Round-trip ring: size = 2 * one-way length.
    bridgeLine_.assign(2 * Lb, 0.0f);
    neckLine_.assign(2 * Ln, 0.0f);
    bPtr_ = 0;
    nPtr_ = 0;
    bridgeLp_.reset();
    neckLp_.reset();
  }

  void shape_excitation(std::vector<float>& values) override {
    std::fill(values.begin(), values.end(), 0.0f);
  }

  void evolve(std::vector<float>& values, int index) override {
    if (bridgeLine_.empty() || neckLine_.empty()) {
      values[index] = 0.0f;
      return;
    }

    // Incoming waves arriving at bow; sign-inverted for rigid-end reflection.
    float bridgeIn = -bridgeLine_[bPtr_];
    float neckIn   = -neckLine_[nPtr_];

    float vString = bridgeIn + neckIn;

    float pressure = bow.next();
    float gain = frictionGain.next();

    float vBow = bowSpeed.value * pressure;
    float vRel = vBow - vString;

    // Friedlander friction table.
    float fricCoef = dsp::bow_friction(vRel, gain, 0.0f, 0.0f, dsp::kBowFrictionMax);

    float bowImpulse = fricCoef * vRel * pressure;

    // Scattering at bow: wave from neck side passes through to bridge side (and
    // vice versa), each receives the bow kick. Cross-coupling couples the two
    // lines so they resonate as one string at the fundamental.
    float outBridge = (neckIn   + bowImpulse) * (1.0f - tubeLoss.value);
    float outNeck   = (bridgeIn + bowImpulse) * (1.0f - tubeLoss.value);

    if (brightness.value < 1.0f) {
      outBridge = bridgeLp_.process(outBridge, brightness.value);
      outNeck = neckLp_.process(outNeck, brightness.value);
    }

    bridgeLine_[bPtr_] = outBridge;
    neckLine_[nPtr_]   = outNeck;

    bPtr_ = (bPtr_ + 1) % int(bridgeLine_.size());
    nPtr_ = (nPtr_ + 1) % int(neckLine_.size());

    values[index] = vString;
  }

private:
  int sampleRate_{48000};
  std::vector<float> bridgeLine_;
  std::vector<float> neckLine_;
  int bPtr_{0};
  int nPtr_{0};
  dsp::OnePole bridgeLp_;
  dsp::OnePole neckLp_;
};

} // namespace mforce

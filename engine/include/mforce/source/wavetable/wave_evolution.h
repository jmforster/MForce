#pragma once
#include "mforce/core/dsp/tuning_allpass.h"
#include "mforce/core/dsp/one_pole.h"
#include "mforce/core/dsp/bow_friction.h"
#include "mforce/core/dsp/biquad.h"
#include "mforce/core/dsp/constants.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstdint>

namespace mforce {

// The base of the wave evolutions (the C# IWaveEvolution): a part the
// wavetable holds in its `evolution` slot and drives — prepare once per
// build, start once per note, adjust at a note's first sample,
// shape_excitation after the fill, evolve once per sample. An evolution's
// pins are Manual (it advances them inside evolve, in its own order) and are
// prepared and started here, once for every member of the family.
struct WaveEvolution : Component {
  void prepare(const RenderContext& ctx, int maxLen) {
    for (auto* pin : pins()) pin->prepare(ctx);
    on_prepare(ctx, maxLen);
  }
  void start(int frames) {
    for (auto* pin : pins()) pin->start(frames);
  }

  // Called once per note, at the first sample — adjust internal state to the
  // note's frequency.
  virtual void adjust(float frequency) = 0;

  // Called once per note after the table fill — modify the initial excitation.
  // Default: no-op. Override for pick-position, pick-direction, etc.
  virtual void shape_excitation(std::vector<float>& /*values*/) {}

  // Called each sample — modify the wavetable in-place.
  virtual void evolve(std::vector<float>& values, int index) = 0;

protected:
  // maxLen is the longest table the owner will ever fill (its floor
  // frequency's length). An implementation captures ctx.sampleRate here if
  // adjust()/evolve() need it, and reserves every buffer it sizes per note or
  // per table to maxLen, so that adjust, shape_excitation and evolve fill
  // within capacity and allocate nothing.
  virtual void on_prepare(const RenderContext& /*ctx*/, int /*maxLen*/) {}
};

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.PluckEvolution
// Modified Karplus-Strong: frequency-dependent averaging window + speed.
// ---------------------------------------------------------------------------
struct PluckEvolution final : WaveEvolution {
  Setting<float> muting{this, "muting", 0.0f, 0.0f, 1.0f};

  explicit PluckEvolution(uint32_t seed = 0xDEAD'BEEFu) : rng_(seed) {}

  const char* type_name() const override { return "PluckEvolution"; }

  void adjust(float frequency) override {
    // SampleCount = Round(Max(24 - 4*log10(f)^2, 2)) + muting*20
    double logF = std::log10(double(frequency));
    sampleCount_ = float(std::round(std::max(24.0 - 4.0 * logF * logF, 2.0))) + muting.value * 20.0f;

    // Speed
    speed_ = 1.0f + muting.value * 2.0f;
    if (frequency < 674.0f) {
      speed_ = float(std::max(9.0 - std::pow(logF, 2.0), 1.0)) + muting.value * 4.0f;
    } else if (frequency > 880.0f) {
      speed_ = float(std::pow(8.67 / std::pow(logF, 2.0), 4.0)) + muting.value * 4.0f;
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    int loops = rng_.floorOrCeiling(speed_);
    int len = int(values.size());

    for (int i = 0; i < loops; ++i) {
      average(values, (index + i) % len);
    }

    if (speed_ < 1.0f) {
      speed_ *= 1.001f;  // legacy: slowly accelerate sub-1 speeds
    }
  }

private:
  void average(std::vector<float>& values, int index) {
    int sampCount = rng_.floorOrCeiling(sampleCount_);
    if (sampCount < 1) sampCount = 1;

    int len = int(values.size());
    float sum = 0.0f;
    for (int i = 0; i < sampCount; ++i) {
      sum += values[(index + i) % len];
    }
    values[index] = sum / float(sampCount);
  }

  float sampleCount_{2.0f};
  float speed_{1.0f};
  Randomizer rng_;
};

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.AveragingEvolution
// Configurable averaging with decay factor, leading/trailing direction.
// ---------------------------------------------------------------------------
struct AveragingEvolution final : WaveEvolution {
  Setting<float> sampleCount{this, "sampleCount", 4.0f,   1.0f, 48.0f};
  Setting<float> speed      {this, "speed",       1.0f,   0.1f, 20.0f};
  Setting<float> decayFactor{this, "decayFactor", 0.999f, 0.9f, 1.0f};
  Setting<bool>  leading    {this, "leading",     false,  false, true};
  Setting<bool>  autoAdjust {this, "autoAdjust",  false,  false, true};

  explicit AveragingEvolution(uint32_t seed = 0xCAFE'BABEu) : rng_(seed) {}

  const char* type_name() const override { return "AveragingEvolution"; }

  void adjust(float frequency) override {
    sampleCount_ = sampleCount.value;
    decayFactor_ = decayFactor.value;

    if (autoAdjust.value) {
      if (frequency < 110.0f) {
        decayFactor_ = 1.0f - (1.0f - decayFactor.value) *
                       (1.0f + std::max(110.0f - frequency, 0.0f) / 10.0f);
      }
      if (frequency > 220.0f) {
        decayFactor_ = 1.0f - (1.0f - decayFactor.value) *
                       std::max(1.0f - (frequency - 220.0f) / 880.0f, 0.0f);
      }
      if (frequency > 440.0f) {
        sampleCount_ = float(sampleCount.value -
          ((sampleCount.value - 1.0f) - (sampleCount.value - 1.0f) *
           std::exp(-0.0008 * double(frequency - 440.0f))));
      }
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    int loops = rng_.floorOrCeiling(speed.value);
    for (int i = 0; i < loops; ++i) {
      average(values, index);
    }
  }

private:
  void average(std::vector<float>& values, int index) {
    int sampCount = rng_.floorOrCeiling(sampleCount_);
    if (sampCount < 1) return;

    int len = int(values.size());
    float sum = 0.0f;
    for (int i = 0; i < sampCount; ++i) {
      if (leading.value) {
        sum += values[(index + i) % len];
      } else {
        int idx = index - i;
        if (idx < 0) idx += len;
        sum += values[idx];
      }
    }
    values[index] = (sum / float(sampCount)) * decayFactor_;
  }

  float sampleCount_{2.0f};
  float decayFactor_{1.0f};
  Randomizer rng_;
};

// ---------------------------------------------------------------------------
// TargetEvolution — morphing Karplus-Strong.
// Instead of averaging toward silence (standard KS), averages toward a
// target waveform. The table continuously evolves from noisy excitation
// toward the target shape. NOT a crossfade — the table is modified in-place,
// so each cycle derives from the previous one by continuous transformation.
// The target is one period synthesised at prepare from harmonic amplitudes
// (fundamental first), peak-normalised, and resampled to each note's table
// length; holdCycles of plain averaging precede the morph.
// ---------------------------------------------------------------------------
struct TargetEvolution final : WaveEvolution {
  Array          targetPartials{this, "targetPartials", nullptr, 0.0f, -1.0f, 1.0f};   // harmonic amplitudes of the target, fundamental first
  Setting<int>   targetLength  {this, "targetLength",   1024, 16, 8192};              // samples in the synthesised period
  Setting<float> morphRate     {this, "morphRate",      0.001f, 0.0001f, 0.1f};       // how fast the morph progresses (0.0001 = slow, 0.01 = fast)
  Setting<int>   holdCycles    {this, "holdCycles",     3, 0, 100};                   // full table cycles of pure KS before the morph begins
  Setting<float> decayFactor   {this, "decayFactor",    0.998f, 0.9f, 1.0f};

  TargetEvolution() { targetPartials.values = {1.0f}; }   // a sine until told otherwise

  const char* type_name() const override { return "TargetEvolution"; }

  void on_prepare(const RenderContext&, int maxLen) override {
    synthesise_target();
    resampledTarget_.reserve(size_t(maxLen));
  }
  void adjust(float) override {
    sampleCount_ = 0;
    morphAmount_ = 0.0f;
    cycleCount_ = 0;
    tableLen_ = 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());

    // Track cycles (one full pass through the table)
    if (tableLen_ != len) {
      tableLen_ = len;
      resample_target(len);
    }

    sampleCount_++;
    if (sampleCount_ >= len) {
      sampleCount_ = 0;
      cycleCount_++;
    }

    // Standard KS averaging (2-sample)
    int next = (index + 1) % len;
    float averaged = (values[index] + values[next]) * 0.5f;

    if (cycleCount_ < holdCycles.value || target_.empty()) {
      // Pure KS phase — organic attack (and the whole note when there is no target)
      values[index] = averaged * decayFactor.value;
    } else {
      // Morph phase — converge toward target
      morphAmount_ = std::min(morphAmount_ + morphRate.value, 1.0f);
      float t = resampledTarget_[index];
      values[index] = averaged * (1.0f - morphAmount_) + t * morphAmount_;
      // Slight decay prevents energy buildup during transition
      values[index] *= (1.0f - (1.0f - decayFactor.value) * (1.0f - morphAmount_));
    }
  }

private:
  // One period from the harmonic amplitudes, peak-normalised: a sum of sines
  // in float. Empty partials or a non-positive length leave no target.
  void synthesise_target() {
    const int tLen = targetLength.value;
    const std::vector<float>& partials = targetPartials.values;
    target_.clear();
    if (partials.empty() || tLen <= 0) return;
    target_.assign(size_t(tLen), 0.0f);
    constexpr float TAU = 2.0f * dsp::kPi;
    for (int i = 0; i < tLen; ++i) {
      float pos = float(i) / float(tLen);
      for (int h = 0; h < int(partials.size()); ++h)
        target_[i] += partials[h] * std::sin(pos * TAU * float(h + 1));
    }
    float peak = 0.0f;
    for (float v : target_) peak = std::max(peak, std::fabs(v));
    if (peak > 0.0f) for (float& v : target_) v /= peak;
  }
  // The target at the note's table length, linearly interpolated, within the
  // capacity prepare reserved.
  void resample_target(int len) {
    if (target_.empty()) return;
    resampledTarget_.resize(len);
    for (int i = 0; i < len; ++i) {
      float pos = float(i) / float(len) * float(target_.size());
      int idx0 = int(pos) % int(target_.size());
      int idx1 = (idx0 + 1) % int(target_.size());
      float frac = pos - std::floor(pos);
      resampledTarget_[i] = target_[idx0] + (target_[idx1] - target_[idx0]) * frac;
    }
  }

  std::vector<float> target_;            // the synthesised period
  std::vector<float> resampledTarget_;   // target_ at the note's table length

  float morphAmount_{0.0f};
  int sampleCount_{0};
  int cycleCount_{0};
  int tableLen_{0};
};

// ---------------------------------------------------------------------------
// EKSEvolution — Extended Karplus-Strong (Jaffe & Smith 1983).
// Adds pick position, pick direction, string stiffness, decay stretch,
// and drum blend to the basic KS averaging loop.
// ---------------------------------------------------------------------------
struct EKSEvolution final : WaveEvolution {
  Setting<float> pickPosition {this, "pickPosition",  0.0f, 0.0f, 1.0f};    // 0=bridge, 0.5=middle (removes even harmonics), 1=nut
  Setting<float> pickDirection{this, "pickDirection", 0.0f, 0.0f, 0.95f};   // 0=down, higher=more up-pick filtering
  Setting<float> stiffness    {this, "stiffness",     0.0f, 0.0f, 0.1f};    // string inharmonicity (allpass dispersion)
  Setting<float> decayStretch {this, "decayStretch",  0.5f, 0.0f, 1.0f};    // 0..1, 0.5=standard KS averaging
  Setting<float> drumBlend    {this, "drumBlend",     1.0f, 0.0f, 1.0f};    // 1=string, 0.5=snare, 0=pure drum

  explicit EKSEvolution(uint32_t seed = 0xEE45'0000u) : rng_(seed) {}

  const char* type_name() const override { return "EKSEvolution"; }

  void on_prepare(const RenderContext&, int maxLen) override { filtered_.reserve(size_t(maxLen)); }
  void adjust(float frequency) override {
    freq_ = frequency;

    // Stiffness allpass coefficient: higher frequency = more dispersion
    float coeff = 0.0f;
    if (stiffness.value > 0.0f) {
      // Approximate: coefficient grows with harmonic number
      // Use a simple model: C = stiffness * (freq/1000)^0.5
      coeff = stiffness.value * std::sqrt(freq_ / 1000.0f);
      coeff = std::clamp(coeff, 0.0f, 0.9f);
    }
    stiffness_.set_coefficient(coeff);
    stiffness_.reset();
  }

  void shape_excitation(std::vector<float>& values) override {
    int len = int(values.size());
    if (len < 2) return;

    // Pick-position comb filter: H(z) = 1 - z^(-floor(beta*N + 0.5))
    // Removes harmonics at multiples of 1/beta
    if (pickPosition.value > 0.001f && pickPosition.value < 0.999f) {
      int delay = int(std::floor(pickPosition.value * float(len) + 0.5f));
      if (delay >= 1 && delay < len) {
        filtered_.assign(size_t(len), 0.0f);   // within the capacity prepare reserved
        for (int i = 0; i < len; ++i) {
          int di = i - delay;
          if (di < 0) di += len;
          filtered_[i] = values[i] - values[di];
        }
        std::copy(filtered_.begin(), filtered_.end(), values.begin());   // never a new buffer for the owner's table
      }
    }

    // Pick-direction lowpass: H(z) = (1-p) / (1 - p*z^-1)
    if (pickDirection.value > 0.001f) {
      float p = std::clamp(pickDirection.value, 0.0f, 0.95f);
      float g = 1.0f - p;
      float prev = 0.0f;
      for (int i = 0; i < len; ++i) {
        float y = g * values[i] + p * prev;
        prev = y;
        values[i] = y;
      }
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());
    if (len < 2) return;

    int next = (index + 1) % len;

    // Weighted average (decay stretch): y = (1-S)*v[n] + S*v[n+1]
    float avg = (1.0f - decayStretch.value) * values[index] + decayStretch.value * values[next];

    // Drum blend: with probability (1-drumBlend)/2, flip sign
    if (drumBlend.value < 1.0f) {
      float flipProb = (1.0f - drumBlend.value) * 0.5f;
      if (rng_.value() < flipProb) {
        avg = -avg;
      }
    }

    // String stiffness allpass: y[n] = C*x[n] + x[n-1] - C*y[n-1]
    if (stiffness_.coefficient() > 0.0f) avg = stiffness_.process(avg);

    values[index] = avg;
  }

private:
  Randomizer rng_;
  std::vector<float> filtered_;   // the pick-position comb's scratch, reserved at prepare
  float freq_{440.0f};
  dsp::TuningAllpass stiffness_;   // the string's dispersion
};

// ---------------------------------------------------------------------------
// ReedEvolution — reed-valve resonator (clarinet / sax).
// Ring-buffer-as-tube structure with reed nonlinearity. reedStiffness
// drives a tanh shaper on the bore signal → odd/asymmetric harmonics; breath
// is injected via a zero-mean noise term that seeds and sustains oscillation.
// ---------------------------------------------------------------------------
struct ReedEvolution final : WaveEvolution {
  // The two pins are advanced here, once per evolve(), so they are Manual.
  Param reedStiffness{this, "reedStiffness", 1.5f, 0.0f, 8.0f, "exp", Pull::Manual};   // tanh drive on bore signal (0 = linear tube)
  Input breath       {this, "breath", Input::Optional, "0-1", false, Pull::Manual};
  Setting<float> tubeLoss  {this, "tubeLoss",   0.02f, 0.0f,  0.5f};   // per-sample loop loss
  Setting<float> loopFilter{this, "loopFilter", 0.5f,  0.05f, 1.0f};   // one-pole loop lowpass coefficient (0..1; higher = brighter)

  explicit ReedEvolution(uint32_t seed = 0xBEED'BEEDu) : rng_(seed) {}

  const char* type_name() const override { return "ReedEvolution"; }

  void adjust(float /*frequency*/) override {
    lpState_ = 0.0f;
  }

  void shape_excitation(std::vector<float>& values) override {
    std::fill(values.begin(), values.end(), 0.0f);
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());
    int next = (index + 1) % len;

    float avg = (values[index] + values[next]) * 0.5f;

    // One-pole lowpass on the loop; kills the square-wave buzz that tanh alone produces.
    lpState_ = loopFilter.value * avg + (1.0f - loopFilter.value) * lpState_;
    float loopOut = lpState_ * (1.0f - tubeLoss.value);

    float drive = reedStiffness.next();
    if (drive > 0.0f) loopOut = std::tanh(drive * loopOut);

    float b = breath.next();
    float noise = rng_.valuePN();
    values[index] = loopOut + b * noise;
  }

private:
  Randomizer rng_;
  float lpState_{0.0f};
};

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

  BowedStringEvolution() = default;

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

// ===========================================================================
// Algorithmic evolutions — abstract transforms (not physical models).
// All update one sample per evolve() call at the given index.
// ===========================================================================

// ---------------------------------------------------------------------------
// ReactionDiffusionEvolution — Gray-Scott reaction-diffusion 1D.
// Two coupled chemicals u, v diffuse along the table and react. Tuning
// (feed, kill, Du, Dv) selects regimes: spots, stripes, mitosis, U-skate,
// chaos. v is read out as the audio sample (scaled to -1..1).
//
// Implementation: a snapshot of (u, v) is taken at each cycle boundary
// (index==0). Within a cycle, each evolve(N) reads the snapshot's neighbours
// and writes the new (u[N], v[N]); the snapshot is refreshed next cycle.
// This decouples per-sample updates from the diffusion stencil so the
// dynamics behave like proper synchronous RD rather than a propagating wave.
// ---------------------------------------------------------------------------
struct ReactionDiffusionEvolution final : WaveEvolution {
  Setting<float> feed    {this, "feed",     0.0367f, 0.0f,  0.1f};   // canonical "mitosis": 0.0367 / 0.0649
  Setting<float> kill    {this, "kill",     0.0649f, 0.0f,  0.1f};
  Setting<float> diffU   {this, "diffU",    0.16f,   0.0f,  1.0f};
  Setting<float> diffV   {this, "diffV",    0.08f,   0.0f,  1.0f};
  Setting<float> dt      {this, "dt",       1.0f,    0.05f, 2.0f};   // integration step
  Setting<int>   subSteps{this, "subSteps", 1,       1,     8};      // RD iterations per audio sample (>=1)

  ReactionDiffusionEvolution() = default;

  const char* type_name() const override { return "ReactionDiffusionEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "subSteps") subSteps.value = std::max(1, subSteps.value);
  }

  void on_prepare(const RenderContext&, int maxLen) override {
    for (auto* v : {&u_, &v_, &uPrev_, &vPrev_}) v->reserve(size_t(maxLen));
  }
  void adjust(float /*frequency*/) override {}

  void shape_excitation(std::vector<float>& values) override {
    const int len = int(values.size());
    u_.assign(len, 1.0f);
    v_.assign(len, 0.0f);
    uPrev_.assign(len, 1.0f);
    vPrev_.assign(len, 0.0f);
    // Seed v with a small perturbation derived from the initial excitation.
    // Anything non-zero in v will get the reaction going.
    for (int i = 0; i < len; ++i) {
      v_[i]     = 0.25f * std::abs(values[i]);
      vPrev_[i] = v_[i];
    }
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 3) { values[index] = 0.0f; return; }

    // Refresh snapshot at the start of each cycle.
    if (index == 0) {
      uPrev_ = u_;
      vPrev_ = v_;
    }

    const int im = (index - 1 + len) % len;
    const int ip = (index + 1) % len;

    float u = u_[index];
    float v = v_[index];
    for (int s = 0; s < subSteps.value; ++s) {
      const float lapU = uPrev_[im] - 2.0f * u + uPrev_[ip];
      const float lapV = vPrev_[im] - 2.0f * v + vPrev_[ip];
      const float uvv  = u * v * v;
      u += dt.value * (diffU.value * lapU - uvv + feed.value * (1.0f - u));
      v += dt.value * (diffV.value * lapV + uvv - (feed.value + kill.value) * v);
    }
    u = std::clamp(u, 0.0f, 1.0f);
    v = std::clamp(v, 0.0f, 1.0f);
    u_[index] = u;
    v_[index] = v;

    // Centre v around zero so the wavetable swings ±. v sits in 0..1 but
    // tends to fill a sub-range, so use 2v-1 as a starting point and let
    // any drift produce DC the source can high-pass downstream.
    values[index] = 2.0f * v - 1.0f;
  }

private:
  std::vector<float> u_;
  std::vector<float> v_;
  std::vector<float> uPrev_;
  std::vector<float> vPrev_;
};

// ---------------------------------------------------------------------------
// SortErosionEvolution — distributed bubble-sort toward a sorted (saw) shape.
// Each evolve(N) does `swapsPerSample` adjacent compare-swap steps starting
// at a rotating pointer. Eventually the table converges to a monotonic ramp.
// `descending` flips the sort direction.
// ---------------------------------------------------------------------------
struct SortErosionEvolution final : WaveEvolution {
  Setting<int>  swapsPerSample{this, "swapsPerSample", 4, 1, 32};
  Setting<bool> descending    {this, "descending", false, false, true};

  SortErosionEvolution() = default;

  const char* type_name() const override { return "SortErosionEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "swapsPerSample") swapsPerSample.value = std::max(1, swapsPerSample.value);
  }

  void adjust(float /*frequency*/) override {
    pos_ = 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2) return;
    for (int s = 0; s < swapsPerSample.value; ++s) {
      const int a = pos_;
      const int b = (pos_ + 1) % len;
      const bool out_of_order =
        descending.value ? (values[a] < values[b]) : (values[a] > values[b]);
      if (out_of_order) std::swap(values[a], values[b]);
      pos_ = (pos_ + 1) % len;
    }
    (void)index;
  }

private:
  int pos_{0};
};

// ---------------------------------------------------------------------------
// CellularAutomatonEvolution — binary 1D Wolfram CA on the wavetable.
// The wavetable is threshold-quantized to bits each cycle, and a Wolfram
// rule (0..255) is applied to each cell from its three-cell neighbourhood.
// Bits flip back to ± waveform via `levelHi` / `levelLo`. Famous rules:
// 30 (chaotic), 90 (Sierpinski), 110 (edge-of-chaos), 184 (traffic).
// ---------------------------------------------------------------------------
struct CellularAutomatonEvolution final : WaveEvolution {
  Setting<int>   rule     {this, "rule",      110,   0,     255};
  Setting<float> threshold{this, "threshold", 0.0f,  -1.0f, 1.0f};   // discretization threshold (sample > threshold → 1)
  Setting<float> levelHi  {this, "levelHi",   0.8f,  0.0f,  1.0f};
  Setting<float> levelLo  {this, "levelLo",   -0.8f, -1.0f, 0.0f};
  Setting<int>   stepEvery{this, "stepEvery", 8,     1,     256};    // do one CA step every Nth sample (slow tempo)

  CellularAutomatonEvolution() = default;

  const char* type_name() const override { return "CellularAutomatonEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "rule")           rule.value = std::clamp(rule.value, 0, 255);
    else if (name == "stepEvery") stepEvery.value = std::max(1, stepEvery.value);
  }

  void on_prepare(const RenderContext&, int maxLen) override {
    state_.reserve(size_t(maxLen));
    snapshot_.reserve(size_t(maxLen));
  }
  void adjust(float /*frequency*/) override {
    sampleCtr_ = 0;
  }

  void shape_excitation(std::vector<float>& values) override {
    const int len = int(values.size());
    state_.assign(len, 0);
    snapshot_.assign(len, 0);
    for (int i = 0; i < len; ++i) state_[i] = (values[i] > threshold.value) ? 1 : 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 3) return;

    if (state_.size() != size_t(len)) {
      state_.assign(len, 0);
      snapshot_.assign(len, 0);
      for (int i = 0; i < len; ++i) state_[i] = (values[i] > threshold.value) ? 1 : 0;
    }

    // Snapshot at cycle boundary so neighbours are read from the
    // previous cycle's complete state, not from cells already mutated
    // earlier in this cycle. Without this, asynchronous-CA dynamics
    // collapse to uniform output very fast.
    if (index == 0) snapshot_ = state_;

    // Tempo control: only do CA work every Nth sample.
    sampleCtr_++;
    if (sampleCtr_ >= stepEvery.value) {
      sampleCtr_ = 0;
      const int im = (index - 1 + len) % len;
      const int ip = (index + 1) % len;
      const uint8_t pat = uint8_t((snapshot_[im] << 2) | (snapshot_[index] << 1) | snapshot_[ip]);
      state_[index] = uint8_t((rule.value >> pat) & 1u);
    }
    values[index] = state_[index] ? levelHi.value : levelLo.value;
  }

private:
  std::vector<uint8_t> state_;
  std::vector<uint8_t> snapshot_;
  int sampleCtr_{0};
};

// ---------------------------------------------------------------------------
// HistogramEqualizeEvolution — gradually pull the wavetable toward a
// uniform amplitude distribution via histogram equalization. Each cycle,
// a histogram + CDF are rebuilt over the current table. Each sample is
// then nudged toward CDF(sample)*2-1 by `rate`. Convergence: sine → tri-ish,
// noise → roughly uniformly distributed waveform.
// ---------------------------------------------------------------------------
struct HistogramEqualizeEvolution final : WaveEvolution {
  Setting<int>   bins{this, "bins", 64,       4,    256};
  Setting<float> rate{this, "rate", 0.00005f, 0.0f, 0.1f};

  HistogramEqualizeEvolution() = default;

  const char* type_name() const override { return "HistogramEqualizeEvolution"; }

  void on_setting_changed(std::string_view name) override {
    if (name == "bins") bins.value = std::clamp(bins.value, 4, 256);
  }

  void on_prepare(const RenderContext&, int) override {
    cdf_.assign(size_t(std::max(2, bins.value)), 0.0f);
    counts_.assign(size_t(std::max(2, bins.value)), 0);
  }
  void adjust(float /*frequency*/) override {
    cdf_.assign(std::max(2, bins.value), 0.0f);
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2 || bins.value < 2) return;

    // Rebuild CDF at the start of each cycle.
    if (index == 0) rebuild_cdf(values);

    // Map current sample value to a bin and look up its CDF target.
    const int b = sample_to_bin(values[index]);
    const float target = cdf_[b] * 2.0f - 1.0f;
    values[index] += rate.value * (target - values[index]);
  }

private:
  int sample_to_bin(float v) const {
    const float t = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f; // 0..1
    int b = int(t * float(bins.value));
    if (b >= bins.value) b = bins.value - 1;
    return b;
  }

  void rebuild_cdf(const std::vector<float>& values) {
    if (int(cdf_.size()) != bins.value) cdf_.assign(bins.value, 0.0f);
    counts_.assign(size_t(bins.value), 0);   // within the capacity prepare reserved
    for (float v : values) counts_[sample_to_bin(v)]++;
    const float invN = 1.0f / float(values.size());
    float running = 0.0f;
    for (int i = 0; i < bins.value; ++i) {
      running += float(counts_[i]) * invN;
      cdf_[i] = running;
    }
  }

  std::vector<float> cdf_;
  std::vector<int>   counts_;   // rebuild_cdf's histogram, sized at prepare
};

// ---------------------------------------------------------------------------
// BezierPullEvolution — pull each sample toward a cubic Bezier curve
// defined by four control values p0..p3 over t in [0,1]. The Bezier is
// resampled to the table length once per note, then each sample drifts
// toward its target by `rate`. Endpoint: the table becomes the Bezier shape.
// ---------------------------------------------------------------------------
struct BezierPullEvolution final : WaveEvolution {
  Setting<float> p0  {this, "p0",   -1.0f,    -2.0f, 2.0f};
  Setting<float> p1  {this, "p1",    0.5f,    -2.0f, 2.0f};
  Setting<float> p2  {this, "p2",   -0.5f,    -2.0f, 2.0f};
  Setting<float> p3  {this, "p3",    1.0f,    -2.0f, 2.0f};
  Setting<float> rate{this, "rate",  0.00005f, 0.0f, 0.1f};

  BezierPullEvolution() = default;

  const char* type_name() const override { return "BezierPullEvolution"; }

  void on_prepare(const RenderContext&, int maxLen) override { target_.reserve(size_t(maxLen)); }
  void adjust(float /*frequency*/) override {
    needsRebuild_ = true;
  }

  void shape_excitation(std::vector<float>& values) override {
    rebuild_target(int(values.size()));
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2) return;
    if (needsRebuild_ || int(target_.size()) != len) rebuild_target(len);
    values[index] += rate.value * (target_[index] - values[index]);
  }

private:
  void rebuild_target(int len) {
    target_.resize(len);
    for (int i = 0; i < len; ++i) {
      const float t = float(i) / float(len);
      const float u = 1.0f - t;
      target_[i] = u*u*u*p0.value + 3.0f*u*u*t*p1.value + 3.0f*u*t*t*p2.value + t*t*t*p3.value;
    }
    needsRebuild_ = false;
  }

  std::vector<float> target_;
  bool needsRebuild_{true};
};

// ---------------------------------------------------------------------------
// BitRotateEvolution — bit-level glitch transform. Each sample is mapped
// to a 32-bit unsigned int via fixed-point, its bits rotated by `shiftBits`,
// then mapped back. `stepEvery` controls how often a sample is rotated (so
// only a fraction of the table mutates per cycle). Endpoint: pseudo-random
// scramble of the original — aliasing-laden, hard-edged.
// ---------------------------------------------------------------------------
struct BitRotateEvolution final : WaveEvolution {
  // The two pins are advanced here every evolve() call (= once per output
  // sample, stepEvery first) so a wired envelope advances on the note's
  // sample clock even between rotations; they are Manual.
  Param shiftBits{this, "shiftBits", 3.0f, 0.0f, 31.0f,  "int", Pull::Manual};   // rotation depth, int 0..31
  Param stepEvery{this, "stepEvery", 4.0f, 1.0f, 256.0f, "int", Pull::Manual};   // samples between rotations, int >= 1

  BitRotateEvolution() = default;

  const char* type_name() const override { return "BitRotateEvolution"; }

  void adjust(float /*frequency*/) override {
    ctr_ = 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int step = std::max(1, int(stepEvery.next()));
    const int bits = int(shiftBits.next());
    ctr_++;
    if (ctr_ < step) return;
    ctr_ = 0;

    const int k = ((bits % 32) + 32) % 32;
    if (k == 0) return;

    const float v = std::clamp(values[index], -1.0f, 1.0f);
    const uint32_t u = uint32_t((double(v) + 1.0) * 0.5 * 4294967295.0);
    const uint32_t rot = (u >> k) | (u << (32 - k));
    values[index] = float(double(rot) / 4294967295.0 * 2.0 - 1.0);
  }

private:
  int ctr_{0};
};

} // namespace mforce

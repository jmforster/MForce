#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>
#include <cstdint>

namespace mforce {

// Ported from C# MForce.Sound.Source.IWaveEvolution
struct WaveEvolution {
  virtual ~WaveEvolution() = default;

  // Called once per note — adjust internal state based on frequency.
  virtual float adjust(float frequency) = 0;

  // Called once after table fill — modify the initial excitation.
  // Default: no-op. Override for pick-position, pick-direction, etc.
  virtual void shape_excitation(std::vector<float>& /*values*/) {}

  // Called each sample — modify the wavetable in-place.
  virtual void evolve(std::vector<float>& values, int index) = 0;

  // Called by the holder during prepare(). SR-sensitive subclasses override
  // to capture ctx.sampleRate for use by adjust()/evolve(). Was previously
  // set_sample_rate(int), called once from WavetableSource::prepare; now
  // receives the full RenderContext so future fields (block size, etc.)
  // propagate without another API change.
  virtual void on_prepare(const RenderContext& /*ctx*/) {}
};

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.PluckEvolution
// Modified Karplus-Strong: frequency-dependent averaging window + speed.
// ---------------------------------------------------------------------------
struct PluckEvolution final : WaveEvolution {
  explicit PluckEvolution(float muting, uint32_t seed = 0xDEAD'BEEFu)
  : muting_(muting), rng_(seed) {}

  float adjust(float frequency) override {
    // SampleCount = Round(Max(24 - 4*log10(f)^2, 2)) + muting*20
    double logF = std::log10(double(frequency));
    sampleCount_ = float(std::round(std::max(24.0 - 4.0 * logF * logF, 2.0))) + muting_ * 20.0f;

    // Speed
    speed_ = 1.0f + muting_ * 2.0f;
    if (frequency < 674.0f) {
      speed_ = float(std::max(9.0 - std::pow(logF, 2.0), 1.0)) + muting_ * 4.0f;
    } else if (frequency > 880.0f) {
      speed_ = float(std::pow(8.67 / std::pow(logF, 2.0), 4.0)) + muting_ * 4.0f;
    }

    // Frequency adjustment
    freqAdj_ = -(sampleCount_ / 24.0f) * float(logF - 2.0) * 0.15f;
    return freqAdj_;
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

  float muting_{0.0f};
  float sampleCount_{2.0f};
  float speed_{1.0f};
  float freqAdj_{0.0f};
  Randomizer rng_;
};

// ---------------------------------------------------------------------------
// Ported from C# MForce.Sound.Source.AveragingEvolution
// Configurable averaging with decay factor, leading/trailing direction.
// ---------------------------------------------------------------------------
struct AveragingEvolution final : WaveEvolution {
  AveragingEvolution(float sampleCount, float speed, float decayFactor,
                     bool leading = false, bool autoAdjust = false,
                     uint32_t seed = 0xCAFE'BABEu)
  : baseSampleCount_(sampleCount), speed_(speed), baseDecayFactor_(decayFactor),
    leading_(leading), autoAdjust_(autoAdjust), rng_(seed) {}

  float adjust(float frequency) override {
    sampleCount_ = baseSampleCount_;
    decayFactor_ = baseDecayFactor_;

    if (autoAdjust_) {
      if (frequency < 110.0f) {
        decayFactor_ = 1.0f - (1.0f - baseDecayFactor_) *
                       (1.0f + std::max(110.0f - frequency, 0.0f) / 10.0f);
      }
      if (frequency > 220.0f) {
        decayFactor_ = 1.0f - (1.0f - baseDecayFactor_) *
                       std::max(1.0f - (frequency - 220.0f) / 880.0f, 0.0f);
      }
      if (frequency > 440.0f) {
        sampleCount_ = float(baseSampleCount_ -
          ((baseSampleCount_ - 1.0f) - (baseSampleCount_ - 1.0f) *
           std::exp(-0.0008 * double(frequency - 440.0f))));
      }
    }

    return 0.0f;  // legacy: no freq adjustment for averaging
  }

  void evolve(std::vector<float>& values, int index) override {
    int loops = rng_.floorOrCeiling(speed_);
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
      if (leading_) {
        sum += values[(index + i) % len];
      } else {
        int idx = index - i;
        if (idx < 0) idx += len;
        sum += values[idx];
      }
    }
    values[index] = (sum / float(sampCount)) * decayFactor_;
  }

  float baseSampleCount_;
  float speed_;
  float baseDecayFactor_;
  bool  leading_;
  bool  autoAdjust_;

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
//
//   morphRate:  how fast the morph progresses (0.0001 = slow, 0.01 = fast)
//   holdCycles: number of full table cycles before morph begins (pure KS attack)
//   target:     one period of the target waveform (resampled to table length)
// ---------------------------------------------------------------------------
struct TargetEvolution final : WaveEvolution {
  TargetEvolution(float morphRate, int holdCycles, float decayFactor,
                  std::vector<float> target, uint32_t seed = 0xFADE'0001u)
  : morphRate_(morphRate), holdCycles_(holdCycles), decayFactor_(decayFactor),
    target_(std::move(target)), rng_(seed) {}

  float adjust(float frequency) override {
    sampleCount_ = 0;
    morphAmount_ = 0.0f;
    cycleCount_ = 0;
    tableLen_ = 0;
    return 0.0f;
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());

    // Track cycles (one full pass through the table)
    if (tableLen_ != len) {
      tableLen_ = len;
      resampledTarget_.resize(len);
      // Resample target to match table length
      for (int i = 0; i < len; ++i) {
        float pos = float(i) / float(len) * float(target_.size());
        int idx0 = int(pos) % int(target_.size());
        int idx1 = (idx0 + 1) % int(target_.size());
        float frac = pos - std::floor(pos);
        resampledTarget_[i] = target_[idx0] + (target_[idx1] - target_[idx0]) * frac;
      }
    }

    sampleCount_++;
    if (sampleCount_ >= len) {
      sampleCount_ = 0;
      cycleCount_++;
    }

    // Standard KS averaging (2-sample)
    int next = (index + 1) % len;
    float averaged = (values[index] + values[next]) * 0.5f;

    if (cycleCount_ < holdCycles_) {
      // Pure KS phase — organic attack
      values[index] = averaged * decayFactor_;
    } else {
      // Morph phase — converge toward target
      morphAmount_ = std::min(morphAmount_ + morphRate_, 1.0f);
      float t = resampledTarget_[index];
      values[index] = averaged * (1.0f - morphAmount_) + t * morphAmount_;
      // Slight decay prevents energy buildup during transition
      values[index] *= (1.0f - (1.0f - decayFactor_) * (1.0f - morphAmount_));
    }
  }

private:
  float morphRate_;
  int holdCycles_;
  float decayFactor_;
  std::vector<float> target_;
  std::vector<float> resampledTarget_;
  Randomizer rng_;

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
  // Configurable parameters
  float pickPosition{0.0f};    // 0=bridge, 0.5=middle (removes even harmonics), 1=nut
  float pickDirection{0.0f};   // 0=down, higher=more up-pick filtering
  float stiffness{0.0f};      // string inharmonicity (allpass dispersion)
  float decayStretch{0.5f};   // 0..1, 0.5=standard KS averaging
  float drumBlend{1.0f};      // 1=string, 0.5=snare, 0=pure drum

  explicit EKSEvolution(uint32_t seed = 0xEE45'0000u) : rng_(seed) {}

  float adjust(float frequency) override {
    freq_ = frequency;

    // Stiffness allpass coefficient: higher frequency = more dispersion
    if (stiffness > 0.0f) {
      // Approximate: coefficient grows with harmonic number
      // Use a simple model: C = stiffness * (freq/1000)^0.5
      stiffnessCoeff_ = stiffness * std::sqrt(freq_ / 1000.0f);
      stiffnessCoeff_ = std::clamp(stiffnessCoeff_, 0.0f, 0.9f);
    } else {
      stiffnessCoeff_ = 0.0f;
    }
    stiffnessState_ = 0.0f;

    return 0.0f;  // tuning allpass handles pitch correction now
  }

  void shape_excitation(std::vector<float>& values) override {
    int len = int(values.size());
    if (len < 2) return;

    // Pick-position comb filter: H(z) = 1 - z^(-floor(beta*N + 0.5))
    // Removes harmonics at multiples of 1/beta
    if (pickPosition > 0.001f && pickPosition < 0.999f) {
      int delay = int(std::floor(pickPosition * float(len) + 0.5f));
      if (delay >= 1 && delay < len) {
        std::vector<float> filtered(len);
        for (int i = 0; i < len; ++i) {
          int di = i - delay;
          if (di < 0) di += len;
          filtered[i] = values[i] - values[di];
        }
        values = std::move(filtered);
      }
    }

    // Pick-direction lowpass: H(z) = (1-p) / (1 - p*z^-1)
    if (pickDirection > 0.001f) {
      float p = std::clamp(pickDirection, 0.0f, 0.95f);
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
    float avg = (1.0f - decayStretch) * values[index] + decayStretch * values[next];

    // Drum blend: with probability (1-drumBlend)/2, flip sign
    if (drumBlend < 1.0f) {
      float flipProb = (1.0f - drumBlend) * 0.5f;
      if (rng_.value() < flipProb) {
        avg = -avg;
      }
    }

    // String stiffness allpass: y[n] = C*x[n] + x[n-1] - C*y[n-1]
    if (stiffnessCoeff_ > 0.0f) {
      float y = stiffnessCoeff_ * avg + stiffnessPrevIn_ - stiffnessCoeff_ * stiffnessState_;
      stiffnessPrevIn_ = avg;
      stiffnessState_ = y;
      avg = y;
    }

    values[index] = avg;
  }

private:
  Randomizer rng_;
  float freq_{440.0f};
  float stiffnessCoeff_{0.0f};
  float stiffnessState_{0.0f};
  float stiffnessPrevIn_{0.0f};
};

// ---------------------------------------------------------------------------
// ReedEvolution — reed-valve resonator (clarinet / sax).
// Ring-buffer-as-tube structure with reed nonlinearity. reedStiffness
// drives a tanh shaper on the bore signal → odd/asymmetric harmonics; breath
// is injected via a zero-mean noise term that seeds and sustains oscillation.
// ---------------------------------------------------------------------------
struct ReedEvolution final : WaveEvolution {
  explicit ReedEvolution(uint32_t seed = 0xBEED'BEEDu) : rng_(seed) {}

  float tubeLoss{0.02f};             // per-sample loop loss
  float loopFilter{0.5f};            // one-pole loop lowpass coefficient (0..1; higher = brighter)
  ValueSource* reedStiffness{nullptr}; // tanh drive on bore signal (0 = linear tube)
  ValueSource* breath{nullptr};

  float adjust(float /*frequency*/) override {
    lpState_ = 0.0f;
    return 0.0f;
  }

  void shape_excitation(std::vector<float>& values) override {
    std::fill(values.begin(), values.end(), 0.0f);
  }

  void evolve(std::vector<float>& values, int index) override {
    int len = int(values.size());
    int next = (index + 1) % len;

    float avg = (values[index] + values[next]) * 0.5f;

    // One-pole lowpass on the loop; kills the square-wave buzz that tanh alone produces.
    lpState_ = loopFilter * avg + (1.0f - loopFilter) * lpState_;
    float loopOut = lpState_ * (1.0f - tubeLoss);

    float drive = reedStiffness ? reedStiffness->next() : 0.0f;
    if (drive > 0.0f) loopOut = std::tanh(drive * loopOut);

    float b = breath ? breath->next() : 0.0f;
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
// ---------------------------------------------------------------------------
struct BowedStringEvolution final : WaveEvolution {
  explicit BowedStringEvolution(uint32_t /*seed*/ = 0xB0ED'0000u) {}

  // Dual-waveguide bowed string (Smith/Serafin).
  //   bridgeLine: carries signal through the round trip bow→bridge→bow
  //   neckLine:   carries signal through the round trip bow→nut→bow
  // Each buffer is 2×(one-way length) so the ring delay equals the full round
  // trip; rigid-end reflection is modelled by sign-inversion on read. The two
  // lines cross-couple at the bow junction (pass-through + bow impulse), which
  // is what ties the combined oscillation to the fundamental period
  // 2(Lb+Ln) = sampleRate / frequency.
  float tubeLoss{0.005f};              // per-sample loop loss on each line
  float bowSpeed{0.3f};                // bow velocity scale
  float bowPosition{0.14f};            // bow along string (0=bridge, 0.5=middle)
  ValueSource* frictionGain{nullptr};  // slope of Friedlander bow table
  ValueSource* bow{nullptr};           // bow pressure envelope (0..1)

  void on_prepare(const RenderContext& ctx) override { sampleRate_ = ctx.sampleRate; }

  float adjust(float frequency) override {
    int totalLen = int(std::lround(float(sampleRate_) / std::max(20.0f, frequency)));
    if (totalLen < 4) totalLen = 4;
    int halfLen = std::max(2, totalLen / 2);
    int Lb = std::max(1, int(std::round(bowPosition * float(halfLen))));
    if (Lb >= halfLen) Lb = halfLen - 1;
    int Ln = halfLen - Lb;
    // Round-trip ring: size = 2 * one-way length.
    bridgeLine_.assign(2 * Lb, 0.0f);
    neckLine_.assign(2 * Ln, 0.0f);
    bPtr_ = 0;
    nPtr_ = 0;
    return 0.0f;
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

    float pressure = bow ? bow->next() : 0.0f;
    float gain = frictionGain ? frictionGain->next() : 4.0f;

    float vBow = bowSpeed * pressure;
    float vRel = vBow - vString;

    // Friedlander friction table.
    float bt = std::abs(gain * vRel) + 0.75f;
    float fricCoef = std::pow(bt, -4.0f);
    if (fricCoef > 0.98f) fricCoef = 0.98f;
    else if (fricCoef < 0.0f) fricCoef = 0.0f;

    float bowImpulse = fricCoef * vRel * pressure;

    // Scattering at bow: wave from neck side passes through to bridge side (and
    // vice versa), each receives the bow kick. Cross-coupling couples the two
    // lines so they resonate as one string at the fundamental.
    float outBridge = (neckIn   + bowImpulse) * (1.0f - tubeLoss);
    float outNeck   = (bridgeIn + bowImpulse) * (1.0f - tubeLoss);

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
  explicit BrassEvolution(uint32_t seed = 0xB8A5'5000u) : rng_(seed) {}

  float tubeLoss{0.01f};             // per-sample loop loss
  float lipTension{0.92f};           // in-loop one-pole lowpass (0..0.99, high = dark)
  float lipFreqRatio{1.2f};          // out-of-loop biquad resonance, as multiple of played freq
  float lipQ{3.0f};                  // biquad Q (higher = sharper resonance peak)
  ValueSource* brassiness{nullptr};  // tanh drive → wave-steepening harmonics
  ValueSource* breath{nullptr};

  void on_prepare(const RenderContext& ctx) override { sampleRate_ = ctx.sampleRate; }

  float adjust(float frequency) override {
    lipState_ = 0.0f;
    // Compute RBJ resonant lowpass coefficients for the out-of-loop biquad.
    // Out-of-loop means no feedback through it → no pitch detune.
    x1_ = x2_ = y1_ = y2_ = 0.0f;
    float cutoff = std::max(20.0f, frequency * lipFreqRatio);
    float omega = 2.0f * 3.1415926536f * cutoff / float(sampleRate_);
    if (omega > 3.0f) omega = 3.0f;
    float q = std::max(0.5f, lipQ);
    float alpha = std::sin(omega) / (2.0f * q);
    float cosOm = std::cos(omega);
    float a0 = 1.0f + alpha;
    float c = 1.0f - cosOm;
    // Peak-normalize to unity so the biquad is a timbre shaper, not a gain stage.
    float scale = 1.0f / (a0 * std::max(1.0f, q));
    b0_ = 0.5f * c * scale;
    b1_ = c * scale;
    b2_ = 0.5f * c * scale;
    a1_ = -2.0f * cosOm / a0;
    a2_ = (1.0f - alpha) / a0;
    return 0.0f;
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
    lipState_ = (1.0f - lipTension) * avg + lipTension * lipState_;
    float loopOut = lipState_ * (1.0f - tubeLoss);

    float drive = brassiness ? brassiness->next() : 0.0f;
    if (drive > 0.0f) loopOut = std::tanh(drive * loopOut);

    float b = breath ? breath->next() : 0.0f;
    float noise = rng_.valuePN();
    rawLoop_[index] = loopOut + b * noise;

    // Out-of-loop biquad for timbral shaping — colors the output without altering
    // the feedback loop, so pitch stays locked to sampleRate / tableLength.
    float x = rawLoop_[index];
    float y = b0_*x + b1_*x1_ + b2_*x2_ - a1_*y1_ - a2_*y2_;
    x2_ = x1_; x1_ = x;
    y2_ = y1_; y1_ = y;
    values[index] = y;
  }

private:
  Randomizer rng_;
  int sampleRate_{48000};
  float lipState_{0.0f};
  // Out-of-loop biquad state
  float b0_{0.0f}, b1_{0.0f}, b2_{0.0f}, a1_{0.0f}, a2_{0.0f};
  float x1_{0.0f}, x2_{0.0f}, y1_{0.0f}, y2_{0.0f};
  std::vector<float> rawLoop_;
};

// ---------------------------------------------------------------------------
// IEvolutionHolder — interface for graph nodes that hold a WaveEvolution.
// WavetableSource dynamic_casts its "evolution" input to this.
// ---------------------------------------------------------------------------
struct IEvolutionHolder {
  virtual ~IEvolutionHolder() = default;
  virtual WaveEvolution* get_evolution() = 0;
};

// ---------------------------------------------------------------------------
// PluckEvolutionSource — graph node holding a PluckEvolution.
// Wire to WavetableSource's "evolution" input.
// ---------------------------------------------------------------------------
struct PluckEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit PluckEvolutionSource(uint32_t seed = 0xDEAD'BEEFu)
  : evo_(0.0f, seed) {}

  const char* type_name() const override { return "PluckEvolution"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"muting", ConfigType::Float, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "muting") { muting_ = value; evo_ = PluckEvolution(muting_, seed_); }
  }
  float get_config(std::string_view name) const override {
    if (name == "muting") return muting_;
    return 0.0f;
  }

  // Not an audio source — exists for graph wiring only
  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  PluckEvolution evo_;
  float muting_{0.0f};
  uint32_t seed_{0xDEAD'BEEFu};
};

// ---------------------------------------------------------------------------
// AveragingEvolutionSource — graph node holding an AveragingEvolution.
// Wire to WavetableSource's "evolution" input.
// ---------------------------------------------------------------------------
struct AveragingEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit AveragingEvolutionSource(uint32_t seed = 0xCAFE'BABEu)
  : evo_(4.0f, 1.0f, 0.999f, false, false, seed), seed_(seed) {}

  const char* type_name() const override { return "AveragingEvolution"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"sampleCount", ConfigType::Float, 4.0f, 1.0f, 48.0f},
      {"speed",       ConfigType::Float, 1.0f, 0.1f, 20.0f},
      {"decayFactor", ConfigType::Float, 0.999f, 0.9f, 1.0f},
      {"leading",     ConfigType::Bool,  0.0f, 0.0f, 1.0f},
      {"autoAdjust",  ConfigType::Bool,  0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "sampleCount") { sampleCount_ = value; rebuild(); }
    else if (name == "speed")       { speed_ = value; rebuild(); }
    else if (name == "decayFactor") { decayFactor_ = value; rebuild(); }
    else if (name == "leading")     { leading_ = (value != 0.0f); rebuild(); }
    else if (name == "autoAdjust")  { autoAdjust_ = (value != 0.0f); rebuild(); }
  }

  float get_config(std::string_view name) const override {
    if (name == "sampleCount") return sampleCount_;
    if (name == "speed")       return speed_;
    if (name == "decayFactor") return decayFactor_;
    if (name == "leading")     return leading_ ? 1.0f : 0.0f;
    if (name == "autoAdjust")  return autoAdjust_ ? 1.0f : 0.0f;
    return 0.0f;
  }

  // Not an audio source — exists for graph wiring only
  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  void rebuild() {
    evo_ = AveragingEvolution(sampleCount_, speed_, decayFactor_, leading_, autoAdjust_, seed_);
  }

  AveragingEvolution evo_;
  uint32_t seed_;
  float sampleCount_{4.0f};
  float speed_{1.0f};
  float decayFactor_{0.999f};
  bool  leading_{false};
  bool  autoAdjust_{false};
};

// ---------------------------------------------------------------------------
// EKSEvolutionSource — graph node holding an EKSEvolution.
// ---------------------------------------------------------------------------
struct EKSEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit EKSEvolutionSource(uint32_t seed = 0xEE45'0000u)
  : evo_(seed) {}

  const char* type_name() const override { return "EKSEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"pickPosition",  ConfigType::Float, 0.0f, 0.0f, 1.0f},
      {"pickDirection", ConfigType::Float, 0.0f, 0.0f, 0.95f},
      {"stiffness",     ConfigType::Float, 0.0f, 0.0f, 0.1f},
      {"decayStretch",  ConfigType::Float, 0.5f, 0.0f, 1.0f},
      {"drumBlend",     ConfigType::Float, 1.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "pickPosition")  evo_.pickPosition = value;
    else if (name == "pickDirection") evo_.pickDirection = value;
    else if (name == "stiffness")     evo_.stiffness = value;
    else if (name == "decayStretch")  evo_.decayStretch = value;
    else if (name == "drumBlend")     evo_.drumBlend = value;
  }

  float get_config(std::string_view name) const override {
    if (name == "pickPosition")  return evo_.pickPosition;
    if (name == "pickDirection") return evo_.pickDirection;
    if (name == "stiffness")     return evo_.stiffness;
    if (name == "decayStretch")  return evo_.decayStretch;
    if (name == "drumBlend")     return evo_.drumBlend;
    return 0.0f;
  }

  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  EKSEvolution evo_;
};

// ---------------------------------------------------------------------------
// ReedEvolutionSource — graph node holding a ReedEvolution.
// ---------------------------------------------------------------------------
struct ReedEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit ReedEvolutionSource(uint32_t seed = 0xBEED'BEEDu)
  : evo_(seed)
  , reedStiffnessSrc_(std::make_shared<ConstantSource>(1.5f)) {
    evo_.reedStiffness = reedStiffnessSrc_.get();
  }

  const char* type_name() const override { return "ReedEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"reedStiffness", 1.5f, 0.0f, 8.0f, "exp"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"breath", false, "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "breath") {
      breathSrc_ = std::move(src);
      evo_.breath = breathSrc_.get();
    } else if (name == "reedStiffness") {
      reedStiffnessSrc_ = std::move(src);
      evo_.reedStiffness = reedStiffnessSrc_.get();
    }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "breath") return breathSrc_;
    if (name == "reedStiffness") return reedStiffnessSrc_;
    return {};
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"tubeLoss",   ConfigType::Float, 0.02f, 0.0f, 0.5f},
      {"loopFilter", ConfigType::Float, 0.5f,  0.05f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "tubeLoss")        evo_.tubeLoss = value;
    else if (name == "loopFilter") evo_.loopFilter = value;
  }

  float get_config(std::string_view name) const override {
    if (name == "tubeLoss")   return evo_.tubeLoss;
    if (name == "loopFilter") return evo_.loopFilter;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (breathSrc_) breathSrc_->prepare(ctx, frames);
    if (reedStiffnessSrc_) reedStiffnessSrc_->prepare(ctx, frames);
  }
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  ReedEvolution evo_;
  std::shared_ptr<ValueSource> breathSrc_;
  std::shared_ptr<ValueSource> reedStiffnessSrc_;
};

// ---------------------------------------------------------------------------
// BowedStringEvolutionSource — graph node holding a BowedStringEvolution.
// ---------------------------------------------------------------------------
struct BowedStringEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit BowedStringEvolutionSource(uint32_t seed = 0xB0ED'0000u)
  : evo_(seed)
  , frictionGainSrc_(std::make_shared<ConstantSource>(4.0f)) {
    evo_.frictionGain = frictionGainSrc_.get();
  }

  const char* type_name() const override { return "BowedStringEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frictionGain", 4.0f, 0.0f, 20.0f, "gain"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"bow", false, "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "bow") {
      bowSrc_ = std::move(src);
      evo_.bow = bowSrc_.get();
    } else if (name == "frictionGain") {
      frictionGainSrc_ = std::move(src);
      evo_.frictionGain = frictionGainSrc_.get();
    }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "bow") return bowSrc_;
    if (name == "frictionGain") return frictionGainSrc_;
    return {};
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"tubeLoss",    ConfigType::Float, 0.005f, 0.0f,  0.5f},
      {"bowSpeed",    ConfigType::Float, 0.3f,   0.01f, 1.0f},
      {"bowPosition", ConfigType::Float, 0.14f,  0.02f, 0.5f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "tubeLoss")         evo_.tubeLoss = value;
    else if (name == "bowSpeed")    evo_.bowSpeed = value;
    else if (name == "bowPosition") evo_.bowPosition = value;
  }

  float get_config(std::string_view name) const override {
    if (name == "tubeLoss")    return evo_.tubeLoss;
    if (name == "bowSpeed")    return evo_.bowSpeed;
    if (name == "bowPosition") return evo_.bowPosition;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (bowSrc_) bowSrc_->prepare(ctx, frames);
    if (frictionGainSrc_) frictionGainSrc_->prepare(ctx, frames);
  }
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  BowedStringEvolution evo_;
  std::shared_ptr<ValueSource> bowSrc_;
  std::shared_ptr<ValueSource> frictionGainSrc_;
};

// ---------------------------------------------------------------------------
// BrassEvolutionSource — graph node holding a BrassEvolution.
// ---------------------------------------------------------------------------
struct BrassEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit BrassEvolutionSource(uint32_t seed = 0xB8A5'5000u)
  : evo_(seed)
  , brassinessSrc_(std::make_shared<ConstantSource>(1.5f)) {
    evo_.brassiness = brassinessSrc_.get();
  }

  const char* type_name() const override { return "BrassEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"brassiness", 1.5f, 0.0f, 10.0f, "exp"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"breath", false, "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "breath") {
      breathSrc_ = std::move(src);
      evo_.breath = breathSrc_.get();
    } else if (name == "brassiness") {
      brassinessSrc_ = std::move(src);
      evo_.brassiness = brassinessSrc_.get();
    }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "breath") return breathSrc_;
    if (name == "brassiness") return brassinessSrc_;
    return {};
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"tubeLoss",     ConfigType::Float, 0.01f, 0.0f,  0.5f},
      {"lipTension",   ConfigType::Float, 0.92f, 0.0f,  0.99f},
      {"lipFreqRatio", ConfigType::Float, 1.2f,  0.5f,  4.0f},
      {"lipQ",         ConfigType::Float, 3.0f,  0.5f,  15.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "tubeLoss")          evo_.tubeLoss = value;
    else if (name == "lipTension")   evo_.lipTension = value;
    else if (name == "lipFreqRatio") evo_.lipFreqRatio = value;
    else if (name == "lipQ")         evo_.lipQ = value;
  }

  float get_config(std::string_view name) const override {
    if (name == "tubeLoss")     return evo_.tubeLoss;
    if (name == "lipTension")   return evo_.lipTension;
    if (name == "lipFreqRatio") return evo_.lipFreqRatio;
    if (name == "lipQ")         return evo_.lipQ;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (breathSrc_) breathSrc_->prepare(ctx, frames);
    if (brassinessSrc_) brassinessSrc_->prepare(ctx, frames);
  }
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  BrassEvolution evo_;
  std::shared_ptr<ValueSource> breathSrc_;
  std::shared_ptr<ValueSource> brassinessSrc_;
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
  float feed{0.0367f};       // canonical "mitosis": 0.0367 / 0.0649
  float kill{0.0649f};
  float diffU{0.16f};
  float diffV{0.08f};
  float dt{1.0f};            // integration step
  int   subSteps{1};         // RD iterations per audio sample (>=1)

  float adjust(float /*frequency*/) override { return 0.0f; }

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
    for (int s = 0; s < subSteps; ++s) {
      const float lapU = uPrev_[im] - 2.0f * u + uPrev_[ip];
      const float lapV = vPrev_[im] - 2.0f * v + vPrev_[ip];
      const float uvv  = u * v * v;
      u += dt * (diffU * lapU - uvv + feed * (1.0f - u));
      v += dt * (diffV * lapV + uvv - (feed + kill) * v);
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

struct ReactionDiffusionEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit ReactionDiffusionEvolutionSource(uint32_t /*seed*/ = 0u) {}

  const char* type_name() const override { return "ReactionDiffusionEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"feed",     ConfigType::Float, 0.0367f, 0.0f,  0.1f},
      {"kill",     ConfigType::Float, 0.0649f, 0.0f,  0.1f},
      {"diffU",    ConfigType::Float, 0.16f,   0.0f,  1.0f},
      {"diffV",    ConfigType::Float, 0.08f,   0.0f,  1.0f},
      {"dt",       ConfigType::Float, 1.0f,    0.05f, 2.0f},
      {"subSteps", ConfigType::Int,   1.0f,    1.0f,  8.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "feed")          evo_.feed = value;
    else if (name == "kill")     evo_.kill = value;
    else if (name == "diffU")    evo_.diffU = value;
    else if (name == "diffV")    evo_.diffV = value;
    else if (name == "dt")       evo_.dt = value;
    else if (name == "subSteps") evo_.subSteps = std::max(1, int(value));
  }

  float get_config(std::string_view name) const override {
    if (name == "feed")     return evo_.feed;
    if (name == "kill")     return evo_.kill;
    if (name == "diffU")    return evo_.diffU;
    if (name == "diffV")    return evo_.diffV;
    if (name == "dt")       return evo_.dt;
    if (name == "subSteps") return float(evo_.subSteps);
    return 0.0f;
  }

  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  ReactionDiffusionEvolution evo_;
};

// ---------------------------------------------------------------------------
// SortErosionEvolution — distributed bubble-sort toward a sorted (saw) shape.
// Each evolve(N) does `swapsPerSample` adjacent compare-swap steps starting
// at a rotating pointer. Eventually the table converges to a monotonic ramp.
// `descending` flips the sort direction.
// ---------------------------------------------------------------------------
struct SortErosionEvolution final : WaveEvolution {
  int  swapsPerSample{4};
  bool descending{false};

  float adjust(float /*frequency*/) override {
    pos_ = 0;
    return 0.0f;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2) return;
    for (int s = 0; s < swapsPerSample; ++s) {
      const int a = pos_;
      const int b = (pos_ + 1) % len;
      const bool out_of_order =
        descending ? (values[a] < values[b]) : (values[a] > values[b]);
      if (out_of_order) std::swap(values[a], values[b]);
      pos_ = (pos_ + 1) % len;
    }
    (void)index;
  }

private:
  int pos_{0};
};

struct SortErosionEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit SortErosionEvolutionSource(uint32_t /*seed*/ = 0u) {}

  const char* type_name() const override { return "SortErosionEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"swapsPerSample", ConfigType::Int,  4.0f, 1.0f, 32.0f},
      {"descending",     ConfigType::Bool, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "swapsPerSample") evo_.swapsPerSample = std::max(1, int(value));
    else if (name == "descending") evo_.descending = (value != 0.0f);
  }

  float get_config(std::string_view name) const override {
    if (name == "swapsPerSample") return float(evo_.swapsPerSample);
    if (name == "descending")     return evo_.descending ? 1.0f : 0.0f;
    return 0.0f;
  }

  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  SortErosionEvolution evo_;
};

// ---------------------------------------------------------------------------
// CellularAutomatonEvolution — binary 1D Wolfram CA on the wavetable.
// The wavetable is threshold-quantized to bits each cycle, and a Wolfram
// rule (0..255) is applied to each cell from its three-cell neighbourhood.
// Bits flip back to ± waveform via `levelHi` / `levelLo`. Famous rules:
// 30 (chaotic), 90 (Sierpinski), 110 (edge-of-chaos), 184 (traffic).
// ---------------------------------------------------------------------------
struct CellularAutomatonEvolution final : WaveEvolution {
  int   rule{110};
  float threshold{0.0f};   // discretization threshold (sample > threshold → 1)
  float levelHi{0.8f};
  float levelLo{-0.8f};
  int   stepEvery{8};      // do one CA step every Nth sample (slow tempo)

  float adjust(float /*frequency*/) override {
    sampleCtr_ = 0;
    return 0.0f;
  }

  void shape_excitation(std::vector<float>& values) override {
    const int len = int(values.size());
    state_.assign(len, 0);
    snapshot_.assign(len, 0);
    for (int i = 0; i < len; ++i) state_[i] = (values[i] > threshold) ? 1 : 0;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 3) return;

    if (state_.size() != size_t(len)) {
      state_.assign(len, 0);
      snapshot_.assign(len, 0);
      for (int i = 0; i < len; ++i) state_[i] = (values[i] > threshold) ? 1 : 0;
    }

    // Snapshot at cycle boundary so neighbours are read from the
    // previous cycle's complete state, not from cells already mutated
    // earlier in this cycle. Without this, asynchronous-CA dynamics
    // collapse to uniform output very fast.
    if (index == 0) snapshot_ = state_;

    // Tempo control: only do CA work every Nth sample.
    sampleCtr_++;
    if (sampleCtr_ >= stepEvery) {
      sampleCtr_ = 0;
      const int im = (index - 1 + len) % len;
      const int ip = (index + 1) % len;
      const uint8_t pat = uint8_t((snapshot_[im] << 2) | (snapshot_[index] << 1) | snapshot_[ip]);
      state_[index] = uint8_t((rule >> pat) & 1u);
    }
    values[index] = state_[index] ? levelHi : levelLo;
  }

private:
  std::vector<uint8_t> state_;
  std::vector<uint8_t> snapshot_;
  int sampleCtr_{0};
};

struct CellularAutomatonEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit CellularAutomatonEvolutionSource(uint32_t /*seed*/ = 0u) {}

  const char* type_name() const override { return "CellularAutomatonEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"rule",       ConfigType::Int,   110.0f, 0.0f,  255.0f},
      {"threshold",  ConfigType::Float, 0.0f,  -1.0f,  1.0f},
      {"levelHi",    ConfigType::Float, 0.8f,   0.0f,  1.0f},
      {"levelLo",    ConfigType::Float,-0.8f,  -1.0f,  0.0f},
      {"stepEvery",  ConfigType::Int,   8.0f,   1.0f,  256.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "rule")            evo_.rule = std::clamp(int(value), 0, 255);
    else if (name == "threshold")  evo_.threshold = value;
    else if (name == "levelHi")    evo_.levelHi = value;
    else if (name == "levelLo")    evo_.levelLo = value;
    else if (name == "stepEvery")  evo_.stepEvery = std::max(1, int(value));
  }

  float get_config(std::string_view name) const override {
    if (name == "rule")       return float(evo_.rule);
    if (name == "threshold")  return evo_.threshold;
    if (name == "levelHi")    return evo_.levelHi;
    if (name == "levelLo")    return evo_.levelLo;
    if (name == "stepEvery")  return float(evo_.stepEvery);
    return 0.0f;
  }

  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  CellularAutomatonEvolution evo_;
};

// ---------------------------------------------------------------------------
// HistogramEqualizeEvolution — gradually pull the wavetable toward a
// uniform amplitude distribution via histogram equalization. Each cycle,
// a histogram + CDF are rebuilt over the current table. Each sample is
// then nudged toward CDF(sample)*2-1 by `rate`. Convergence: sine → tri-ish,
// noise → roughly uniformly distributed waveform.
// ---------------------------------------------------------------------------
struct HistogramEqualizeEvolution final : WaveEvolution {
  int   bins{64};
  float rate{0.00005f};

  float adjust(float /*frequency*/) override {
    cdf_.assign(std::max(2, bins), 0.0f);
    return 0.0f;
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2 || bins < 2) return;

    // Rebuild CDF at the start of each cycle.
    if (index == 0) rebuild_cdf(values);

    // Map current sample value to a bin and look up its CDF target.
    const int b = sample_to_bin(values[index]);
    const float target = cdf_[b] * 2.0f - 1.0f;
    values[index] += rate * (target - values[index]);
  }

private:
  int sample_to_bin(float v) const {
    const float t = (std::clamp(v, -1.0f, 1.0f) + 1.0f) * 0.5f; // 0..1
    int b = int(t * float(bins));
    if (b >= bins) b = bins - 1;
    return b;
  }

  void rebuild_cdf(const std::vector<float>& values) {
    if (int(cdf_.size()) != bins) cdf_.assign(bins, 0.0f);
    std::vector<int> counts(bins, 0);
    for (float v : values) counts[sample_to_bin(v)]++;
    const float invN = 1.0f / float(values.size());
    float running = 0.0f;
    for (int i = 0; i < bins; ++i) {
      running += float(counts[i]) * invN;
      cdf_[i] = running;
    }
  }

  std::vector<float> cdf_;
};

struct HistogramEqualizeEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit HistogramEqualizeEvolutionSource(uint32_t /*seed*/ = 0u) {}

  const char* type_name() const override { return "HistogramEqualizeEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"bins", ConfigType::Int,   64.0f,    4.0f,   256.0f},
      {"rate", ConfigType::Float, 0.00005f, 0.0f,   0.1f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "bins")      evo_.bins = std::clamp(int(value), 4, 256);
    else if (name == "rate") evo_.rate = value;
  }

  float get_config(std::string_view name) const override {
    if (name == "bins") return float(evo_.bins);
    if (name == "rate") return evo_.rate;
    return 0.0f;
  }

  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  HistogramEqualizeEvolution evo_;
};

// ---------------------------------------------------------------------------
// BezierPullEvolution — pull each sample toward a cubic Bezier curve
// defined by four control values p0..p3 over t in [0,1]. The Bezier is
// resampled to the table length once per note, then each sample drifts
// toward its target by `rate`. Endpoint: the table becomes the Bezier shape.
// ---------------------------------------------------------------------------
struct BezierPullEvolution final : WaveEvolution {
  float p0{-1.0f};
  float p1{ 0.5f};
  float p2{-0.5f};
  float p3{ 1.0f};
  float rate{0.00005f};

  float adjust(float /*frequency*/) override {
    needsRebuild_ = true;
    return 0.0f;
  }

  void shape_excitation(std::vector<float>& values) override {
    rebuild_target(int(values.size()));
  }

  void evolve(std::vector<float>& values, int index) override {
    const int len = int(values.size());
    if (len < 2) return;
    if (needsRebuild_ || int(target_.size()) != len) rebuild_target(len);
    values[index] += rate * (target_[index] - values[index]);
  }

private:
  void rebuild_target(int len) {
    target_.resize(len);
    for (int i = 0; i < len; ++i) {
      const float t = float(i) / float(len);
      const float u = 1.0f - t;
      target_[i] = u*u*u*p0 + 3.0f*u*u*t*p1 + 3.0f*u*t*t*p2 + t*t*t*p3;
    }
    needsRebuild_ = false;
  }

  std::vector<float> target_;
  bool needsRebuild_{true};
};

struct BezierPullEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit BezierPullEvolutionSource(uint32_t /*seed*/ = 0u) {}

  const char* type_name() const override { return "BezierPullEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"p0",   ConfigType::Float, -1.0f,    -2.0f, 2.0f},
      {"p1",   ConfigType::Float,  0.5f,    -2.0f, 2.0f},
      {"p2",   ConfigType::Float, -0.5f,    -2.0f, 2.0f},
      {"p3",   ConfigType::Float,  1.0f,    -2.0f, 2.0f},
      {"rate", ConfigType::Float,  0.00005f, 0.0f, 0.1f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "p0") evo_.p0 = value;
    else if (name == "p1") evo_.p1 = value;
    else if (name == "p2") evo_.p2 = value;
    else if (name == "p3") evo_.p3 = value;
    else if (name == "rate") evo_.rate = value;
  }

  float get_config(std::string_view name) const override {
    if (name == "p0") return evo_.p0;
    if (name == "p1") return evo_.p1;
    if (name == "p2") return evo_.p2;
    if (name == "p3") return evo_.p3;
    if (name == "rate") return evo_.rate;
    return 0.0f;
  }

  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  BezierPullEvolution evo_;
};

// ---------------------------------------------------------------------------
// BitRotateEvolution — bit-level glitch transform. Each sample is mapped
// to a 32-bit unsigned int via fixed-point, its bits rotated by `shiftBits`,
// then mapped back. `stepEvery` controls how often a sample is rotated (so
// only a fraction of the table mutates per cycle). Endpoint: pseudo-random
// scramble of the original — aliasing-laden, hard-edged.
// ---------------------------------------------------------------------------
struct BitRotateEvolution final : WaveEvolution {
  int shiftBits{3};
  int stepEvery{4};

  float adjust(float /*frequency*/) override {
    ctr_ = 0;
    return 0.0f;
  }

  void evolve(std::vector<float>& values, int index) override {
    ctr_++;
    if (ctr_ < stepEvery) return;
    ctr_ = 0;

    const int k = ((shiftBits % 32) + 32) % 32;
    if (k == 0) return;

    const float v = std::clamp(values[index], -1.0f, 1.0f);
    const uint32_t u = uint32_t((double(v) + 1.0) * 0.5 * 4294967295.0);
    const uint32_t rot = (u >> k) | (u << (32 - k));
    values[index] = float(double(rot) / 4294967295.0 * 2.0 - 1.0);
  }

private:
  int ctr_{0};
};

struct BitRotateEvolutionSource final : ValueSource, IEvolutionHolder {
  explicit BitRotateEvolutionSource(uint32_t /*seed*/ = 0u) {}

  const char* type_name() const override { return "BitRotateEvolution"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  WaveEvolution* get_evolution() override { return &evo_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"shiftBits", ConfigType::Int, 3.0f, 0.0f, 31.0f},
      {"stepEvery", ConfigType::Int, 4.0f, 1.0f, 256.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "shiftBits")      evo_.shiftBits = std::clamp(int(value), 0, 31);
    else if (name == "stepEvery") evo_.stepEvery = std::max(1, int(value));
  }

  float get_config(std::string_view name) const override {
    if (name == "shiftBits") return float(evo_.shiftBits);
    if (name == "stepEvery") return float(evo_.stepEvery);
    return 0.0f;
  }

  void prepare(const RenderContext&, int) override {}
  float next() override { return 0.0f; }
  float current() const override { return 0.0f; }

private:
  BitRotateEvolution evo_;
};

} // namespace mforce

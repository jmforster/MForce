#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <memory>

namespace mforce {

// Ported from C# MForce.Sound.Source.IWaveEvolution
struct WaveEvolution {
  virtual ~WaveEvolution() = default;

  // Called once per note — adjust internal state based on frequency.
  // Returns a frequency adjustment (currently unused in C++ WaveSource).
  virtual float adjust(float frequency) = 0;

  // Called each sample — modify the wavetable in-place.
  virtual void evolve(std::vector<float>& values, int index) = 0;
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
  void prepare(int) override {}
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
  void prepare(int) override {}
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

} // namespace mforce

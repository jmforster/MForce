#pragma once
#include "mforce/core/dsp_wave_source.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// Ported from C# MForce.Sound.Source.Additive.BasicAdditiveSource
// Sum of sine partials up to ~12 kHz with:
//   evenWeight/oddWeight: amplitude of even/odd harmonics (fundamental always 1)
//   rolloff: amplitude = weight / pow(partialNum, rolloff). 0=flat, 1=1/n, >1=steeper
//   freqVarPct/freqVarSpeed: per-partial frequency wander (triangle-wave modulation)
//   amplVarPct/amplVarSpeed: per-partial amplitude wander
struct BasicAdditiveSource final : WaveSource {
  // After WaveSource's three, in the order wave() advanced them.
  Param evenWeight  {this, "evenWeight",   1.0f, 0.0f, 10.0f,  "gain"};
  Param oddWeight   {this, "oddWeight",    1.0f, 0.0f, 10.0f,  "gain"};
  Param rolloff     {this, "rolloff",      1.0f, 0.0f, 10.0f,  "exp"};
  Param freqVarPct  {this, "freqVarPct",   0.0f, 0.0f, 1.0f,   "0-1"};
  Param freqVarSpeed{this, "freqVarSpeed", 0.0f, 0.0f, 100.0f, "hz"};
  Param amplVarPct  {this, "amplVarPct",   0.0f, 0.0f, 1.0f,   "0-1"};
  Param amplVarSpeed{this, "amplVarSpeed", 0.0f, 0.0f, 100.0f, "hz"};

  BasicAdditiveSource(int, uint32_t seed = 0xADD1'0000u)
  : rng_(seed) {}

  const char* type_name() const override { return "BasicAdditiveSource"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  void on_prepare(const RenderContext& ctx, int frames) override {
    WaveSource::on_start(frames);

    // Reset arrays — will be initialized on first sample
    partialCount_ = 0;
    partialPos_.clear();
    freqOffset_.clear();
    amplOffset_.clear();
    freqVarDir_.clear();
    amplVarDir_.clear();
  }

protected:
  float wave() override {
    float ew  = evenWeight.current();
    float ow  = oddWeight.current();
    float ro  = rolloff.current();
    float fvp = freqVarPct.current();
    float fvs = freqVarSpeed.current() / float(sample_rate());
    float avp = amplVarPct.current();
    float avs = amplVarSpeed.current() / float(sample_rate());

    // Compute partial count up to ~12 kHz
    int count = (currFreq_ > 0.0f) ? int(12000.0f / currFreq_) : 0;
    if (count < 1) count = 1;

    // Grow arrays if needed
    if (count > partialCount_) {
      int old = partialCount_;
      partialCount_ = count;
      partialPos_.resize(count, 0.0f);
      freqOffset_.resize(count, 0.0f);
      amplOffset_.resize(count, 0.0f);
      freqVarDir_.resize(count, 0);
      amplVarDir_.resize(count, 0);

      for (int i = old; i < count; ++i) {
        freqOffset_[i] = rng_.valuePN() * fvp;
        freqVarDir_[i] = (rng_.valuePN() >= 0.0f) ? 1 : -1;
        amplOffset_[i] = rng_.valuePN() * avp;
        amplVarDir_[i] = (rng_.valuePN() >= 0.0f) ? 1 : -1;
      }
    }

    constexpr float TAU = 2.0f * 3.14159265358979323846f;
    float val = 0.0f;

    for (int i = 0; i < count; ++i) {
      int pnum = i + 1;  // 1-based partial number
      float freq = currFreq_ * float(pnum);

      // Even/odd weight
      float weight = (pnum % 2 == 0) ? ew : (pnum == 1 ? 1.0f : ow);

      // Rolloff: 1 / pow(pnum, rolloff)
      float ampl = weight * (ro == 0.0f ? 1.0f : (1.0f / std::pow(float(pnum), ro)));

      // Update frequency variation
      freqOffset_[i] += fvp * 2.0f * fvs * float(freqVarDir_[i]);
      if (std::fabs(freqOffset_[i]) >= fvp && fvp > 0.0f) {
        freqOffset_[i] = fvp * float(freqVarDir_[i]);
        freqVarDir_[i] *= -1;
      }

      // Update amplitude variation
      amplOffset_[i] += avp * 2.0f * avs * float(amplVarDir_[i]);
      if (std::fabs(amplOffset_[i]) >= avp && avp > 0.0f) {
        amplOffset_[i] = avp * float(amplVarDir_[i]);
        amplVarDir_[i] *= -1;
      }

      // Advance phase
      partialPos_[i] += freq * (1.0f + freqOffset_[i]) / float(sample_rate());
      partialPos_[i] -= std::floor(partialPos_[i]);

      val += std::sin(partialPos_[i] * TAU) * ampl * (1.0f + amplOffset_[i]);
    }

    // Legacy normalization
    return val / 5.0f;
  }

private:
  Randomizer rng_;

  int partialCount_{0};
  std::vector<float> partialPos_;
  std::vector<float> freqOffset_;
  std::vector<float> amplOffset_;
  std::vector<int>   freqVarDir_;
  std::vector<int>   amplVarDir_;
};

} // namespace mforce

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
  BasicAdditiveSource(int sampleRate, uint32_t seed = 0xADD1'0000u)
  : WaveSource(sampleRate), rng_(seed)
  , evenWeight_(std::make_shared<ConstantSource>(1.0f))
  , oddWeight_(std::make_shared<ConstantSource>(1.0f))
  , rolloff_(std::make_shared<ConstantSource>(1.0f))
  , freqVarPct_(std::make_shared<ConstantSource>(0.0f))
  , freqVarSpeed_(std::make_shared<ConstantSource>(0.0f))
  , amplVarPct_(std::make_shared<ConstantSource>(0.0f))
  , amplVarSpeed_(std::make_shared<ConstantSource>(0.0f)) {}

  void set_even_weight(std::shared_ptr<ValueSource> v)   { evenWeight_ = std::move(v); }
  void set_odd_weight(std::shared_ptr<ValueSource> v)    { oddWeight_ = std::move(v); }
  void set_rolloff(std::shared_ptr<ValueSource> v)       { rolloff_ = std::move(v); }
  void set_freq_var_pct(std::shared_ptr<ValueSource> v)  { freqVarPct_ = std::move(v); }
  void set_freq_var_speed(std::shared_ptr<ValueSource> v){ freqVarSpeed_ = std::move(v); }
  void set_ampl_var_pct(std::shared_ptr<ValueSource> v)  { amplVarPct_ = std::move(v); }
  void set_ampl_var_speed(std::shared_ptr<ValueSource> v){ amplVarSpeed_ = std::move(v); }

  const char* type_name() const override { return "BasicAdditiveSource"; }
  SourceCategory category() const override { return SourceCategory::Additive; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency",    440.0f, 0.01f, 20000.0f, "hz"},
      {"amplitude",    1.0f,   0.0f,  10.0f,    "0-1"},
      {"phase",        0.0f,  -1.0f,  1.0f,     "cycles"},
      {"evenWeight",   1.0f,   0.0f,  10.0f,    "gain"},
      {"oddWeight",    1.0f,   0.0f,  10.0f,    "gain"},
      {"rolloff",      1.0f,   0.0f,  10.0f,    "exp"},
      {"freqVarPct",   0.0f,   0.0f,  1.0f,     "0-1"},
      {"freqVarSpeed", 0.0f,   0.0f,  100.0f,   "hz"},
      {"amplVarPct",   0.0f,   0.0f,  1.0f,     "0-1"},
      {"amplVarSpeed", 0.0f,   0.0f,  100.0f,   "hz"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "evenWeight")   { set_even_weight(std::move(src)); return; }
    if (name == "oddWeight")    { set_odd_weight(std::move(src)); return; }
    if (name == "rolloff")      { set_rolloff(std::move(src)); return; }
    if (name == "freqVarPct")   { set_freq_var_pct(std::move(src)); return; }
    if (name == "freqVarSpeed") { set_freq_var_speed(std::move(src)); return; }
    if (name == "amplVarPct")   { set_ampl_var_pct(std::move(src)); return; }
    if (name == "amplVarSpeed") { set_ampl_var_speed(std::move(src)); return; }
    WaveSource::set_param(name, std::move(src));
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "evenWeight")   return evenWeight_;
    if (name == "oddWeight")    return oddWeight_;
    if (name == "rolloff")      return rolloff_;
    if (name == "freqVarPct")   return freqVarPct_;
    if (name == "freqVarSpeed") return freqVarSpeed_;
    if (name == "amplVarPct")   return amplVarPct_;
    if (name == "amplVarSpeed") return amplVarSpeed_;
    return WaveSource::get_param(name);
  }

  void prepare(const RenderContext& ctx, int frames) override {
    WaveSource::prepare(ctx, frames);
    evenWeight_->prepare(ctx, frames);
    oddWeight_->prepare(ctx, frames);
    rolloff_->prepare(ctx, frames);
    freqVarPct_->prepare(ctx, frames);
    freqVarSpeed_->prepare(ctx, frames);
    amplVarPct_->prepare(ctx, frames);
    amplVarSpeed_->prepare(ctx, frames);

    // Reset arrays — will be initialized on first sample
    partialCount_ = 0;
    partialPos_.clear();
    freqOffset_.clear();
    amplOffset_.clear();
    freqVarDir_.clear();
    amplVarDir_.clear();
  }

protected:
  float compute_wave_value() override {
    float ew  = evenWeight_->next();
    float ow  = oddWeight_->next();
    float ro  = rolloff_->next();
    float fvp = freqVarPct_->next();
    float fvs = freqVarSpeed_->next() / float(sampleRate_);
    float avp = amplVarPct_->next();
    float avs = amplVarSpeed_->next() / float(sampleRate_);

    ew  = evenWeight_->current();
    ow  = oddWeight_->current();
    ro  = rolloff_->current();
    fvp = freqVarPct_->current();
    fvs = freqVarSpeed_->current() / float(sampleRate_);
    avp = amplVarPct_->current();
    avs = amplVarSpeed_->current() / float(sampleRate_);

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
      partialPos_[i] += freq * (1.0f + freqOffset_[i]) / float(sampleRate_);
      partialPos_[i] -= std::floor(partialPos_[i]);

      val += std::sin(partialPos_[i] * TAU) * ampl * (1.0f + amplOffset_[i]);
    }

    // Legacy normalization
    return val / 5.0f;
  }

private:
  Randomizer rng_;
  std::shared_ptr<ValueSource> evenWeight_;
  std::shared_ptr<ValueSource> oddWeight_;
  std::shared_ptr<ValueSource> rolloff_;
  std::shared_ptr<ValueSource> freqVarPct_;
  std::shared_ptr<ValueSource> freqVarSpeed_;
  std::shared_ptr<ValueSource> amplVarPct_;
  std::shared_ptr<ValueSource> amplVarSpeed_;

  int partialCount_{0};
  std::vector<float> partialPos_;
  std::vector<float> freqOffset_;
  std::vector<float> amplOffset_;
  std::vector<int>   freqVarDir_;
  std::vector<int>   amplVarDir_;
};

} // namespace mforce

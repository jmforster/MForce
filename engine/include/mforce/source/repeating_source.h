#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <cmath>

namespace mforce {

// Ported from C# MForce.Sound.Source.RepeatingSource
// Repeats a source with variable duration and gaps between repetitions.
// Good for sound effects (footsteps, dripping, rhythmic textures).
//
// Each repetition: prepare source for duration seconds, play it, then
// silence for gap seconds. Both duration and gap are randomized by varPct.
struct RepeatingSource final : ValueSource {
  float duration{0.5f};       // seconds per repetition
  float durVarPct{0.0f};      // random variation of duration
  float gapDuration{0.2f};    // seconds of silence between repetitions
  float gapVarPct{0.0f};      // random variation of gap
  int sampleRate{48000};

  RepeatingSource(int sr, uint32_t seed = 0xAEAE'0000u)
  : sampleRate(sr), rng_(seed) {}

  void set_source(std::shared_ptr<ValueSource> s) { source_ = std::move(s); }
  std::shared_ptr<ValueSource> get_source() const { return source_; }

  const char* type_name() const override { return "RepeatingSource"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override { return {}; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") { source_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source") return source_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"duration",    ConfigType::Float, 0.5f,  0.001f, 10.0f},
      {"durVarPct",   ConfigType::Float, 0.0f,  0.0f,   1.0f},
      {"gapDuration", ConfigType::Float, 0.2f,  0.0f,   10.0f},
      {"gapVarPct",   ConfigType::Float, 0.0f,  0.0f,   1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "duration")    { duration = value; return; }
    if (name == "durVarPct")   { durVarPct = value; return; }
    if (name == "gapDuration") { gapDuration = value; return; }
    if (name == "gapVarPct")   { gapVarPct = value; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "duration")    return duration;
    if (name == "durVarPct")   return durVarPct;
    if (name == "gapDuration") return gapDuration;
    if (name == "gapVarPct")   return gapVarPct;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    (void)frames;
    ctx_ = ctx;
    prepare_repetition();
  }

  float next() override {
    if (gapCount_ == 0) {
      // Playing source
      if (srcRemaining_ > 0) {
        srcRemaining_--;
        cur_ = source_ ? source_->next() : 0.0f;
      } else {
        // Source finished — enter gap
        gapCount_ = int(std::round(currGap_ * float(sampleRate)));
        cur_ = 0.0f;
      }
    } else {
      // In gap
      gapCount_--;
      if (gapCount_ == 0) {
        prepare_repetition();
      }
      cur_ = 0.0f;
    }
    return cur_;
  }

  float current() const override { return cur_; }

private:
  void prepare_repetition() {
    float d = duration * (1.0f + rng_.valuePN() * durVarPct);
    if (d < 0.001f) d = 0.001f;
    currGap_ = gapDuration * (1.0f + rng_.valuePN() * gapVarPct);
    if (currGap_ < 0.0f) currGap_ = 0.0f;

    int samples = int(std::round(d * float(sampleRate)));
    if (source_) source_->prepare(ctx_, samples);
    srcRemaining_ = samples;
    gapCount_ = 0;
  }

  std::shared_ptr<ValueSource> source_;
  Randomizer rng_;
  RenderContext ctx_{48000};  // captured at prepare() for use by prepare_repetition() during playback
  float currGap_{0.0f};
  int srcRemaining_{0};
  int gapCount_{0};
  float cur_{0.0f};
};

} // namespace mforce

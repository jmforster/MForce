#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/smoothness_interpolator.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <vector>
#include <cmath>

namespace mforce {

// Ported from C# MForce.Sound.Source.SegmentSource
// Generates a segmented waveform: a sequence of value/width pairs with
// smoothed transitions. Good for sound effects (footsteps, impacts).
//
// Values array: [width0, val0, width1, val1, ...] pairs.
// Widths < 1.0 are interpreted as seconds; >= 1.0 as samples.
// Each cycle, widths and values are randomized by varPct.
// Optional gap (silence) appended to each cycle.
// OneShot mode plays once and stops.
struct SegmentSource final : ValueSource {
  bool oneShot{false};
  int sampleRate{48000};

  void set_amplitude(std::shared_ptr<ValueSource> s)   { amplitude_ = std::move(s); }
  void set_smoothness(std::shared_ptr<ValueSource> s)  { smoothness_ = std::move(s); }
  void set_widthVarPct(std::shared_ptr<ValueSource> s) { widthVarPct_ = std::move(s); }
  void set_valVarPct(std::shared_ptr<ValueSource> s)   { valVarPct_ = std::move(s); }
  void set_gap(std::shared_ptr<ValueSource> s)         { gap_ = std::move(s); }
  void set_gapVarPct(std::shared_ptr<ValueSource> s)   { gapVarPct_ = std::move(s); }

  std::shared_ptr<ValueSource> get_amplitude() const   { return amplitude_; }
  std::shared_ptr<ValueSource> get_smoothness() const  { return smoothness_; }
  std::shared_ptr<ValueSource> get_widthVarPct() const { return widthVarPct_; }
  std::shared_ptr<ValueSource> get_valVarPct() const   { return valVarPct_; }
  std::shared_ptr<ValueSource> get_gap() const         { return gap_; }
  std::shared_ptr<ValueSource> get_gapVarPct() const   { return gapVarPct_; }

  const char* type_name() const override { return "SegmentSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",   1.0f, 0.0f, 10.0f, "0-1"},
      {"smoothness",  0.5f, 0.0f, 1.0f,  "0-1"},
      {"widthVarPct", 0.0f, 0.0f, 1.0f,  "0-1"},
      {"valVarPct",   0.0f, 0.0f, 1.0f,  "0-1"},
      {"gap",         0.0f, 0.0f, 10.0f, "sec"},
      {"gapVarPct",   0.0f, 0.0f, 1.0f,  "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")   { amplitude_ = std::move(src); return; }
    if (name == "smoothness")  { smoothness_ = std::move(src); return; }
    if (name == "widthVarPct") { widthVarPct_ = std::move(src); return; }
    if (name == "valVarPct")   { valVarPct_ = std::move(src); return; }
    if (name == "gap")         { gap_ = std::move(src); return; }
    if (name == "gapVarPct")   { gapVarPct_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")   return amplitude_;
    if (name == "smoothness")  return smoothness_;
    if (name == "widthVarPct") return widthVarPct_;
    if (name == "valVarPct")   return valVarPct_;
    if (name == "gap")         return gap_;
    if (name == "gapVarPct")   return gapVarPct_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"oneShot", ConfigType::Bool, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "oneShot") { oneShot = (value != 0.0f); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "oneShot") return oneShot ? 1.0f : 0.0f;
    return 0.0f;
  }

  SegmentSource(std::vector<float> values, int sr, bool os = false,
                uint32_t seed = 0x5E6A'0000u)
  : values_(std::move(values)), sampleRate(sr), oneShot(os), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(1.0f)),
    smoothness_(std::make_shared<ConstantSource>(0.5f)),
    widthVarPct_(std::make_shared<ConstantSource>(0.0f)),
    valVarPct_(std::make_shared<ConstantSource>(0.0f)),
    gap_(std::make_shared<ConstantSource>(0.0f)),
    gapVarPct_(std::make_shared<ConstantSource>(0.0f))
  {
    widthIsSecs_ = !values_.empty() && values_[0] < 1.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (amplitude_) amplitude_->prepare(ctx, frames);
    if (smoothness_) smoothness_->prepare(ctx, frames);
    if (widthVarPct_) widthVarPct_->prepare(ctx, frames);
    if (valVarPct_) valVarPct_->prepare(ctx, frames);
    if (gap_) gap_->prepare(ctx, frames);
    if (gapVarPct_) gapVarPct_->prepare(ctx, frames);
    done_ = false;
    update_segments();
  }

  float next() override {
    amplitude_->next();
    smoothness_->next();
    gap_->next();
    gapVarPct_->next();
    widthVarPct_->next();
    valVarPct_->next();

    interp_.setSmoothness(smoothness_->current());

    if (done_) { cur_ = 0.0f; return cur_; }

    if (currSegCount_ >= int(currVals_[currSeg_ * 2])) {
      if (currSeg_ >= int(currVals_.size()) / 2 - 1) {
        if (oneShot) { done_ = true; cur_ = 0.0f; return cur_; }
        else update_segments();
      } else {
        currSeg_++;
        currSegCount_ = 0;
      }
    }

    currSegCount_++;

    float prevVal = (currSeg_ == 0) ? 0.0f : currVals_[currSeg_ * 2 - 1];
    float nextVal = currVals_[currSeg_ * 2 + 1];
    float width = currVals_[currSeg_ * 2];
    float pos = (width > 0.0f) ? float(currSegCount_) / width : 1.0f;

    cur_ = interp_.interpolate(prevVal, nextVal, pos) * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  void update_segments() {
    float gapSamples = std::round(
        rng_.range(gap_->current() * (1.0f - gapVarPct_->current()),
                   gap_->current() * (1.0f + gapVarPct_->current()))
        * (widthIsSecs_ ? float(sampleRate) : 1.0f));
    int gapInt = int(gapSamples);

    int pairCount = int(values_.size()) / 2;
    int totalPairs = pairCount + (gapInt > 0 ? 1 : 0);
    currVals_.resize(totalPairs * 2);

    float wvp = widthVarPct_->current();
    float vvp = valVarPct_->current();
    float rate = widthIsSecs_ ? float(sampleRate) : 1.0f;

    for (int i = 0; i < pairCount; ++i) {
      currVals_[i * 2] = std::round(rng_.range(
          values_[i * 2] * (1.0f - wvp), values_[i * 2] * (1.0f + wvp)) * rate);
      currVals_[i * 2 + 1] = rng_.range(
          values_[i * 2 + 1] * (1.0f - vvp), values_[i * 2 + 1] * (1.0f + vvp));
    }

    if (gapInt > 0) {
      currVals_[(totalPairs - 1) * 2] = float(gapInt);
      currVals_[(totalPairs - 1) * 2 + 1] = 0.0f;
    }

    currSeg_ = 0;
    currSegCount_ = 0;
  }

  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> smoothness_;
  std::shared_ptr<ValueSource> widthVarPct_;
  std::shared_ptr<ValueSource> valVarPct_;
  std::shared_ptr<ValueSource> gap_;
  std::shared_ptr<ValueSource> gapVarPct_;
  std::vector<float> values_;
  std::vector<float> currVals_;
  bool widthIsSecs_{false};
  bool done_{false};
  int currSeg_{0};
  int currSegCount_{0};
  Randomizer rng_;
  SmoothnessInterpolator interp_{0.0f, false};
  float cur_{0.0f};
};

} // namespace mforce

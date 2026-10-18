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
struct SegmentSource final : Generator {
  // In the order they are advanced.
  Param amplitude  {this, "amplitude",   1.0f, 0.0f,  10.0f, "0-1"};
  Param smoothness {this, "smoothness",  0.5f, 0.0f,  1.0f,  "0-1"};
  // Global time-scale of the whole shape: 1 = as drawn, <1 narrower, >1
  // wider — every segment width multiplied, read once per cycle. Gap is NOT
  // scaled (it's its own knob).
  Param width      {this, "width",       1.0f, 0.05f, 20.0f, "ratio"};
  Param gap        {this, "gap",         0.0f, 0.0f,  10.0f, "sec"};
  Param gapVarPct  {this, "gapVarPct",   0.0f, 0.0f,  1.0f,  "0-1"};
  Param widthVarPct{this, "widthVarPct", 0.0f, 0.0f,  1.0f,  "0-1"};
  Param valVarPct  {this, "valVarPct",   0.0f, 0.0f,  1.0f,  "0-1"};
  Setting<bool> oneShot{this, "oneShot", false, false, true};
  // timeMode (shape_editor_design.md): explicit units for the widths.
  // "auto" keeps the legacy first-width inference (values[0] < 1 =>
  // seconds) so patches without the setting are byte-identical; the
  // shape editor always writes samples/seconds explicitly.
  Setting<int> timeMode{this, "timeMode", 0, 0, 2, kTimeModeLabels};
  // values[] is the [width0, val0, width1, val1, ...] segment table. The
  // patch loader's generic set_array path uses this to wire arbitrary
  // shapes from JSON. widthIsSecs_ is re-derived from values[0] each time.
  Array values{this, "values", nullptr, 0.0f, -10.0f, 10.0f};

  explicit SegmentSource(std::vector<float> initial, bool os = false,
                         uint32_t seed = 0x5E6A'0000u)
  : rng_(seed)
  {
    values.values = std::move(initial);
    oneShot.value = os;
    refresh_units_();
  }

  const char* type_name() const override { return "SegmentSource"; }
  SourceCategory category() const override { return SourceCategory::Generator; }

  void on_setting_changed(std::string_view name) override {
    if (name == "timeMode") {
      timeMode.clamp();
      refresh_units_();
    }
  }

  void on_array_changed(std::string_view name) override {
    if (name == "values") refresh_units_();
  }

  void on_start(int) override {
    done_ = false;
    // Deferred to the first compute(): update_segments() reads the
    // width/var/gap pins via current(), and at start() time a wired
    // chain (e.g. Note→Curve→width) still holds the PREVIOUS note's value
    // on a reused voice — Matt heard it as nondeterministic clicks/thumps
    // per keypress. The first compute() runs after the pins advance for
    // THIS note.
    pendingInit_ = true;
  }

  float compute() override {
    interp_.setSmoothness(smoothness.current());

    if (pendingInit_) { pendingInit_ = false; update_segments(); }

    // Empty shape (a node fresh from the menu, or values cleared) used to
    // index currVals_[0] out of bounds on the first sample.
    if (done_ || currVals_.size() < 2) return 0.0f;

    if (currSegCount_ >= int(currVals_[currSeg_ * 2])) {
      if (currSeg_ >= int(currVals_.size()) / 2 - 1) {
        if (oneShot.value) { done_ = true; return 0.0f; }
        else update_segments();
      } else {
        currSeg_++;
        currSegCount_ = 0;
      }
    }

    currSegCount_++;

    float prevVal = (currSeg_ == 0) ? 0.0f : currVals_[currSeg_ * 2 - 1];
    float nextVal = currVals_[currSeg_ * 2 + 1];
    float segWidth = currVals_[currSeg_ * 2];
    float pos = (segWidth > 0.0f) ? float(currSegCount_) / segWidth : 1.0f;

    return interp_.interpolate(prevVal, nextVal, pos) * amplitude.current();
  }

private:
  static constexpr const char* kTimeModeLabels[] = {
    "auto", "samples", "seconds", nullptr
  };

  void refresh_units_() {
    if (timeMode.value == 1)      widthIsSecs_ = false;
    else if (timeMode.value == 2) widthIsSecs_ = true;
    else widthIsSecs_ = !values.values.empty() && values.values[0] < 1.0f;  // legacy auto
  }

  void update_segments() {
    const std::vector<float>& shape = values.values;
    float gapSamples = std::round(
        rng_.range(gap.current() * (1.0f - gapVarPct.current()),
                   gap.current() * (1.0f + gapVarPct.current()))
        * (widthIsSecs_ ? float(sample_rate()) : 1.0f));
    int gapInt = int(gapSamples);

    int pairCount = int(shape.size()) / 2;
    float wvp = widthVarPct.current();
    float vvp = valVarPct.current();
    float wscale = width.current();
    float rate = widthIsSecs_ ? float(sample_rate()) : 1.0f;

    currVals_.clear();
    currVals_.reserve((pairCount + 1) * 2);
    if (wscale == 1.0f) {
      // Exact legacy path — byte-identical at the default width.
      for (int i = 0; i < pairCount; ++i) {
        currVals_.push_back(std::round(rng_.range(
            shape[i * 2] * (1.0f - wvp), shape[i * 2] * (1.0f + wvp)) * rate));
        currVals_.push_back(rng_.range(
            shape[i * 2 + 1] * (1.0f - vvp), shape[i * 2 + 1] * (1.0f + vvp)));
      }
    } else {
      // Error-diffused width scaling (2026-08-30): per-segment rounding
      // floored every segment at 1 sample (the stepping consumes >=1 per
      // segment), so wscale<1 was inaudible on unit-width sampled shapes.
      // Diffusion drops segments whose scaled width rounds to zero — a
      // width-1 texture at 0.5 emits every other point (decimation).
      // Draw count per pair matches the legacy path, so varPct streams
      // stay aligned across width changes.
      float accExact = 0.0f;
      long long emitted = 0;
      for (int i = 0; i < pairCount; ++i) {
        const float w = rng_.range(
            shape[i * 2] * (1.0f - wvp), shape[i * 2] * (1.0f + wvp))
            * rate * wscale;
        const float v = rng_.range(
            shape[i * 2 + 1] * (1.0f - vvp), shape[i * 2 + 1] * (1.0f + vvp));
        accExact += std::max(0.0f, w);
        const long long target = (long long)std::llround(accExact);
        const long long wi = target - emitted;
        if (wi <= 0) continue;
        emitted = target;
        currVals_.push_back(float(wi));
        currVals_.push_back(v);
      }
      if (currVals_.empty() && pairCount > 0) {
        // Degenerate scale: keep one segment so the shape isn't silence.
        currVals_.push_back(1.0f);
        currVals_.push_back(shape[pairCount * 2 - 1]);
      }
    }

    if (gapInt > 0) {
      currVals_.push_back(float(gapInt));
      currVals_.push_back(0.0f);
    }

    currSeg_ = 0;
    currSegCount_ = 0;
  }

  std::vector<float> currVals_;
  bool widthIsSecs_{false};
  bool done_{false};
  bool pendingInit_{false};
  int currSeg_{0};
  int currSegCount_{0};
  // Per-note determinism (onsets-v2 addendum): re-anchor draws.
  void reseed() override { rng_.reanchor(); }
  Randomizer rng_;
  SmoothnessInterpolator interp_{0.0f, false};
};

} // namespace mforce

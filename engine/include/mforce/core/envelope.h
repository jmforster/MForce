#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>
#include <algorithm>

namespace mforce {

// Ported from C# MForce.Utility.RampType
enum class RampType { Linear, Expo, InverseExpo, Sine };

// ---------------------------------------------------------------------------
// Ported from C# MForce.Utility.Ramp
// Moves from startVal to endVal over normalized position [0,1].
// ---------------------------------------------------------------------------
struct Ramp {
  float startVal{0.0f}, endVal{1.0f};
  RampType type{RampType::Linear};
  float power{0.0f};
  float holdPct{0.0f};

  float value(float pos) const {
    if (pos <= holdPct) return startVal;

    float t = (pos - holdPct) / (1.0f - holdPct);
    float range = endVal - startVal;

    if (type == RampType::Linear) {
      return startVal + range * t;
    }

    if (type == RampType::Expo) {
      return startVal < endVal
        ? startVal + range * std::pow(t, power)
        : endVal - range * std::pow(1.0f - t, power);
    }

    if (type == RampType::InverseExpo) {
      return startVal < endVal
        ? endVal - range * std::pow(1.0f - t, power)  // note: inverted vs Expo
        : startVal + range * std::pow(t, power);
    }

    // Sine
    if (power == 0.0f) {
      constexpr float PI = 3.14159265358979323846f;
      return startVal + range * (std::cos((1.0f + t) * PI) + 1.0f) * 0.5f;
    }

    // Pseudo-sine: two spliced expo curves
    if (startVal < endVal) {
      return t < 0.5f
        ? startVal + range * std::pow(t * 2.0f, power) * 0.5f
        : endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f + 0.5f;
    }
    return t < 0.5f
      ? endVal - range * std::pow(1.0f - t * 2.0f, power) * 0.5f
      : startVal + range * std::pow(t * 2.0f, power) * 0.5f + 0.5f;
  }
};

// ---------------------------------------------------------------------------
// Ported from C# StagedValueSource + MEnvelope + preset subclasses.
// A multi-stage envelope where each stage is a Ramp with timing controls.
// ---------------------------------------------------------------------------
struct Envelope : ValueSource {

  struct Stage {
    Ramp ramp;
    float percent{0.0f};  // fraction of total duration; 0 = expand to fill
    float minSec{0.0f};
    float maxSec{0.0f};
  };

  explicit Envelope(int sampleRate) : sampleRate_(sampleRate) {}

  const char* type_name() const override { return "Envelope"; }
  SourceCategory category() const override { return SourceCategory::Envelope; }

  // Per-instance decorrelation knobs (multiplex / sound-design). Defaults
  // reproduce pre-2026-05-06 behavior.
  // stage_accuracy: 1.0 = exact stage durations; <1.0 = each non-expand
  //   stage's duration is scaled by a random factor in [stage_accuracy, 1].
  // ramp_accuracy:  1.0 = unmodulated; <1.0 = output multiplied by
  //   1 + (1 - ramp_accuracy) * lfo, lfo in [-1, 1].
  float stage_accuracy{1.0f};
  float timeScale_{1.0f};
  float ramp_accuracy{1.0f};

  // Output range mapping (PerformSource P1, perform_source_design.md §2.4):
  // returned value = lo + (hi - lo) * raw, raw being the 0..1-domain stage
  // value. Null = 0/1 = identity (bit-compatible). Pullable params per the
  // credo — velocity→maxValue is the garden velocity wiring once
  // PerformSource lands. ALL internal state (gate anchor, sustain rewrite)
  // stays in the raw domain; only next()/current() report mapped values.
  std::shared_ptr<ValueSource> minValue_;
  std::shared_ptr<ValueSource> maxValue_;

  // Stage-timing semantics (2026-08-06, piano-chuff fix). Default false:
  // Stage.percent is a FRACTION of note duration, clamped to
  // [minSec, maxSec] — the semantics every pre-existing patch was authored
  // against (bit-exact preserved). Opt-in true (JSON "timeMode":"seconds"
  // on the adsr preset, or make_adsr_abs): Stage.percent is a LITERAL
  // duration in seconds; minSec/maxSec are ignored and the only clamp is
  // the physical one to [0, note duration]. An 8 ms attack renders as 8 ms
  // whether the note lasts 0.5 s or 10 s, and a fixed decay no longer
  // varies with note length. The expand stage (percent == 0) still fills
  // the remainder in both modes.
  bool absolute_time{false};

  // Engine-wide reflection allowance: envelopes lay their stages out over
  // (frames - allowance) so bounded internal-reflection dispersal (piano
  // body reflections are 1-5 ms; Haas fusion ends ~30 ms) can finish
  // INSIDE the note. Flipped 0 -> 10 ms 2026-08-13 after the corpus null
  // test passed at 0 — an intentional corpus-wide 10 ms layout shift.
  static constexpr float kReflectionAllowanceSec = 0.010f;

  void set_gated(bool g) { gated_ = g; }

  // Live note-off: jump to the START of the release phase NOW — the first
  // stage after the expand/sustain stage (the final stage when no expand
  // exists; for adsr that is the same stage). The jumped-to stage is
  // re-anchored to the current output (click-free); later release stages
  // (e.g. the damper's hold-closed choke window) then run normally.
  // Returns the remaining release frames so the caller can bound the
  // voice's lifetime. Safe on the audio thread under the audio mutex — no
  // allocation.
  int gate_release() {
    if (stages_.empty() || stageCounts_.empty()) return 0;
    int last = int(stages_.size()) - 1;
    // When the expand IS the last stage (make_adsr_abs release-0 quirk,
    // AS shapes) there is nothing to release INTO: report 0 so the
    // voice's post-key-up lifetime is set by envelopes that do have a
    // release phase (the damper), not by the held expand's nominal
    // remainder — which made a live staccato voice outlive its sound by
    // seconds while the thud rang the re-opened string (the 2026-08-13
    // QWERTY vibrating-tail bug). Earlier stages still complete
    // naturally (a hammer strike released mid-attack finishes its burst).
    if (expandIdx_ == last) return 0;
    int relStage = (expandIdx_ >= 0 && expandIdx_ < last) ? expandIdx_ + 1
                                                          : last;
    if (currStage_ >= relStage) {
      int rem = std::max(0, stageEnd_ - ptr_);
      for (int i = currStage_ + 1; i < int(stageCounts_.size()); ++i)
        rem += stageCounts_[i];
      return rem;
    }
    gateFrom_ = cur_;
    gateActive_ = true;
    gateStage_ = relStage;
    stageStart_ = ptr_ + 1;
    currStage_ = relStage;
    stageEnd_ = stageStart_ + stageCounts_[relStage];
    int rem = 0;
    for (int i = relStage; i < int(stageCounts_.size()); ++i)
      rem += stageCounts_[i];
    return rem;
  }

  void set_seed(uint32_t s) { seed_ = s; }
  uint32_t get_seed() const { return seed_; }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"stage_accuracy", ConfigType::Float, 1.0f, 0.0f, 1.0f},
      {"ramp_accuracy",  ConfigType::Float, 1.0f, 0.0f, 1.0f},
      {"sustainLevel",   ConfigType::Float, 0.7f, 0.0f, 1.0f},
      // Keyboard tracking of envelope times (classic synth env-KBD-track):
      // multiplies every stage duration. paramMap-able per note, so a hit
      // envelope can shorten with pitch (treble hammers contact shorter).
      {"timeScale",      ConfigType::Float, 1.0f, 0.01f, 10.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "stage_accuracy") { stage_accuracy = std::clamp(value, 0.0f, 1.0f); return; }
    if (name == "ramp_accuracy")  { ramp_accuracy  = std::clamp(value, 0.0f, 1.0f); return; }
    if (name == "timeScale")      { timeScale_ = std::clamp(value, 0.01f, 10.0f); return; }
    // Live sustain rewrite (decay endVal, expand start/end, release startVal
    // all carry the sustain value). Lets paramMap drive sustainLevel per
    // note (frequency curves). Accepted for adsr-preset envelopes AND for
    // stage-form envelopes whose shape satisfies the exact invariant the
    // rewrite relies on — a preset adsr re-saved as stages (UI roundtrip)
    // must not silently kill its residue curve (3n, v6 res_curve patches:
    // stage-loaded envelopes no-opped the per-note sustain).
    if (name == "sustainLevel" && has_adsr_shape_()) {
      // No [0,1] clamp: sustain is a ramp amplitude and legitimately
      // exceeds 1 in test envelopes (_envacc_test holds 2.6); the old
      // clamp corrupted them the moment anything pushed the value back.
      stages_[1].ramp.endVal   = value;
      stages_[2].ramp.startVal = value;
      stages_[2].ramp.endVal   = value;
      stages_[3].ramp.startVal = value;
      return;
    }
  }

  // The four-slot sustain invariant: 4 stages, the third is the expand
  // (percent 0), and decay-end / expand-start / expand-end / release-start
  // hold one equal value. When it holds, the sustain rewrite is safe by
  // construction whether the envelope came from make_adsr or a stage list.
  bool has_adsr_shape_() const {
    if (adsrLayout_ && stages_.size() == 4) return true;
    return stages_.size() == 4 &&
           stages_[2].percent == 0.0f &&
           stages_[1].ramp.endVal   == stages_[2].ramp.startVal &&
           stages_[2].ramp.startVal == stages_[2].ramp.endVal &&
           stages_[2].ramp.endVal   == stages_[3].ramp.startVal;
  }

  float get_config(std::string_view name) const override {
    if (name == "stage_accuracy") return stage_accuracy;
    if (name == "ramp_accuracy")  return ramp_accuracy;
    if (name == "timeScale")      return timeScale_;
    if (name == "sustainLevel")
      return has_adsr_shape_() ? stages_[2].ramp.startVal : 0.0f;
    return 0.0f;
  }

  void add_stage(Stage s) { stages_.push_back(s); }

  int stage_count() const { return int(stages_.size()); }
  Stage&       stage(int i)       { return stages_[i]; }
  const Stage& stage(int i) const { return stages_[i]; }

  // Append a stage with sensible defaults. startVal continues the prior
  // stage's endVal (or 0 if empty).
  void add_stage_default() {
    Stage s;
    s.ramp.type     = RampType::Linear;
    s.ramp.power    = 0.0f;
    s.ramp.holdPct  = 0.0f;
    s.ramp.startVal = stages_.empty() ? 0.0f : stages_.back().ramp.endVal;
    s.ramp.endVal   = 0.0f;
    s.percent = 0.2f;
    s.minSec  = 0.0f;
    s.maxSec  = 0.0f;  // 0 = unbounded (engine sentinel)
    stages_.push_back(s);
  }

  void remove_stage(int i) {
    if (i >= 0 && i < int(stages_.size())) stages_.erase(stages_.begin() + i);
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"minValue", 0.0f, -100000.0f, 100000.0f},
      {"maxValue", 1.0f, -100000.0f, 100000.0f},
    };
    return descs;
  }
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "minValue") { minValue_ = std::move(src); return; }
    if (name == "maxValue") { maxValue_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "minValue") return minValue_;
    if (name == "maxValue") return maxValue_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (minValue_) minValue_->prepare(ctx, frames);
    if (maxValue_) maxValue_->prepare(ctx, frames);
    totalFrames_ = frames;
    // Note-contained sound (2026-08-13): stages lay out over the layout
    // window; the trailing allowance stays programmatically silent so
    // bounded reflection dispersal can finish inside the note.
    const int allowFrames = int(kReflectionAllowanceSec * float(sampleRate_));
    const int layoutFrames = std::max(0, frames - allowFrames);
    float duration = float(layoutFrames) / float(sampleRate_);

    // Reseed both RNGs from seed_ each prepare so renders are deterministic
    // for a given (seed, frames) pair. Multiplex perturbs seed_ per instance.
    rng_.seed(seed_);
    lfo_rng_.seed(seed_ ^ 0xDEADBEEFu);
    lfo_pos_ = 1;
    lfo_period_frames_ = 0;
    lfo_from_ = 0.0f;
    lfo_to_   = 0.0f;

    stageCounts_.resize(stages_.size(), 0);

    int expandIdx = -1;
    int totCount = 0;

    for (int i = 0; i < int(stages_.size()); ++i) {
      if (stages_[i].percent == 0.0f) {
        expandIdx = i;     // last 0% stage becomes expand
        stageCounts_[i] = 0;
        continue;
      }

      float stgDur = absolute_time ? stages_[i].percent          // literal seconds
                                   : duration * stages_[i].percent;
      stgDur *= timeScale_;
      // Apply stage_accuracy: multiply by a random factor in [stage_accuracy, 1]
      // so multiplex clones get jittered ramp lengths. No-op when == 1.
      if (stage_accuracy < 1.0f) {
        std::uniform_real_distribution<float> dist(stage_accuracy, 1.0f);
        stgDur *= dist(rng_);
      }
      stgDur = absolute_time
        ? std::clamp(stgDur, 0.0f, duration)                     // physical clamp only
        : std::clamp(stgDur, stages_[i].minSec, stages_[i].maxSec > 0 ? stages_[i].maxSec : stgDur);
      stageCounts_[i] = int(std::lround(stgDur * sampleRate_));

      // Rounding fix for last non-expand stage
      if (i == int(stages_.size()) - 1 && expandIdx < 0) {
        if (std::abs(totCount + stageCounts_[i] - layoutFrames) <= 1)
          stageCounts_[i] = layoutFrames - totCount;
      }

      totCount += stageCounts_[i];
    }

    if (expandIdx >= 0) {
      stageCounts_[expandIdx] = std::max(0, layoutFrames - totCount);
      // Gated notes need a live expand stage to hold in; a zero-count
      // expand (short nominal duration) would fall straight into release.
      if (gated_ && stageCounts_[expandIdx] == 0) stageCounts_[expandIdx] = 1;
    }
    expandIdx_ = expandIdx;
    gateActive_ = false;

    ptr_ = -1;
    currStage_ = 0;
    stageStart_ = 0;
    stageEnd_ = stageCounts_.empty() ? 0 : stageCounts_[0];
  }

  float next() override {
    float raw = next_raw_();
    if (minValue_ || maxValue_) {
      if (minValue_) minValue_->next();
      if (maxValue_) maxValue_->next();
      const float lo = minValue_ ? minValue_->current() : 0.0f;
      const float hi = maxValue_ ? maxValue_->current() : 1.0f;
      mapped_ = lo + (hi - lo) * raw;
    } else {
      mapped_ = raw;
    }
    return mapped_;
  }

  // Raw 0..1-domain stage evaluation. cur_ and the gate anchor live HERE,
  // in the raw domain — the range mapping applies strictly on the way out.
  float next_raw_() {
    // Pre-prepare safety: stageCounts_ is sized in prepare(); without it,
    // stageCounts_[currStage_] indexes a null buffer. Reachable when UI
    // display paths (e.g. draw_formant_strip) cascade next() through a
    // ValueSource chain that includes an unprepared Envelope.
    if (stageCounts_.empty()) { cur_ = 0.0f; return cur_; }

    if (gated_ && currStage_ == expandIdx_ && expandIdx_ >= 0 &&
        ptr_ + 1 >= stageEnd_) {
      // Held note: sit on the expand stage until gate_release().
      cur_ = stages_[currStage_].ramp.value(1.0f);
      if (ramp_accuracy < 1.0f)
        cur_ *= 1.0f + (1.0f - ramp_accuracy) * lfo_next_();
      return cur_;
    }

    ++ptr_;

    // Advance stage if needed
    while (ptr_ >= stageEnd_ && currStage_ < int(stages_.size()) - 1) {
      stageStart_ = stageEnd_;
      ++currStage_;
      if (currStage_ >= int(stageCounts_.size())) {
        cur_ = 0.0f; return cur_;
      }
      stageEnd_ += stageCounts_[currStage_];
    }

    // Past last stage → output 0. (This is also the reflection-allowance
    // window: every envelope is programmatically silent there. A damper
    // env emitting 0 = lifted for those last 10 ms is inaudible — the
    // string residual is already at the containment floor.)
    if (ptr_ >= stageEnd_ && currStage_ == int(stages_.size()) - 1) {
      cur_ = 0.0f;
      return cur_;
    }

    int count = stageCounts_[currStage_];
    float pos = (count > 0) ? float(ptr_ - stageStart_) / float(count) : 1.0f;
    cur_ = stages_[currStage_].ramp.value(pos);
    if (gateActive_ && currStage_ == gateStage_) {
      // Jumped-to stage re-anchored to the value at gate_release() so a
      // mid-attack key-up releases from where it was, click-free.
      const Ramp& r = stages_[currStage_].ramp;
      float denom = r.endVal - r.startVal;
      float shape = (std::fabs(denom) > 1e-9f) ? (cur_ - r.startVal) / denom
                                               : pos;
      cur_ = gateFrom_ + (r.endVal - gateFrom_) * shape;
    }

    // ramp_accuracy: multiply by 1 + (1 - ramp_accuracy) * lfo, lfo in [-1,1].
    if (ramp_accuracy < 1.0f) {
      cur_ *= 1.0f + (1.0f - ramp_accuracy) * lfo_next_();
    }
    return cur_;
  }

  float current() const override { return mapped_; }

  // ----- Preset factories -----

  // Attack → Release (expand)
  static Envelope make_ar(int sampleRate, float attackPct, float attackMin = 0.0f, float attackMax = 1.0f) {
    Envelope env(sampleRate);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, attackPct, attackMin, attackMax});
    env.add_stage({{1.0f, 0.0f, RampType::Sine,   0.0f}, 0.0f, 0.0f, 0.0f});  // expand
    return env;
  }

  // Attack → Decay → Sustain (expand) → Release
  static Envelope make_adsr(int sampleRate,
                            float attackPct, float decayPct, float sustainLevel, float releasePct,
                            float attackMin = 0.05f, float attackMax = 1.0f,
                            float decayMin = 0.025f, float decayMax = 0.5f,
                            float releaseMin = 0.0f, float releaseMax = 0.0f) {
    Envelope env(sampleRate);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, attackPct, attackMin, attackMax});
    env.add_stage({{1.0f, sustainLevel, RampType::Linear, 0.0f}, decayPct, decayMin, decayMax});
    env.add_stage({{sustainLevel, sustainLevel, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f}); // expand
    env.add_stage({{sustainLevel, 0.0f, RampType::Sine, 0.0f}, releasePct, releaseMin, releaseMax});
    env.adsrLayout_ = true;  // enables live sustainLevel rewrites via set_config
    return env;
  }

  // Attack → Decay → Sustain (expand) → Release with LITERAL-SECONDS stage
  // times (absolute_time = true; JSON: preset "adsr" + "timeMode":"seconds").
  // No min/max stage clamps — the fractional preset's 0.05 s attack floor is
  // what smeared the piano's locked 8 ms attack into a 50 ms chuff and made
  // knock decay vary with note duration (dsp run 22 diagnosis). A 0-second
  // stage renders as 0 frames (percent == 0 keeps its expand meaning, so
  // decay 0 simply vanishes and sustain expands; release 0 keeps the
  // documented "release fills the sustain region" quirk).
  static Envelope make_adsr_abs(int sampleRate,
                                float attackSec, float decaySec,
                                float sustainLevel, float releaseSec) {
    Envelope env(sampleRate);
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, attackSec, 0.0f, 0.0f});
    env.add_stage({{1.0f, sustainLevel, RampType::Linear, 0.0f}, decaySec, 0.0f, 0.0f});
    env.add_stage({{sustainLevel, sustainLevel, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f}); // expand
    env.add_stage({{sustainLevel, 0.0f, RampType::Sine, 0.0f}, releaseSec, 0.0f, 0.0f});
    env.adsrLayout_ = true;
    env.absolute_time = true;
    return env;
  }

  // NOTE (2026-08-13): a piano damper envelope is a plain 3-stage Envelope
  // authored in stage-list form — hold-open (expand at 0), fast 0->1 drop
  // (the felt lands in ~40 ms; min=max pins it), hold-closed choke window
  // (the string physics kills the ring; standard pct+max triple). It has
  // NO preset on purpose: presets are UI conveniences, and inventing one
  // for a single patch duplicated dispatch across both loaders (the exact
  // drift bug class the patch linter exists for). See gen_v6m.py.

private:
  // Smoothed-random LFO for ramp_accuracy. Cosine interpolation between
  // random ±1 targets; per-segment period jitters ±50% around base = N/10.
  void start_lfo_segment_() {
    lfo_from_ = lfo_to_;
    std::uniform_real_distribution<float> targetDist(-1.0f, 1.0f);
    lfo_to_ = targetDist(lfo_rng_);
    int basePeriod = std::max(1, totalFrames_ / 10);
    std::uniform_real_distribution<float> jitter(0.5f, 1.5f);
    lfo_period_frames_ = std::max(1, int(basePeriod * jitter(lfo_rng_)));
    lfo_pos_ = 0;
  }

  float lfo_next_() {
    if (lfo_pos_ >= lfo_period_frames_) start_lfo_segment_();
    float t = float(lfo_pos_) / float(lfo_period_frames_);
    constexpr float PI = 3.14159265358979323846f;
    float smooth = (1.0f - std::cos(t * PI)) * 0.5f;
    ++lfo_pos_;
    return lfo_from_ + (lfo_to_ - lfo_from_) * smooth;
  }

  int sampleRate_;
  int totalFrames_{0};
  int ptr_{-1};
  int currStage_{0};
  int stageStart_{0};
  int stageEnd_{0};
  float cur_{0.0f};      // raw 0..1-domain stage value (gate anchor domain)
  float mapped_{0.0f};   // range-mapped output — what next()/current() report
  std::vector<Stage> stages_;
  // True when stages were built by make_adsr — the fixed 4-stage layout that
  // sustainLevel set_config knows how to rewrite.
  bool adsrLayout_{false};
  // Note-contained sound (2026-08-13 spec):
  // gated_: live-note mode — the expand stage holds until gate_release().
  // endHold_: past the last stage, hold its endVal instead of emitting 0.
  // gateActive_/gateFrom_: final stage re-anchored to the value at
  //   gate_release() so a mid-attack key-up releases from where it was.
  bool  gated_{false};
  int   expandIdx_{-1};
  bool  gateActive_{false};
  int   gateStage_{-1};
  float gateFrom_{0.0f};
  std::vector<int>   stageCounts_;

  // Per-instance decorrelation state
  uint32_t seed_{0};
  std::mt19937 rng_;       // for stage_accuracy duration jitter
  std::mt19937 lfo_rng_;   // for ramp_accuracy LFO
  int   lfo_pos_{0};
  int   lfo_period_frames_{0};
  float lfo_from_{0.0f};
  float lfo_to_{0.0f};
};

} // namespace mforce

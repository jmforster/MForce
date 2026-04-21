#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/envelope.h"
#include "mforce/music/basics.h"
#include "mforce/music/pitch_curve.h"
#include <cmath>
#include <memory>

namespace mforce {

// PitchBendSource — wraps a semitone-offset Envelope and produces
// baseHz * 2^(semi(t)/12) per sample. Lives at the frequency endpoint
// (via ConstantSource::set_override) for the duration of one note.
struct PitchBendSource : ValueSource {
  float baseHz{0.0f};
  std::shared_ptr<Envelope> env;

  PitchBendSource(float hz, std::shared_ptr<Envelope> e)
    : baseHz(hz), env(std::move(e)) {}

  void prepare(const RenderContext& ctx, int frames) override { if (env) env->prepare(ctx, frames); }

  float next() override {
    float semi = env ? env->next() : 0.0f;
    cur_ = baseHz * std::exp2(semi / 12.0f);
    return cur_;
  }
  float current() const override { return cur_; }

  const char* type_name() const override { return "PitchBend"; }
  SourceCategory category() const override { return SourceCategory::Envelope; }
private:
  float cur_{0.0f};
};

// Compile a PitchCurve (Score/Performer semantic) into an Envelope (DSP).
// v1: linear ramps only; nodes define hold regions, trans straddles boundaries.
//
// Pattern: for N semi[] nodes, the Envelope has up to 2*N-1 stages:
//   hold_0, ramp_{0→1}, hold_1, ramp_{1→2}, ..., hold_{N-1}
// Each hold takes (dur[i] - trans/2 for interior, - trans on one side for
// endpoints). Each ramp takes trans of total duration. Last hold may be
// the "fill" stage (percent=0, expand to remainder).
inline std::shared_ptr<Envelope> compile_pitch_curve(const PitchCurve& curve,
                                                     int sampleRate) {
  curve.validate();
  const size_t n = curve.semi.size();
  auto env = std::make_shared<Envelope>(sampleRate);

  // Fill-last detection: zero dur at last index means expand.
  bool fillLast = (curve.dur.back() == 0.0f);

  for (size_t i = 0; i < n; ++i) {
    const float semi = curve.semi[i];

    // Hold stage duration: dur[i] minus adjacent ramp-halves.
    float holdPct = curve.dur[i];
    if (i > 0)     holdPct -= curve.trans[i - 1] * 0.5f;
    if (i + 1 < n) holdPct -= curve.trans[i] * 0.5f;
    if (holdPct < 0.0f) holdPct = 0.0f;

    bool isFill = (fillLast && i == n - 1);

    Envelope::Stage hold;
    hold.ramp = Ramp{semi, semi, RampType::Linear, 0.0f};
    hold.percent = isFill ? 0.0f : holdPct;  // 0 = expand-to-fill
    env->add_stage(hold);

    if (i + 1 < n) {
      Envelope::Stage ramp;
      ramp.ramp = Ramp{semi, curve.semi[i + 1], RampType::Linear, 0.0f};
      ramp.percent = curve.trans[i];
      env->add_stage(ramp);
    }
  }

  return env;
}

// ---------------------------------------------------------------------------
// Performer compilers — Score-symbol → PitchCurve with Performer-chosen timing.
// ---------------------------------------------------------------------------

// Bend articulation: direction=-1 starts below nominal and rises to pitch
// (bend-up attack); direction=+1 starts above and releases to nominal
// (pre-bent attack). Performer picks the timing.
inline PitchCurve compile_bend(const articulations::Bend& b) {
  const float offset = float(b.direction * b.semitones);
  PitchCurve c;
  c.semi  = {offset, 0.0f};
  c.dur  = {0.15f, 0.0f};  // 15% ramp region; last 0 = fill remainder
  c.set_uniform_trans(0.1f);
  return c;
}

// BendMordent ornament: holds nominal briefly, bends to aux, returns, holds
// nominal for the remainder. Full excursion lives inside one note's sustain.
inline PitchCurve compile_bend_mordent(const BendMordent& bm) {
  const float aux = float(bm.direction * bm.semitones);
  PitchCurve c;
  c.semi  = {0.0f, aux, 0.0f};
  c.dur  = {0.12f, 0.15f, 0.0f};  // brief setup, aux hold, fill remainder
  c.set_uniform_trans(0.08f);
  return c;
}

// Slide run input: a single note that the Performer can realize (noteNumber,
// duration in beats, and optional Slide.speed if this note carries a Slide).
struct SlideRunNote {
  float noteNumber;
  float durationBeats;
  float slideSpeed{0.0f};  // 0 for anchor (no slide); speed value for slide notes
};

// Combine a slide run (anchor + N slide-marked notes) into a single PitchCurve.
// semi[i]  = noteNumber[i] − anchor.noteNumber  (semitone offset from anchor)
// dur[i]   = durationBeats[i] / totalBeats
// trans[i] = 2 * slide[i+1].speed * dur[i+1]
//            (ramp straddles boundary; chosen so that `speed` fraction of
//             note i+1's duration is occupied by the ramp — half of the
//             ramp lies in i's tail, half in i+1's head).
// The curve's full duration equals the sum of all input note durations.
inline PitchCurve combine(const std::vector<SlideRunNote>& run) {
  if (run.size() < 2)
    throw std::runtime_error("combine: slide run must have at least 2 notes");

  float totalBeats = 0.0f;
  for (const auto& n : run) totalBeats += n.durationBeats;
  if (totalBeats <= 0.0f)
    throw std::runtime_error("combine: total duration must be positive");

  const float anchorNN = run.front().noteNumber;
  const size_t n = run.size();

  PitchCurve c;
  c.semi.resize(n);
  c.dur.resize(n);
  c.trans.resize(n - 1);

  for (size_t i = 0; i < n; ++i) {
    c.semi[i] = run[i].noteNumber - anchorNN;
    c.dur[i] = run[i].durationBeats / totalBeats;
  }
  for (size_t i = 0; i + 1 < n; ++i) {
    c.trans[i] = 2.0f * run[i + 1].slideSpeed * c.dur[i + 1];
  }
  return c;
}

} // namespace mforce

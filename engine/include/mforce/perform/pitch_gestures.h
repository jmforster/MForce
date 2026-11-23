#pragma once
#include "mforce/music/basics.h"
#include "mforce/contract/pitch_curve.h"
#include <stdexcept>
#include <vector>

namespace mforce {

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

#pragma once
#include "mforce/music/structure.h"
#include <cmath>

namespace mforce {

namespace passage_anchors {

// grid_complete(passage, grid) — quantize the passage's TOTAL length to the
// grid by adjusting the last note, and return the delta applied.
//
// A passage, like a phrase, stops at a barline, not mid-beat: without this a
// passage ends at things like 9.38 or 37.25 beats. The cause is upstream and
// deliberate: sample_cell DRAWS its pulse per candidate (without that draw
// every take would render the same), and nothing else reconciles the
// accumulated total with the beat grid.
//
// NEAREST, not ceiling: ceiling every phrase onto the NEXT barline adds
// +0.25 beats at the median on top of an already-correct draw.
//
// Safe to mutate: Phrase's copy ctor deep-clones through Figure::clone(), so a
// composed Passage's units are its own and cannot reach realizedMotifs. The
// "figures are never mutated" convention is about the motif pool.
inline float grid_complete(Passage& passage, float grid) {
  if (grid <= 0.0f) return 0.0f;

  FigureUnit* last = nullptr;
  float total = 0.0f;
  for (auto& phrase : passage.phrases) {
    for (auto& fig : phrase.figures) {
      if (!fig) continue;
      for (auto& u : fig->units) {
        total += u.duration;
        last = &u;
      }
    }
  }
  if (!last || total <= 0.0f) return 0.0f;

  // A shrink that would leave the final note shorter than the tokenizer's own
  // duration grid is worse than the off-grid ending it fixes, so those round
  // up instead. Same for a target of zero.
  constexpr float kMinFinal = 0.25f;
  float target = std::round(total / grid) * grid;
  if (target <= 0.0f || last->duration + (target - total) < kMinFinal) {
    target = std::ceil(total / grid - 1e-6f) * grid;
  }
  const float delta = target - total;
  if (std::abs(delta) < 1e-3f) return 0.0f;

  last->duration += delta;
  return delta;
}

} // namespace passage_anchors

} // namespace mforce

#pragma once
// PartialSet: the abstract base of a set of partials.
#include "mforce/core/driven_part.h"
#include "mforce/source/additive/formant.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// PartialSet — a part that renders partials. The additive source that holds
// one drives it through DrivenPart's lifecycle: prepare once per build, start once
// per note, next once per sample, then one batched sum per sample. A set
// keeps only its declarations, its hooks and its work.
// ---------------------------------------------------------------------------
struct PartialSet : DrivenPart {
  virtual int partial_count() const = 0;
  virtual float get_partial_value(float amplitude, float frequency, float phaseDiff,
                                  int index, Formant* formant, float formantWeight,
                                  float formantFloor) = 0;

  // Batched entry point: add every partial's contribution for one sample to
  // `acc` and return it. One virtual call per sample instead of one per
  // partial — that is what lets the per-partial body inline and hoist its
  // per-sample invariants.
  //
  // The accumulator is threaded in/out rather than each implementer returning
  // a subtotal, so a nested set (CompositePartials) sums in exactly the same
  // order as one flat loop over all partials. Float addition is not
  // associative: subtotalling would change the last bits of the output.
  //
  // The default body is the exact loop the additive source used to run
  // (NaN = past cutoff, gate that partial only), so an implementer that
  // doesn't override keeps identical behaviour.
  virtual float sum_partials(float acc, float amplitude, float frequency,
                             float phaseDiff, Formant* formant,
                             float formantWeight, float formantFloor) {
    const int n = partial_count();
    for (int i = 0; i < n; ++i) {
      float v = get_partial_value(amplitude, frequency, phaseDiff, i,
                                  formant, formantWeight, formantFloor);
      if (std::isnan(v)) continue;
      acc += v;
    }
    return acc;
  }
};

} // namespace mforce

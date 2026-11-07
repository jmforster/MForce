#pragma once
// PartialSet: the abstract base of a set of partials.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// PartialSet — a part that renders partials. The additive source that holds
// one drives it: prepare once per build, start once per note, next once per
// sample, then one batched sum per sample. The base owns the pin lifecycle
// its members share: prepare and start forward to every pin, next advances
// every Param pin, each in declaration order, then the hook runs — so a set
// keeps only its declarations and its work, and a pin it adds is never left
// unprepared. A set's connectable pins are Manual: no sweep runs on a part.
// ---------------------------------------------------------------------------
struct PartialSet : Component {
  void prepare(const RenderContext& ctx) {
    for (auto* pin : pins()) pin->prepare(ctx);
    on_prepare(ctx);
  }
  void start(int frames) {
    for (auto* pin : pins()) pin->start(frames);
    on_start(frames);
  }
  void next() {
    for (auto* pin : pins())
      if (pin->kind == PinKind::Param) static_cast<Param*>(pin)->next();
    on_next();
  }

  virtual int partial_count() const = 0;
  virtual float get_partial_value(float amplitude, float frequency, float phaseDiff,
                                  int index, IFormant* formant, float formantWeight,
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
                             float phaseDiff, IFormant* formant,
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

protected:
  // The hooks: after the pins, in the same order the set would have called them.
  virtual void on_prepare(const RenderContext&) {}
  virtual void on_start(int) {}
  virtual void on_next() {}
};

} // namespace mforce

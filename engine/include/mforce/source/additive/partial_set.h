#pragma once
// PartialSet: the abstract base of a set of partials.
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/additive/formant.h"
#include <array>
#include <cassert>
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
  // prepare also lists the set's Param pins, so next() walks that list and
  // not every setting: the same calls Partials once wrote by hand, through
  // pointers kept inside the node (no heap, as Component's own sweep list).
  void prepare(const RenderContext& ctx) {
    paramCount_ = 0;
    for (auto* pin : pins()) {
      pin->prepare(ctx);
      if (pin->kind == PinKind::Param) {
        assert(paramCount_ < kMaxParams && "a set declares more Param pins than PartialSet::kMaxParams");
        params_[size_t(paramCount_++)] = static_cast<Param*>(pin);
      }
    }
    on_prepare(ctx);
  }
  void start(int frames) {
    for (auto* pin : pins()) pin->start(frames);
    on_start(frames);
  }
  void next() {
    for (int i = 0; i < paramCount_; ++i) params_[size_t(i)]->next();
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

private:
  // The Param pins, listed at prepare in declaration order. No set declares
  // more than eight today; the assert says when one does.
  static constexpr int kMaxParams = 16;
  std::array<Param*, kMaxParams> params_{};
  int paramCount_{0};
};

} // namespace mforce

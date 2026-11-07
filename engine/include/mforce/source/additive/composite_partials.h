#pragma once
// CompositePartials: a PartialSet.
#include "mforce/source/additive/partial_set.h"
#include "mforce/source/additive/formant.h"

namespace mforce {

// ---------------------------------------------------------------------------
// CompositePartials — combines several partial sets into one.
// Concatenates partial arrays from all children. Used to mix, e.g.,
// SequencePartials + ExplicitPartials in a single AdditiveSource.
// Ported from legacy CompositePartials.cs.
// ---------------------------------------------------------------------------
struct CompositePartials final : PartialSet {
  // The sets it concatenates, in wiring order.
  Slot<PartialSet> partials{this, "partials", "a PartialSet part", true};

  const char* type_name() const override { return "CompositePartials"; }

  // --- The set's hooks: each verb forwarded to the sets, in slot order ---
  void on_prepare(const RenderContext& ctx) override {
    for (auto* p : partials.views) p->prepare(ctx);
  }

  void on_start(int frames) override {
    for (auto* p : partials.views) p->start(frames);
  }

  void on_next() override {
    for (auto* p : partials.views) p->next();
  }

  int partial_count() const override {
    int total = 0;
    for (auto* p : partials.views) total += p->partial_count();
    return total;
  }

  float get_partial_value(float amplitude, float frequency, float phaseDiff,
                          int index, IFormant* formant, float formantWeight,
                          float formantFloor) override {
    int offset = 0;
    for (auto* p : partials.views) {
      int count = p->partial_count();
      if (index < offset + count)
        return p->get_partial_value(amplitude, frequency, phaseDiff,
                                    index - offset, formant, formantWeight,
                                    formantFloor);
      offset += count;
    }
    return 0.0f;
  }

  // Walk each set once instead of re-scanning the set list per partial index
  // (the get_partial_value path above is O(sets) per partial). Order matches:
  // sets in declaration order, partials in index order within each set — and
  // threading `acc` through keeps the addition order identical to one flat
  // loop over the concatenated index space.
  float sum_partials(float acc, float amplitude, float frequency,
                     float phaseDiff, IFormant* formant, float fmtWt,
                     float fmtFloor) override {
    for (auto* p : partials.views)
      acc = p->sum_partials(acc, amplitude, frequency, phaseDiff,
                            formant, fmtWt, fmtFloor);
    return acc;
  }
};

} // namespace mforce

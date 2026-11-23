#pragma once
#include "mforce/core/dsp_value_source.h"
#include <vector>

namespace mforce {

// The base of the wave evolutions (the C# IWaveEvolution): a part the
// wavetable holds in its `evolution` slot and drives — prepare once per
// build, start once per note, adjust at a note's first sample,
// shape_excitation after the fill, evolve once per sample. An evolution's
// pins are Manual (it advances them inside evolve, in its own order) and are
// prepared and started here, once for every member of the family.
struct WaveEvolution : Component {
  void prepare(const RenderContext& ctx, int maxLen) {
    for (auto* pin : pins()) pin->prepare(ctx);
    on_prepare(ctx, maxLen);
  }
  void start(int frames) {
    for (auto* pin : pins()) pin->start(frames);
  }

  // Called once per note, at the first sample — adjust internal state to the
  // note's frequency.
  virtual void adjust(float frequency) = 0;

  // Called once per note after the table fill — modify the initial excitation.
  // Default: no-op. Override for pick-position, pick-direction, etc.
  virtual void shape_excitation(std::vector<float>& /*values*/) {}

  // Called each sample — modify the wavetable in-place.
  virtual void evolve(std::vector<float>& values, int index) = 0;

  // maxLen is the longest table the owner will ever fill (its floor
  // frequency's length). An implementation captures ctx.sampleRate here if
  // adjust()/evolve() need it, and reserves every buffer it sizes per note or
  // per table to maxLen, so that adjust, shape_excitation and evolve fill
  // within capacity and allocate nothing.
  virtual void on_prepare(const RenderContext& /*ctx*/, int /*maxLen*/) {}
};

} // namespace mforce

#pragma once
// Formant: the abstract base of a formant.
#include "mforce/core/driven_part.h"

namespace mforce {

// ---------------------------------------------------------------------------
// Formant — a gain over absolute frequency imposed on a source and invariant
// across fundamentals: the filter half of the source-filter pair, where the
// partials are the source's spectrum and move with the pitch. The additive
// source that holds one drives it through DrivenPart's lifecycle — prepare
// once per build, start once per note, next once per sample — and asks its
// gain per partial. A formant answers 0 outside every band it shapes, so the
// consumer's factor is floor + weight × gain everywhere, in band or out.
// ---------------------------------------------------------------------------
struct Formant : DrivenPart {
  virtual float gain(float freq) const = 0;
};

} // namespace mforce

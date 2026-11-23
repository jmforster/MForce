#pragma once

namespace mforce::dsp {

// The polynomial band-limited step (polyBLEP): the correction a phase-accumulated
// waveform adds at a discontinuity so the step's aliasing is smoothed over the
// two samples around it. `pos` is the phase in [0, 1) and `phaseIncr` the
// phase advanced per sample; the correction is non-zero only within one
// increment of the wrap, on either side, and the caller adds it with the sign
// its own discontinuity needs (a saw subtracts, a pulse adds at one edge and
// subtracts at the other).
inline float blep(float pos, float phaseIncr) {
  if (pos < phaseIncr) {
    const float t = pos / phaseIncr;
    return t + t - t * t - 1.0f;
  }
  if (pos > 1.0f - phaseIncr) {
    const float t = (pos - 1.0f) / phaseIncr;
    return t * t + t + t + 1.0f;
  }
  return 0.0f;
}

} // namespace mforce::dsp

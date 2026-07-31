#pragma once
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// fast_sin_turns(x) == sin(2*pi*x) for x in [0,1), the phase convention every
// oscillator in the engine already uses (partialPos_, WaveSource phase, ...).
//
// Why: the additive hot loop spends ~40% of its remaining time inside sinf
// (measured 2026-07-31 by rendering the profiling ladder with the sin call
// removed entirely: 2.15x vs 1.30x for the surrounding optimizations alone).
// sinf is a libm call that does its own range reduction — reduction we do not
// need, because the argument is already a normalized phase.
//
// Accuracy: the argument is folded to a quarter turn exactly (both folds are
// exact float subtractions in their respective ranges), then a degree-9 odd
// minimax polynomial in turns covers [0, 0.25] with max |error| = 3.4e-9.
// That is ~35x below float epsilon (1.19e-7), so the result is at or inside
// the rounding of sinf itself — this is a different rounding, not a coarser
// one. Summed incoherently over ~100 partials the worst-case error floor is
// ~3e-8 of full scale (about -150 dBFS), i.e. ~9 bits below the LSB of a
// 16-bit render. Verified in practice: the 14-patch additive null test comes
// out byte-identical with this substituted for std::sin.
//
// Coefficients fit by iteratively-reweighted least squares against
// sin(2*pi*t) on a 200k-point grid over [0, 0.25].
// ---------------------------------------------------------------------------
inline float fast_sin_turns(float x) {
  // Fold the second half-turn onto the first, carrying the sign.
  // x - 0.5 is exact for x in [0.5, 1).
  float s = 1.0f;
  if (x >= 0.5f) { x -= 0.5f; s = -1.0f; }
  // Fold the second quarter onto the first. 0.5 - x is exact for
  // x in (0.25, 0.5] (Sterbenz).
  if (x > 0.25f) x = 0.5f - x;

  constexpr float K1 =  6.2831851603e+00f;
  constexpr float K3 = -4.1341655081e+01f;
  constexpr float K5 =  8.1601007325e+01f;
  constexpr float K7 = -7.6549859620e+01f;
  constexpr float K9 =  3.9537311267e+01f;

  const float u = x * x;
  float p = K9;
  p = p * u + K7;
  p = p * u + K5;
  p = p * u + K3;
  p = p * u + K1;
  return s * (x * p);
}

} // namespace mforce

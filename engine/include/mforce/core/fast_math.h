#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

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

// ---------------------------------------------------------------------------
// fast_exp2(x) == exp2(x), for the frequency-motion layer in the additive
// partial loop.
//
// Why: `std::exp2` is a CRT call, and the motion layer calls it once per
// partial per sample. Measured with tools/ablate_layers.py on
// patches/viola_default.json (96 partials, motion + shimmer + onset +
// bandwidth all on): zeroing the motion depth alone cut the pre-render from
// 1387 ms to 478 ms — the motion path was 65.6% of the whole loop, and
// essentially all of it was this one libm call. Same class of finding as the
// truncf-is-a-CRT-call one a commit earlier.
//
// Method: split x = k + f with k integer and |f| <= 0.5, evaluate 2^f with a
// degree-6 minimax polynomial, then apply 2^k exactly by building the power of
// two from its exponent bits. Because the scale factor is exact, the fitted
// error IS the total relative error.
//
// Accuracy: max |relative error| of the fit is 1.9e-9; evaluated in float32
// with Horner it is 1.04e-7, i.e. 0.88 float32 epsilon — at the rounding limit
// of the type, not a coarser approximation. Over the range the motion layer
// actually uses (|cents| <= 200, so |f| <= 1/6) it is 6.8e-8 = 0.57 eps.
// Coefficients fit by IRLS on a 400k-point grid:
// research/additive_perf/fit_exp2.py.
//
// Out-of-range behaviour differs from std::exp2 deliberately: x is clamped to
// +-126 so the exponent construction cannot overflow or hit UB in the int
// conversion. exp2 of +-126 is already far outside anything a cents offset can
// produce (that is 126 octaves).
// ---------------------------------------------------------------------------
inline float fast_exp2(float x) {
  if (x > 126.0f) x = 126.0f;
  else if (x < -126.0f) x = -126.0f;

  // Nearest integer via a biased truncation — round-half-away-from-zero, which
  // keeps |f| <= 0.5. float(int(...)) rather than std::floor/roundf for the
  // same reason truncf was replaced: those are CRT calls under SSE2 baseline.
  const int   k  = int(x + (x >= 0.0f ? 0.5f : -0.5f));
  const float f  = x - float(k);

  constexpr float E0 = 1.0000000006e+00f;
  constexpr float E1 = 6.9314720572e-01f;
  constexpr float E2 = 2.4022646892e-01f;
  constexpr float E3 = 5.5503288171e-02f;
  constexpr float E4 = 9.6184889638e-03f;
  constexpr float E5 = 1.3399915275e-03f;
  constexpr float E6 = 1.5345769988e-04f;

  float p = E6;
  p = p * f + E5;
  p = p * f + E4;
  p = p * f + E3;
  p = p * f + E2;
  p = p * f + E1;
  p = p * f + E0;

  // 2^k with k in [-126, 126]: bias into the IEEE-754 exponent field.
  const std::uint32_t bits = std::uint32_t(k + 127) << 23;
  float scale;
  std::memcpy(&scale, &bits, sizeof(scale));
  return p * scale;
}

} // namespace mforce

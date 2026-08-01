#!/usr/bin/env python3
"""Fit the minimax polynomial for fast_exp2 (core/fast_math.h).

Same method as the fast_sin_turns fit: iteratively-reweighted least squares
against the exact function on a dense grid, which converges to the minimax
(equal-ripple) solution. Prints coefficients ready to paste, plus the error
bound that goes in the header comment.

2^f is fitted on f in [-0.5, 0.5]; the integer part of the exponent is applied
exactly by constructing the power of two from its bit pattern, so the fitted
error IS the total relative error of fast_exp2 over its whole range.

Usage: python research/additive_perf/fit_exp2.py [degree]
"""
import sys

import numpy as np

DEG = int(sys.argv[1]) if len(sys.argv) > 1 else 6

x = np.linspace(-0.5, 0.5, 400001)
y = np.exp2(x)

# IRLS toward equal ripple. Relative error is the right objective: the result
# gets multiplied by an exact power of two, so relative error is preserved.
w = np.ones_like(x)
for _ in range(60):
    V = np.vander(x, DEG + 1, increasing=True)
    # weighted least squares on relative error: minimize w*(P(x)-y)/y
    A = V / y[:, None] * w[:, None]
    b = np.ones_like(x) * w
    coef, *_ = np.linalg.lstsq(A, b, rcond=None)
    err = (V @ coef - y) / y
    a = np.abs(err)
    w = w * (1.0 + 4.0 * a / (a.max() + 1e-300))
    w /= w.max()

V = np.vander(x, DEG + 1, increasing=True)
err = (V @ coef - y) / y
print(f"degree {DEG} minimax for 2^f on [-0.5, 0.5]")
print(f"max |relative error| = {np.abs(err).max():.4e}")
print(f"float32 epsilon      = {np.finfo(np.float32).eps:.4e}")
print(f"ratio to eps         = {np.abs(err).max()/np.finfo(np.float32).eps:.3f}")
print()
for i, c in enumerate(coef):
    print(f"  constexpr float E{i} = {c:.10e}f;")

# Sanity: evaluate in float32 with Horner exactly as the header will.
c32 = coef.astype(np.float32)
xf = x.astype(np.float32)
p = np.full_like(xf, c32[-1])
for c in c32[-2::-1]:
    p = (p * xf + c).astype(np.float32)
rel = (p.astype(np.float64) - y) / y
print(f"\nfloat32 Horner max |rel err| = {np.abs(rel).max():.4e} "
      f"({np.abs(rel).max()/np.finfo(np.float32).eps:.2f} eps)")

# The range that actually matters for the motion layer: cents/1200 for a
# plausible motionDepth of up to +-200 cents.
m = np.abs(x) <= 200.0 / 1200.0
print(f"over the motion range |f| <= 1/6: max |rel err| = "
      f"{np.abs(rel[m]).max():.4e}")

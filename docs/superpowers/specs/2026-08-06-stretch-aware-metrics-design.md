# Stretch-aware heterodyne + line mask (dsp backlog 3e-NEXT b)

Dipsy, 2026-08-06. Python-side (research/ml_ears), no engine change.

## Problem

Every refmetrics extractor assumes partials sit at `k*f0`:

- `harmonic_env` reads the max in `[k*f0*(1-0.03), k*f0*(1+0.03)]`
- `motion_stats` heterodynes at `k*f0`
- `broadband_ratios` builds its line mask at `k*f0 +- 0.06*f0`

A piano partial sits at `f_n = n*f0*sqrt(1 + B n^2)`. The absolute offset is
`~f0*B*n^3/2`, so it leaves `broadband_ratios`' fixed `0.06*f0` window once
`n^3 > 0.12/B` — at C4's measured `B ~ 1.1e-4` that is `n > 10.6`. Every
partial above ~11 is then counted as *inter-harmonic energy*, which is the
standing explanation for the piano reference's band2 ratios of 120 (C3),
260 (C5), 2056 (G4) against O(1) for a real bowed string.

`harmonic_env`'s window is relative (`3%` of `k*f0`) so it survives longer,
but fails on the same law: the offset in percent is `B n^2/2`, past 3% at
`n > 23` for C4 and `n > 8` for C6 (`B ~ 9.6e-4`).

`motion_stats` degrades differently — the heterodyne LP half-width is 40 Hz,
so a partial displaced by more than that leaves the passband entirely and the
harmonic drops out at the `a.mean() < 1e-7` break, silently shortening the
analysis.

## Design

1. `partial_freq(f0, B, n) = n*f0*sqrt(1 + B*n*n)`. At `B=0` this is exactly
   `n*f0` (multiplication by 1.0 is exact in IEEE), so every existing caller
   is bit-identical — that is the regression gate, not an argument.

2. `B` becomes a keyword argument, defaulting to `0.0`, on `harmonic_env`,
   `motion_stats` and `broadband_ratios`. In `motion_stats` the cents
   conversion divides the frequency deviation by the partial's own stretched
   centre, not by `k*f0`.

3. `estimate_inharmonicity(x, sr, f0_nom)` — sequential predict → peak → refit
   tracker, least squares on `(f_n/n)^2 = a + b n^2` with one 3-sigma outlier
   pass, `B = b/a`, `f0 = sqrt(a)`. This is the algorithm `piano_analysis.py`
   already uses; it MOVES to refmetrics and piano_analysis imports it, so
   there is one implementation rather than two. (Direction matters:
   piano_analysis imports matplotlib, so the dependency cannot go the other
   way — the scorer inner loop must not pull a plotting stack.)

4. Which `B` scores the candidate: **the reference's**. `B` is LOCKED from
   measurement in the piano encoder, never searched, so both sides should
   agree by construction. Masking both with the reference's `B` means a
   candidate that fails to stretch correctly is penalised (its partials fall
   off the mask) — that is real signal, and it is the opposite of the bug
   being fixed, where the *reference's own* partials fell off the mask.
   The candidate's own `B` is measured anyway and reported as a diagnostic,
   so a stretch mismatch is visible instead of being absorbed into term1/3.

5. Opt-in per instrument: `reference_build.inharmonic: true` (piano only).
   Absent → `B` is never measured and never stored, so the viola and clarinet
   references regenerate byte-identically.

## Verification

- Synthetic ground truth: a signal built from partials at a known `B`, with
  known noise between them. `estimate_inharmonicity` must recover `B`, and
  `broadband_ratios` must return the planted noise ratio with the stretch and
  a hugely inflated one without.
- `B=0` regression: viola + clarinet references regenerate byte-identical.
- No-duplication check: `piano_analysis.py` re-run must reproduce
  `out/piano_analysis_data.json` unchanged after its fitter is replaced by the
  refmetrics import.
- Payoff: piano reference broadband ratios before vs after.

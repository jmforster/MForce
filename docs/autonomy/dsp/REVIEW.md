# DSP lane — review queue

## Awaiting Matt

### 1. v6 frequency-curve ladder [listen] — NEW (run 4)
renders/fable1_v6/. Your v4 verdicts implemented via the new paramMap
curve mechanism (any param/config can now follow a frequency→value curve,
evaluated per note):
- v6_01/02/03 — full calibrated recipe, cluster residue curved lo/mid/hi
  per your guesses (8-15% @ 50 Hz → 25-40% @ 400 Hz). Which curve, or
  where between?
- v6_04 — bandwidth floor curved (1.5% @ 50 Hz → 8% @ 400 Hz), isolated:
  does the low-frequency "rumble" die while texture survives up high?
Verdict decides: default residue/floor curves, and whether curve knots
join the CMA-ES search space (your note that curves complicate it is
handled in the spec update — knots become dims, +4-6 dims).

### 2. CMA-ES stage-d best-of-run [listen] — pending completion
600-eval viola optimization relaunched this morning (the overnight launch
died before writing state). Best-of-run WAV vs v5_04_cal_sustain A/B will
land here when it finishes (~2 h, checkpointed/resumable).

## Resolved

2026-07-28 (Matt):
- v5 grounded A/B: "no worse, maybe better, at the limit of my ability to
  tell" → measured/calibrated values ADOPTED as default sustain recipe;
  backlog 2b closed.
- v4 residue ladder: 25% too much at low frequencies only → the
  frequency-dependence principle (logged to memory), v6 curve ladder above.
- v4_03 bw floor: reads as low-freq "rumble," curve it before abandoning
  → v6_04.
- v4_04/05: attack→sustain transition now smooth.
- UI fix: confirmed good.
- Stage-d render time: "accept long render."

2026-07-27: v3 verdicts (cluster_tight_low wins; sustain stack rich/stringy;
attack too distinct → v4 residue).

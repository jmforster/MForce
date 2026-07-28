# DSP lane — review queue

## Awaiting Matt

### NEW (run 3). CMA-ES optimizer — first result vs hand-tuning [listen]
Renders: renders/cmaes_smoke/ — `opt_best_104ev.wav` vs `v5_04_cal_sustain.wav`
(same 11-note ladder, direct A/B). The optimizer warm-started at v5_04 and ran
104 evals of the Iowa-viola scorer; measured distance 1.28→0.945 (biggest gain
on the inter-harmonic broadband term). It's a SMALL, early move (not converged),
so expect a subtle difference, not a transformation.
What it changed: formantWeight 1.0→0.84, low-body formant boosted / high cut
(band scalers 1.45/1.26/0.96/0.81), spectral reshape (harm ~3 and ~16 up),
onset tilt/spread up — and **vibrato cut hard (depth 0.025→0.010, speed 5.5→
4.4)** because the Iowa refs barely vibrate.
Verdict decides: (1) is the metric pointing the right way — does opt_best sound
closer to a real viola, or just different? (2) whether to invest in the full
600-eval run (needs the render-speed fix first: eval is ~real-time render-bound,
600 evals ≈ 2 h — BACKLOG #3d/#8); (3) whether to **re-pin vibrato** to your
taste before optimizing (the scorer will always minimise it toward the dry Iowa
samples). Background: reports/2026-07-28-dipsy-run3.md.

### 0. v5 Iowa-grounded A/B [listen] — updated run 2
Renders: renders/fable1_v5/. Question: does measurement beat hand-tuning?
- v5_01_iowa_sustain vs v3_05_sustain_stack — sustain character only
- v5_02_iowa_full and v5_03_iowa_shim60 vs v4_04_full_res — full recipe;
  v5_03 backs shimmer 0.9→0.6 in case the measured value pumps
- run 2 adds **v5_04_cal_sustain / v5_05_cal_full** — calibrated variants
  (verified 6.4c vs 7.3c Iowa target). Primary listen: v5_05 vs v5_02 vs
  v4_04.
Verdict decides: whether derive_motion.py's numbers become the default
sustain recipe and whether the calibration cycle (item 2b) is worth a run
before CMA-ES subsumes it. Background: reports/2026-07-27-dipsy-run1.md

### 1. v4 residue ladder [listen]
Renders: renders/fable1_v4/ (5 WAVs). Background: docs/Fable1_results.md.
- v4_01 vs v4_02: cluster residue 10% vs 25% — which level, or between?
- v4_03: bandwidth floor 0.06 in isolation — does permanent slight
  noisiness read as bow/string texture or as hiss?
- v4_04 vs v4_05: full recipe, residue moderate vs high — does the attack→
  sustain transition blur the way you wanted?
Verdict decides: the default viola attack/sustain recipe going forward, and
whether residue amounts become CMA-ES search dimensions or fixed constants.

### 2. UI fix confirmation [look]
Load patches/fable1_v4/v4_04_full_res.json in the rebuilt mforce_ui:
inspector should show maxPartials 96 and keyboard playback should match the
CLI render's timbre. Verdict decides: whether the UI load path has more
lurking gaps (if it still sounds wrong, that's a new bug to chase).

## Resolved

(v3 verdicts folded 2026-07-27: cluster_tight_low wins; sustain_stack
"rich, stringy"; attack_stack good but transition too abrupt → v4)

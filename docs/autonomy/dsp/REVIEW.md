# DSP lane — review queue

## Awaiting Matt

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

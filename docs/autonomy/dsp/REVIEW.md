# DSP lane — review queue

## Awaiting Matt

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

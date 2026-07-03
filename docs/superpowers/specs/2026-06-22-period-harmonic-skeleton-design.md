# Random Harmonic Skeleton for PeriodPassageStrategy

**Date:** 2026-06-22
**Status:** Approved (design); pre-implementation
**Addresses:** Audition issue #1 — the consequent's start pitch was hardwired (always continued onto V); the period's harmonic frame should be randomly chosen.

## Problem

For a monophonic melody, harmony is generated *from* the melody (Phase 2), not the reverse — so "pick a chord progression" can't drive the line. What the listener hears as harmonic movement is the **boundary pitches of the two phrases**: where each phrase starts and ends. Today the antecedent starts on the tonic, ends on V (HC), and the consequent **continues from the running cursor** (lands on V) — hardwired, transposed, and never varied.

## Design

`PeriodPassageStrategy` randomly selects a period's harmonic skeleton from a fixed table, expressed as the start/end scale-degrees of the two phrases: `[s1, e1, s2, e2]`.

**The table (Matt's universe, for now)** — degrees: I=0, IV=3, V=4, vi=5:

| progression | s1 e1 s2 e2 |
|---|---|
| I-V-V-I   | 0 4 4 0 |
| I-I-V-I   | 0 0 4 0 |
| I-I-I-I   | 0 0 0 0 |
| I-IV-V-I  | 0 3 4 0 |
| I-IV-I-I  | 0 3 0 0 |
| I-vi-V-I  | 0 5 4 0 |
| I-IV-vi-I | 0 3 5 0 |

**Mapping to the two pitch levers** (the only things that move a mono line):
- `antecedent.startingPitch` = pitch at degree `s1`; `antecedent.cadenceTarget` = `e1`; `antecedent.cadenceType` = `e1==4 ? 1 : 2`.
- `consequent.startingPitch` = pitch at degree `s2`; `consequent.cadenceTarget` = `e2`; `consequent.cadenceType` = `e2==4 ? 1 : 2`.
- Degree→Pitch resolved with `PitchReader(sec.scale)` anchored at the passage tonic (`seed.startingPitch`), stepping `d` scale-degrees. Boundary pitch = the chord **root** (root-only for now; 3rd/5th deferred).

This also fixes the consequent transposition: setting `consequent.startingPitch` explicitly resets the cursor, so `s2 == I` gives a true tonic restatement and `s2 == V` a deliberate dominant start.

## Where it lives

In `plan_passage` (`period_passage_strategy.h`), at the top of the per-period loop, before variant resolution. It has `locus` → `locus.piece->sections[locus.sectionIdx].scale` and `seed.startingPitch` (tonic), so it can resolve degree→Pitch.

The Parallel/Modified variant branches copy `p.antecedent` into the consequent then override cadence from `p.consequent`; add one line so they **also** copy `p.consequent.startingPitch`.

**Selection seed:** a `Randomizer` seeded from `locus.pieceTemplate->masterSeed` (per period) — so the existing 20-render seed sweep produces varied skeletons.

## Opt-in / opt-out

Apply the random skeleton **only when cadences are unspecified** — `p.antecedent.cadenceType == 0 && p.consequent.cadenceType == 0`. Authored periods (e.g. `test_k467_period`, which sets cadence fields) are left untouched. A template opts *into* randomization by omitting cadence fields. (`patches/period_markov_test.json` will have its cadence fields removed to enable it.)

## Verification

- Render the 20-seed sweep; confirm the chosen skeleton **varies** across seeds (e.g. different `ante_last` pitch classes — not always G).
- Confirm authored `test_k467_period` is byte-identical to before (cadences set → skipped).
- Spot-check a `s2==I` render restates at the tonic (consequent first note == antecedent first note pitch).

## Non-goals / follow-ups

- **Harmony track fidelity:** `cadence_chord` only returns V (type 1) or I (type 2), so IV/vi boundaries aren't reflected in the post-melody harmony track. Mono-irrelevant; extend later when harmony parts matter.
- **Chord tones beyond the root** (boundary on 3rd/5th) — deferred.
- **Author-overridable progression table** (vs hardcoded) — deferred; hardcoded set for now.
- This does not address #2 (weird rhythms) or #4 (cadential approach).

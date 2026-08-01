# DSP lane — review queue

## Awaiting Matt

### 0b. Per-partial rng streams — the reproducibility fix [read, decision]
The re-roll fragility is NOT a perf byproduct — it's latent engine design
(shared rng + order-dependent draws + the >16kHz early-return skipping
draws). The exp2 revert restored the old stream, but ANY future last-bit
frequency change re-rolls high-note bandwidth patches again, and the
CMA-ES objective stays discontinuous. Proposed: per-partial seeded
streams (noise independent of draw order). One-time re-roll of every
bandwidth patch's noise on landing — recommend doing it BEFORE the
clarinet 600-eval run so the objective is continuous. Your call.

### 1. liar2 width-multiplier ladder [listen] (run 13)
renders/liar2/ liar2_110_x3/x5/x7/x10/x14 + same at 220 (literature
widths x multiplier; x7 = the shipped "excellent" scale). Verdict: the
optimal multiplier, or several worth keeping as styles.

### 2. Vowel sequences on the validated architecture [listen] (run 13)
renders/vowelseq2/ — OO-EE, O-OO-EE, AH-O-OO, EE-AH-OO, OO-AH-EE at
110/220, liar2 recipe (crossfade, 5-formant, x7, floor 0), playable
instrument-style patches (word scales with note length; try chords —
your liar2 chord observation).

### 3. Clarinet breath v2 [listen] (run 13)
renders/clarinet_c2/ — your 3 complaints mapped exactly to params the
optimizer drifted from the measurement; c2a_cal = bed hand-set to the
clarinet_bed values you preferred (2900Hz/0.016/0.15s), c2b_pink =
tilted down-spectrum (2300Hz, wider — the white-vs-pink axis), c2c_quiet
= cal at level 0.011. Verdict decides: which bed goes into the 600-eval
run — where bed dims will be FROZEN at your pick (the optimizer
demonstrably wanders off-measurement on them; scorer can't see the bed).

### 4. FM oversample default [read] — unchanged, awaiting your read
(carrier-frequency-and-deviation rule proposal; see run-12 note.)

### 5. FM matrix [listen] — parked at your request until re-listen.

## Resolved

2026-08-01 (Matt, folded in run 15):
- exp2 A/B: fluctuation traced to shimmer walk (always in the patch;
  A == run-9 original bit-for-bit); last-note dip = A's roll only.
  Shimmer GAIN FLOOR queued (approved). Perf: REVERTED fast_exp2 per
  Matt (diminishing returns); truncf + sin stay (bit-exact/cleared).

2026-07-31 (Matt, folded in run 13):
- liar2: upgraded to "excellent" post UI fixes; chords sharpen the word;
  0.5s durations intelligible at high pitch. Width multiplier ladder +
  vowelseq regen delivered (items 1-2 above).
- Clarinet: tone "really nice"; breath fails as separate/loud/too-high/
  too-early — all four = optimizer drift vs measurement; c2 variants
  above; bed dims to be frozen in the long run.
- nulltest: "no audible differences" — perf work cleared for keeps.
- FM matrix: crazy results, re-listen later (parked).

2026-07-30 pm (Matt, folded in run 11):
- Expand round 3: "Nice sounds" — front PARKED at your request until
  tomorrow (backlog holds next steps).
- FormantSeq r2: still buzzy-sawtooth, no vowel character; your
  "weight near 1" read points at the real mechanism — additive-boost
  can't CUT between formants. Run 11 adds formantFloor (out-of-band
  suppression); vowel BASELINE set (OO-EE, O-OO-EE, AH-O-OO, LIAR +
  invented) at low AND high f (your LIAR-at-high-f birdy note) replaces
  further novelty rounds until the baseline sounds like vowels.
- FM alias v2: C8 pair decisive; C7 os8 still audible -> os16 being
  added and rendered for C7/C8.
- Clarinet noise-bed: APPROVED ("we'll be in familiar territory without
  the new feature") -> implementing, then clarinet CMA-ES.
- Dropdown recheck: good; closed.

## Resolved

2026-07-30 (Matt, folded in run 9):
- v6 curves: all fine, taste-level — **lo adopted as default viola
  recipe** (patches/viola_default.json); tunable in UI Curves tab.
- FormantSequence round 1: "all nearly identical, subtle — FormantWeight
  too low?" — mechanically right: fmtWt 2 boosts in-band <=3x while the
  out-of-band partials dominate. Round 2 regenerating at w5/w9 + a
  flat-rolloff variant with formant-dominance verified in dB.
- Expand round 2 ranking (verbatim, reference):
  > fifth_r2 best — recursion adds pleasing bass "whoosh", even lusher
  > than r1, bump base partials to 10 next. leslie_swirl_combo best/most
  > subtle of the leslies (po softens) — try recurse 1 and 2. micro_r2 ==
  > leslie_micro_r2 (slot wasted, reuse). leslie_wide8 least subtle but
  > usable. wide2narrow: useful musical attack IF much faster — 0.02/
  > 0.05/0.08 s versions. fifth_leslie_morph: super cool sound effect —
  > quick-attack version + audio-rate morphing. pi_swept: even MORE
  > villain vibe. narrow2wide: very cool, special effect. semitone_r2 ~
  > r1, sound effect. breathe_lfos too in-your-face, sound effect only.
  > sweep_slow_ramp + phase_swirl least useful.
  All requested variants in run-9 batch (expand round 3).
- UI curve editor: good except vla_partials.bandwidth1 missing from the
  Add-curve dropdown (fix in progress, run 9); folder picker good;
  velocity bug unreproducible-and-gone.
- Second instrument: Bb clarinet chosen ("sounds wonderful"; trumpet
  samples disappointing — no attack character, skipped). Matt's
  observations logged: wind/brass attack speed & breath amount inversely
  proportional to frequency; these samples = gold standard for the
  breathy attack we've failed at repeatedly. My call per his question:
  ATTACK ANALYSIS FIRST (run-9 agent), then CMA-ES — a viola-shaped
  encoder won't invent breath structure, and run-7 already flagged
  scorer-vs-ear divergence on structural terms.
- FM alias A/B: inaudible as rendered — regenerating per his spec (5 Hz
  freq LFO to expose aliasing, notes C4-C8, os1 vs os8).

2026-07-29: curve editor requested->landed; CMA-ES "no worse" -> stage e
approved; expand round-1 categorization (see git history for verbatim).

2026-07-28: v5 grounded adopted; frequency-dependence principle; v4/UI
verdicts. 2026-07-27: v3 verdicts.

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

### 2. CMA-ES stage-d best-of-run [listen] — READY
611 evals complete: score 0.514 vs 0.945 (100-eval smoke) vs ~1.28
(warm start). Every scorer term improved; motion distance halved.
- renders/fable1_v6/v6_05_cmaes_best.wav — best patch on the C2..E6
  ladder, A/B against v5_04_cal_sustain.wav (hand-calibrated) and the
  v6_01..03 curve ladder
- renders/fable1_v6/v6_05_cmaes_best_4note.wav — the optimizer's own
  4-note render (C3/G3/D4/A4, matches the Iowa reference set)
Patch: patches/fable1_v6/v6_05_cmaes_best.json (also
research/ml_ears/cmaes_runs/viola1/best_patch.json + resumable state).
Verdict decides: (a) does metric-optimized beat hand-tuned to your ear —
the entire ml-ears bet; (b) if close-but-off, WHICH term sounds wrong
(that retunes scorer weights); (c) whether stage e (second instrument)
proceeds.

### 3. Recursive partial-expansion survivors [listen] — NEW (run 5)
renders/expand_sweep/. 18 ExpandRule regimes on one FullPartials base
(220 Hz, 4 s), novelty-ranked (higher = timbrally farther from the
conventional library + the un-expanded control). The metric says which
regimes transform most; your ear decides which are actually worth keeping.
Suggested listens (top novelty + the high-ranking rule-breakers):
- flat_taper (84), converge_spacing (69), semitone_r1 (66) — top movers
- golden_spacing (54, φ/φ² semitone spacing) + noninteger_pi (45, π-semitone
  spacing) — rule-breakers that ranked high; the accidental-discovery bets
- control_noexpand (0) — the plain base, for reference
Verdict decides: which regimes graduate to named patches / a v-series, and
whether recurse 2-4 is worth the perf work (item 8) to explore next.

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

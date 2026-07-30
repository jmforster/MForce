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

MATT: I need to be able to experiment with patches in the UI instead of just
guessing at what the curves should be. Go ahead and implement full support
("curve editor") in UI so that I can do this. While you're at it, a couple of
other UI changes/fixes:
1 - in the audition sweep folder picker, set to all file types instead of JSON.
(do we have to use the "pick any file to pick the folder" trick (as you called it)?)
2 - if you change Velocity, then try to generate, you get the "no patch loaded" error

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

MATT: I thought I gave feedback on this already, that the cmaes_best WAV was
at least "no worse" than the compared one. Let me know if I'm missing something.

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

MATT: These are pretty promising, let's keep experimenting. Some sound like chords
with a big stack of tones (for obvious reasons). noninteger-pi sounds like an
old time movie diminished chord flourish (as villain ties heroine to RR tracks)
(again, obvious reason). Others sound like effects on single notes. Others are
more like sound effects. Here's that categorization, ranked best>worst in each
category, with descriptions:
Musical:
microcluster-0/1 - a bit like a Leslie effect, with 0 being subtler than 1
wide-microcount - even more like a Leslie
diverge-spacing - less pleasant but a similar sound to the above
phase-swirl-r1 - slightly "metallic" chorusy effect
steep-taper - quite similar to phase-swirl but a little less interesting

Re: your last note, none of these use recursion? Your runs have been very brief,
so I don't think performance is an issue for these batch renders.

Give me another batch based on feedback above, using recursion in a few, and 
enveloping vs. sweeping the spacing, etc. on a few (ie, an attack that starts wide
and becomes narrow).

And, since as I said your runs are really brief, once these done, proceed to 
FormantSequence experiments.

Chordy:
fifth-spread-r1 - fifthy, organy, quite nice
fifth-spread-r0 - very similar, not quite as "full sounding"
non-integer-pi - diminished chord, ear doesn't distinguish the wider spread of 3.14
Sound effects:
converge-spacing - hard to describe, spacy, ominous
extreme-detune - like a slowed down bell attack
flat-taper - halfway between a musical effect and a videogame laser sound
golden-spacing - halfway between dissonance and noise
inverted-taper - another lasery sound effect


### 4. FormantSequence sweep survivors [listen] — NEW (run 6)
renders/formantseq_sweep/ (15 WAVs, all-unique verified). Suggested:
- audio_rate_30 + narrow_audio_50 — audio-rate spectral-envelope
  modulation, the unexplored-territory bets (novelty top)
- deep_weight_10 — absurd resonance depth on a 1 Hz vowel morph
- vowel_glide_ramp / vowel5_red_walk — the "musical" end: vocal glides
  and RedNoise-wandering vowels
- control_static — reference
Verdict decides: which morph families graduate to named patches; whether
audio-rate blend deserves its own deep-dive; whether FormantSequence
motion joins the viola recipe (slow body-resonance drift).

### 5. Expand-sweep round 2 [listen] — NEW (run 7)
renders/expand_sweep2/ (14, all-unique). Built on your categorization:
- Leslie family refined: leslie_micro_r2 / leslie_wide8 /
  leslie_swirl_combo (microcluster+swirl hybrid) — low novelty score is
  expected (familiar = musical); judge by ear
- Recursion depth 2: semitone_r2 + micro_r2 + fifth_r2 + phase_swirl_r2
  (semitone_r2 is the novelty top at 86)
- Swept spacing per your note: attack_wide2narrow / attack_narrow2wide
  (novelty 86!) / sweep_slow_ramp / breathe_lfo_05 / breathe_lfo_3
  (cyclic wide-narrow "breathing") / fifth_leslie_morph (chord<->Leslie
  LFO morph) / *pi_swept (pi collapsing to 0.1)
Verdict decides: category winners -> named patches; whether breathing
joins the Leslie family; next sweep axes.

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

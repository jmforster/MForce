# DSP lane — review queue

## Awaiting Matt

### 1. v6 residue/floor curve defaults [listen — now UI-explorable]
renders/fable1_v6/ v6_01/02/03 (residue curves lo/mid/hi) + v6_04 (bw
floor curve). Since run 7 you can also open these in the rebuilt UI's
Curves tab and drag values while playing. Verdict decides: default curves
for the viola recipe (or just tune them yourself in the editor and say
"use what's in my saved patch").

### 2. FormantSequence sweep survivors [listen] (run 6)
renders/formantseq_sweep/ (15, all-unique). Suggested: audio_rate_30 +
narrow_audio_50 (audio-rate spectral-envelope modulation — the unexplored
bets), deep_weight_10, vowel_glide_ramp / vowel5_red_walk (musical end),
control_static (reference). Verdict decides: which morph families
graduate to named patches; whether audio-rate blend gets a deep-dive;
whether slow formant drift joins the viola recipe.

### 3. Expand-sweep round 2 [listen] (run 7, built on your round-1 notes)
renders/expand_sweep2/ (14, all-unique).
- Leslie family refined: leslie_micro_r2 / leslie_wide8 /
  leslie_swirl_combo (low novelty score expected — familiar = musical)
- Recursion depth 2: semitone_r2 (novelty top, 86) + micro_r2 + fifth_r2
  + phase_swirl_r2
- Swept spacing per your note: attack_wide2narrow / attack_narrow2wide
  (86) / sweep_slow_ramp / breathe_lfo_05 / breathe_lfo_3 (cyclic
  breathing) / fifth_leslie_morph (chord<->Leslie LFO morph) / *pi_swept
Verdict decides: category winners -> named patches; whether breathing
joins the Leslie family; next sweep axes.

### 4. UI curve editor + fixes [look] (run 7)
Rebuilt mforce_ui: Curves tab next to Properties (breakpoint table +
plot, add/delete points and curves, edits audible immediately). Real
Select Folder dialog replaces the pick-a-file trick. Velocity repro
didn't reproduce, but two adjacent defects with that symptom fixed
(stale-playback dirty flag; misleading generate gate). If generate still
errors after velocity changes, the message now names the actual cause —
report the exact text.

### 5. Second CMA-ES instrument — download unblock [read]
Only viola samples exist locally. Pick the next Iowa MIS instrument
(flute / cello / trumpet / clarinet...) and download its ff samples into
research/inst_samples/<name>/. The optimizer is config-driven now — a
config + your samples is all stage e needs.

### 6. Oversampled FM alias A/B [listen] (run 8), optional
renders/fm_oversample/: fm_alias_os01.wav (aliased original) vs
fm_alias_os08.wav (8x oversampled, 39.6 dB suppression, metric-proven).
Verdict decides: whether oversample defaults >1 for bright/high-index FM,
and whether the oversampled "modulate everything" sweep runs next.

## Resolved

2026-07-29 (Matt, folded in run 7 — raw comments preserved below):
- Curve editor + UI fixes: requested -> LANDED run 7 (item 4 above is the
  acceptance check).
- CMA-ES stage-d A/B: "at least no worse" -> optimized >= hand-tuned
  accepted; stage e proceeding (config mechanism landed run 7; samples
  download-gated, item 5 above).
- Expand-sweep round 1 categorization -> acted on in run 7 (item 3
  above IS the requested next batch: recursion, swept/enveloped spacing).
  Full verbatim notes, kept for reference:
  > Musical: microcluster-0/1 (Leslie, 0 subtler), wide-microcount (more
  > Leslie), diverge-spacing (less pleasant, similar), phase-swirl-r1
  > (slightly metallic chorus), steep-taper (like phase-swirl, less
  > interesting).
  > Chordy: fifth-spread-r1 (fifthy, organy, quite nice), fifth-spread-r0
  > (similar, less full), non-integer-pi (diminished chord; ear doesn't
  > distinguish the 3.14 spread — old-time-movie villain flourish).
  > Sound effects: converge-spacing (spacy, ominous), extreme-detune
  > (slowed-down bell attack), flat-taper (musical effect / videogame
  > laser), golden-spacing (between dissonance and noise), inverted-taper
  > (another laser).
  > "Keep experimenting; none of these use recursion? runs are brief so
  > perf isn't an issue; next batch: recursion in a few, enveloped/swept
  > spacing (attack wide->narrow); then FormantSequence experiments."

2026-07-28 (Matt): v5 grounded A/B "no worse, maybe better" -> measured
values adopted as default sustain; v4 residue 25% too much at low freq
only -> frequency-dependence principle (memory) + v6 curves; bw floor =
low-freq rumble -> curve it; attack->sustain transition smooth; UI
array-restore fix confirmed; "accept long render" for stage d.

2026-07-27 (Matt): v3 — cluster_tight_low wins; sustain stack rich,
stringy; attack too distinct -> v4 residue direction.

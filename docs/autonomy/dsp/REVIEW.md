# DSP lane — review queue

## Awaiting Matt

### 1. rng re-roll acceptance [listen] (run 16)
renders/rng_ab/ — A_shared vs B_perpartial for v6_cmaes_best +
viola_default. One-time permanent re-roll (deltas to +-4dB/note; the new
realizations are the forever ones). Not approval of a mechanism — just:
do the B realizations sound fine?

### 2. Shimmer gain floor [listen] (run 16)
renders/shimmer_floor/ — v6_cmaes_best at floor 0 / 0.3 / 0.5 (final-note
dip -12.6 / -8.6 / -4.4 dB, same location). Verdict: default floor.

### 3. liar2 x8/x9 fills [listen] (run 16)
renders/liar2/ liar2_110_x8/x9 + 220 — completes your 3..14 bracket
around x7.

### 4. FM matrix2 [listen] (run 16)
renders/fm_matrix2/{modulated,ramped}/ — your 11 Y picks at lo/med/hi +
effect-ramp attacks 0.1/0.2/0.5s. NOTE: t1_07 'PM' was an illusion — the
FMSource phase param is dead (bug, fix backlogged); what you liked was
plain FM + that envelope. Verdict: keepers -> named patch families.

### 5. Clarinet best_locked [listen] (run 16)
renders/cmaes_clarinet/best_locked.wav — 588-eval run with YOUR bed
frozen verbatim; optimizer improved tone terms (motion halved, broadband
0.95->0.71) around it. vs the c2c_quiet you hand-tuned. Verdict: is this
the clarinet keeper; resume to 600+?

### 6. Formant catalog [read] (run 16)
docs/research/formants/CATALOG.md — your UC xls = the Csound Appendix D
singing table verbatim (a/e/i/o/u = Italian close-mid set); Hillenbrand
(M/W/C speech, F0-F4) + P&B + IPA F1/F2 downloaded; F5 exists in NO
measured speech corpus (structural) -> Speech grid extrapolates top
formants. Assembly plan at the end awaits your go.

(also: piano_mf downloaded, 86/88 keys — A0/Bb0 missing at mf on the
server; MANIFEST.md has options. No action needed yet.)

## Resolved

2026-08-01 pm (Matt, folded in run 16): rng streams GO ("go ahead");
liar2 x7 ~optimal (+x8/x9 for rigor); vowelseq "near on the money" ->
IPA catalog research; clarinet bed LOCKED to hand-tune, register-law
revoked (memory amended); FM oversample convention agreed; FM matrix Y/N
table -> matrix2 batch; piano = next instrument (mf first).

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

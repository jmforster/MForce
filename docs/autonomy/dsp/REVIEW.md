# DSP lane — review queue

## Awaiting Matt

### 13. Piano — first optimized render [listen] (run 20)
`renders/cmaes_piano/` — `piano_smoke_baseline_C2C4C6.wav` (encoder center,
pre-optimization) vs `piano_smoke_best_C2C4C6.wav` (108-eval smoke best,
1.521 -> 0.900, every term improved), plus `piano_smoke_best_twohand.wav`
(10s C-major, LH octaves + RH arpeggio, polyphony 8). Both engine features
are live and measurement-locked: partials sit within 0.1 cents of the
sqrt(1+B n^2) stretch at the measured B(f0), high partials decay faster per
the measured law, knock band CONVERGED to 2484-8189 Hz vs the measured
2.5-8 kHz without being told. `inharm_decay_test.wav` = the bare
verification render (C2/C4/C6, no optimization).
Verdict decides: does the smoke best read as piano-ward; full 600-eval run
worth it; what is most wrong to your ear (attack / decay / knock / body).

### 14. Click PoCs — the t3_23 answer [listen] (run 20)
`renders/fm_clicks/` — your click question answered: t3_23 wires velvet
noise into FM phase, so each impulse sign-flips the carrier for EXACTLY one
sample. Rate is constant (velvet density; measured 5-193/s tracking the
dial) — it READ as modulation-linked because click loudness = 2|sin theta|,
so impulses near carrier zero-crossings vanish and FM sweeps where the
nulls fall. Density and jump size are both ValueSources = envelope-able
with ZERO engine work. PoCs: `poc1_attack_amp400` (crackle fades over
0.2s), `poc2_attack_dens2000to20` (crackle THINS AND fades),
`poc3_bell_attack600` (bell, 0.15s click burst). Note these clicks are
phase perturbations INSIDE the carrier — coupled to the tonal path by
construction, not a parallel noise sum.
Verdict decides: is this the noisy-attack direction worth pursuing; if the
single-sample clicks are too harsh, a SlewLimiterSource (backlog 3g) turns
steps into tunable-brightness chirps on the same dial.

### 11. Answer: what the power renorm would do (item 5 follow-up) [read] (run 20)
Your read is right — with the current formula, power is N/A at count=1 by
construction: the single side partial sits at position t=0 on the taper, and
pow(0, power) = 0 puts it at the loPct floor no matter what power says.
The renorm (t=(j+1)/(count+1)) would move every side partial's sampling
point inward: at count=1 the lone partial reads t=0.5, so its level becomes
power-DEPENDENT — power 1 puts it halfway between floor and full, power 2 a
quarter of the way up, power 0.5 about 71%. Cost: at count>=2 every position
shifts slightly too, so EVERY existing expand patch changes sound a little.
Given your item-4 verdict (depth second-order, expand front retiring), my
recommendation is LEAVE IT: the degenerate case is now documented in the
code and in this queue, and we skip a sound-changing edit to a retiring
front. Say the word if you want the renorm anyway.

### 12. F1-retuning — the possible feature, explained (item 1 follow-up) [read] (run 20)
The physics: a formant only speaks through the harmonics inside it. The 11
flagged grid entries are notes where f0 exceeds the vowel's F1 (e.g. soprano
A5 = 880 Hz vs /u/ F1 ~370 Hz) — NO harmonic falls inside the first formant
band, so the vowel's main resonance boosts nothing and the note comes out
thin with the wrong color. Real sopranos jaw-open to RAISE F1 until it
tracks the sung pitch (F1 ~= f0): h1 lands back in the resonance, power
returns, vowel identity blurs (why opera text is hard to catch up high).
The feature = per-note F1 floor: effective F1 = max(patch F1, f0).
Two implementations:
(a) NO ENGINE WORK, single-vowel patches: the paramMap frequency-curve
    mechanism already sets configs per-note — a curve on the F1 Formant
    node's frequency ([[low, F1], [F1, F1], [1100, 1100]]) IS max(F1, f0)
    as piecewise-linear. Could ship the 11 flagged patches today.
(b) ENGINE CONFIG, for FormantSequences/grids: a `tuneF1ToF0` flag on
    Formant (applied at prepare, where f0 is known) — one flag instead of
    authoring a curve per formant node per vowel spectrum. Worth it only if
    the sung-vowel direction gets real use.
Verdict decides: ship (a) for the 11 flagged patches now, build (b), or
leave documented.

## Resolved

2026-08-04 pm (Matt, folded in run 20): FM PM "2-level FM, fine" but t3_23
clicks -> investigated (item 14: velvet-phase mechanism, controllable,
PoCs). Wander cells: niche, no action. Engine gaps (noise amplitude, adsr
jitter): no objection. Expand depth: agreed second-order -> front RETIRED;
power-at-count-1 question answered in item 11 (recommend leave-it).
Piano shimmer plan (searchable dims, depth seeded 0.15): proceeded, no
objection raised. CombineTest op 3 -> Add per Matt (renders, peak 0.55).
Vowel grid naming fine; F1-retuning explained in item 12. Piano first pass
APPROVED -> executed (item 13). Shimmer floor: closed, no objection.

2026-08-02 pm (Matt, folded in run 17): rng A/B fine; floor 0.8 (his
hand-tune, measured safe); x8 = width default; FM held; clarinet
best_locked = KEEPER (clarinet_default.json); formant assembly plan GO
(grid built); piano next steps = analysis executed.

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

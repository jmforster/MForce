# DSP lane — review queue

## Awaiting Matt

### 19. Piano after the calibration bundle [listen] (run 23)
renders/cmaes_piano2/ — `piano_smoke2_baseline_C2C4C6` vs
`piano_smoke2_best_C2C4C6` + `piano_smoke2_best_twohand`. Your 1+2+3 all
landed and are VERIFIED in the render: attack now 4.9-5.7 ms (the 8 ms
lock rendering honestly; was ~33-34 ms chuff), knock at the real
measured scale (was ~68x over and smeared), C2/C6 early decay lands on
the real trajectory, and the re-fit decay curve is validated by the
optimizer itself (decayScale converged at 1.046). Smoke2: 1.188 ->
0.876, still descending at 110 evals.
Honest residuals, measured: (a) attack is now uniformly ~5 ms while
real pianos trend 36/13/9 ms down the registers — a per-register attack
curve is the next calibration step; (b) C4's late decay still dies with
no aftersound coast, and real C4's overtones decay SLOWER than its
fundamental — both point at option 4 (second decay stage), the
structural fix you deferred.
Verdict decides: is this piano-ward enough to fund option 4 and/or the
600-eval run; which residual bothers your ear most.

### 20. Vowel compare ladders — your called-out list [listen] (run 23)
renders/vowel_tweak/ — 36 WAVs (26 variants + grid A/B copies + 3
references) + README with per-entry diagnosis and the one thing to
listen for per variant. Measured causes behind your reports: sung U has
NO resolved F2 peak (speech UW's virtue — its F2 rides a clear harmonic
at ~1 kHz); Alto_U is flat within 2.2 dB right in EH-F1 territory
(your "between EH and OO", verbatim); Soprano_A has a 2.6-octave hole
h3->h9 (your "individual partials"); Alto/Soprano E and I peak on the
SAME harmonic — Soprano E's mid peak actually sits ABOVE I's (inverted
e/i). Grid untouched — pick winners and I fold them in.
Note for the future: if the soprano statics still disappoint, the
remaining real-singer lever is vibrato sweeping partials across the
formant bands — pitch-modulation-layer work.

### 21. Node graphs are playable [try] (run 23)
Per your item-17 verdict. Play/Stream (menu, Space, S) now work in
node-graph mode: the stream resolves Mixer -> Channel -> sources with
per-channel volume/pan and mirrors StereoMixer::render exactly (equal-
power pan, master gains, soft clip) — true stereo, no double-pan.
Play == Stream there (no notes); PC keyboard visible-but-disabled with
a tooltip. Also fixed a latent audio-thread use-after-free on
load/delete-while-streaming that patch mode had too.
Hands-on: New Node Graph -> Sine -> Channel -> Mixer ch1 -> Space;
drag frequency and pan live. Good demos: rn_test.json (RedNoise — the
crackling-fire case), wander_pan_test.json (live stereo motion),
gs_chaotic.json.

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

## Resolved

2026-08-06 (Matt, folded in run 23): UI batch "all looks good and works
as advertised" + NodeGraphs-should-be-playable -> built (item 21).
Vowel grid full audition: male speech all excellent, bass/tenor good
except U; alto/soprano A/E-I/U called out with specifics; O the best;
"do a compare pass on only the ones called out" -> 26-variant measured
ladder (item 20). Piano options: "Yes, start with 1+2+3" -> all three
landed + re-smoke (item 19).

2026-08-05 (Matt, folded in run 22): clicks/slew "dead end, revert all
code" -> REVERTED (d256536+50da8af; CombinedSource op-parse fix kept;
3g retired). Piano smoke "very far from a piano; chuff; string-y;
brighter->darker" + the t1_04 FM-accident observation -> measurement-only
diagnosis (item 16): chuff root-caused to adsr fraction+50ms-clamp
semantics (a BUG - optimizer scored 2.2s notes vs auditioned 3.5s),
double decay structurally absent, register curve miscalibrated 3x both
directions, t1_04 virtues measured (2.6ms attack, 112-line forest,
highs-die-first). F1-retuning "let's do (a)" -> shipped (item 18). Four
UI requests -> built (item 17).

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

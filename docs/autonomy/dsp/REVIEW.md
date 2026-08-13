# DSP lane — review queue

## Awaiting Matt

### 27. KS PIANO v6 rung 2 — shaped excitation [listen] (2026-08-10)
renders/dsp/pending/ks_piano_v6/ — README inside. Rung 1 RESOLVED same day:
Matt picked v6b_noise_short ("_long sounds like a distorted guitar pluck").
Rung 2 = excitation shaped to the band targets measured from his 2021 demo
audio (body-dominant, 600-1500 scooped, ~5% click at 4-10 kHz): pitch-tracked
BW lowpass body + pre-bank 4-9.5 kHz click path, grid-searched objectively
(tools/opt_ks_piano_v6c.py; winner err 0.0245; the click MUST tap raw noise —
the bank strips all HF, first grid run proved it). Optimizer re-bracketed
burst decay to 40 ms (the LP removes the hash that made long bursts read as
distortion). v6c verdict (Matt, same day): away from piano except C6; low registers
"drumstick on a buzzy string, turned up too high." v6c2 rev shipped:
his attack-window click measures ~0.002 (HF = post-attack sheen, not a
transient), so the click got its own envelope + pitch-dependent gain
(0.03@C2 -> 0.15@C6), grid-searched with a new attack-window scoring term
(opt_ks_piano_v6c2.py). A/B: v6c2_shaped (2 ms tick) + v6c2_bloom (15 ms
bloom) vs v6b_noise_short. All earlier renders kept.
v6c3 verdict CONFIRMED fresh-eared 2026-08-12 (+ new obs: volume falls
dramatically with pitch). v6d SHIPPED same day: pitch-fixed knock
(80-300 Hz, own env; two decays 80/30 ms) + level curve recalibrated
against FULL-CHAIN attack peaks (v6c3 equalized excitation only; the KS
loop loses another ~9 dB at C2 — buildup ~ periods spanned by the burst).
Attack peaks now flat 0.371/0.372/0.372. Caveat flagged in README: flat
attack peaks raise C2 sustain 2.3x C4 — listen for bass bloom; curve can
target any register profile once ears pick the direction.
v6d verdict (Matt, same day): knock inaudible in full patch, too dark;
band top ~500 by ear. v6e SHIPPED: engine paramMap vcurve
(velocity->multiplier, composes with freq curve); velocity->brightness
on the adjusted knock base (80-500, gain 2.0 — C2 level comp drops
x8.5 -> x3.6); velocity-ladder score; v6e_flat A/B control. Verified
soft-C4 centroid 283 vs 349 Hz, hard notes identical.
v6e verdict (Matt, same day): "on the money"; harpsichord-y-ness much
reduced (hotter 80-500 knock). v6f/v6g SHIPPED — ladder complete:
engine RenderContext.noteOffFrame + KSPianoString releaseFb (in-loop
damper, 0.82; C2 post-off env 2089->158->1 vs ctrl 2089->538->46) and
detune-vs-pitch curve (0.3->5 cents). A/B: v6f_ctrl / v6f_damper /
v6g_detune.
2026-08-12 pm verdicts: damper 0.82 IN; damperNoise 0.03 IN; detune
axis audible (12 too much). CMA-ES ks_run1 (600 evals) LAUNCHED on the
v6h Matt-locked seed. QUEUED FOR PASS 2 (Matt, while run1 cooks):
- knock band lo/hi as searched dims (he hand-found lowCutoff 30 =
  "dramatic effect"; current 80-500 was ear-picked under time pressure)
- detune: slider was capped at 20 cents (unison-beating regime — flange
  only, mechanism fine); ceiling raised to 1200 in ks_piano_string.h
  (REBUILD PENDING: cli in use by run1, UI was open). Hand-playing the
  knob requires removing the string.detune paramMap curve first — the
  per-note curve stomps the knob at every note-on.
ks_run1 COMPLETE (600 evals): 1.2447 -> 0.6584 (-47%). Terms: harm
1.85->1.01, motion 1.03->0.21, attack HELD 0.08 (locks worked),
broadband STUCK 1.17 (inter-harmonic energy — same residual gap as the
viola; a mechanism gap, not a tuning gap; future front). Winner rendered
as renders/dsp/pending/ks_piano_v6/v6i_cmaes.wav (A/B vs v6h_seed).
Bold choices: bank hotter/faster (resStart 55, resDecay 4ms), LP 4.5xf0,
bass brightness 0.90, t60 x0.6 (our 25s C2 was long vs Iowa), fbCoeff
x2, inharm x0.5, detune_hi PINNED at the 8-cent bound -> widen in pass 2
(with knock band lo/hi dims). Verdict decides: does run1's winner beat
v6h_seed by ear; pass-2 dim list. Then ENDGAME (agreed): CMA-ES joint
optimization of the full v6 settings matrix vs Iowa piano references
(viola pipeline; ~200x realtime renders) — no per-knob ear tuning.
Analysis: docs/research/afpiano_2021/ANALYSIS.md.

### 26. Housekeeping 2026-08-10 — leftovers and judgment calls [read]
The patch/score restructure (docs/patch_triage_2026_08_10.md rev 2 + your
annotations) is executed. Things you didn't rule on, or where I applied a
pattern you should sanity-check:
- **Two files modified in the working tree by an unidentified session** (dirty
  since before 2026-08-09): `patches/clarinet_c2/c2c_quiet.json` and
  `patches/fable1_v6/v6_01_res_curve_lo.json`. Both dirs are otherwise
  triaged (c2 remainder in old/, v6 gated in place). Verdict decides: keep
  the modifications (commit), discard them (checkout), and then where the
  two families go (old/, presumably).
- **"the 2 comp ones moved to renders/dsp/pending"** — the two templates are
  patch-tree JSONs, not renders, so I read this as scores/pending/:
  template_golden_phase1a + template_shaped_test + the 11 jazz_turnaround
  patches all landed there. Say if you meant something else.
- **Pattern-applied strays you didn't list:** fm_bell/fm_brass/fm_ep_test →
  patches/baselines (fit the *_test pattern); untracked test_k467_v1-v4/
  _structural/_v4_rep + test_aaab + test_lib_billy/blue/miley →
  scores/baselines; liar3/ → old/; k467_bars_1_to_{12,27}.dun →
  scores/baselines.
- **renders/ dirs I could not classify** (left in place at renders/ root,
  awaiting your call — delete, scratch, or a lane): algev, algev_convert,
  bakeoff, bare_output, exp2_ab, features_0730, fig_demo, fm_phase,
  markov_contrast, markov_figures, markov_phrases2, matt, noise_amp,
  novelty, null_test_templates, nulltest_noise, passage_bruckner,
  passage_chords, passage_strategies, phrase_aware_ab, probe_keyctx,
  rng_ab, shimmer_floor, stage3_regress, stage4_wandering,
  transform_variety, voicing_open_items, wander_fix, warmstart, _probe,
  _smoke_final, plus loose files (_phrase_test_*, wolfie_par_*, matt).
- **Missing renders cited by open comp items:** `passage_end_grid` (item 17),
  `markov_phrases5` (item 11), `markov_phrases6` (item 13) no longer exist —
  most likely casualties of the 2026-08-08 big-render deletion. Those items
  need a re-render (same seed per their READMEs) or a verdict from memory.

### 25. RESOLVED 2026-08-10 — see Resolved section. (Kept here one cycle for
context continuity; safe to prune next run.)

Soprano E / I / U — the alto-formant candidate [listen] (run 25)
`renders/dsp/pending/vowel_soprano_alt/` [deleted post-verdict] — 8 WAVs, 4 A/B pairs, `_cur` is what you
rejected and `_altf` is the candidate. Same note (A4), same partials, same
envelope; only the formant table differs. **The O pair is a control** — O is
the family you rate best, so if `_altf` is worse there the idea is wrong.

Not another guess. Measured first: vowel separability collapses monotonically
with pitch (mean pair distance 26.94 bass / 23.87 tenor / 21.39 alto / **14.80
soprano** dB), because at 440 Hz soprano E and I put F1 on the *same* harmonic
(h1) and F2 on the *same* harmonic (h5) — they can only differ in gain. Six
harmonics live below 3 kHz at soprano pitch against 27 at bass. Soprano U sat
**5.20 dB** from O, the closest pair in the whole grid, which is "lacks vowel
character" as a number.

Then a 2×2 crossing recipe with pitch showed the recipes were still giving
away ~7 dB: alto formants sung at 440 keep **16.90 dB** of E/I contrast where
the soprano recipe keeps 10.20. So: alto formants, unmoved, sung at A4.
Measured effect — E-I 10.20 → **16.90** (+6.69), U-O 5.20 → **13.86** (+8.66),
I-O +4.72, E-O +2.23; E-U and I-U do not move.

This is **not** pass-2's `e3_altoXpose` (your "pennywhistle"), which scaled the
formants ×2 along with the pitch. Formants are a property of the vocal tract,
not the note — a singer's tract does not shrink an octave up.

**Verdict decides:** do any of the three become the vowel they should be? If
yes they get locked alongside the other seven; if no, the honest read is that
sung vowels at 440 Hz are sampling-limited and the front retires rather than
getting a pass 3. Caveat stated plainly: separability is not correctness — a
bigger number means the vowels differ more from each other, not that any one
is right.

### 22b. KS PIANO v5 — full description, no direct taps [listen] (2026-08-09 overnight)
renders/dsp/pending/ks_piano_v5/ — 7 WAVs + README. Matt's verdict on v1-v4:
"kick drum, disappointed." v5: every patch has ALL six described elements;
the hammer.direct/string.direct dry taps are removed (description has no
dry path), exciteGain carries level. Variations move one mechanism each:
a=straight, b=hammer ring, c=bright loop, d=double-envelope, e=flange,
f=body, g=two-hand phrase. Measured vs old v3_full: C4 sustain much
slower (-3.5 dB @1s vs -8.7; b: -1.2), C6 treble lives longer (b/c:
-16/-15 dB @1s vs -25). C2 attack centroid still dark (116-157 Hz).
Patches patches/pending/ks_piano_v5/, generator tools/gen_ks_piano_v5.py.

### 24. Node-graph stream fix [try] (run 24)
Per your item-21 report. Streams no longer fade out: root cause was
the 30s prepared duration + fractional envelopes (your streams were
one long fade to silence at exactly t=30). Now steady (measured 100.3%
at t=10, flat through t=60); 2-hour ceiling remains (true infinite
needs an engine envelope hold mode — backlogged). wander_pan was NOT a
UI bug — the patch's deltaSpeed self-cancelled per-sample; param fixed,
it now pans. gs_chaotic: GrayScottSource only exists on the unmerged
chord-walker branch — unknown nodes now load inert with a warning
instead of ImGui erroring every frame.

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

2026-08-10 pm (Matt, hands-on): item 25 soprano alt-formant candidates
CLOSED — Matt merged the full vowel_grid catalog into
patches/library/voice/ as the new canonical voice library, overwriting
with the soprano_alt candidates "where the new was an improvement", then
deleted the pending dirs. The library/voice tree (59 files: 20 sung + 36
speech + words/liar2 pair + README) IS the verdict record; filenames
normalized to lowercase-with-uppercase-vowel-suffix (sing_alto_A.json).
Note: the run-25 "seven locked winners" as separate tuned files are
superseded by this merge — the merged file contents are Matt's picks.

2026-08-10 (Matt, via patch-triage manifest annotation): item 22 KS PIANO
A/B CLOSED — "Already convinced KS is the future here." The KS/physical
route beats the additive lane for piano; the v1-v4 ladder is superseded by
v5 (item 22b, still open). patches/ks_piano/ + cmaes_piano* moved to
old//deleted per render policy. Next iteration funding rides on the v5
verdict.

2026-08-09 (Matt, folded in run 25): vowel pass 2 (item 23) closed. Seven
winners LOCKED into `patches/library/voice/`, all seven byte-identical to the
auditioned WAVs. Soprano E/I/U not locked -> measured instead of swept
(separability collapses with f0; E and I share both formant harmonics at 440),
and one measurement-derived candidate queued as item 25. Verbatim, kept for
reference value — the Soprano diagnoses drove the analysis:

> Diminishing returns. Only 1 improved on the previous "winners", and that
> very marginally. Lock new Tenor_U_u1_f3peak.
>
>  Happy to stick with prior _winners for
> - Alto A
> - Alto E
> - Alto I
> - Alto U
> - Bass U
> - Soprano A
>
> Still unsatisfactory:
> - Soprano E - all sound like I except _altoXpose which sounds like a pennywhistle
> - Soprano I - all new attempts inferior to _winner but _winner too partially/organy
> - Soprano U - all new attempts inferior to _winner but _winner lacks vowel character
>
> Alto A - all 4 sound identical, so let's lock the previous _winner.
> Alto E - new ones very similar but have some wobble - lock _winner.
> Alto I -

(The note ends mid-line at "Alto I - ". The summary list above it says stick
with the prior winner for Alto I, so that is what was locked.)

2026-08-06 pm (Matt, folded in run 24): piano additive "still far-off"
(harpsichord buzz, boing) + Alpha Forever pivot -> Gyutai research
(docs/research/alpha_forever.md: alive-but-quiet, Balazs="9b0",
Allpass Resonator topology, nested-allpass 2025 quote) + KS piano
BUILT (item 22). Vowel winners + Soprano_E miss + "one more pass" on
ring -> pass 2 (item 23). Stream decay/wander_pan/gs_chaotic -> fixed/
diagnosed (item 24). Session limit hit mid-run; all three agents
resumed and completed after reset.

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

# DSP lane — review queue

## Awaiting Matt

### 16. Piano diagnosis — the chuff was a bug, plus your FM accident measured [read] (run 22)
Full numbers in research/ml_ears/piano_diagnosis.py output; the short form:

THE CHUFF: `adsr` preset semantics. make_adsr treats attack as a FRACTION
of note duration and clamps each stage (attack floor 0.05s) — so the
"locked 8 ms" attack rendered as a 50 ms linear ramp (measured 10/90 rise
43.6 ms vs real piano 9-36 ms), the knock ALSO hit the 50 ms floor and its
decay rendered 267 ms vs the real 69 ms — AND the optimizer scored 2.2s
eval notes while you heard 3.5s renders, so CMA-ES never even scored what
you auditioned. Knock energy is ~150x overweight but smeared -> chuff.

STRING-Y SUSTAIN: two causes. (a) Double decay is structurally absent — a
single exponential + shimmer IS the string signature; real C4 drops -9.4
dB by 0.5s then coasts at ~-1 dB/s, candidate does one steady slope (too
slow early, too fast late). (b) The decay register curve is miscalibrated
(C2 3.4x too fast, C4/C5 3x too slow) — no decayScale can fix both ends.
Also term1: one global envelope can't serve C2 and C6 (13-20 dB per-note
error; candidate bass is fundamental-only dark).

YOUR FM ACCIDENT (t1_04): measured virtues = 2.6 ms attack (vs additive's
~40), a 112-line quasi-harmonic forest at f0~36 Hz (multi-string bass
thickness; NOT inharmonicity — fitted B~5e-6), highs-die-first sideband
collapse, and onset tilt +3.7 dB vs real C2's +4.5. Caveat: its pitch
comes from the modulation-rate grid, not `frequency` — playing a scale
needs a mapping study first.

OPTIONS (pick a bundle; no code written yet):
1. Fix attack semantics for this path (absolute seconds / expose clamps)
   — small, highest leverage, everything else inherits it.
2. Recalibrate knock level+decay from measurement (post-1, cheap).
3. Rebuild decay register curve from per-note measurement (no engine
   work; fixes bass-too-fast AND mid-too-slow).
4. Second decay stage (prompt/aftersound) — the one STRUCTURAL gap;
   engine feature.
5. Per-register spectral envelope (lock from measured spectra like B).
6. FM-for-bass hybrid: characterize t1_04's pitch mapping, then either
   FMSource for octaves 1-2 or port its virtues (attack, line density)
   into additive.
7. Full 600-eval run as-is — measurement argues AGAINST (optimizer would
   be scoring the bug).
My recommendation: 1+2+3 next run (all calibration, no new features),
4 as the follow-up feature, 6 as a parallel exploration when you want it.

### 15. Slew click ladder — the brightness dial you asked for [listen] (run 21)
`renders/slew_clicks/` — the follow-up to REVIEW 14. `SlewLimiterSource` now
exists and sits between the velvet phase source and FMSource, so each click's
one-sample phase step becomes a glide. Six files: `slewclick_ctrl_none`
(unchanged PoC) then `slewclick_f20000 / f05000 / f01000 / f00200 / f00050`,
falling fallRate = longer glide.

What the dial actually does, so you know what to listen FOR: the impulse
jumps phase 0.5 cycles and slides back over `0.5/fallRate` seconds, and a
linear phase glide is a constant frequency offset — so during the slide the
carrier is detuned by `fallRate/2` Hz. Low fallRate = a long, gentle, small
bend; high = a short, violent one. It trades duration against deviation
rather than simply dulling the click.

Measured over the 0.25 s attack window: centroid 7116 Hz (control) → 6404 →
4745 → 3667 → 3237 Hz at fallRate 50, i.e. 0.455x, with the >8 kHz share
going 37.9% → 13.6%. Monotonic, all six distinct. `f20000` is deliberately
the degenerate end (0.025 ms glide ≈ 1 sample) and should be
indistinguishable from the control — if you CAN hear a difference there,
something is wrong and I want to know.

Verdict decides: which rung (if any) is the usable noisy-attack character;
whether to wire fallRate to an envelope so the chirp character evolves across
the attack (zero engine work — it is already a ValueSource); or whether the
whole click direction is a dead end and item 3g retires.

MATT: Dead end, please revert all code specific to this feature.

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

MATT: Very far from a piano sound. Attack is relatively slow with a "chuff" vs.
being instant and percussive. The sustain phase is string-y (not necessarily a
bad thing) and has a progression from brighter to darker.

One challenge is the very different sound of low piano notes vs. mid vs. high.
Interesting, one of your fm WAVs (fm_matrix2/t1_04_cratio_audiorate_lo) sounds
pretty close to a first or second octave piano note. Not sure if/how that accident
can be leveraged.

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

MATT: Dead end, please revert all code specific to this feature.

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

MATT: Let's do (a).

MATT (new request - UI changes/enhancements):

1. Ability to copy and paste nodes - add Edit top level menu with Cut/Copy/Paste and
   support standard shortcut keys ctrl-x / ctrl-c / ctrl-v

2. Ability to convert graphs from node > instrument and vice versa:
   - add "Convert to <type> graph" to new Edit menu, where <type> is the "other"
     type (if node graph loaded <type> = "Patch", else type = "Node")

3. Unless I'm missing something UI does not have ability to add Output, Channel,
   or Mixer nodes. Add all 3 under a new top level "Output" category, last in list.

4. Been using Audition a lot, obviously, let's give it an upgrade:
   - Change navigation to File > Audition...
   - Selecting that brings up independent window with left and right panes
   - Left pane top contains sweep (renamed "Source") folder selector and control buttons
   - Left pane remainder contains file list (WAVs)
   - Right pane top contains curated (renamed "Target") folder selector
   - Right pane remainder contains file list (JSON patches)
   - Controls work as now, with addition of right pane file list refreshes after Save
   - Only action in right pane is if user clicks a filename and hits Delete, deletes file

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

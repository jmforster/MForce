# DSP lane — review queue

## Awaiting Matt

### 17. The four UI requests — built [try] (run 22)
All in the new build (title bar 08-05 12:06 @7648c61): Edit menu with
Cut/Copy/Paste + ctrl-x/c/v (multi-select, links inside the selection
rewire on paste); Edit > Convert to Patch/Node graph (same-session
round trip restores instrument+curves verbatim; cold conversion uses the
frequency heuristic + default score); Output category (Output patch-mode,
Channel/Mixer node-mode, gated with reasons); File > Audition... window —
Source pane (folder + controls + WAVs), Target pane (folder + patch list,
refreshes after Save, click + Delete + Enter deletes the file).
Headless checks green (--roundtrip 8/8, --convert-roundtrip
byte-identical). Hands-on list: box-select copy/paste, conversion node
placement, Audition pane sizing + delete flow, and NOTE Delete/Backspace
node-delete now requires editor focus (was global).

### 18. F1-retuned sopranos — re-rendered [listen] (run 22)
renders/vowel_grid/ — the 11 flagged WAVs replaced in place per your
"Let's do (a)". The true trio (Soprano E/I/U at A4) gained h1
+8.2/+21.4/+14.9 dB (matching the predicted skirt deficit to 0.01 dB);
the other 8 carry the curve but are no-ops at their scored note
(merged-formant class, retune only matters if played higher).
Tradeoff to listen for: power returns, vowel identity blurs toward
F2-only cues — the physiologically accurate soprano situation.

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

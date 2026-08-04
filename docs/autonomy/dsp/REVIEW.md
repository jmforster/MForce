# DSP lane — review queue

## Awaiting Matt

### 7. FM "PM" cells were never PM — re-take the verdict [listen] (run 19)
renders/fm_phase/ (t1_06_ctrl vs t1_06_after, t1_07_ctrl vs t1_07_after,
t3_23_patchfixed) + the re-rendered patches themselves.
FMSource's `phase` param was inert: t1_06 and t1_07 rendered BYTE-IDENTICAL
to twins with `phase` deleted, so the two "PM" topologies you auditioned in
the run-12 matrix batch were plain FM. t3_23 was doubly dead (its offsets
were whole cycles). All three are fixed and re-rendered.
Verdict decides: (a) whatever you said about t1_06/t1_07/t3_23 in the FM
matrix audition should be treated as void — do these three now earn a place
in the keeper set? (b) t1_06 is the interesting one to my eye: a slow +-1
cycle sweep is a frequency deviation, so it splits every partial into a
1.7 Hz sideband cluster (6 -> 109 peaks) WITHOUT brightening. That is a
texture the FM matrix had no other way to reach.

### 8. Seven wander cells were running at the wrong rate [listen] (run 19)
renders/wander_fix/ — *_before vs *_after for the 7 t2_11_all_noise_wander
cells (fm_matrix + fm_matrix2 modulated/ramped).
`WanderNoiseSource`'s rate param is `speed`; the generator wrote `frequency`,
which the loader silently dropped, so n3 ran at the default 1.0 instead of
7.0 in every cell of that row.
Verdict decides: same as 7 — the row's audition verdict was taken on a
patch that wasn't doing what the label said. Worth re-ranking?

### 9. Two engine gaps closed — sanity check [read] (run 19)
(a) White/Pink/Blue/VioletNoiseSource had NO `amplitude` param at all (the
comment claimed "no modulatable params — spectral shape is fixed", which
conflates shape with level). Setting noise level needed an extra multiplier
node. All four now take `amplitude`, default 1.0.
(b) The `adsr` Envelope preset dropped all six of make_adsr's randomization
ranges (attackMin/Max, decayMin/Max, releaseMin/Max) — so an adsr envelope
could not express stage jitter. Now passed through. 375 adsr nodes exist and
0 set those keys, so nothing existing changes.
Verdict decides: object if either is wrong-headed. Otherwise no action —
both are additive and A/B-verified bit-exact (327/327 identical across the
renderable affected patches), with linearity proved separately by
tools/test_noise_amplitude.py.

### 4. Expand round 4 — deep recursion [listen] (run 18)
renders/expand_sweep4/ (18 cells, recurse 3-4, first time past depth 2).
Novelty top: flat_deep, supersonic_r4, inflate_r3, semitone_r3, pi_r4.
Rule-breakers: pi_r4 (irrational spacing, never repeats), supersonic_r4
(most of the cloud above Nyquist by construction), phase_scram_r4,
absurd_6250. NOTE: flat_deep and supersonic_r4 render at peak 0.99 —
they are at the ceiling, so judge tone not level.
Verdict decides: (a) any keepers; (b) whether depth is worth more rounds
— my measurement says NO, it is second-order (holding partial count and
spread fixed, halving depth moves the embedding 4.36 vs a batch median
pair of 18.98; loPct moves it 50.46). If you agree, item 5 closes and
the expand front retires to "available, not a research direction".

### 5. `power` is inert at count=1 [read] (run 18)
apply_expand_rule tapers side partials by pow(t, power), t = j/count. At
count=1, t is always 0 and pow(0,p)=0, so power does nothing and every
side partial sits on the loPct floor. Found because peaked_deep rendered
BYTE-IDENTICAL to micro_r3. Proven live by taper_flat_c2/taper_steep_c2
(identical but for power, at count=2, and they differ).
Verdict decides: leave it (defensible — one side partial IS at the end of
the taper, and it is documented now), or renormalise t so count=1 is not
degenerate (e.g. t=(j+1)/(count+1)). The second changes how every existing
expand patch sounds. Engine work either way — backlog 12.

### 6. Piano shimmer dims — NOT lockable [read] (run 18)
research/ml_ears/piano_beating.py + out/piano_beating.json. I could not
measure unison beat RATE reliably: raising the analysis floor 0.50 ->
0.75 -> 1.12 Hz makes 9 of 11 rates climb with it (artifact); only C5/C6
hold, at ~1.9-2.0 Hz. What IS solid: single-strung B0/C1 modulate at
0.016-0.029 vs 0.106-0.285 multi-strung — a real 5-9x depth contrast,
magnitude order 0.1-0.2.
Verdict decides: I plan to leave shimmerHz + shimmerCoherence SEARCHABLE
in the piano encoder and seed shimmerDepth near 0.15 rather than lock it.
Say if you'd rather I spend a third attempt on the direct spectral-split
method (backlog 13) before the encoder instead.

### 1. Vowel grid [listen/look] (run 17)
patches/vowel_grid/ + renders/vowel_grid/ — your Speech_M/W/C x 12 +
Sing_Bass..Soprano x 5 folder, 56/56 verified (rendered envelope ==
patch spec exactly). README lists 11 physics-limited entries incl. the
soprano problem (F1 < f0: real sopranos retune F1 to the note — a
future feature if wanted). Verdict: spot-check by ear; is F1-retuning
worth building; naming scheme OK?

### 2. Piano analysis report [read] (run 17)
research/ml_ears/out/piano_analysis_report.md — measured: inharmonicity
V-curve (C6's 12th partial +422 cents sharp!), universal double decay,
hammer knock = real broadband transient. TWO new engine features
proposed (per-note inharmonicity config riding the freq-curve mechanism;
per-partial decay rates) with B+decay LOCKED from measurement per your
clarinet-bed pattern. Verdict: approve the two features -> piano encoder
+ first optimization.

### 3. Shimmer floor answer [read, closed unless you object]
Floor 0.8 keeps 83% of measured fluctuation (0.326->0.270) — the floor
trims the deep dives, not the shimmer. Adopted in viola_default +
cmaes-best. Depth compensation impossible (at cap).

## Resolved

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

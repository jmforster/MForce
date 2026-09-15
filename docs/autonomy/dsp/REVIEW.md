# DSP lane — review queue

Rebuilt 2026-09-14 (fall cleaning): the September queues that lived only
in STATUS now have entries, verdicted queues moved to Resolved stubs,
leapfrogged August items parked. Awaiting = only what still needs Matt.

## Awaiting Matt

### 53. STK bowed extensions round 1 — 12 cells [listen] (2026-09-13)
**renders/dsp/audition/stk_bowed_ext1/** (README inside) — 13 zero-code
cells on the canonical validated chassis, retuned to EQUAL TEMPERAMENT
(control = +0c; the port inherits STK's own +7/+25/+49c sharpness,
extensions don't), 12/13 passed gates+dedup. HEADLINE CELL: **hysteresis
stick/slip junction inside the proven chassis locks ±7c across C3-C5** —
the in-tune multi-octave lock the native harness never achieved. Also:
torsion coupling (+5-16c pull), dispersion allpass, bow-bite pressure
gesture (live pin — STK can't do this), body A/B/ablation.
VERDICT: does the hysteresis cell earn the junction roadmap's next slot;
which extension axes have character worth keeping; any cell → your bench.

### 54. STK bowed extensions round 2 — cello register, 5 cells + an IR pick [listen] (2026-09-13)
**renders/dsp/audition/stk_bowed_ext2/** (README inside) — Larson's
Music 421 numbers on the chassis: control2 +0c, width_h2 (his #1
feature), tors_phys (Z/Zt=0.306), disp16 (D-string range), larson_btd
(b+t+d combined, +25c retunable). Two hyperbolic cells killed and
diagnosed (stick must leak ~2%, plateau must be continuous) — own round
later. ALSO NEEDS YOUR EAR: pick a public cello body IR from the links
in the README — the round-3 entry point is fitting that measured body
curve as a biquad stack on the chassis, A/B vs bachd.wav.
Related one-listen: **bowed_REF_legato.wav** (staged with the port refs)
— STK's slur mechanism, context for the backlog-68 articulation
brainstorm; no verdict owed, just ears-context.

### 55. feedback_curves5b axis tour — 14 cells [listen] (2026-09-03)
**renders/dsp/pending/feedback_curves5b/** (README inside; pre-rename
queue location) — reed001a deep-dive, noise keytrack baked at -0.85:
cutoff multiple x drive shape x junction jitter, criticals re-measured
per cell; control = fb5b_m3p0_lin_j0 (per-note peaks flat 0.083-0.087
C3→C7). Full 144-cell grid in renders/dsp/sweep/feedback_curves5b/.
VERDICT: which axes move character; winners pull neighbors from the
grid; winning shape/mult then applies to the other r4 keepers
(backlog 37 live thread).

### 56. feedback_inloop1 — formants IN the loop, 10 cells + control [listen] (2026-09-05)
**renders/dsp/audition/feedback_inloop1/** (README inside; tool
tools/gen_feedback_inloop1.py) — oboe_default's formant bandpasses moved
INSIDE the loop (placement x resonance, normalized, loop-referenced
criticals). VERDICT: does in-loop formant coloration beat the
outside-the-loop bell; which placement/res cells live.

### 38. AF saxophone — source exhausted, build awaiting go [discuss] (2026-08-18)
docs/research/af_sax/RECIPE.md: complete value table from the breakdown
video; only 3 TUNEs remain. BLOCKED on design go-ahead: needs a new
ModDelayLoop node (audio-rate delay mod + in-loop tanh + gain>1) and a
body strategy. Post-steering note: this IS a steal-first target — the
actual patch file is on the AF Discord (parse_af_patch.py will decode
it and settle the TUNEs); grabbing it beats building from the video
table. Reference audio af_sax_demo.wav.

### 39. Wheel + pressure, live [try — PARKED on hardware] (2026-08-20)
Matt's current MIDI keyboard has no wheels; pends a different
controller. P3 wired CC1 + channel pressure into InstrumentState,
smoothed per voice; wire a Note node's `wheel` pin through a Curve into
something audible (Overall_lpf.cutoffFreq) and ride it, whenever
hardware appears.

### (parked) Excitation rounds 1-3 — 33 cells, leapfrogged (2026-08-23)
**renders/dsp/pending/excite1/ + excite2_sustain/ + excite3_triptych/**
(were items 46/47/48) — SegmentSource textures into a KS string:
13 candidates, 9 sustained beds, 11 attack-x-bed triptych cells (+
segment_sweep3 combo raw verdict). Round-1 partials were folded (49:
level dominates, keytrack DOWN, jins/jags "plucky not bowy"). The bow
question these were probing moved to the loop family + STK/hysteresis
chassis; the SURVIVING use is attack textures for the loop family's
`source` pin (old 37a intent). No verdict owed unless that front
reopens; renders stand.

## Resolved

Compact stubs only; full detail in run reports, STATUS digests, and the
verdict logs cited. Newest first.

- **stk_port1 — 4 families, port-vs-REF pairs** (09-13, verdicted rounds
  2-5): bowed "port matches REF"; clarinet/flute canon validated ±1-2c;
  brass closed as parity-with-a-poor-reference ("roughly the same, only
  C4 actually producing a note"). Meta-verdict recorded: STK skeletons
  underwhelm everywhere — the campaign's yield is the validated chassis
  + the documented missing-ingredients list (nonlinear bore, real body
  IR, finite bow width), not finished instruments. "Brassy" needs
  NONLINEAR bore propagation; linear bore + valve = "saxy not brassy".
  Full record: docs/research/stk_port/STK_PORT_NOTES.md.
- **string_harness1/2 — waveguide rounds 1-2** (09-12/13, round-2 ears
  logged in STRING_HARNESS_NOTES.md §Round 2 + commit 601335a):
  multi-attack gone except C5/C6 (possibly beating), "distorted guitar
  played by a bow" saturation, all cells read two octaves at 4.0x gain;
  32/36/05 = reference cell; b48 column dead. Next measurement round
  (f0-vs-2f0 across gain, C6 beat-rate vs detune) is campaign work, not
  a queue.
- **brass_harness1** (09-13 prelim, then superseded): "very saxy not
  brassy," squeaky high / buzzy low — retro-explained by the linear-bore
  finding above. Brass path continues via nonlinear-bore surrogate
  (Shaper + drive ← level follower) or donor survey, not this queue.
- **hysteresis_bow1/2** (09-08, verdicted same day): round 1 double-slip
  buzz; round 2's on-pitch cells = "syn-drum attached to an oboe" —
  mechanism right, character wrong; suspects logged in
  audition/hysteresis_bow2/README, feed bow round 3.
- **valve2 (+ valve1)** (09-08): rounds 1-4 failed to leave the reed
  basin; ceiling named and ACCEPTED by Matt — memoryless single-input
  junction = reed family. Log: docs/research/feedback_sweeps/
  VALVE_VERDICTS.md. En route: backlog 66 fixed, 65 filed.
- **feedback_curves5a — keytrack round** (09-02, verdicted 09-03): ramp
  curve unnecessary; noise keytrack KEEPER, sweet spot exponent
  -0.80..-0.95 per patch, Matt fine-tunes by hand. Led to r5b (item 55).
- **50. Bow family 30 cells** (08-24, PARKED same day, no verdict owed):
  superseded by the KSString-modes decision, itself since superseded by
  the harness campaign (KSString family → IDEAS 2026-09-14). Cells +
  conditioning/brt ladders stay in bow_family/ for reference; the sizzle
  split (fractional-read-head vs undamped recirculation) is recorded in
  this file's git history and backlog-33's IDEAS entry.
- **52. KSString bow mode** (08-24): "character + promise, problems on
  the periphery." Direction since moved to IDEAS (fall cleaning).
- **41. UI piano dynamic octaves + top C** (08-21/29): "fine."
- **51. Listen-here** (08-24/29): "works"; drill-out tap tweak shipped.
- **40. Triangle power** (08-21/29): "works fine."
- **49. Smoothing the bed** (08-23/29): LEVEL dominates; keytrack DOWN;
  jag40 > jag15; jins/jags "plucky not bowy." Folded into the (parked)
  excitation stub above.
- **35/36/37. AF piano thread** (closed 08-29): "that's AF piano, we're
  beyond that." Superseded by the KS piano family.
- **42. Curve editing + Envelope min/max** (08-22/23): works.
- **45 + snare_corner2. Snare corners** (08-23): "terrible"; snares
  PARKED with diagnosis (crack needs internal structure).
- **44/43. Segment sweeps rounds 1-2** (08-23): verdicted area by area;
  full log docs/research/oneshot_sweep/ROUND1_VERDICTS.md.
- **34. AFP excitation ladder** (08-15/22): approach EXHAUSTED.
- **33. AFP excitation stage analysis** (08-15/22): mooted by decoded
  ground truth.
- **32. Groups + Listen tap** (08-15): "work great, and this is huge."
- **31. 3n closed** (08-15): UI re-saves of library patches SAFE.
- **30. Mappings dialog / Parameter retirement** (08-14): "All good."
- **29. Stable node identity** (08-14): "Works."
- **28. Note-contained sound + Piano_bright baseline** (08-13/14):
  Piano_bright = KS piano library baseline (since piano_default).
- **27. KS v6 ladder + CMA runs 1-3** (08-10..13): superseded by
  Piano_bright; CMA re-scope notes live in the IDEAS entry.
- **26. Housekeeping 2026-08-10**: all answered.
- **25. Soprano alt-formant candidates**: re-do → backlog 3r.
- **24. Node-graph stream fix**: "Good, done."
- **22b. KS v5**: superseded by v6.
- **11. Power renorm**: "Leave it."

Older resolutions (2026-07-27 .. 2026-08-10) are preserved in git
history of this file and the run reports.

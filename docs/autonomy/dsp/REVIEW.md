# DSP lane — review queue

Rebuilt 2026-09-14 (fall cleaning); Matt's same-day verdict pass folded
the same evening. Awaiting = only what still needs Matt.

## Awaiting Matt

### 58. stk_bowed_ext3 — Larson's cello body, recovered [listen] (2026-09-14)
**renders/dsp/audition/stk_bowed_ext3/** (README inside) — the body he
never published, fit from his own WAVs (LTAS ratio of 4 with/without
pairs, tools/fit_larson_body.py; fit err median 0.68 dB at the pairs'
own 2.8 dB spread) as a 14-biquad stack on the canonical chassis.
Cells: ctl3_nobody / body_m48 (Maestre violin) / body_lar (the
recovered cello body) / btd_lar (his full width+torsion+dispersion+body
recipe). All +0c locks except btd_lar's documented D-string-scoped top
slots. In-graph validation: rendered LTAS diff matches the fitted
target, median +0.83 dB. A/B vs renders/scratch/stk_ref/mohonk05/
{bachd,bowedbtbodyd}.wav. THE QUESTION: does the recovered body turn
the chassis into a cello — and does body_lar beat body_m48 enough to
become the chassis default?

### 59. stk_port2 — Saxofony, BlowHole, BandedWG ports vs STK [listen] (2026-09-15)
**renders/dsp/audition/stk_port2/** (README inside) — the remaining STK
families, ported and machine-validated at native 22050 (rate discipline;
canon to 48k comes after ears). 7 A/B pairs: saxofony default +
pos_bridge (the "blowed string", blow position = its sax↔clarinet
axis; ±1c locks, noise-free 0.04 dB at C4), blowhole default (register
vent + dynamic tonehole; locks C3-C5, top slots out of model range at
22050, noisy variants register-flip — the ref flips against itself
too), bandedwg tbar struck+bowed / bowl struck / glass bowed (banded
waveguides: struck bars/bowls with EXACTLY matching decay rates —
first percussion-with-physics in the graph — and bowed bars that
bloom over ~4 s like the model does).
THE QUESTION: which families earn extension rounds on the validated
chassis (the bowed precedent)? Saxofony = strings/sax-adjacent;
BandedWG = new percussion territory; BlowHole = another reed.

### 38. AF saxophone — build awaiting go [discuss] (2026-08-18)
docs/research/af_sax/RECIPE.md: complete value table from the breakdown
video; only 3 TUNEs remain. BLOCKED on design go-ahead: needs a new
ModDelayLoop node (audio-rate delay mod + in-loop tanh + gain>1) and a
body strategy. Steal-first note: the actual patch file is on the AF
Discord (parse_af_patch.py decodes it) — Matt 09-14: "still blocked on
my annoyance with Discord." Alternative: green-light the build from the
video table (the 3 TUNEs get swept by ear).

### 39. Wheel + pressure, live [try — PARKED on hardware] (2026-08-20)
Matt's current MIDI keyboard has no wheels; pends a different
controller. Wire a Note node's `wheel` pin through a Curve into
something audible and ride it, whenever hardware appears.

## Resolved

Compact stubs only; full detail in run reports, STATUS digests, and the
verdict logs cited. Newest first.

- **53. stk_bowed_ext1** (09-13, verdicted 09-14): hysteresis cells (3)
  REJECTED — "extremely harsh noise alongside the bow/note" (patches →
  old/stk_bowed_ext1_rejects/, renders deleted; mechanism note: pairs
  with the ext2 diagnosis that stick must LEAK ~2% — next hysteresis
  round builds that in before re-audition). ext_perf = runaway vibrato,
  root-caused: VibMul (±1.0 sine x 0.27 curve) SUMMED into the neck
  delay ratio — a fifth of wobble; Matt's ear-validated 0.05 scaling
  folded into VibAmp's curve coefficients (mathematically identical,
  canonical form), re-rendered, measured ±19c at C4 — "probably the
  most stringy vs. saxy" cell. All other cells = saxophony, KEPT in
  place for hand-experimentation (audition/sweep dirs stand).
- **54. stk_bowed_ext2** (09-13, verdicted 09-14): "all noisy and
  complex in a good way... could be good if tamed by filters we're
  planning to add anyway." NO REJECTS; cells stand. IR links were
  garbage/malware-adjacent — IR pick DROPPED as a Matt task; round 3
  proceeds from Larson's MEASURED body curve in STK_PORT_NOTES (biquad
  stack fit, A/B vs bachd.wav) — no download needed.
- **55. feedback_curves5b axis tour** (09-03, verdicted 09-14): "all
  promising, tho very similar... they go on that heap." No axis
  separation delivered, no neighbor round requested — backlog 37's r5b
  thread CLOSES; cells stand in pending/sweep for hand-play.
- **56. feedback_inloop1** (09-05, verdicted 09-14): "No to formants in
  the loop. None successful." Patches → old/feedback_inloop1/, renders
  deleted. Formants stay OUTSIDE the loop (oboe_default's bell form).
  In-loop BREATH (backlog 37d) is a separate question, still open.
- **57. Two one-worders** (decided 09-14, both EXECUTED same day):
  (a) pan law — "make mono patches write x1" → StereoMixer equal-power
  law normalized to unity at center (engine, verified exactly sqrt2 x
  old within 1.4 LSB; WAV loudness now == UI); manifest refrozen.
  (b) Vibrato — audio-rate modulation never wanted; per-note pins
  ALREADY WORK via stock dynamicPins (speed/depth are setting
  descriptors): proven by render — depth curve 0.001@C3/0.08@C6 gives
  19c vs 226c measured wobble. Backlog 22 closed, nothing to build.
- **(parked) Excitation rounds 1-3** (08-23, notes 09-14): "lots of good
  stuff, just no time." Insights recorded to backlog 37: ZITHERING fast
  enough approaches continuous bow excitation; creak_fine
  (excite3_triptych) demonstrates best — at lo-f "a 20-foot tall cello
  on Mars." Renders stand; no verdict owed unless the attack-texture
  front reopens.
- **stk_port1 — 4 families, port-vs-REF pairs** (09-13, verdicted rounds
  2-5): bowed "port matches REF"; clarinet/flute canon validated ±1-2c;
  brass closed as parity-with-a-poor-reference. Meta-verdict: STK
  skeletons underwhelm — the yield is the validated chassis + the
  missing-ingredients list (nonlinear bore, real body IR, finite bow
  width). "Brassy" needs NONLINEAR bore propagation.
  Full record: docs/research/stk_port/STK_PORT_NOTES.md.
- **string_harness1/2 — waveguide rounds 1-2** (09-12/13): round-2 ears
  in STRING_HARNESS_NOTES.md §Round 2. Next measurement round is
  campaign work.
- **brass_harness1** (09-13 prelim): "very saxy not brassy" —
  retro-explained by the linear-bore finding.
- **hysteresis_bow1/2** (09-08): "syn-drum attached to an oboe" —
  mechanism right, character wrong; suspects feed bow round 3.
- **valve2 (+ valve1)** (09-08): ceiling named and accepted — memoryless
  single-input junction = reed family. VALVE_VERDICTS.md.
- **feedback_curves5a — keytrack round** (09-02/03): noise keytrack
  KEEPER at exponent -0.80..-0.95; ramp curve unnecessary.
- **50. Bow family 30 cells** (08-24): PARKED, no verdict owed;
  KSString family → IDEAS.
- **52. KSString bow mode** (08-24): "character + promise"; → IDEAS.
- **41. UI piano dynamic octaves + top C** (08-21/29): "fine."
- **51. Listen-here** (08-24/29): "works."
- **40. Triangle power** (08-21/29): "works fine."
- **49. Smoothing the bed** (08-23/29): LEVEL dominates; keytrack DOWN.
- **35/36/37. AF piano thread** (08-29): "we're beyond that."
- **42. Curve editing + Envelope min/max** (08-22/23): works.
- **45 + snare_corner2. Snare corners** (08-23): parked with diagnosis.
- **44/43. Segment sweeps rounds 1-2** (08-23): verdicted area by area;
  docs/research/oneshot_sweep/ROUND1_VERDICTS.md.
- **34. AFP excitation ladder** (08-15/22): approach EXHAUSTED.
- **33. AFP excitation stage analysis** (08-15/22): mooted.
- **32. Groups + Listen tap** (08-15): "work great, and this is huge."
- **31. 3n closed** (08-15): UI re-saves of library patches SAFE.
- **30. Mappings dialog / Parameter retirement** (08-14): "All good."
- **29. Stable node identity** (08-14): "Works."
- **28. Note-contained sound + Piano_bright baseline** (08-13/14).
- **27. KS v6 ladder + CMA runs 1-3** (08-10..13): superseded.
- **26. Housekeeping 2026-08-10**: all answered.
- **25. Soprano alt-formant candidates**: re-do → backlog 3r.
- **24. Node-graph stream fix**: "Good, done."
- **22b. KS v5**: superseded by v6.
- **11. Power renorm**: "Leave it."

Older resolutions (2026-07-27 .. 2026-08-10) are preserved in git
history of this file and the run reports.

# DSP lane — review queue

Rebuilt 2026-09-14 (fall cleaning); Matt's same-day verdict pass folded
the same evening. Awaiting = only what still needs Matt.

## Awaiting Matt

### 62. bwg_perc2 — tuned percussion, decay fixed [listen — BLOCKED, re-render coming] (2026-09-16)
**renders/dsp/audition/bwg_perc2/** — marimba/xylo/vibes/chime/tom/
woodblock/glass with realistic decay times (round 1's fault: everything
rang like a gong; fixed). YOUR 09-16 NOTE: "so quiet I can barely hear
it at 100%... everything sounds glassy." The quiet is a render-gain
calibration bug on my side (queue cells rendered ~20 dB below library
patches) — next run re-renders this queue at proper loudness; hold your
verdict until then. "Glassy" is recorded as a first impression to
re-test at real level.

### 65. Metal plates & plucks, safe-filter retry [listen] (2026-09-16)
Two folders. **renders/dsp/audition/mesh2d_ext2/**: 14 struck-plate
sounds — every "special edge" version sits next to an identical plain
version so you can A/B what the special edge adds. Four plate sizes,
two strike spots, three hammer hardnesses. These do NOT track pitch —
a plate's pitch IS its size (same as a real gong); making them playable
by key needs one of the routes we discussed (size-per-note = coarse,
strike-spot-per-note = timbre walk, or plate-as-attack into a pitched
string). **renders/dsp/audition/pierce1d_2/**: 4 plucked strings (the
ones you liked, rebuilt on the mathematically-safe filter) — these DO
track pitch; C3 then C4 in each file. If they sounded same-pitch to
you, tell me — that would be a bug.
Background in one line: the special edge is a filter that shifts energy
between frequencies without ever adding any (we found and fixed a
real flaw in the published design to get that guarantee).
THE QUESTION: A/B the twins — does the special edge add anything you'd
use? And plucks: better/worse than the 63 round you liked?

MATT (09-17, folded): "None of these track pitch" — mesh: correct and
structural, see above; plucks: they should — flag if not. "What does
'the passive round' mean" — jargon, retired; plain-language REVIEW
entries from now on.

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

- **63. pierce1d_1** (folded 09-17): Matt's verdicts arrived via chat
  09-16 and were never folded here — hygiene miss, his "thought I
  responded?" is right. Verdicts: plucks "pretty nice" (nostalgia
  caveat); the stale-v1 speaker incident came from this queue (rules
  now in WORKFLOW.md); s8k upwelling = partly parametric pump, not
  passive physics. Superseded by pierce1d_2 (REVIEW 65).
- **64. mesh2d_ext1** (folded 09-17): verdicted 09-16 in chat
  ("underwhelming... a bunch of dings... variety as narrow as could
  be, not key-tracked") and superseded by REVIEW 65 the same night —
  should have left Awaiting immediately; Matt's "isn't this out of
  date?" is right. Lessons executed in 65: feature-audibility gate
  (round-1 metrics celebrated -60 dB trivia), passive filter for full
  drive, axes that move audible furniture.

- **BandedWG re-audition + ring-out revert question** (Matt 09-16,
  answered same day): verdict was KEEP — adaptive ring-out demonstrably
  helps musical notes (reverb/piano tails complete; quiet-enders
  byte-identical; cost one abs+multiply per voice-sample) — and the
  residual "still hear the hard cutoff" was the 8 s CAP chopping
  near-lossless resonators (struck bowl T60 = minutes). Fixed: the cap
  now fades 80 ms instead of cutting (offline + live; one deliberate
  gate DIFF, piano_seg, refrozen). Matt's framing adopted: ambient
  ring-forever sounds are Stream-mode citizens / get duration = ring
  time; the cap+fade is the notes-world backstop. His sound notes
  recorded verbatim: "glass less glassy than tbar, imeo... more
  inharmonicity which I associate with metal things"; bowed cells "very
  quiet, very slow bloom, cut off before fully bloomed" even held — NOT
  a physics threshold: the bowed patches bake the bow-stop at ~4.5 s in
  a seconds-mode AdsrEnv (gen script), so holding a note can't extend
  the bow. Next bowed round should switch to a gated/hold envelope.
- **60. bwg_perc1** (09-16, verdicted same day): "lots of fun...
  near-automatic wins. Well-plowed ground. However, names don't align
  with sounds, and main issue is they are almost all loooong ambient
  sounds — TomDrum sounds like a gong. Woodblock is a pretty good tom
  in low register, somewhat marimba-ish high. We need characteristic
  envelopes, or the physics equivalent (ultra-rapid decay)." → REVIEW
  62 (constant-T60 gain curves) executed same day; round-1 renders kept
  for A/B.
- **61. Donor papers** (09-16, verdicted same day): "Sure" to the
  1D-Pierce-probe-then-Mesh2D order. On brass: "I'm counting on you..
  if we need a non-linear bore, let's do it!" → nonlinear-bore campaign
  is GREENLIT as a future front (donor survey names the lineage:
  OpenWind / MoReeSC / NESS, or native amplitude-dependent waveshaping
  along the bore).

- **58. stk_bowed_ext3 — Larson body** (09-14, verdicted 09-16): btd_lar
  REJECTED ("4 detuned cellos at the bottom... completely haywire at the
  top"). ctl3_nobody / body_m48 / body_lar all have "promising bow
  attacks"; body_lar does NOT beat body_m48, no_body credible — no
  chassis-default change. ALL cells "lose string attack character and
  turn into whistles at the top end" → the WHISTLE-TOP issue is the
  named gate for higher-register bowed work (bowed thread continues
  toward cello). Folded 2026-09-16.
- **59. stk_port2 — Saxofony/BlowHole/BandedWG (+brass)** (09-15,
  verdicted 09-16): Brass, Saxofony, BlowHole ABANDONED for now ("I've
  done better tweaking the oboe and string patches"; brass mostly
  silent/near-silent low, squeaky-shrill high; saxofony string-like low
  + raucous whistle high, reed_hard/soft low-volume zithering; blowhole
  only _default low C interesting). Note: "silent" pairs passing = the
  gates scored PARITY WITH THE REFERENCE (STK's own silence), not
  quality — same lesson as backlog 72's lucky-seed find: machine gates
  need a floor for "audible at all." BandedWG KEEPER, two directions:
  (1) percussive sounds — tuned drums, xylo, chimes (low-hanging
  fruit); (2) percussive/bowed cells as excitation sources. Known
  defect to fix first: all bandedwg renders "cut off abruptly with a
  click" (Matt couldn't find a culprit node — likely render-window, not
  patch). New areas queued: DAFX04 P_101 (brass beyond STK?) and
  Chafe 2D-mesh extensions (harness feasibility). Folded 2026-09-16.

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

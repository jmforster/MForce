# DSP lane — review queue

Rebuilt 2026-09-14 (fall cleaning); Matt's same-day verdict pass folded
the same evening. Awaiting = only what still needs Matt.

## Awaiting Matt

### 70. Trombone, attempt 2 [VERDICTED 09-18: "Yes, I'd play it" — folds to Resolved when the 74a round lands]
**renders/dsp/audition/trombone2/** (8 files, README inside) and the
patch at **patches/audition/trombone1/trombone_attempt2.json** — it sits
in the same folder as attempt 1 so you can load one, then the other.

**First, the thing you caught.** You said low C doesn't sound and the
lowest note is G2 at 49 Hz. You were right, and there was no bug in the
patch — I had been writing note names an octave below the ones your
keyboard shows you. The UI names a key by dividing its MIDI number by
12; I was using the other common convention. So my "low C" was the key
you call **C3**, and the key you pressed labelled C2 was a whole octave
below anything I had ever tuned. Your 49 Hz, your 784 Hz top note and
your "tink" above it are all reproduced exactly in the render. I also
rendered the same patch through the command-line renderer and through
the UI's own render path to be sure: they agree to six decimal places,
so there is no live-versus-offline difference at all. Everything from
here on is in YOUR note names.

**What that fixed.** Last round only ever tuned from C3 upward, and you
play down to G2. Below C3 the patch was guessing — up to 80 cents sharp,
nearly a quarter tone, with the lip set to a guess instead of a measured
value. This round solves **every chromatic note from F2 to G6**, the whole
range you reported as playable. All 51 of them speak and the worst one is
**1.9 cents** off (last round: 37 notes, worst 4.2). Nothing that worked
before got worse — that is gated, not hoped for.

**Volume.** Two causes, both fixed. The queue was never compared to a real
patch; I now render `patches/library/winds/oboe1.json` through the same
pipeline and match it. And the instrument itself was **14.3 dB louder at
the top of its range than at the bottom**, so levelling a line that crosses
the range set the gain from the top note and buried the trombone register.
There is a measured per-note level trim in the patch now and that spread is
**0.0 dB** — it is in the patch, so it is there when you play it live.
`soft` is **12 dB louder** than the one you couldn't hear.

**Brightness.** There is a lowpass in the patch. `line.wav` is the darker
corner and `line_brighter.wav` is the brighter one, same take otherwise.
**This is the one thing only your ears can decide — if you answer nothing
else, answer which of those two is the instrument.**

**The low register.** I tested two theories about the attack and both were
wrong, and I am not dressing them up: the sustain down there is as periodic
as the top, and the bottom actually settles onto its pitch FASTER than the
top does. What was genuinely broken is the two things above — those notes
were a quarter tone sharp and 14 dB quiet. `low_before.wav` against
`low_after.wav` is that comparison on the notes you were playing, and the
pair is deliberately NOT level-matched, so you hear the loudness difference
as it really is. If the rattle is still there after that, it is a third
thing and I have nothing measurable on it yet.

**Your soft/loud observation was the useful one.** "High C in soft
struggles to lock in while loud locks much quicker" — that is real and it
now has a number. A note won't start at all until the blowing pressure
clears its own threshold, and that threshold climbs steeply at the top:
everything up to B5 starts at 8 kPa, C6 needs 12, F6 needs 36, G6 needs 72.
Last round blew the same pressure at every pitch, so its soft setting was
literally below the pressure the top notes need. The patch now carries a
measured per-note breath map, and the top two notes went from "won't start
unless another note just played" to starting on their own. The honest cost:
this instrument's soft end is limited by its top note, so soft and loud are
31 and 50 kPa — a narrower range than I claimed last round, because that
window had been measured on a line where each note coasted in on the
previous one's energy.

**Still the weak spot, unchanged:** blowing harder barely changes the
timbre (centroid 473 → 477 Hz). A real trombone opens up enormously between
p and ff and this one does not. Now at least it is loud enough for you to
judge that for yourself.

THE QUESTION: same as last time. **Would you put it on a track and play
it?** And: `line` or `line_brighter`?

MATT: Yes, I'd play it. Decent trombone in the lows and trumpet in the
highs. Somehow you fixed the attack on low notes.. it's not perfect but
way better. Line beats line_brighter. Nice mellow tone. Further refinement
possible, of course, especially the blowing harder thing.

Report: reports/2026-09-18-trombone2.md.

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

- **69. Trombone, attempt 1** (verdicted 09-18, superseded by 70 the same
  day): the full assembly — measured Smyth & Scott bell, one-tube bore with
  the lip picking the partial, key-gated envelope, fast release, 37 notes
  inside 5 cents. Matt, verbatim: "Would I put it on a track and play it?
  No, not quite yet. But it is way, way better. Everything is waaay too
  bright, but a final LPF takes care of that. The top register with the lpf
  added is a usable trumpet, with a 'straining' squeaky attack. The brass
  character disappears in the lower register, attack comes off as a kind of
  rattle and the sustain phase isn't quite right either, but I can't quite
  pin down why. If by low C you meant C2, that does *not* sound for me
  (using 'loud' patch). Lowest sounding note for that patch is G2 (49Hz).
  G6 (784) is highest note (A/B/C7 make sound but it's like a tink)...
  overall a pretty great range. Overall volume is still very low, so it's
  tough to confirm your (lack of) loudness/briteness linkage diagnosis -
  because 'soft' is barely audible at 100% speakers. I did notice high C in
  'soft' struggles to lock in while 'loud' locks much quicker, in case
  that's relevant. Go ahead with next pass, though I haven't given you much
  re: 'fixing' the actual trombone range (maybe adjustments to 'lip' /
  excitation for lower notes?)." All five items answered in entry 70. The
  one that mattered most was mine to own: the round's note names were an
  octave below the UI's, so "C2 does not sound" was me tuning a different
  octave from the one he was playing — not a live-path bug, and the two
  render paths agree to six decimals. Renders superseded by
  renders/dsp/audition/trombone2/; attempt 1's patch kept beside attempt 2
  for A/B. Report: reports/2026-09-18-trombone1.md.
- **68. One-mass lip, first ears** (verdicted 09-18, Matt in REVIEW):
  "Attack indeed resembles a brass attack, though it's still pretty far
  from a trombone patch. Definitely not saxy." — the mass-on-a-spring lip
  is the first wind exciter to escape the reed basin. Four defects
  recorded, all actionable: (1) 18–33c sharp (deferring it was MY spec's
  sequencing, not Matt's ask — mis-attributed in the entry); (2) attack
  time tied to frequency, low notes take "forever," only whole notes
  playable; (3) on QWERTY the note evolves and releases regardless of the
  key being held (envelope not key-gated); (4) release mirrors the attack
  — "b-waa-OO" — unnatural at current strength. Matt's directive:
  "proceed with intonation + bell + whatever else is known to be lacking
  in pursuit of a usable trombone patch" → trombone round dispatched
  same day (next entry when it lands). Report:
  reports/2026-09-17-onemass-lip1.md.
- **67. Brightener on the old lip patches** (verdicted 09-18): Matt
  listened despite the "optional" label: **no good** — the brightener on
  the memoryless-lip carriers does not make brass. Expected result, per
  the entry's own record: the lips were the problem, not the tube; the
  round was prep for 68 and 68 is the real attempt. Kept from the round:
  steepener-inside-the-tube placement, the harness tuning correction
  (34c flat C3 → 22c sharp C5, not uniformly sharp), the open STK
  darker-at-weak-settings oddity (one cause eliminated, real one
  unknown). Cells rejected: patches → patches/old/nlbore_brass1/,
  renders deleted. Hygiene note on the record: the entry was left
  muddled — a "superseded" note on top of the unedited body still
  carried A/B instructions; fold-on-supersede means collapsing the body,
  not stapling a warning above it. Report:
  reports/2026-09-17-nlbore-brass1.md.
- **66. Does forte get brassy?** (queued and verdicted 09-17): the
  loudness-brightness effect (a delay whose length wobbles with the signal
  through it) hung on the flute and oboe outputs, zero engine code. Matt:
  "It definitely brightens in a brassy way." Element KEPT. Measured
  bit-identical when off (−231 dB), monotonic in six steps, 9.5× the
  distortion for 9× the drive. Honest correction on the record: it does
  mild FM, not shock formation — the source paper says so when shock
  handling is omitted, which it was, deliberately. It bites ~6× harder on
  the oboe than the flute, i.e. on bright fast material, which pointed at
  the lips as the next build. Superseded same day by REVIEW 67, which does
  exactly that. Report: reports/2026-09-17-nlbore-probe1.md.
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

# DSP lane — review queue

Rebuilt 2026-09-14 (fall cleaning); Matt's same-day verdict pass folded
the same evening. Awaiting = only what still needs Matt.

## Awaiting Matt

### 74. One breath per line, done properly [listen] (2026-09-20)
**renders/dsp/audition/articulation1/** — 6 files, README inside.
Regenerated; these replace yesterday's files of the same names.

You listened to yesterday's phrased renders and told me what was wrong:
a tick at every pitch change, nothing happening at all on repeated
notes, and the envelopes stretched out of shape over a whole line. All
three had one cause. Yesterday a phrase became ONE LONG NOTE to the
engine, so every envelope that measures itself as a percentage of "the
note" was suddenly measuring itself against the whole line — oboe1's
release, written as a quarter of a note, became a three-second die-off.

So we rebuilt it the way you described it this morning: a player never
looks ahead, they just read each note's markings. Every note now carries
two facts of its own — how it BEGINS, and whether the breath keeps going
past its end. Nothing is a "phrase" to the engine any more. Envelopes are
measured against their own note again, always.

What that buys, on these files:

* **repeated notes get a tongue.** A player cannot slur a repeated note,
  so the only way to say it twice is to articulate it. Dented about
  11 dB on the oboe over roughly 50 ms — the middle of the range I
  measured off a real player (7 to 23 dB).
* **pitch changes get a slur instead.** The pitch slides to the new note
  over about 15 ms rather than jumping. That slide IS the slur, and it
  is also what removes the tick you heard: jumping the pitch instantly
  puts a kink in the tube's stored wave.
* **no dropouts.** Yesterday's oboe tongue throttled the air going INTO
  the resonating tube, and on a loop that is already near its ignition
  edge that killed the sound outright at 9 of 28 note boundaries (one
  came back an octave up). The tongue now dents the sound AFTER the
  tube, where it cannot put the fire out. Zero dropouts in both phrased
  lines and both hold cells.

Also yours to use, in the UI: the Patch Output settings pane has a
**sustaining** checkbox and an **onsets** field now, so teaching a patch
no longer means editing JSON. Only a patch you have checked as
sustaining will phrase at all — that is deliberate, it is what
guarantees a piano still plays a phrase-marked passage as plain notes,
provably, byte for byte. If you mark a passage with `|` on a patch that
is not checked, the transport says so instead of quietly doing nothing.
And the polyphony spinner finally has a visible label; it has been an
unlabelled mystery number since it was added.

THE QUESTION: **play `otj_oboe_phrased.wav` — does it sound like one
breath per line, with tongues only on the repeated notes and clean slurs
between pitches?** The `flat` file next to it is the before. Same pair
for the trombone, and the `phrase_hold_*` files are the isolated
consonant, the same note eight times on one breath.

If you play only one file, play `otj_oboe_phrased.wav`.

Known limits, stated up front: the trombone's tongue reads much gentler
than the oboe's (about 1.4 dB at the output) because its bore rings
straight through a breath interruption — if it reads as slurring rather
than tonguing, the TDip envelope in that patch is the whole knob. Live
keyboard phrasing is now pure wiring but still unbuilt (backlog 53).
Within-note swell is still 74a8, its own conversation. And I could not
drive the UI myself, so the `|`-passage behavior in the transport is
covered by reasoning and by the score-side renders, not by my ears —
that part is yours to confirm.

Report: reports/2026-09-20-note-onsets-v2.md; spec
docs/superpowers/specs/2026-09-20-note-onsets-v2-design.md.

### 72. Trombone, attempt 4 — the held note [listen] (2026-09-19)
**renders/dsp/audition/trombone4/** — 8 files, README inside.

**What changed, in one line:** the tone control at the end of the chain now
moves with how hard you blow, instead of sitting at one fixed setting.

You said the loudness/brightness link was there but "only in the attack —
once the notes settle, their sustain phases sound identical to me, just
louder", and told me to check it empirically. I did. The instrument *was*
making a different tone when blown harder, all the way through the held part
of the note — and then throwing most of it away at the very last step. The
culprit is the tone control you picked yourself last round ("line beats
line_brighter"). It is one fixed setting, and you picked it back when the
instrument was stuck permanently at full blast, so it was chosen to tame a
sound that had no dynamics. Sitting where it sits, it was removing most of
the movement: on the low F it let +1% of +25% through; on F5, +27% of +163%.

So the tone control now opens when you blow harder and closes when you play
softly. **At your normal playing strength it is exactly the number you set
and the sound is unchanged** — I checked that sample by sample against
attempt 3 and the two files differ by one step of a 16-bit file, which is
rounding. How far it opens and closes was solved, not picked by ear: the rule
is that the amount the instrument brightens between soft and hard has to
survive to the output.

Also in this build: six of the 51 notes had their hardest setting pulled in,
because their attack squeaks at maximum. Velocity 0.8 is untouched — the
"not-great trumpet player" squeak you said you like is still exactly there.
One of those six, C#6, was not squeaking at maximum, it was dropping a whole
octave.

**On the rattle map you sent** — you guessed "some Curve just needs some
tweaking" and added "have a feeling it's not that simple". It isn't. I
measured the rate of that oscillation on nine notes and tested it against
every candidate in the instrument. **The oscillation is the note itself** —
its own pulse rate, to within one percent on every note where the
measurement gives a clean answer. Not the air column's echo, not the lip,
not any envelope timing. So there is no rate to retune: changing it means
changing the pitch. Why it sounds like an impact down low is that below
about 40 pulses a second the ear stops hearing pitch and starts hearing
separate events — and you located that boundary by ear to the semitone. You
said the sustain appears at D2, 36.7 Hz. Measured, D2 is the first note that
holds at all; C1 and F1 produce no oscillation whatsoever, so all there is
to hear down there is the thump and the ring-down. Those notes are also
below everything that is tuned (every setting stops at F2), which is why D2
and E2 play 12–27 cents sharp.

**THE QUESTION: play `hold_soft.wav` then `hold_hard.wav`.** They are the
same note held four seconds, matched to the same loudness in the held part
on purpose, so volume can't fool either of us. Is the held part a different
tone now, or still the same tone louder? If you play only one file, play
**hold_hard.wav**.

Two things I did NOT fix and want you to know before you listen. Soft is now
darker AND about 4.5 dB quieter than last round — that is the soft end of the
same tone control, and if it's too much it is one number. And nothing still
changes *during* a held note: measured, the tone is identical to three digits
from 0.2 s to 2.2 s. Your "swell after the attack" needs the breath to move
while a note is sounding, which nothing in the current setup can do — that's
a design conversation (a breath controller, or a per-note shape), not a tweak.
Report: reports/2026-09-19-trombone4.md.

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

- **73. One breath per line — phrasing lands** (auditioned 09-19,
  SUPERSEDED by entry 74 the next day): Matt drove the v2 redesign the
  morning after listening. His findings from that pass: a retune tick at
  every pitch change; no articulation at all on repeated notes; and the
  envelope-stretch symptoms (oboe1's quarter-of-a-note release becoming
  a ~3 s die-off over a line, its attack stretching to about a second).
  One cause under all three — v1 delivered a phrase as ONE LONG NOTE, so
  every percent-mode stage re-scoped from the note to the whole line.
  v2 makes the note the timebase again, gives every note its own
  onset/hold, and replaces the instant retune with a glide. Report
  reports/2026-09-20-note-onsets-v2.md; v1's divergence from its own
  spec is on the record in the v2 spec §9.

- **71. Trombone, attempt 3 — the blowing-harder round** (verdicted 09-19,
  SUPERSEDED by entry 72 the same day): the two-gain-stages fix landed, but
  only half the instrument. Matt, verbatim: "Line is still good, no
  regression... the loudness/brightness linkage is there, but it's subtle,
  and *only* there in the attack - once the notes settle, their sustain
  phases sound identical to me, just louder... what's missing to my ears
  is any swell or brightening *after* the attack, which is pretty
  characteristic of brass." → backlog 74(a5), attempt 4's headline.
  His octave-by-octave rattle map, new evidence that reopens the thread
  attempt 2 closed (→ 74(a6)): a "dullish impact put thru a spring
  reverb, with a second / softer spring reverb rattle at the release";
  octave 1 is ONLY that, regardless of hold; octave 2 (from D2/36.7 Hz)
  sustains but with a formant-modulation "B-wAWWW" at G2 becoming
  "B-wAHHH" by C3; by G3 the formant shift is gone and the rattle has
  compressed into a sluggish horn attack, improving through octave 3; by
  G4 the release tail is a terminating buzz, by C5 no longer a problem
  "and possibly even a feature"; octave 5 is the best overall sound
  (trumpet territory), attack intermittently squeaky, especially now at
  max velocity (→ 74(a7)) — at 0.8 it "squeaks just enuf to sound like a
  not-great trumpet player - but is so perfectly realistic that I kind of
  like it"; octave 6 sustain weakens to "tinks" at A/B/C (74(b) stands).
  His mechanism guess, recorded as the map's shape: an oscillation in the
  attack, repeated in the tail, linked to frequency — too slow low,
  perfect in octave 5, too fast at the top of octave 6 — "as if some
  Curve just needs some tweaking. Have a feeling it's not that simple."
  (Likely relevant and to be verified in the round: the solved maps start
  at F2/midi 29 — what the patch does below that is currently
  uncharacterized, and octaves 1–2-below-F2 are exactly where the rattle
  is worst.) Two side reports spun off: the last note of a passage cuts
  off abruptly even with a trailing Rest → backlog 75 [bug]; the UI can
  barely handle a patch this size on his laptop → backlog 76 [perf], and
  Matt added auto-grouping/auto-layout + a Patch node to the GOALS
  wishlist himself. Report: reports/2026-09-18-trombone3.md.

- **70. Trombone, attempt 2** (verdicted 09-18, superseded by 71 the same
  day): the range Matt actually plays solved end to end — 51 chromatic
  notes F2–G6 (HOUSE names) inside 1.9 cents, a measured per-note breath
  map so the top two notes start from silence, queue loudness matched to a
  library patch, and the note-naming mismatch that cost round 1 an octave
  found and fixed. Matt, verbatim: "Yes, I'd play it. Decent trombone in
  the lows and trumpet in the highs. Somehow you fixed the attack on low
  notes.. it's not perfect but way better. Line beats line_brighter. Nice
  mellow tone. Further refinement possible, of course, especially the
  blowing harder thing." Acted on: the 1100 Hz "line" voicing corner is
  now the instrument's, fixed, and the blowing-harder thing is entry 71's
  whole round — it was two of my own gain stages, not the physics
  (reports/2026-09-18-trombone3.md §1). Full detail:
  reports/2026-09-18-trombone2.md.

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

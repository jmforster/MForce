# DSP lane — review queue

## Awaiting Matt

### 35. afp31_v1 — the from-scratch AFNoding-031 rebuild [listen] (2026-08-16)
renders/dsp/pending/afp31/afp31_v1.wav (C2/C4/C6 hard + C4 soft);
patch patches/pending/afp31/afp31_v1.json; generator tools/gen_afp31.py;
recipe docs/research/afpiano_scratch/RECIPE.md. Built per the build
video: noise x 10 ms gated env -> resonant SVF LP (normalize, res 6)
with cutoff = min(12*f0, 1660 Hz) — his min(pitch+12, stiffness) clamp
CALIBRATED against the measured step_3 knee (AF cutoff units are ~2.5
octaves above naive MIDI; first cut at 277 Hz left C6 nearly silent,
atk 0.005 -> 0.363 after calibration) -> x6 -> gentle SVF (his 1P) ->
KSPianoString (3 combs = his 3 strings, t60 = 6900/f == his fixed 0.999
held feedback, releaseFb 0.8 == his release value, 210 ms damper drop
== his smoothing) -> SVF HP res 3 (Body) -> SVF LP res 8 normalized
with velocity vcurve +12..+64 semitones (his velocity filtering).
Measured: attacks 0.28-0.39 all registers (no dead notes), sustain
-16 dB @1.5 s uniformly, centroids 693->1207 Hz (mid-heavy, AFP zone).
New engine node SVFSource powers 4 roles (in your Filters menu after a
restart). TUNE list (first A/B targets): brightness/res (6), exc
release (120 ms), body_hp cutoff (0.75x f0) and res, vel_lp scale, the
x6, string brightness/dispersion. Your Listen-here workflow applies —
drill the excitation, compare against the AFP stage WAVs.
V2 same day after Matt's v1 verdict ("sizzle that never decays;
sawtooth-y"): BOTH v1 defects were mine, not the recipe's. (1) The
excitation env held full-level noise while the key was down — but the
env followers are fed by the TRIGGER pulse, not the gate (transcript
25:52); a 0.999 comb integrates continuous noise into a non-decaying
sizzle. One-shot now (10 ms attack / 350 ms release). (2) My damper
stage lacked min/maxSec caps, so the felt landed from the note's
MIDPOINT — v1's "sustain" was noise masking a choked string; fixed to
the Piano_bright 3-stage shape (210 ms landing at note end). Plus
brightness now pitch-tracked (flat 0.72 was eating C6's fundamental —
dead by 0.5 s; now -17.8 dB @0.5 s and singing). Decay profiles all
registers: attack -> graceful piano-slope decay -> clean damp.
LISTEN: afp31_v2.wav. Sawtooth-y verdict awaits re-listen on v2 —
if it persists, first suspects are exc resonance 6 and vel_lp top
(+64 semis) per the TUNE list.
V3 + V3B (2026-08-17, after Matt's v2 verdict): every complaint traced
to a readable value and the frames were read (RECIPE pass 2).
"Chuff too high-frequency" = my x6 unit calibration was WRONG (AF units
are plain MIDI; excitation clamp is literally ~277 Hz — matches the
dark AFP excitation always measured). "Attack too harsh + tail too
long" = the hit envelope is a 10 ms PULSE into a 0 ms-attack / 361 ms-
release follower — instant attack, short hold, exp tail (v2 had a Sine
ramp + Sine tail). "Ba-oh/nasal in low octaves" = my vel_lp read: the
velocity crossfade 12..64 ADDS to LIMITED pitch, so the res-8 peak is
register-independent above the clamp (v2 tracked raw f0 and parked the
peak in the 1.5-2.6 kHz nasal zone on bass notes); body_hp likewise now
min(f0, 139 Hz). One honest fork: the faithful 277 Hz clamp leaves C6
at -48 dB (his video never plays up there) — **afp31_v3.wav = fully
faithful (dark/dead top), afp31_v3b.wav = identical except the
excitation cutoff floors at f0 so treble stays alive.** A/B both; low
and mid registers are byte-similar between them.
V4 (2026-08-17, Matt: "any other guessed settings readable off the
video?" — yes, and two changed the topology; RECIPE pass 3):
**LISTEN afp31_v4.wav — supersedes v3/v3b.** (1) The excitation SVF
cutoff is NOT pitch-tracked: it's a separate Brightness knob FIXED at
91.50 MIDI ~= 1.6 kHz (eerily = my retracted 1660 Hz "calibration" —
the 2021 knee was this filter), res 1.2 (not 6). Only the 1P tracks
min(2f0, 277) — and it's 6 dB/oct, so C6 loses ~12 dB, not 48: the
v3b floor concession is withdrawn, one faithful patch again. New
SVFSource Lowpass1P/Highpass1P modes power it. (2) In-loop damping
decoded (z_damp_1325): loop-filter cutoff = gate-follower x knob 135
MIDI ~= 20 kHz while held — the loop is nearly OPEN; all release choke
is the follower slide. So string brightness is FLAT 0.926 (20 kHz
one-pole coeff), not my pitch-tracked curve, and held feedback reads
0.9995 (not 0.999) -> t60 = 13800/f. (3) Body HP = limited + Low cut
3.30 semis (min(2f0, 277) x 1.21, res 3). (4) The missing FINAL 1P
added after vel_lp, cutoff = 2f0. Measured: C4 sustains -14 dB with
gentle decay then clean 210 ms damp; C6 alive (peak -8.9 dB, faster
fade = known short-delay frac loss, engine not patch). Faithful
caveat: C2 peaks ~15 dB under C4 (Body HP at 158 Hz + final 1P tilt
— that's what the topology does; his video bass is fundamental-light
too). V5 (same day, applying the new exhaust-the-source rule): the reverb
pass was read too (RECIPE pass 4) — **LISTEN afp31_v5.wav, supersedes
v4**. The "Inner reverberation" layer is now implemented: second white
noise x follower (attack 16.9 ms / release 462.2 ms, read off the
ORIGINAL panel) x strength 0.15, summed with the hit noise before the
excitation SVF — his fake-reverb/body layer, was recipe section 6
backlog. Measured: C4 tail now carries a -25 dB bed at 2 s instead of
bare string. Remaining guesses, now explicitly enumerated: detune
cents (knob never shown settled; ours 1.0), Reverb strength exact
value (knob ~0.17; ours 0.15), per-string damping-cutoff differences
if any. Everything else is frame-read.
V6+V7 (2026-08-17, Matt's v5 verdict: "hammer noise too loud from
~523 Hz, linearly more noticeable on up" — v4/v5 both good, iterating
on v5): **LISTEN afp31_v7.wav** (v6 is superseded and honest-failed:
its -3 dB/oct excitation gain taper measured as a PURE volume fade of
the top register — excitation feeds tone and noise through a linear
chain, so gain cannot move the hammer-to-tone ratio; my lever-1 offer
was arithmetically wrong). v7 = v5 + the temporal lever: new Envelope
config **timeScale** (keyboard tracking of envelope times, per-note
mappable — classic synth primitive, multi-use) shortens the hit + bed
envelopes above 523 Hz (x1 -> x0.4 at 4.2 kHz, loglog), so the 361 ms
chuff no longer outlives the shortening treble ring. C2/C4 renders
identical to v5; C6 excitation window ~74%; score gains a 2093 Hz
note (t=32 s) to judge the tracking where it bites. If the top still
reads noisy after this, the next suspects are the fixed 1.6 kHz
excitation band itself (his Brightness knob — fidelity vs taste fork)
and the reverb-bed strength.
V8 (2026-08-18, Matt's v7 verdict: highs still too noisy + "no
brightness difference with velocity, volume drops a bit too much"):
**LISTEN afp31_v8.wav.** The velocity half was a CONFIRMED translation
bug, not taste: his crossfade interpolates SEMITONES 12..64 linearly
(multiplier = 2^((12+52v)/12), exponential); my 2-point vcurve
interpolated the multiplier linearly, so vel 0.3 landed at 3.7 kHz —
inaudible against the 1.6 kHz excitation band — where his math gives
1.37 kHz. Measured: v7 hard/soft C4 spectra identical (his ear was
right); v8 soft C4 clearly differs — the res-8 peak sits at 1.37 kHz
(mid-focused "ivory whisper" color) vs 7.1 kHz on hard. Volume-vs-
velocity is faithful (he multiplies excitation by velocity linearly,
so do we) — re-judge loudness now that timbre works; if still too
steep, an instrument velocity-gain curve is a small engine add.
Highs: the Body HP clamped at ~336 Hz, so C6/C7 kept their whole
0-1.6 kHz chuff band UNDER the tone; v8 tracks ~0.7*f0 above 480 Hz
(taste departure from his clamp). Measured C7 first-300 ms
chuff-to-tone: -9.2 -> -26.8 dB.
V9 (2026-08-18, Matt's v8 verdict "very nice" + 2 items):
**LISTEN afp31_v9.wav.** (1) Ultra-high sustain (optional ask): the
string-loop LP (brightness 0.926 ~= 20 kHz) is a real per-pass loss at
thousands of passes/sec; v9 opens it to 0.992 above a 1.6 kHz knee
(flat below — lower notes byte-unchanged). 2093 Hz note: -85 dB @0.6 s
vs v8's -94, alive at 1 s where v8 was silent; score gains a 2637 Hz
note (t=40 s). Fully-open frac-delay loss still bounds the top —
further stretch = engine work (allpass frac read), flagged not done.
(2) Detune semantics changed engine-wide to AF per-side cents (600 =
tritones both ways, as Matt observed the old total-spread gave minor
thirds): KSPianoString outer combs now +/-detune, descriptor default
halved, ALL stored patches compensated (node scalars AND paramMap
curves halved — Piano_bright renders bit-exact, hash f5174254 verified
before/after; halving is exact in binary so every patch is
sound-preserving, not approximate).
V10 (2026-08-18, Matt's v9 verdict: detune great -> clamp raised to
7200 ("just for fun", 6 octaves per side); bass octaves much too
quiet, octave 1 barely audible): **LISTEN afp31_v10.wav.** Three bass
levers, all taste departures from his clamp topology: (1) Body HP
below the knee sat at a CONSTANT 2.42*f0 — every bass note lost
~20 dB of fundamental; now 0.95*f0 (rumble still cut). (2) Excitation
gain shelf x6 -> x30 at 20 Hz. (3) Excitation 1P floored at 100 Hz
(faithful 2f0 = 65 Hz at C1 left almost no noise band to strike
with). Score gains C1 (t=48 s). Measured register peaks now a bell
around C4: C1 -16.6 / C2 -11.4 / C4 -5.5 / C6 -11.8 / C8 -18.6 dB
(was C2 -20.8, C1 unmeasured-and-inaudible). Peaks understate bass
loudness (longer sustain integrates louder); judge by ear.
V11 (2026-08-18, Matt asked whether the hammer noise is really the
same frequency at every pitch — in AF yes, fixed 1.6 kHz; in a real
piano no): **A/B afp31_v11.wav vs v10.** Experiment: excitation SVF
cutoff now tracks pitch, sqrt-ish law through the AF value (800 Hz at
the bottom, 1614 at C4, 6 kHz at 4186+). Attack centroids scale
76 Hz (C1) -> 2212 Hz (C8), were pinned ~1.6 k. C8 peak +4 dB (knock
now near its tone). Loser reverts with one curve. Matt also hit a
Mappings-dialog gap: exc_svf.cutoffFreq missing from the dropdown —
likely a wire left connected into the pin (dialog hides ref-wired
targets, loader restriction); if his pin is bare, it's a UI bug to
chase. [resolved: Matt's bad — a wire was connected]
V12 (2026-08-18, Matt: exc_svf cutoff didn't audibly change the
hammer but exc_1p did): **LISTEN afp31_v12.wav, supersedes v11.**
He's right and v11 tracked the wrong filter: the 1P at 277 Hz was the
LOWER corner — the actual band edge — while the SVF's 1614 Hz corner
sits ~15 dB down the 1P slope where movement is nearly inaudible.
v12 puts the real-piano tracking on exc_1p: 2*f0, floored 100 Hz
(bass thump), capped 4 kHz (treble tick); v11's SVF tracking kept so
the corners scale together. Widening the band pumped +12 dB into the
treble -> exc gain taper above 277 (6 -> 1.5 at 2 kHz, LEVEL only)
+ volume 4.0 -> 3.0 (C4 was pinned at the 0.7 limiter). Attack
centroids 76 Hz (C1) -> 2287 Hz (C8). Register peaks: 0.11 / 0.20 /
0.66 / 0.34 / 0.35 (C4 a touch hot vs neighbors — ears to judge).
UI (same session): Curves-window Add-curve node combo now pre-selects
the editor-selected node (was Mappings-dialog only); properties
header shows just the type, no parentheses (name lives in the rename
field). PENDING RELINK: mforce_ui.exe was running — close it and
rebuild to get both tweaks + detune 7200 range.

### 37. afks_v1 — the "beautiful" AF KS patch, rebuilt from its file [listen] (2026-08-18)
renders/dsp/pending/afks/afks_v1.wav; patch patches/pending/afks/
afks_v1.json; generator tools/gen_afks.py; decoded source
docs/research/af_ks/parse_full.txt (patch "20211206_9403", decoded
with the generalized tools/parse_af_patch.py). Character: NOT a piano
— the exciter noise is GATED (runs while held, ~75 ms velocity-scaled
release) and the loop's effective feedback is 0.998^2 = 0.996, so the
string reaches a bowed/ebow-like EQUILIBRIUM (measured: held C4 sits
at -6 dB until note-off). Chain: gated noise -> two pitch-tracked
SVFs res 2 (first also velocity-scaled: his cutoff = pitch x vel in
MIDI units) minus a 1P of the raw noise -> HP 466 -> single-loop
string (t60 = 1723/f, releaseFb 0.911, fast 20 ms damp landing) ->
5-bandpass NOTCH body (196/1008/1605/2960/11470 Hz, res 0.4,
subtracted at 0.675). Skipped v1 (in file, not built): per-voice
Strum stagger (PolyID mod 6 x 3.3 ms — engine has no voice-id
concept), random pitch drift (LFO->follower, 0.078 semis), Abs() on
the noise, Tubular invert (knob at 0 anyway), pickup position (knob
at 1.0 = degenerate). Also fixed in passing: CombinedSource "add"
matched no enum label and fell back to Mix (averaging) with only a
stderr warning — gt generator now says "sum" with volume halved
(byte-identical output).

### 36. afp31_gt — GROUND TRUTH from the recovered patch file [listen] (2026-08-18)
Matt recovered the original AFNoding-031 patch via Wayback Machine;
the base64 AF clipboard format is fully decoded (zero unparsed bytes)
in docs/research/afpiano/parse_full.txt — every knob and wire. The
file corrected several frame-era errors, biggest: the FINAL FILTER IS
A HIGHPASS (1P at 2*f0, thinning lows — ours was a LP, darkening),
gain x2 not x6, exc res 8.46 not 1.2, hit release 144 ms not 361,
reverb release 1444 ms + band-passed 298-1917 Hz, string damping
FIXED per string (20.1k/15.0k/11.4k, no gate mod), held fb
0.999/0.995/0.999 (middle string = built-in double decay), detune
~0.001 cents (effectively zero). **LISTEN afp31_gt.wav** — faithful,
taste layers (timeScale, bass rebalance, treble body tracking, hammer
pitch tracking) deliberately excluded so we finally hear HIS patch.
A/B against afp31_v12: verdict decides which taste layers get
re-applied on top of ground truth. Register peaks: C1 .078 / C2 .109 /
C4 .320 / C6 .117 / C8 .067. Engine gaps (approximated): per-comb
damping cutoffs, per-comb feedback, exp follower tails.

### 34. AFP excitation ladder — moves A+E [listen] (2026-08-15)
renders/dsp/pending/ks_piano_v6/pb_exc_*.wav + README_pb_exc.md.
Four arms on Piano_bright: ctrl / A (350 ms burst decay, no bed) /
AE (full measured envelope: 20 ms attack, 350 ms decay -> 0.18 bed,
450 ms release) / AE_lo (bed 0.09). Measured: the bed brings C6 sustain
from dead (-82 dB @1.5 s) to alive (-24/-30); attack peaks rebalance
(C4 ~2x — judge timbre first, levels re-anchor later). HELD keys now
carry a bow-like noise bed on the AE arms — hand-play them, not just
the WAVs. Anti-result: lengthening the release alone is a no-op from a
zero bed. REV 2 same day: Matt's Listen-here verdict on rev 1 ("chuff of white
noise, feeble hit") measured TRUE — step_3 hits hit 50% in ~4-30 ms,
rev 1's linear 350 ms decay was at ~92% there. Arms rebuilt as the
measured CLACK (fast drop 50%@~17 ms, 10%@~100 ms, bed -32 dB,
verified at the Combined7 tap; attack spectrum was already on target —
the gap was temporal). Same filenames. REV 3 (same day, Matt: "much better but weak attack +
chuffy tail"): all three claims MEASURED — rise time already matches
(7.7 ms both); the weak attack is a CREST deficit (his transient 4.6x
over first-30 ms energy vs our 2.7x) plus COMPONENT SEPARATION (his
bright rap and low clunk peak ~30 ms apart; ours stacked at -7 ms);
tail centroid 664 vs his 501 Hz. Two new arms shipped
(pb_exc_B1_sharp: 2 ms attack + steeper drop; pb_exc_B2_sep: + knock
bloom delayed to 25 ms) — HONEST RESULT: crest only 2.7 -> 3.0 and the
knock delay did NOT move separation (the low peak is the HammerBank's
fundamental ring, not the knock). Envelope lever is exhausted at ~3.0:
the resonant bank + body LP smear whatever the envelope sharpens, and
his 1-4 kHz rap has no un-smeared path in our chain (our click band
sits at 4-9.5k). The crest/separation/tail gaps are all move-B
territory: restructure excitation filters toward his measured chain
(resonant 4P body with the ~1.5-2k knee, a FAST 1-4k rap path, mix
rebalance). REV 4 (the survivor): pb_exc_trace.wav — env1 TRACED point-for-point
from the step_3 median hit envelope; verified shape-vs-shape at the
string-input tap (head, shoulder, 100 ms level all inside noise
wobble). Three intermediate theories (spike, resonant ping, impulse
click) retired: the crest metrics that motivated them were window-
alignment artifacts. Bed reduced to 0.02 in this arm. Remaining known
gap: tail COLOR (centroid 664 vs his 501 Hz) = move B knee territory,
untouched. VERDICT (Matt, end of day): rev 4 "just sounds terrible
compared to his" — envelope-shape matching is EXHAUSTED as an approach
(four revs, shape verified matching, still wrong). Front PARKED for a
change of approach; candidate directions for tomorrow logged below.
Sleep-on-it directions (not yet chosen):
(1) SUBSTITUTION BISECT — sample one clean hit from his step_3 WAV and
    play it AS our excitation (one-shot wavetable; wav_reader.h already
    in tree). If our string then sounds right, the gap really is our
    excitation synthesis and we distill against a known-good; if it
    still sounds wrong, we have been polishing the wrong component and
    the string/loop is the suspect. Decisive either way.
(2) GET THE REAL PATCH — Alpha Forever is downloadable freeware and he
    said "probably this will be a preset in Forever": if the piano
    preset ships, every node value becomes READABLE at source quality,
    no video archaeology. Check first tomorrow; obsoletes half the
    reverse-engineering if it lands.
(3) LITERAL CHAIN CLONE — reproduce his exact 4-path graph (1P + three
    SVF + resonant 4P, mixer 1.00/0.76/0.33/0.47) instead of adapting
    our chain toward it; may need a true SVF node (engine gap).

### 33. AFP excitation stage analysis [read] (2026-08-15)
docs/research/afpiano_2021/ANALYSIS.md — new dated section from your 4
stage WAVs + corrected transcript. Headlines: step_3's "one more filter"
is a 4-pole resonant lowpass (measured -18..-20 dB/oct, knee ~1.5-2 kHz,
kills >4 kHz) = the frames' "Filter 4P Modulated with Emphasis"; the
3-SVF stage is MILD sculpting (no resonances, Q~0 confirmed); his noise
burst rings to ~350 ms where ours dies in 20-40 ms (candidate mechanism
for the harpsichordy-attack family); "noises so identical" REFUTED as
frozen noise (hit correlation ~0.09 — perceptual consistency, not
seeding). Post-string gaps: pitch-tracked 1st-order ZDF allpass in-loop
+ a post-sum 1P tone filter, both absent, both cheap.
step_0 CORRECTED per Matt's ear: thump + ~4 s HELD-KEY noise bed at
~18% of burst level, core slope -1..-1.7 dB/oct ("pinker than white"
confirmed; "white noise" is his source label, not the tap spectrum).
**Verdict decides which of the proposed moves proceed: (A) burst-length
ladder, (B) exc_lp knee retune + emphasis bump, (C) post-string 1P +
in-loop 1P allpass rung, (D) frozen noise = skip, (E) held-key noise
bed via excitation sustainLevel (~-15 dB rel burst) + slight noise
tilt.**

## Resolved

Pared 2026-08-15 at Matt's request — compact stubs only; full detail
lives in the run reports (docs/autonomy/dsp/reports/) and git history.

- **32. Groups + Listen tap** (2026-08-15): "work great, and this is
  huge" — piano patch de-black-boxed. Follow-ups landed same day:
  ctrl-click deselect (pin-hover aware), hover-vs-selected cues with the
  blue selection kept, drill-out position loss fixed. Piano_bright
  accidental overwrite restored from git, hash-verified. Shared-source
  group refusal + duplicate-changes-sound trap → backlog 3q.
- **31. 3n closed** (2026-08-15): all 33 UI-save fidelity failures fixed
  (5 root causes, incl. engine adsr-shape sustain rewrite); gate 196
  patches, exception list empty but FormantSequence1 (backlog 3p).
  UI re-saves of library patches are SAFE now. The cello/viola
  untracked-repair decision closed by the 2026-08-15 reconciliation
  commit (5c5c139) — both entered library/strings WITH repairs.
- **30. Mappings dialog / Parameter retirement** (2026-08-14): "All
  good." QWERTY question answered: node graphs play via a synthesized
  Parameter frequency node (NodeGraph mode keeps the type).
- **29. Stable node identity** (2026-08-14): "Works"; his exc_body
  rename committed (3a316ac).
- **28. Note-contained sound + Piano_bright baseline** (2026-08-13/14):
  three audition-fix rounds (damper stages, UI preset parity, top-octave
  anchors), evening-run engine fixes (dispersion shedding, allpass
  fractional read), no-auto-promote rule established, then Matt's
  hand-tune became **Piano_bright = the KS piano library baseline**.
  Bass containment warns below ~E4 are expected/benign for this family.
- **27. KS v6 ladder + CMA runs 1-3** (2026-08-10..13): full history in
  reports; ended superseded by Piano_bright. Standing notes that
  survive: broadband inter-harmonic gap is a MECHANISM gap (same as
  viola); CMA endgame must retarget the UI-renamed node ids and the
  re-baselined (verb-free) objective; eval notes must cover searched
  curve regions. Analysis: docs/research/afpiano_2021/ANALYSIS.md.
- **26. Housekeeping 2026-08-10**: all sub-questions answered by Matt;
  the last thread (c2c_quiet / fable1_v6 "where did they go") resolved
  2026-08-15: his bulk move committed as renames into library/strings,
  c2c_quiet + v6_05_floor08_deep parked in patches/old/ (5c5c139).
- **25. Soprano alt-formant candidates**: renders+patches deleted; Matt
  recalls no winners but wants a re-do → backlog 3r (re-render with
  variation arms).
- **24. Node-graph stream fix**: "Good, done."
- **22b. KS v5**: superseded by v6.
- **11. Power renorm**: "Leave it." Documented degenerate case stands.

Older resolutions (2026-07-27 .. 2026-08-10) are preserved in git
history of this file and the run reports; verbatim verdict text with
reference value (expand round-2 ranking, vowel pass-2 notes) lives in
the 2026-08-10 revision of this file.

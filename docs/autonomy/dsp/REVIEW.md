# DSP lane — review queue

## Awaiting Matt

### 51. Listen-here fixed — taps sound, monitor at unity [try] (2026-08-24)
Backlog 32, both halves, in today's mforce_cli + mforce_ui builds.
(a) **Engine**: a shared source's advancing consumer could fall outside the
tap's render cone — RefSource wrapping gives the advance to the FIRST-WIRED
consumer, and when the tap re-roots the graph that consumer may not render,
so the shared envelope froze at 0.0 and muted everything it gated. That was
your all-zero wt tap on creak__wtsaw15_bedlow (bed, wired first, owned
bow_env). The loader now proves starvation from the patch JSON (advancer
node unreachable from the output) and promotes the tapped-side RefSource to
advancing. Null gate 180/180 — full-graph renders are byte-identical.
(b) **UI**: instrument volume is leveled for the patch output, so raw
excitation taps sat 20-30 dB down (your "silent" att/bed/sums — they DID
render). Per your call: Listen taps now monitor at unity volume, ignoring
instrument.volume. TRY: Listen on att / bed / wt / sums in the excite4
patches — beds should loop audibly, one-shots fire per note-on.

### (parked) THE BOW gets a family — 30 cells (2026-08-24)
PARKED 2026-08-24 evening — superseded by the KSString-modes decision
(item 52, now in Resolved): BowedStringEvolution is kept for old time's
sake / comparison / experimentation, not production. The 30 cells +
conditioning/brt ladders stay in bow_family for reference; **no verdict
owed**. Original entry follows for the record.
**renders/dsp/pending/bow_family/** — your parameter notes turned into axis
ladders off the exact discovery bytes (baseline included), notes 36/60/84,
3.5 s bows: RedNoise frequency 100→1400 + **rnfreq_track** (bow events
follow the note instead of sitting at 350 Hz — the stochastic-bow reading
says they should), density 0.15→1.0 (your "hesitant" axis), smoothness /
continuity / rampVariation, the TBD pair boost + zeroCrossTendency, bow
position 0.05→0.40, friction 2/8, tubeLoss ladder, and three combo cells
from your verbatim characterizations (hesitant / bright / darklow).
FOUND EN ROUTE: bowSpeed x frictionGain is ONE axis — the Friedlander
recursion has an exact scale symmetry (verified corr=1.0 on rendered
pairs), so shape depends only on the product ("drive"; baseline 1.2,
fric2=0.6, fric8=2.4) and the leftover is pure gain. The bowspeed cells
were exact duplicates and were dropped.

**YOUR SIZZLE REPORT (same day, headphones): measured and split in two.**
All 30 r1 cells carry it because both mechanisms are engine-side:
(1) DOMINANT — the KS-bend fractional read head (Approach A) is wrong for
a LIVE evolution: vibrato makes the reader drift past the writer, and the
output continuously sweeps the seam between this pass and last pass of
the string state. Vibrato depth 0 collapses the inter-harmonic floor
~25-30 dB at note 36. Hear it: **vib000** (no vibrato — but also no
vibrato, which is the open problem, backlog 33).
(2) SECONDARY — bow noise recirculating undamped: the loop had only flat
tubeLoss, no frequency-dependent loss. NEW `brightness` setting on
BowedStringEvolution (one-pole per line write, default 1.0 = original
node byte-identical — discovery bytes locked): **brt095/090/080/065/050**
ladder, plus **vib000_brt080** (both fixes together — the cleanest cell).
**THE FLUTTER (Matt's rename, and he's right — it's a third artifact):
~5-7 Hz amplitude modulation, and it IS the RedNoise.** Bow force =
bowSpeed x pressure, so the noise's slow content modulates amplitude
directly (constant pressure: mod-energy 487 -> 4 at n84). Highpassing the
pressure only partly helps — the friction nonlinearity demodulates the
noise, so its envelope variance becomes new LF wobble past any cutoff.
Depth is the knob: **conditioning ladder** pressure_const040 (character-
free anchor), cond_hp005/020/060/200 (const 0.4 + HP'd noise), and
**cond_hp200_half** (noise at half depth — flutter 487 -> 11, the best
character-keeping cell on paper). All patch-level: two nodes (BWHighpass +
CombinedSource) on the bow pin, no engine change — adoptable into the
discovery patch directly if it survives your ears.
VERDICT: which axes matter, does freq-tracking beat fixed 350, do the
combos read as characters, names for keepers; on the sizzle pair — does
vib000_brt080 kill the HF plateau; and on the conditioning ladder — where
does character survive minus flutter (cond_hp200_half is the candidate).

### 49. Smoothing the bed — duck / smooth / Helmholtz, 9 cells [listen] (2026-08-23)
**renders/dsp/pending/excite4_smooth/** — your "just use a sawtooth ;-)"
taken at face value, plus the two cheaper dials, all on the triptych
harness: **sus-ladder** (bed ducks to 0.5/0.3 after the attack),
**smooth bed** (4 ms merging slips, sine interp; isolated and ducked), and
the **Helmholtz layer** — a drawn ramp+jag fed to WavetableSource, whose
fill takes exactly one period at the note frequency → a pitch-locked
single-period saw with drawn roughness, bowed by the envelope
(creak__wtsaw_jag15 / jag40 / wtsaw15+low buzz bed). The wtsaw cells took
~6x less drive — the pitch-lock resonates the combs, structurally the bow
behavior. VERDICT: which mechanism kills the noise; does wtsaw finally bow;
jag 15 vs 40.

### 48. Triptych — bow without zither, 11 cells [listen] (2026-08-23)
**renders/dsp/pending/excite3_triptych/** — your attack/sustain(/release)
architecture: one-shot attack (creak / chaos scrape / jagged-in) crossfading
into a STATIONARY looping bed (buzz / fine / velvet — uniform statistics,
seam at zero, ~1.2 s passes wobbled by varPct, i.e. the anti-zither fix),
3x3 plus two cells with a release seg swelling as the bow settles
(creak__buzz__rel, jaggedin__fine__rel). NOTE: a silent-drop bug
(CombinedSource pin is source1, not source) meant the excite2 creak_into_*
cells you already heard had NO creak — both regenerated; re-listen counts.
VERDICT: does the triptych bow; which attack x bed pairs live; is the
release seg worth keeping.

### 47. Sustained excitation probe — the bow experiment, 9 cells [listen] (2026-08-23)
**renders/dsp/pending/excite2_sustain/** — textures LOOPING into the string
while the note lasts (SegmentSource oneShot=false, varPct 0.15 re-randomizes
each pass so the loop has no pitch of its own), bow-pressure Envelope on
seg.amplitude, same string harness as excite1. Seven beds (scrape buzz/fine,
cluster tailoff, shrinkgrow, jagged out, dots slow-wander, crunch) + two
composed string-attack cells: **creak_into_buzz_bed / creak_into_cluster_bed**
(one-shot fast reverse bounce + gated bed). Probe set built ahead of your
full r1 verdicts — those verdicts re-aim it. VERDICT: does anything bow?

### 46. Excitation round 1 — 13 candidates into a pitched string [listen] (2026-08-23)
**renders/dsp/pending/excite1/** — the excitation-candidates list from the
sweep verdicts, each played as SegmentSource → KSPianoString (afp31_gt's
damper + t60 keytrack) at notes 36/60/84. Includes the creak question
(reverse_full_faster/_fastest), the scrape coupling test (scrape_buzz), both
jagged legs, the cluster→thumps, dots cells, and combo_ref as the plain-
strike reference. Auto-leveled under the 0.7 limiter. VERDICT: whose
character survives/transforms interestingly through the string; which earn
the sustained-excitation round (gated, looping with varPct — the bow
experiment). Also round 3 combo/combopair (segment_sweep3/) still awaits
its raw verdict.

### 41. UI piano — dynamic octaves + top C [try] (2026-08-21)
`draw_keyboard_panel` (built into today's mforce_ui). Two changes off MISC.md:
(a) the keyboard always ends on the C above the top octave now (extra playable
white key on the right); (b) on resize it adds/removes a whole octave at a
width threshold instead of scaling key width without bound — default 4 octaves,
clamped 1..10. **Band retuned 2026-08-22 per Matt, twice: 20/40 → 30/60
("too wide") → 25/45.** No oscillation risk — the octave count is a pure
function of width each frame. TRY: resize the Keyboard panel wide and
narrow, confirm the thresholds feel right and the trailing top C sounds.
Constants MIN_KEY_W 25 / MAX_KEY_W 45 / default octaves 4 at the top of the
render block if it wants another pass.

### 40. Triangle `power` shape control [listen] (2026-08-21, shark-fin fixed)
New `power` ValueSource on TriangleSource (MISC.md). Signed, neutral at 1:
`|power|<=1` linear (= today), `power>1` CONCAVE sides (pinched/spiky),
`power<-1` CONVEX sides (domed/rounded). **Both sides now bend the same way**
(symmetric) — your shark-fin complaint on the first cut is fixed; the old
per-leg shark-fin math is preserved behind an **Asymmetric** checkbox on the
node (default off). Default power 1.0 byte-identical (all 5 Triangle baselines
in the 196/196 gate). Scratch: `renders/scratch/misc_0821/tri_pow{1,3,neg3}.wav`
(first cut = now the asymmetric shape) + `tri_pow3_sym.wav` /
`tri_pow3_asym.wav`. DECIDE: does the symmetric concave/convex feel match
intent, and is the signed convention (1 = linear, cross 1 up for concave /
cross -1 down for convex) the one you want, vs. a plain fractional exponent
(power 2 concave / 0.5 convex)? The band [-1,1] is a linear dead-zone by
design — say if you'd rather it bend continuously from 0.

### 39. Wheel + pressure, live [try] (2026-08-20)
The one piece of the 2026-08-20 session no human has exercised: P3 wired
CC1 mod wheel and channel pressure from the MIDI keyboard into
InstrumentState, smoothed per voice (~10 ms). Nothing hears them until a
patch does: add a Note node, wire its `wheel` (or `pressure`) pin through
a Curve into something audible — Overall_lpf.cutoffFreq on the WIP piano
is a natural first target — then play and ride the wheel. Everything else
from today (gold pins, Note faces, control strips, group editing,
drill camera, minimap) you verdicted live at the canvas; no listen items,
P3's null gate was 120/120 bit-identical.

### 35. afp31_v1 — the from-scratch AFNoding-031 rebuild [listen] (2026-08-16)
renders/dsp/pending/afp31/afp31_v1.wav (C2/C4/C6 hard + C4 soft);
patch patches/audition/afp31/afp31_v1.json; generator tools/gen_afp31.py;
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

### 38. AF saxophone — source exhausted, build awaiting go [discuss] (2026-08-18)
docs/research/af_sax/RECIPE.md: complete value table from the breakdown
video (2560x1440 — every knob read; frames archived). Architecture:
self-oscillating SVF as the tone source (gate rescales resonance to ~0),
ADSR 10/731/0.19/6.2 as "air pressure", multiplicative breath
y = x(1 + 0.1*noise*env), velocity-scaled legato glide (600->15 ms,
first note instant), 5.8 Hz TRI vibrato FM'd by 1.3 Hz RND with 6 s
fade-in, THE character = feedback delay loop at 2x period with
AUDIO-RATE delay modulation (x(1 + 0.36*signal)), in-loop tanh + 1P
damping (pitch+35 semis), loop gain crossfaded 0.5->1.16 by the
expression "Power" follower whose ATTACK = f(velocity) 600->16 ms (the
scoop). Body = stock reverb, size 0.06 decay 0.39 lp 8408 wet 1.0.
Only 3 TUNEs remain (vibrato depth, SVF tap, tanh shape). BLOCKED on
design go-ahead: needs a new ModDelayLoop node (audio-rate delay mod +
in-loop tanh + gain>1) and a body strategy (mini reverb node vs skip).
Reference audio af_sax_demo.wav (the played demo Matt prefers).
BEST SOURCE: the actual patch file is on the AF Discord — grab when
joining; parse_af_patch.py will decode it and settle the TUNEs.

### 37. afks_v1 — the "beautiful" AF KS patch, rebuilt from its file [listen] (2026-08-18)
renders/dsp/pending/afks/afks_v1.wav; patch patches/audition/afks/
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

## Resolved

Pared 2026-08-15 at Matt's request (and again 2026-08-22) — compact stubs
only; full detail lives in the run reports (docs/autonomy/dsp/reports/)
and git history.

- **52. KSString bow mode** (2026-08-24, verdicted same day): "character +
  promise, problems on the periphery"; all attacks too slow for a string.
  DECISION: KSString modes is the model; BowedStringEvolution parked.
  Full verdict text in the 08-24 report; direction carried by backlog 34.

- **42. Curve editing + Envelope min/max** (2026-08-22, resolved
  2026-08-23): Matt tested minValue/maxValue explicitly — works as
  advertised; the knot-editor fix and Properties-pane curve editor have
  been in daily use through the sweep sessions without complaint.
- **45 + snare_corner2. Snare corners** (2026-08-23, resolved same day):
  both "terrible"/"nothing good" — snares PARKED with Matt's diagnosis
  (crack needs internal structure; density collapse alone doesn't redden).
  Full detail in oneshot_sweep/ROUND1_VERDICTS.md; revisit at instrument-
  roster time.
- **44. Segment sweep round 2** (2026-08-23, resolved same day, area by
  area): jagged paid off; reverse bounces re-based and parked for
  excitation; cluster->thump beats scrape->thump; dots2 kept+extended;
  scrape/kick/twohit2/texture closed. Full log:
  docs/research/oneshot_sweep/ROUND1_VERDICTS.md. seq() pause bug found by
  Matt's ears and fixed mid-review.
- **43. Segment sweep round 1** (2026-08-23, resolved same day): Matt
  auditioned ALL cells, area by area — "many are promising." Full verdicts +
  round-2 directions in docs/research/oneshot_sweep/ROUND1_VERDICTS.md
  (standout: scrape_then_thump; kick corner in wide atoms; scheduled
  clusters; dots = time-domain synth-kick recipe; rulebreak demoted;
  fixed_fine_zct dropped as "earsplitting"). Round 2 designed from the log.

- **34. AFP excitation ladder** (2026-08-15, resolved 2026-08-22 housekeeping):
  four revs of envelope-shape matching, shape verified matching at the
  string-input tap, then Matt's final verdict "just sounds terrible compared
  to his" — approach EXHAUSTED, front parked. Of the three logged
  change-of-approach directions, (2) get-the-real-patch HAPPENED (Wayback →
  item 36 ground truth); (1) substitution bisect and (3) literal chain clone
  remain available if the gt A/B still leaves a gap. Full rev history in this
  file's git history.
- **33. AFP excitation stage analysis** (2026-08-15, resolved 2026-08-22):
  the analysis stands at docs/research/afpiano_2021/ANALYSIS.md; its
  proposed moves A-E were mooted by the decoded ground-truth patch (item
  36) — moves now derive from real values, not frame inference.
- **32. Groups + Listen tap** (2026-08-15): "work great, and this is
  huge" — piano patch de-black-boxed. Follow-ups landed same day:
  ctrl-click deselect (pin-hover aware), hover-vs-selected cues with the
  blue selection kept, drill-out position loss fixed. Piano_bright
  accidental overwrite restored from git, hash-verified. Shared-source
  group refusal + duplicate-changes-sound trap → backlog 27 (absorbs 3q).
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

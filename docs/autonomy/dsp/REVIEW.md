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

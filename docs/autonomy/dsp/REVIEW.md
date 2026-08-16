# DSP lane — review queue

## Awaiting Matt

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
untouched. LISTEN: pb_exc_trace vs pb_exc_ctrl, plus Listen-here at
the string input.

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

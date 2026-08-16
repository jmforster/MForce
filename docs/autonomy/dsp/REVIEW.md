# DSP lane — review queue

## Awaiting Matt

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

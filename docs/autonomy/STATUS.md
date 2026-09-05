# Status — open this file first

Updated: 2026-09-02 (r4 verdict session, Fable 5 interactive with Matt).
STATUS and both BACKLOGs compacted: done items are now one-line ledgers in
the lane backlogs; run-by-run history lives in the lane `reports/` and in
git history of these files. Stale gates fixed in the same pass (dsp 15's
dead CombineTest ask, 12's folded verdict, 16→3r supersession, 27+3q merge).
**GOALS.md awaits Matt's refresh** — the July goals are largely satisfied,
and the current center of gravity (PerformSource/pin model, AF archaeology,
KS piano family, live MIDI) grew out of interactive sessions, not GOALS.

| Lane | Dev | State | Next up | Awaiting Matt |
|---|---|---|---|---|
| dsp | Dipsy | **09-02: r4 VERDICTED — "nailed it, concept proven."** First feedback-loop patch in library: **flute002 → library/winds/flute_default.json** ("killer, makes Clarinet1 sound fake"-class integrated attacks); reed001a library-grade, awaiting a name (oboe?). Winds > brass > strings (no bow attack yet). Full record: docs/research/feedback_sweeps/R4_VERDICTS.md — incl. code-verified physics answers (voice state IS cleared per note; noise RNG free-runs → real per-hit non-determinism). Cross-cutting gap: noise amp + drive ramp not keytracked (cutoff is) → patches only characteristic in home register **r5a RUN same day**: keytrack round — 6 keepers × 3×3 (noise-amp exp × ramp-time exp), 54/54 rendered to renders/dsp/pending/feedback_curves5a/ (README inside; n00_r00 = control). New render format per Matt: 5 notes C3..C7, 2 s each. Axes verified: pre-bloom hiss 56x C3→C7 flattened by n80; attack 0.38s→0.04s across board at r70. Mechanisms: hiss.amplitude ← power-form CurveNode; driveenv timeScale ← dynamicPins per-note setting **5a verdicted 09-03** (noise kt keeper at -0.80..-0.95, ramp curve dropped) → **r5b RUN 09-03**: reed001a deep-dive 6×4×6 (cutoff mult × drive shape incl. overshoot/tongue × junction jitter ±15%), criticals re-measured per cell, 144/144 rendered **09-05: CURVE-MORPH SHIPPED same day as spec** (docs/superpowers/specs/2026-09-05-curve-morph-design.md + plan): shared Curve evaluator (CurveNode+Shaper delegate, byte-identical), per-segment interp overrides (Envelope vocab + Hold; Stage holdPct REMOVED — scanned unused), Shaper editor gestures (right-drag segment = Expo+power, power dots, presets bow/reed1/reed2/lip/jet/hard/sine/saw/triangle), point-space morph pin + A/B editor workflow. Demo: renders/dsp/audition/curve_morph/oboe_morph_demo.wav (origin-steepened curve B morphed in over each note). Gates parallelized (null gate + roundtrip, --jobs; ~3x) — next gate lever: mforce_cli batch mode (render is 378x realtime; 95% of gate cost is process spawn+load). Pre-existing find: UI roundtrip drops wiring_smoke's __perf_freq (Matt spun off fix task). **09-04 session: junction mechanism ground-truthed → docs/research/feedback_sweeps/JUNCTION_OPERATING_POINT.md** — r5c should use directed axes (origin-asymmetry ratio + drive floor relative to measured oscillation threshold), not breakpoint jitter; ear-validated on patches/pending/oboes/oboe_dv_junc_tweaked.json (Matt's sandbox, awaiting his name — nothing supersedes oboe_default) | Matt's r5b axis-tour verdict, then pull neighbors from the full grid; apply winning shape/mult to other keepers. Backlog 53 (live mono mode) parked. Remaining 37a: in-loop breath (d). 34a; excite 46-48 | **feedback_curves5b axis tour 14 cells = the live queue** (full 144 in sweep/); amp_release/tuned/keytrack demos. **09-05 NEW: renders/dsp/audition/feedback_inloop1/ — 10 cells + control, oboe_default formants moved IN-loop (placement × res, normalized, loop-referenced criticals; README inside, tool: tools/gen_feedback_inloop1.py).** 09-03: **oboe_default promoted** (Matt's build-out of the r5b reed line: formant bandpass bell, Vibrato, Reverb — second feedback-loop patch in library/winds) |
| comp | Wolfie | **09-05: run 27 (interactive) — #22 done.** The three purge-casualty A/Bs (REVIEW 11/13/17) re-rendered at HEAD, same seeds, faithful to the originals → renders/comp/audition/{markov_phrases5,markov_phrases6,passage_end_grid}. Additive1.json rescued from patches/old/ → baselines/; 7 comp harnesses repointed | #21 zero-event check → #8 re-observe | **10 items, ALL verdictable** — the whole comp/REVIEW.md queue awaits ears; nothing blocked |

## dsp

Landed 2026-08-21 (Opus 4.8, MISC.md list): WhiteNoise density/boost/
continuity/zeroCrossTendency restored; Triangle `power` (symmetric, old
shark-fin behind `asymmetric` flag); UI piano dynamic octaves + trailing
top C. All byte-identical at defaults, null gate 196/196.
Before that: **PerformSource P1–P3 all landed** 08-18..20 (paramMap story
is now graph nodes engine+editor; gold pins, Note/Curve nodes, instrument
cache, RtMidi, wheel/pressure smoothers), plus Matt's hands-on day 08-20
and the AF archaeology thread (afp31 v1–v12 + decoded ground truth, afks,
sax recipe).

Review queue (dsp/REVIEW.md): **51 Listen-here fixed [try]**, **50 bow
family 30 cells [listen]**, 49 excite4 duck/smooth/Helmholtz [listen —
partially verdicted, + 7 follow-up cells unheard: jin ladder, hot attacks],
48 triptych [listen, partial], 47 sustained probe [listen, verdicted-ish],
46 excite1 [listen — interim only; piano_ab A/B + segment_sweep3 combo
verdicts still open], 41 piano [try], 40 Triangle [listen], 39 wheel [try],
35-38 AF items. **The day's full narrative + all verdicts:
docs/research/oneshot_sweep/ROUND1_VERDICTS.md** (bow discovery, parameter
notes, excite rounds). Bow patch: patches/baselines/
bow_evolution_discovery.json (byte-copy of Matt's pending/ save).

## comp

Run 26 (08-10) landed the phrase-aware cadence (composer/realizer cursor
disagreement fixed, arrivals 1/4→4/4), `cadentialArrival: approach`, and
revived six templates that had been rendering silence. Run 27 (09-05,
interactive) closed #22: the three purge-casualty A/Bs re-rendered.

Review queue (comp/REVIEW.md), now fully verdictable: 18 cadential arrival
[listen], 16 section key [listen], 9 Bruckner v2 [listen], 10 pedal_chords
[listen], 12 voicing_ab [listen], 14 passage-mode [read], 19 octave naming
[decide] — all under renders/comp/pending/ — plus the run-27 re-renders
under renders/comp/audition/: 11 literal repeats [listen], 13 phrase
endings [listen], 17 passage endings [listen].

## Standing notes

- The dsp and comp *scheduled* runs have collided twice in this shared
  working copy (runs 21, 25); staggering the two schedules is still open.
- **patches/library/ curated by Matt 2026-08-22 (bdc390d): 74 files, final
  patches only from here on.** KS piano baseline is now
  keys/acoustic_piano/piano_default.json (Piano_bright retired); clarinet_*,
  fm rhodes, v6 strings, percussion kick/snare are gone. rt_smoke cases
  repointed. Tools/docs that name the old files are historical.
- **patches/audition/ is the listening queue (renamed from pending/ 2026-08-22,
  gitignored); patches/pending/ is now Matt's sandbox — runs never write there.**
- **renders/{dsp,comp}/audition/ follows the same rename (Matt 2026-09-03): future
  round queues go to audition/, renders/*/pending/ = Matt's hand-work. The
  feedback_curves5a/5b queues predate the rename and stay in dsp/pending/.**
- Nightly scheduled run purges patches/old/ >30 days.
- Misc task drops: Matt can leave a dated list in a lane's MISC.md
  (pattern established 08-21); see WORKFLOW.md.
- Undecided feature ideas live in IDEAS.md (new 08-22), not the backlogs;
  runs read it, never act on it.

How this works: [WORKFLOW.md](WORKFLOW.md) · reports: dsp/reports/ ·
comp/reports/ · latest: dsp/reports/2026-08-30-dipsy-ui-round-ksbow-shape-editor-spec.md,
comp/reports/2026-08-10-wolfie-run26.md

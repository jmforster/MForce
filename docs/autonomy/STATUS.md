# Status — open this file first

Updated: 2026-08-22 (housekeeping pass, Fable 5 interactive with Matt).
STATUS and both BACKLOGs compacted: done items are now one-line ledgers in
the lane backlogs; run-by-run history lives in the lane `reports/` and in
git history of these files. Stale gates fixed in the same pass (dsp 15's
dead CombineTest ask, 12's folded verdict, 16→3r supersession, 27+3q merge).
**GOALS.md awaits Matt's refresh** — the July goals are largely satisfied,
and the current center of gravity (PerformSource/pin model, AF archaeology,
KS piano family, live MIDI) grew out of interactive sessions, not GOALS.

| Lane | Dev | State | Next up | Awaiting Matt |
|---|---|---|---|---|
| dsp | Dipsy | **08-24: KSString IS THE MODEL** — bow mode verdicted same day ("character + promise, problems on the periphery"); flutter/sizzle/seam all root-caused; BowedStringEvolution parked; Listen-here fixed; gate re-frozen 180/180 | backlog 34: KSString mode family (fast attacks via source pin, resonance balance, top-octave bow keytrack, KSPipe); menu reorg | **10 items** |
| comp | Wolfie | dormant since run 26 (08-10) | #22 re-renders → #21 zero-event check → #8 re-observe | **10 items** (3 blocked on #22) |

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
revived six templates that had been rendering silence. Nothing since.

Review queue (comp/REVIEW.md): 18 cadential arrival [listen], 16 section
key [listen], 9 Bruckner v2 [listen], 10 pedal_chords [listen], 12
voicing_ab [listen], 14 passage-mode [read], 19 octave naming [decide];
**11, 13, 17 are BLOCKED** — their renders died in the 08-08 purge;
re-render is backlog #22, the first task of the next comp run.

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
- Nightly scheduled run purges patches/old/ >30 days.
- Misc task drops: Matt can leave a dated list in a lane's MISC.md
  (pattern established 08-21); see WORKFLOW.md.
- Undecided feature ideas live in IDEAS.md (new 08-22), not the backlogs;
  runs read it, never act on it.

How this works: [WORKFLOW.md](WORKFLOW.md) · reports: dsp/reports/ ·
comp/reports/ · latest: dsp/reports/2026-08-24-dipsy-bowfamily-listenfix.md,
comp/reports/2026-08-10-wolfie-run26.md

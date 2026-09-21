# Note onsets v2 — per-note phrasing, hold, and the end of the phrase object

2026-09-20. Brainstorm (Matt driving, after auditioning REVIEW 73) → spec →
plan → build, in one day, dispatched to Opus and re-verified by the
coordinator. Spec
`docs/superpowers/specs/2026-09-20-note-onsets-v2-design.md`; plan
`docs/superpowers/plans/2026-09-20-note-onsets-v2.md`. Supersedes the v1
build of 2026-09-19 (report `2026-09-19-note-transitions.md`).

## Why there is a v2 one day after v1

v1's spec described deadline-extension — each note extends the voice's
gate, no component ever asks what is next. The build received whole
phrases and baked their total at `prepare`, so a phrase became ONE LONG
NOTE to every duration consumer, silently re-scoping every percent
envelope in the patch: oboe1's unbounded 25% release turned into a 3 s
die-off over a line, and its attack stretched to a second. Matt heard
exactly that, plus retune ticks at every pitch change and no articulation
at all on repeated notes. The duration consequence never got its own
design gate in the v1 brainstorm; v2 is what v1's own spec said.

## The model, in one paragraph

A player never looks ahead. The SCORE looked ahead when it drew the slur;
the player just reads each note's markings. So every note is atomic and
carries two facts: **onset** (how it begins — `breath`, `tongue`, `slur`)
and **hold** (does the excitation continue past its end). The phrase
exists only in notation. Whether a note CONTINUES a living voice is never
declared — it is derived from engine state, the same test the live
keyboard's key-overlap makes against the same state.

## What landed, task by task

1. `refactor(engine,ui): transition -> onset everywhere` — da35fec
2. `refactor(engine): PerformedNote boundary object` — a1763b4
3. `feat(engine): gate_release(releaseRefFrames) re-layout + per-voice
   envelope collection` — f94e97d
4. `feat(engine): resumable line voices — per-note hold/onset delivery,
   play_phrase retired` — 252b360
5. `feat(loader): v2 score keys onset/hold + sustaining consult` — 1e9ac03
6. `feat(ui): v2 emission (onset/hold), per-note generate, sustaining
   status note` — 8b3e68e
7. `feat(ui): sustaining + onsets on the Output settings panel, labels
   fixed` — 4d46113
8. `feat(dsp): articulation1 v2 — post-loop oboe tongue, slur glide,
   sustaining declarations` — 9afb8fb

The load-bearing pieces:

- **`PerformedNote`** is the Performance→Realization boundary object:
  noteNumber, velocity, duration, onsetId, hold, curve. The loose
  argument list with the PitchCurve dangling at the end is gone, and so
  is `play_phrase`'s receive-the-whole-vector API. `music::Note` gains
  nothing — onset and hold are Interpretation products.
- **Resumable voices.** The offline render loop was one synchronous pass
  per phrase; that is where `prepare(total)` came from and both died
  together. `render_chunk` is now the single sample loop (perform tick,
  root pull, tap-only advance, per-node capture) and ring-out, tail, cap
  fade and containment live in the callers — which is what lets one voice
  render in pieces. A `hold: true` note renders its samples and SUSPENDS
  WARM; the next note resumes the same voice at the boundary sample. The
  live path already worked this way; v2 brings the offline path to the
  same shape.
- **Envelope timebase is the NOTE, always.** Percent stages resolve
  against the note that prepared the voice. No phrase timebase, no scope
  knob. Its necessary companion: at `gate_release` the release stage is
  RE-RESOLVED against the releasing note's length, so a line opening on a
  quarter and ending on a half gets the half note's release.
- **Glide.** On a pitch change inside a line the frequency ramps over
  `glideMs` (default 15) through the existing bend machinery: an
  instantaneous delay retarget puts a kink in a waveguide's stored wave —
  the measured retune tick — and the glide is the waveguide-native fix.
  It doubles as portamento later. Note for the record: a legacy paramMap
  patch delivers frequency by a PUSH evaluated once at Setup, so its
  retune stays instantaneous; only pulled frequency chains glide, which
  is every taught wind patch.
- **`sustaining`.** Declared in the instrument block, never inferred.
  A non-sustaining instrument never receives hold, so the piano invariant
  holds BY CONSTRUCTION rather than by a delivery special case.

## Gates, all green, with numbers

1. **Null gate**: 79/79 manifest entries byte-identical after EVERY
   engine-touching task, including the intermediate checkpoint that gated
   the `render_chunk` extraction as a pure refactor before any semantics
   changed. (The two phrase_smoke baselines report as not-in-manifest;
   add at the next deliberate refreeze.)
2. **engine_tests**: ALL PASS, 494 → 506 checks. New coverage: release
   re-layout at the envelope level; and per-note delivery end to end —
   hold mechanics (no release dip across the boundary), release re-layout
   in a render, glide pitch arrival + boundary smoothness,
   finish_open_lines, mid-line retune.
3. **Piano invariant**: piano_default with `hold: true` and onset names
   on a two-note score is md5-IDENTICAL to the same score without them,
   at CLI level and through the UI's gencheck path. The loader says so
   once on stderr instead of ignoring it silently.
4. **v2 smoke assertions** (phrase_smoke_phrased, rewritten to v2 keys):
   the held pair sustains THROUGH the 0.5 s boundary — deepest trough
   −0.18 dB against either side, gate −3 dB — and the sounding length
   tracks the flat render's within the release delta (1.092 s vs
   0.974 s). It is deliberately no longer byte-identical to the flat
   render: percent stages resolve per note, not over a phrase total.
5. **gencheck parity**: scale 1.000000, max 1 LSB of 16-bit on
   phrase_smoke_phrased.
6. **Roundtrip corpus** (baselines + library, 135 patches): the same 8
   known pre-existing findings — 7 render diffs + 1 id change, backlog
   79 — and 0 new.
7. **articulation1 v2 machine gates**: oboe phrased line 0 deep
   dropouts (worst boundary −2.5 dB), dent −10.9 dB at repeated pitches
   and 0.0 dB at slurred ones; oboe hold cell 0 dropouts, −10.9 dB;
   trombone phrased 0 dropouts (worst −6.0 dB), dent −1.4 dB repeated /
   0.0 dB slurred; hold cell 0 dropouts, −1.2 dB. Dormancy: both
   instruments' FLAT renders are byte-identical to the same taught patch
   with the gesture's trigger unwired.
8. **Taught-patch UI roundtrip**: onsets, sustaining, the PerformNode
   `onset` field, the NameGate name, TDip's seconds-mode stages and
   trigger wire, and the oboe's repointed `graph.output` all survive
   save/load.

## The day's diagnosis chain, cited

- **Identical-WAV fingerprint.** The morning's diagnostic renders showed
  v1's phrase-as-one-long-note re-scoping percent envelopes; the fix is
  §5's per-note timebase, not a clamp. The v1 maxSec clamps in the taught
  oboe are REVERTED — under v2 phrase-safety needs no authoring at all.
- **Dropout roulette.** Dipping the drive INSIDE a marginal feedback loop
  killed 9 of 28 boundaries, one re-igniting an octave up. The oboe's
  tongue therefore moves POST-LOOP: an output-side dent cannot extinguish
  the oscillation. The trombone's lip mechanism measured no dropouts and
  keeps its breath-side placement. (Backlog 72 owns the underlying
  marginality.)
- **Reference calibration.** A real player's repeated-note dents measure
  7–23 dB over 40–60 ms, depth tracking musical weight (A|A −22.6/50 ms,
  A|A −19.5/40, F#|F# −10.8/40, D|D −6.8). The shipped consonant sits
  mid-range at −12 dB over ~58 ms. Depth as an expressive input
  (velocity, beat) is future work.
- **Strip-draw perf root cause.** Same-day, separate commit (030a4d5):
  strip drawing was O(all samples × strips) per frame. That is the real
  backlog 76, now closed out.

## Investigated en route, not a regression

A phrased oboe1 line showed gencheck-vs-CLI scale 1.002499 / 313 LSB.
It is not a delivery divergence. A held line renders about 7 dB hotter
than four separate attacks (peak 0.98 vs 0.73) because one continuously
excited line accumulates more energy than four cold starts — which pushes
the sum into the soft-clip knee, where the UI's direct-instrument render
and the CLI's mixer path have always differed. The same patch at half
gain: scale 1.000000, 1 LSB. Same-pitch held line (no glide) and a single
2 s note: 1.000000, 1 LSB. Gain staging for phrased renders is worth
remembering when teaching the next patch.

## Deferred, on record

- Live keyboard phrasing is now pure wiring — the delivery machinery is
  done; backlog 53 owns the pool floor, last-note steal and click-free
  cuts.
- Performer breath contour (74a8) is where phrase-scale shape belongs:
  patches own note-scale character only, and a line's crescendo is the
  PERFORMER varying breath.
- Onset-keyed envelope variants (backlog 68), depth-as-expression, and
  polyphonic phrasing are all out of v2's cut line.
- The spec §5 lint — a sustaining patch whose output-path envelope has no
  sustain stage should warn — is filed as a new backlog item, not built.
- The passage-string UI pass (a `|` passage phrasing on the taught oboe,
  a `|`-free passage unchanged, the not-sustaining status note) is
  Matt's: the emission path has no headless entry point, so it is covered
  by construction and by the score-side gates above, and confirmed by
  ear.

## Two facts a reader will want

- **Onset's object form is reserved, so the scalar is not a dead end.**
  `"onset": "slur"` ≡ `"onset": {"name": "slur"}`, expressive form
  `{"name": "slur", "glideMs": 80}` — backward compatible, no migration.
- **Continuation is not a fact.** There is no "cont" key, no grouping
  pass and no lookahead anywhere. The engine asks its own state whether a
  voice is open, offline and live alike.

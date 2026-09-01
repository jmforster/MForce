# Ideas — parked, not decided

Feature ideas that are plausible but NOT yet decided. Nothing here is a
backlog item: autonomous runs READ this file for context but never act on
an entry. Promotion to a lane backlog is Matt's call (one word in a
session or in REVIEW/GOALS); a promoted entry moves out of here the same
day. Keep each entry short — the question, the mechanism sketch, the
known issues, and what would make us decide. (Matt, 2026-08-22: "we need
an IDEAS for features like this instead of cluttering backlog when we
might not decide to do it.")

## dsp

- ~~**DampedDelayLine — fused delay + damping + internal pitch
  compensation**~~ — RESOLVED 2026-09-01, and NOT as a fused node: Matt
  pushed back on baking a lowpass into the delay ("I'd rather find
  redNoise settings that sound good vs. knee-jerk sticking on a
  lowpass"), and the fusion turned out to be a workaround for the graph
  not being able to ask its neighbors about lag. Landed instead as
  `ValueSource::phase_delay_at(f)` (default 0) + DelayLine `compensate`
  (walks its loop, subtracts tap z^-1 + member lags per sample). Any
  filter, or none, stays composable; a graph WITHOUT compensate is the
  deliberately-coupled scoop variant (cutoff motion as the effect, per
  Matt's 09-01 verdict on cutoff-as-release). See
  docs/feedback_loop_design.md §5.

- **Per-stage Envelope bindings (e.g. `Envelope.attack.percent` from a
  Curve)** — 2026-08-22. Right tier is per-note (stage layout happens in
  `prepare()`), i.e. a dynamic-pin setting target like `sustainLevel` /
  `timeScale` already are. Engine side small (stage-field settings,
  per-instance descriptor list). Issues: stage IDENTITY (index names break
  on insert/delete — role names for the adsr shape, or optional stage
  names); UI dual representation (stage table vs settingValues — the
  `sustainLevel` sync headache, multiplied); fraction-mode percent is
  clamped to [minSec,maxSec] so a driven value can be silently clamped.
  Precedent/need: the additive-piano per-register attack finding
  (research/ml_ears/out/piano_analysis_report.md §3: C2 36 ms → C6 9 ms vs
  a uniform 8 ms lock). **Decide after:** Matt plays with `timeScale`
  (per-note mappable, scales ALL stages) — if that suffices, this stays
  parked.

- **One-shot table source vs SegmentSource** — 2026-08-23, from the
  Passport-roots brainstorm. No "play this array once" source exists today
  (WavetableSource fills from an input and loops at `frequency`; legacy
  `Wavetable(float[])`/`EffectWavetableSource` were never ported). The segment
  sweep uses SegmentSource as the player (width-1 segments = raw samples when
  the generator pre-sums overlaps). Likely both survive: a OneShotTable for
  "any waveform, play once" (also what the sampled-hit substitution test
  needs) and SegmentSource for the compact, per-trigger-re-randomizing
  parametric flavour; generator families that prove out may become engine
  nodes so variation lives per hit. **Decide after** round-1/2 verdicts say
  which families are gold. Matt's own notes: docs/notes/SegmentRevisit.md,
  docs/notes/Stoned2.txt.

## comp

(nothing yet)

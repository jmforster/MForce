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

- **PCHIP "smooth" curve mode; retire smoothness on Shaper?** — 2026-09-05,
  from Matt's curve-editor review. Problem: SmoothnessInterpolator's sine
  easing forces zero slope at every breakpoint, so several segments
  approximating one smooth function come out scalloped (Matt's 5-segment
  accelerating ascent). Right tool: monotone cubic interpolation (PCHIP,
  Fritsch–Carlson slopes) — C1 through the points, no overshoot, no
  scalloping; lives in `Curve` as a third evaluation path.
  Interaction design (Matt: "I don't see how the 2 methods will interact"):
  they DON'T — modes are exclusive per curve. A Shaper curve is either
  "points" (linear default + per-segment signed-curvature overrides, the
  09-05 gesture set) or "smooth" (PCHIP over all points; per-segment
  overrides disallowed/greyed — PCHIP chooses knot slopes from neighbors,
  so a Hold or Expo override inside it is contradictory). Morph: PCHIP
  lerps point-space same as today; mode is shared structure between A/B.
  Smoothness pin on Shaper becomes vestigial under this scheme — BUT
  library winds carry smoothness 0.6 on their Junctions, so removal is
  behavior-changing for keepers. Path: loader keeps honoring the pin
  (legacy), UI stops exposing it on new Shapers, full removal only after
  Matt re-verdicts/re-saves the winds at 0.5 or re-tunes. SegmentSource
  keeps smoothness untouched (textures want the 0..1 sweep).
  **Decide:** whether PCHIP earns a build slot, and whether Shaper
  smoothness deprecates with it.

## comp

(nothing yet)

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

## comp

(nothing yet)

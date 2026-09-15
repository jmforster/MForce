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
  Smoothness STAYS in the UI for points mode (Matt 2026-09-05, reversing
  the deprecation idea): library winds carry 0.6 on their Junctions, and
  out-of-range values are a sound-design zone — Matt hit smoothness 9 by
  accident with "crazy (possibly happy) results." Mechanism: above 0.5 the
  interpolator blends 2(1-s)*linear + 2(s-0.5)*sine, so s>1 runs the blend
  weights negative/super-unity = wild overshoot between points; nothing
  clamps a wired pin at eval time (the 0..1 descriptor range is advisory).
  Treat that as a feature: document the extrapolation zone rather than
  clamp it. SegmentSource smoothness untouched as ever.
  **Decide:** whether PCHIP earns a build slot (smoothness question is
  settled: keep it).

- **CMA-ES endgame re-scope** (was backlog 3, moved 2026-09-14) — staged
  core DONE (spec/scorer/optimizer, smoke 1.28→0.945, 600 evals ≈ 50 min);
  parked on scope, not speed: objective re-baselined, KS became the piano
  baseline, UI renamed the encoder's node ids. Prereqs before any 600-eval
  spend: retarget ids, verb-free objective, settle REVIEW 0b shared-rng
  discontinuity. Target instrument is Matt's pick. **Decide:** whether an
  optimizer round earns a slot in the steal-first world.

- **Additive SIMD 2d/2e** (was backlog 8-open, moved 2026-09-14) — hot
  loop already 90.6 → 26.1 ns/sample/partial; 2d restaged in
  specs/2026-07-31-additive-simd-soa-design.md (four anti-results recorded
  there — don't retry); 2e iFFT overlap-add = review:listen when
  attempted. **Decide:** when additive CPU actually hurts again.

- **Peak-mode envelope follower node** (was backlog 3s, moved 2026-09-14)
  — resurrect run-21 Peak mode only (|x| + asymmetric attack/release) as
  its own small node; AF uses the block as cheap AD generator and step
  de-clicker. Matt's named uses: auto-wah (follower → SVF cutoff),
  sidechain ducking. Code in git history (~2026-08-05).

- **MIDI input off the UI-frame cadence** (was backlog 18, moved
  2026-09-14) — trigger notes on RtMidi's callback thread; prereqs in
  place (instrument cache). Thread-discipline checklist preserved in
  backlog git history. **Decide:** "when live feel matters" (Matt) —
  today's 0-16.7 ms jitter sits under the ~11 ms buffer floor.

- **Vibrato/bend on live-evolution wavetables** (was backlog 33, moved
  2026-09-14) — fractional-read-head resample laps the writer on live
  tables (~25-30 dB inter-harmonic broadband at depth 0.01). Options:
  Approach B loop-length modulation, in-loop allpass tuning, or
  evolution-aware routing. Parked with BowedStringEvolution; revisit only
  if live-evolution tables return as a production path.

- **KSString physical-mode family** (was backlog 34, moved 2026-09-14) —
  strings/flutes/horns on the KS architecture (+ KSPipe sibling).
  Superseded in spirit by the waveguide harness campaign + junction
  roadmap; the ksbow verdict notes (slow attacks, over-resonance,
  top-octave screech, trumpet-adjacency hint) stand in backlog git
  history if the KS route reopens.

- **Toggle/selector node for instant A/B audition** (was backlog 57,
  moved 2026-09-14) — both branches wired, widget flips which is heard.
  Questions preserved: engine citizen vs UI-only swap, click-free live
  switching, N-way, does the inactive branch render, group boundary,
  save semantics. Wishlist — no go.

- **Crackle revisit — standalone AND as loop excitation** (was backlog
  58, moved 2026-09-14) — only excitation sub that failed in the oboe
  loop; suspected under-feeding between sparse impulses, unverified.

- **Envelope hold/loop mode** (was backlog 3i, moved 2026-09-14) —
  one-flag change in envelope.h::next() so UI streams run truly forever
  (UI currently preps 7200 s with absolute_time overrides).

- **Squeaker modulated even/odd partial weight** (was backlog 3l, moved
  2026-09-14) — AdditiveSource evenWeight/oddWeight as refs have no
  FullPartials equivalent; deliberately not flattened (would silently
  change Matt's patch). Options: promote to ValueSource params, or accept
  the loss and rewrite Squeaker.

- **WanderNoise deltaSpeed rate-normalization** (was backlog 3j, moved
  2026-09-14) — applied per sample, so audio-rate wander self-cancels;
  behavior-changing for existing wander patches.

- **Attribute proof-of-effect ladders** (was backlog 24, moved
  2026-09-14) — A/B ladders for suspects from docs/config_pin_census.md
  (HammerBank harm1-4/numBands strongest); "when bored". Check the
  mapped column before calling anything dead.

- **Scorer trust debt: derive_motion takes no B; F1 unstable** (was
  backlog 3e-b2, moved 2026-09-14) — 6.4x cutoff-sensitivity on F1 after
  two attempts (likely needs a longer window, not a different filter).
  Standing input to the steering decision that ML-ears/comp scorers get
  cheap discrimination backtests before trust.

## comp

(nothing yet)

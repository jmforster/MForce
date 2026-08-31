# 2026-08-29pm/30 — Dipsy interactive: UI round, KS-bow starter, shape-editor spec

Continuation of the 08-29 session (see 2026-08-29-dipsy-grail-hunt-ui-batch.md
for the grail closure + first batch, commits ba5200b/4f60248/51f3860).

## Landed this batch (gate re-frozen twice, final 181/181)

Engine:
- **SegmentSource `width` pin** (global time-scale of the shape, Matt's
  ask): <1 narrower / >1 wider; initial per-segment rounding floored
  unit-width sampled shapes at 1 sample ("0.5 sounds identical to 1") —
  replaced with **error-diffused scaling** that decimates segments whose
  scaled width rounds to zero. wscale==1 keeps the exact legacy code path.
- **SegmentSource stale-pin fix**: update_segments() ran in prepare(),
  reading pins via current() before the chain computed for the note — a
  wired Note→Curve→width chain returned the previous voice tenant's value
  (nondeterministic clicks/thumps per keypress, found via Matt's
  piano_seg_wcurve). Deferred to first next(). Third instance of the
  lazy-first-sample idiom → formalization queued as backlog 36(c).
- **KSString**: kMaxCombs 3→10 ("outside the Steinway"), even-spread
  offsets bit-exact with the old 1/2/3 layouts; **spreadJitter** +
  **excStagger** settings (the decoherence pair) — the even spread +
  shared excitation made a descending decoherence front audible (partial
  n re-phases at t ∝ 1/n); jitter breaks the beat ladder, stagger breaks
  the coherent onset.
- Loader: dynamicPins null entries = promoted-but-unwired (skip).

UI:
- **Envelope Properties reorg** (Matt spec): accuracy settings inline
  under params (no heading); presets get "Duration" section + "Shape"
  table (per-stage Curve/Power columns); envelope display below.
- **Promotion circles** replace promote/demote buttons: hollow = click
  to promote (pin appears, wire in editor), gold = click to demote.
  Fixed en route: json operator[] resurrecting erased entries
  (stuck-gold bug), gold-wire drag-off detach, wire-delete now unwires
  without demoting.
- **Listen tap follows drill-out** (REVIEW 51 tweak).
- **SegmentSource**: shape preview above the points table; pair-aware
  point rows (per-float delete had been shifting widths into values —
  Matt's alternating-preview bug; also un-clamped width cells from the
  ±10 value range).
- Curve editor: %.6g (16000 no longer 1.6e+04), "+ point" wording.

## Corpus / library

- **piano_seg** promoted to library/keys/acoustic_piano by Matt — the
  halfhump piano (excite-round harvest), simpler than piano_default and
  beats it in the normal range. Third library piano.
- Manifest re-frozen twice: +piano_seg & segment stale-pin fix (08-29pm),
  then the width-scaling diffusion (08-30, piano_seg moves ≤1 sample per
  segment boundary — Matt ear-checked: "width fix good").

## Review housekeeping (all Matt verdicts)

- 49 excite4 CLOSED (level dominant, keytrack the bed; seams secondary;
  wtsaw ok jag40+; hots dropped; jins/jags "plucky not bowy" → 34a).
- 52→Resolved stub; 50 marked parked/no-verdict-owed; 51 Listen-here
  verdicted works; 41 keyboard fine; 40 Triangle works fine;
  **35/36/37 AF piano CLOSED — "we're beyond that"** (KS piano family
  superseded it). 39 wheel blocked on hardware (no wheels on current
  controller). Remaining queue: 48/47/46 remainders, 38 sax [discuss]
  (patch still needs the AF Discord download; account access lost).

## KS-bow thread (backlog 34a)

- **patches/scratch/ksbow_creak.json**: ksbow_c modernized (Note face,
  t60 dynamic pin, no paramMap) + creak texture into the source pin +
  width keytrack curve. Matt: creak reads as thump — consistent with
  "one-shot bursts into a resonant string read as pluck"; knobs offered
  (direct up, width up, oneShot-off + gap = continuous bow-grain).
  Settings tweaks alone didn't get creak; led to the shape-editor need.

## Shape editor — SPEC SETTLED

docs/shape_editor_design.md (lineage: Stoned2.txt + SegmentRevisit.md).
Always-points (no dense format), straight lines + global smoothness
(per-point curves deferred on the interpose question), one engine change
(SegmentSource timeMode setting, legacy inference kept), generator
registry (Pulse Train founding, sweep atoms ported incrementally),
3-ghost variance preview, >512-point read-only guard. All open questions
decided by Matt same day. Build order agreed: timeMode → shell+curve
adapter → segment adapter → Pulse Train → ghosts → atom trickle.

## Also

- Wheel-pans-canvas (08-26 code) identified as the answer to Matt's
  "2-finger panning suddenly works" mystery.
- Backlog 36 grew (c): formalize first_sample() init hook + the
  stale-pin-at-prepare rule.
- AF setup re-derived: plugin DLLs in Downloads\Alpha_Forever, host =
  SAVIHost rename-trick, Engine→Run was the no-sound fix.

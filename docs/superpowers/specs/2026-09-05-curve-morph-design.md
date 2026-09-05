# Curve unification, per-segment interpolation, and Shaper morph

Status: SPEC, brainstormed 2026-09-05 (Matt + Fable 5, interactive).
Lineage: feedback_loop_design.md §3.5 (Shaper; morph pin deferred there),
shape_editor_design.md (client model), JUNCTION_OPERATING_POINT.md
(origin-asymmetry/threshold findings this design makes playable),
docs/research/feedback_sweeps/ (nature archetypes swept in r3/r4).
Feeds both planned autonomy lanes: instrument search (drawable
bow/double-reed curves) and novelty search (audio-rate morph).

## 1. Motivation

Three point-list evaluators exist: `Shaper::map` is a hand-mirror of
`CurveNode::map`, and Shaper + SegmentSource each own a
SmoothnessInterpolator. Meanwhile two of nature's five junction curves
are not drawable with global interpolation: the bow's falling flank and
the double reed's early slam need per-segment shape control. And the
morph pin deferred in feedback_loop_design.md now has its use case: the
loop patches exist, and JUNCTION_OPERATING_POINT.md showed the junction
curve's origin asymmetry and threshold position are *the* character
axes — morphing the curve makes them playable dimensions instead of
sweep parameters.

## 2. Curve value type

Extract `Curve`: breakpoint list + interp policy + `eval(x)` — the
shared math only, one canonical serialization. Composition, not
hierarchy (the `WavetableSource`-has-a-`Wavetable` pattern).

- Shaper and CurveNode points-mode adopt it outright.
- SegmentSource keeps its delta/varPct/gap/oneShot machinery and
  converts to/from Curve at the editor boundary only.
- CurveNode's expressions mode (exprKnots) stays CurveNode's own
  extension on top; LogX/LogLog stay CurveNode-only **global domain
  settings** (coordinate systems, not segment shapes; need x > 0, so
  meaningless for bipolar junction curves).
- JSON byte-identical everywhere. Internal refactor, proven by the
  null gate.

Terminology ruling: "knots" tolerated in code, **never in UI** — UI
copy says "points" (Matt 2026-09-05).

## 3. Per-segment interpolation

An optional per-segment override of the global interp: "how this point
connects to the next," stored on the segment's left point.

- Enum reuses the Envelope stage vocabulary — Linear, Expo (with
  power), Sine — plus **Hold** (flat until next point; reed-closure
  plateaus, stairsteps). The enum is genuinely shared: Hold thereby
  becomes a legal Envelope stage type too (value pinned at startVal
  for the stage). The old `Stage::holdPct` field is REMOVED in the
  same stroke (Matt 2026-09-05): scanned every patch/score tree —
  zero nonzero uses; the UI never exposed it; pitch_bend.h's holds
  are flat ramps and untouched. Loader ignores the stale key in old
  JSONs; audio at holdPct=0 was already identical, so the null gate
  is unaffected. No new vocabulary, one less field.
- Storage: optional parallel array beside `values` (e.g.
  `"segs": [[type, power], ...]`), written only when overrides exist.
  All existing patches parse unchanged; null gate stays green. (A
  4-column `values` was considered; rejected — forces migration.)
- Smoothness rule: the modulatable `smoothness` pin applies **only to
  segments at default interp**; an overridden segment is exact and
  ignores it. Global feel-knob stays global, overrides are surgical.
- Global interp dropdown change resets all local overrides (confirm
  dialog when overrides exist; undoable).
- Per-segment Expo subsumes local power-law needs (a 2-knot log-log
  segment is y = k·x^n); log domains remain global-only, for keytracks
  spanning decades of Hz.

With this, the bow's stick-slip curve — steep rise, **falling flank** —
is drawable in 4-5 points, and sweep tooling gains power-jitter axes,
not just y-jitter.

## 4. Editor

Constraint: **clients stay distinct.** One canvas, per-client
vocabulary — the Shaper client has no timeMode or cycle options;
SegmentSource keeps widths/gaps; nothing leaks across.

Gestures (Shaper client is the v1 surface for per-segment editing;
the Curve type supports it for every client, adoption later):

- Click mid-segment: new point on the line (unchanged).
- Right-click-drag on a segment: creates an Expo override; drag
  distance sets power. A distinct-colored dot appears mid-segment and
  **persists** as the curvature handle (drag to adjust power;
  right-click the dot to clear back to default interp).
- Preset menu (hardcoded list, additions by code for now):
  **bow, reed1, reed2, lip, jet, hard** — the five nature curves + the
  degenerate sixth, per-segment overrides included where the shape
  needs them (bow flank) — plus the standard waveforms
  **sine, saw, triangle** drawn as transfer curves across the domain
  (sine/triangle fold, saw wraps via a near-vertical drop; open-loop
  these are classic wavefolder/wrapper shapes, in-loop they're
  periodic junction nonlinearities). Insert replaces the current
  curve; undoable. A curve-library *file* format is deferred until the
  hardcoded list feels cramped.

## 5. Morph pin (Shaper only, v1)

`morph` pin, range 0-1, default 0. Point-space interpolation: matched
breakpoints (and segment powers) lerp between curve A and curve B, then
the blended curve is evaluated. Output-space blending was rejected:
features smear (two knees at different x become a mush); point-space
slides the knee, which is the physical truth (embouchure moves the
closure point).

### Shared structure, two geometries

Correspondence is guaranteed by the data model, not repaired by the
editor: point count, point order, and per-segment interp **types** are
shared structure; x/y positions and segment **powers** are per-curve
geometry. Divergence is unrepresentable.

- "+ morph" control births B as a copy of A; A/B toggle selects the
  active curve; the inactive curve ghosts faint; optional thin overlay
  shows the live blend at the current pin value. Deleting B collapses
  to single-curve (keeps the active curve).
- Drag point / adjust power dot: geometric — active curve only.
- Add point: structural — inserted in both, landing ON each curve's
  existing line (shape-preserving on both sides).
- Delete point: structural — removed from both.
- Segment type change: structural (shared); the gesture sets the
  active curve's power and initializes the other's to the same value.
- Editor morph scrubber (if added): visualization only; the pin is the
  single source of truth (round-trip stays honest).

### Serialization and boundaries

Curve B saves as `values2` (+ `segs2`); absent = single-curve. Morph
pin wired with no B: ignored (B degenerates to A). B present, pin
unwired: pin default 0 = pure A = byte-identical. Loader validates
equal point counts.

### What morph buys (mechanism, not timbre claims)

The graph routes everything — no modes: velocity/Note node = per-note
curve; envelope/LFO on the pin = slow morph (attack curve into sustain
curve; "curve per envelope segment" collapses into "shape the morph
envelope"); any audio-rate source = a 2-D nonlinearity y = f(x, m(t))
— intermodulation inside the loop, sideband/chaos territory, alias-
risky: novelty-lane material, free to allow since a pin is a pin.
Calibration note for sweeps: oscillation threshold moves with the
curve, so critical drive must be bisected at both endpoints (crit(m)
is not linear between them). A morph can carry a note across ignition
or death mid-note — expressive and calibration-hostile.

## 6. Non-goals / deferred

- N-curve morph (2 must prove musical first).
- Curve-library file format (hardcoded presets first).
- Morph on CurveNode; per-segment gesture in non-Shaper editor
  clients; SegmentSource per-segment overrides.
- Envelope-stages-as-Curve unification (the shared enum keeps the door
  open; Hold walks through it as an Envelope stage type, nothing else
  does now).
- Per-segment log domains.

## 7. Validation

- Null gate byte-identical after each stage (library corpus, standing
  pre-commit rule).
- Unit tests: (a) Curve eval parity — old Shaper::map / CurveNode::map
  vs new Curve on identical inputs; (b) per-segment override eval incl.
  Hold and Expo power; (c) smoothness-ignores-overridden-segments;
  (d) morph endpoints exact (m=0 ≡ A, m=1 ≡ B) and midpoint point-space
  (knee x slides, not smears); (e) loader round-trip values/segs/
  values2/segs2; (f) unwired-morph byte-identity.
- Reference: re-render one library keeper (oboe_default) pre/post each
  stage; byte-identical until a morph is actually wired.

## 8. Build order (increments, shape-editor style)

1. Curve type extraction; Shaper + CurveNode points-mode swap to it.
   Null gate frozen before/after. Tests (a).
2. Per-segment data model + eval; Hold joins the shared enum and
   Stage::holdPct is removed (envelope.h + patch_loader.cpp; loader
   tolerates the stale key). Tests (b)(c)(e).
3. Shaper editor client: gesture, power dot, global-reset confirm.
4. Presets menu (the six curves).
5. Morph pin: engine lerp + serialization. Tests (d)(e)(f).
6. Editor A/B workflow (birth-as-copy, ghost, structural-edit
   mirroring, blend overlay).

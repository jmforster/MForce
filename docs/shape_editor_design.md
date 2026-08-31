# Shape editor — design (v1 scope)

**Status: v1 SHIPPED 2026-08-30** — increments 1–5 (timeMode, canvas +
curve client, segment client, Pulse Train generator, variation ghosts),
commits 227d8b7..06593c3, same evening as the spec. Matt's canvas verdict:
"brilliant"; segment client "works great". Remaining: the atom-port
trickle (jagged, dots, clusters, beds — one pure function each, add to the
generator combo as they land).

2026-08-30, Dipsy + Matt. Lineage: docs/notes/Stoned2.txt (categories that
generate random examples; "wtf is happening, I'm redesigning MForce!"),
docs/notes/SegmentRevisit.md (the pulse-train vocabulary), and the 08-29/30
sessions: the SegmentSource point table is the bottleneck of the excitation
lane — nobody hand-edits 6,152 pairs, and the sweep generators that CAN
author them live in Python, outside the UI loop.

## What it is

One modal **breakpoint editor window**, opened by **double-clicking any
shape preview** (SegmentSource's preview, CurveNode's plot; WavetableSource
drawn fills are a future client). A shared canvas edits a list of points;
a thin per-client adapter converts between the canvas and each node's
native form. Edits push live through the existing paths (push_array /
knots), so the graph keeps sounding while you edit.

**Always points.** There is no dense/per-sample format and no freehand
draw mode (Matt 2026-08-30): "draw" means clicking points in. Sparse
points are the representation that varPct randomization operates on
musically (wobbling 20 points regenerates a coherent shape; wobbling 6000
samples is just noise), and smoothness applied after the fact covers the
shaping.

## The unified point model

- Canvas: points are (x, y), **absolute x** on screen (that's what eyes
  and mouse want), y = amplitude/value.
- Connections: **straight lines**, shaped after the fact by the client's
  existing global smoothness/interp. Per-point bespoke curves
  (Envelope-style shape + power per point) are **deferred** — the open
  question that deferred them: when a point is interposed between two
  others, what shape does it inherit? Parked until a sound demands it.
- Clients + encoders:
  - **SegmentSource**: serializes to delta-x (width) / value pairs —
    width IS delta-x, value is the arrival y; the engine format is
    unchanged ("bytecode nobody reads").
  - **CurveNode**: serializes to absolute (x, y) knots, sorted.
  - Future: WavetableSource fill shapes (ramp+jag etc.) as points.

## Units (the one engine change)

SegmentSource's first-width-decides-units inference is a trap. Add an
explicit `timeMode` setting (samples | seconds), Envelope precedent:

- Loader: patches WITHOUT the setting keep the legacy inference —
  byte-identical, no migration.
- Editor: always writes the explicit setting; canvas displays seconds
  (with a samples readout).

No other engine change in v1. The editor is UI-only beyond this.

## Editing (v1)

- Click empty canvas: add point (inserted in x-order).
- Drag point: move (x clamped between neighbors for segment clients;
  curve clients allow reorder-on-commit like the table does today).
- Right-click / Del on point: remove (respect client minimums — curves
  need >= 2).
- Pan/zoom (wheel = zoom-x, drag background = pan). Numeric readout of
  the hovered/selected point; the existing tables remain for exact entry.
- **Density limit**: shapes above ~512 points open read-only (preview +
  "too dense to edit — regenerate instead"). Sampled textures (creak et
  al.) are generator territory, not point-dragging territory.

## Generators ("categories", the heart)

A registry of preset generators; each publishes parameters and emits
sparse points into the canvas. Buttons: **Generate** (fresh random seed,
shown) / **Regenerate** (same params, new seed) / seed field (type one
back in to reproduce). Generated points are then hand-editable — the
points are the artifact; params are not stored in the patch.

- **Pulse Train** is the founding preset, parameterized straight from
  SegmentRevisit.md: pulse count, base width + width ramp (click→clack→
  clunk), spacing mode (separate / butt-up / overlap) + spacing ramp
  (dense→sparse / sparse→dense: "pile up into a jagged mountain"),
  peak decay toward lead-in/tail-off, per-point variation.
- Subsequent presets port the **sweep atom vocabulary** from
  tools/gen_segment_sweep.py + gen_excite3.py (jagged, dots, clusters,
  buzz/fine beds, slip trains, creak) one small pure function at a time —
  the sweep machinery becomes interactive.
- **Variation vocabulary**: presets use consistent `varPct`-style naming,
  with cadence explicit where it matters (per-cycle vs per-note). The
  canvas can preview variance as a ghosted band around the base shape
  (render K instances min/max) so "how much will this wobble" is visible
  while setting it.

## Non-goals (v1)

- Freehand/dense drawing; per-sample formats.
- Per-point curve/power (deferred, see above).
- Mixed units within one shape.
- Editing >512-point sampled textures point-wise.
- Storing generator params in patches (points are the artifact).

## Implementation sketch

- `ShapeEditorState` (open flag, client adapter, points vector, view
  transform, selection, generator panel state) + `draw_shape_editor()`
  modal, in main.cpp alongside the other dialogs.
- Adapter interface: `load() -> points/units/limits`, `apply(points)`
  (pushes through push_array / curveKnots + update), `label()`.
- Double-click detection on the existing preview widgets (both already
  render via PlotLines; wrap with an InvisibleButton overlay or
  IsItemHovered + IsMouseDoubleClicked).
- Generators: `std::vector<Point> gen_pulse_train(const Params&, uint32_t
  seed)` style pure functions, UI-side (engine never sees them).
- Engine: SegmentSource `timeMode` setting + loader back-compat only.
  Gate expectation: 181/181 untouched (setting absent = legacy path).

## Decisions (Matt 2026-08-30)

1. Dragging a point's x moves ONLY that point (neighbors' widths
   compensate); hold Shift to slide the tail with it.
2. Y clamps to [-1, 1] in the editor (engine still accepts beyond; exact
   entry via the table remains the escape hatch).
3. Variation preview = ghost instances (they show width-wobble shifting
   points in TIME, which a vertical band cannot): 3 ghosts, faint,
   drawn only while a varPct is nonzero.
4. Legacy paramMap-dialog curve shapes: out of scope — v1 serves graph
   CurveNodes and SegmentSource only.

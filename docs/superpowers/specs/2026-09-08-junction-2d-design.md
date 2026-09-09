# 2D junction — the curve gains a second input (brass enablement)

Status: SPEC DRAFT, 2026-09-08 (Fable 5, per Matt's "yes on the brass
enablement spec"; awaiting Matt's review — no implementation until
approved).
Sibling: 2026-09-08-hysteresis-junction-design.md (discrete state for
bow). Together they close the loop family's "everything lands on the
reed spectrum" ceiling from opposite ends: hysteresis adds a branch
decision, this adds a continuous second axis.
Lineage: LOOP_PATCH_ANATOMY.md junction feature column ("2D junction —
drive as second curve axis"), valve rounds 1-3 (docs and backlog 65/66
from the same day), 2026-09-05-curve-morph-design.md (whose morph pin
is this feature's embryo), JUNCTION_OPERATING_POINT.md.

## 1. Motivation — two probes bought this knowledge

Round 1 put the lip resonator (bandpass near f0) IN SERIES in the loop:
verdict generic reed. Mechanism: a series filter culls harmonics the
junction regenerates identically each pass; equilibrium restores the
curve's own spectrum.

Rounds 2-3 coupled the resonator into Junction.drive (operating-point
modulation, closure floor, opening cap): verdict "not a hint of brass."
Mechanism: drive scales the WINDOW of one fixed curve; the family of
transfer shapes reachable by scaling one curve is one-dimensional, and
its settled spectrum stays in the reed basin.

Conclusion, stated as the ceiling it is: **a memoryless single-input
curve cannot be a lip, however its input or scale is modulated.** The
lip's flow nonlinearity is a genuine function of two things — pressure
across the lips AND lip opening — and those enter with different signs
and shapes. The junction needs `y = f(x, s)`: x the circulating
pressure signal (as today), s a control-rate-to-audio-rate STATE input
(the lip resonator, or anything else — this is a general primitive, not
a brass-only bolt-on: jet deflection for flutes, glottal area for
voice, bow force for the hysteresis sibling's soft cousin).

## 2. Step zero costs nothing: the morph pin IS a 2-point slice

The curve-morph work (09-05) left ShaperSource with values2/segs2 and a
`morph` param that is a real ValueSource pin advanced PER SAMPLE
(shaper_source.h next() — morphCur_ updates every tick; map() lerps
breakpoints in point space). Therefore:

    curve A = lip nearly closed, curve B = lip wide open,
    morph <- Valve -> normalize to 0..1 (Shaper rescale, one node)

is `f(x, s)` with a 2-point basis in s — TODAY, zero engine code. This
wiring is the spec's validation gate:

- If the best 2-point slice moves the sound out of the reed basin,
  the full feature is justified and the engine work below buys finer
  control (more basis curves, proper normalization, editor support).
- If NO curve pair driven by the lip state escapes the reed basin,
  the 2D premise itself is wrong and we stop before writing C++ —
  the ceiling is somewhere else (and the hysteresis sibling becomes
  the only funded path).

The probe round for this gate reuses the valve2 generator's chain
(Valve keytracked near f0) with morph as the coupling target instead of
drive. Curve pairs from the editor's physical presets (reed1 -> lip,
closed-clamp -> open-slam, symmetric -> origin-asymmetric r5c shapes).

## 3. Engine feature (v1), if the gate passes

- Shaper grows a curve STACK: `curves[]` = N point-lists (N small, 2-8)
  with matching structure, plus `state` — a ValueSource pin mapped
  0..1 across the stack; evaluation lerps the two bracketing curves in
  point space (the morph machinery generalized from 2 fixed endpoints
  to N stations). values/values2 remain as the N=2 degenerate case —
  existing patches byte-identical, null gate green.
- `state` and `morph` are the same mechanism; morph becomes an alias
  for state over the N=2 stack (one concept, not two competing pins).
  Hysteresis mode (sibling spec) keeps its discrete A/B switch; a
  patch uses stack-lerp OR hysteresis, not both (same v1 exclusivity).
- Per-segment interp overrides lerp as today (signed log-power rule
  from curve-morph); station curves share segment structure, enforced
  at load like segs2.
- UI: the A/B editor generalizes to a station strip; the morph-pin A/B
  workflow already shipped is the N=2 case, so the editor cost is
  incremental, not new.
- Real-time: per-sample cost = one extra lerp per breakpoint pair vs
  morph today, no allocation. Same ADAA caveat as hysteresis: a
  state-varying curve is not a fixed memoryless map, so the aliasing
  ladder is quantify-at-192k first, keytrack knots second (Matt's
  damp-curve top-octave adjustment applies to this family's C5/C6
  problems regardless), oversampled loop region last.

## 4. Compensation debt to settle FIRST (backlog 66)

Both this feature's wiring and valve2's already trip the DelayLine
compensation walk: it descends param pins, so a state/morph chain that
reaches the tap pollutes the member list (r090's +300 cents) and can
evict real members past kMaxMembers=8 (r100's -85 cents). The morph
probe of §2 wires Valve -> morph and WILL route control back to the
tap the same way. Fix (or at least bound) backlog 66 before the gate
round, else every measurement needs the direct-tap workaround and a
tuning check. Proposed minimal fix: walk input_descriptors only —
signal flows through inputs; drive/morph/frequency/amplitude are
control. One audit pass over existing loop patches to confirm no loop
compensates through a param pin today.

## 5. Validation

1. Null gate 196/196 (feature absent everywhere).
2. §2 gate round verdict from Matt's ears (the whole point).
3. Unit: state=const slices reproduce single-curve evaluation exactly;
   state sweep 0..1 on a 2-stack matches today's morph output
   byte-for-byte (it IS that path).
4. In-loop: criticals bisected at the shipped root (round-1 lesson),
   pitch verified per note against the control before any queue is
   called ready (round-3 lesson).

## 6. Open questions for Matt

1. Gate placement: run §2's zero-code round before reviewing v1
   scope, or review both specs now and let the gate only decide
   priority? (My default: run the gate as soon as backlog 66 is
   bounded; it changes what v1 needs to be.)
2. Station count N: is 2-8 the right cap, or is 2-3 enough for
   everything you can imagine authoring?
3. Naming: `state` vs `morph` as the surviving pin name in UI copy.

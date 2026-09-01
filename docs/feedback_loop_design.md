# Feedback loops: tap edges, DelayLine, Shaper

Status: SPEC — agreed with Matt 2026-08-31 (interactive brainstorm).
Lineage: KS-bow thread (backlog 34/34a), shape editor
(docs/shape_editor_design.md), breath/tone coupling principle.

## 1. Motivation

Every excitation experiment into a linear resonator loop ends at "pluck":
a one-shot burst circulates and decays, and nothing in the loop can create
anything new. What separates bowed/blown/buzzed from plucked — and what
separates *possible new instruments* from resynthesis — is a nonlinearity
inside the loop: a curve the circulating signal passes through every
sample (bow friction, reed table, jet deflection). Classical physical
modeling only ever uses the three or four curves nature provides. MForce
now has an editor whose job is drawing arbitrary curves.

This spec makes feedback loops first-class in the ValueSource graph so a
drawn curve can sit inside one. Goals, in priority order:

1. **Novelty**: arbitrary drawn nonlinearities inside resonant loops —
   period doubling, subharmonics, multiphonics, self-oscillation, chaos.
   The curve is data (breakpoints + seed), so the space is sweepable:
   batch-generate curves, render, filter by novelty metric, listen.
2. **Physical models for free**: bow = friction curve, clarinet = reed
   curve, flute = jet sigmoid, on the same skeleton.
3. **Modular ethos**: loops are built from free nodes in the main graph,
   not inside a container. Every existing node becomes loop-legal.

## 2. Concepts

**Tap edge.** A new kind of wire, originating at a small *tap pin* on
every node (lower right, alongside the output pin). A normal wire pulls a
fresh sample (`next()`); a tap wire reads what its source computed last
tick (`current()`) without advancing it.

**Cycle legality.** A cycle in the graph is legal if and only if at least
one of its edges is a tap edge. Cycles of only normal edges remain
illegal (today they are unrepresentable in the file format and unguarded
in the UI; see §5.3).

**The z⁻¹ is positional, not a mode.** A tap is implemented by the
existing RefSource read (`current()` without advancing). A forward ref
reads a value its advancer computed earlier *this* tick; a tap consumer
necessarily evaluates *before* its source in the tick order (the forward
path of the cycle forces this), so the same read sees *last* tick's
value. The one-sample delay falls out of topology; no delay code exists
in the tap itself.

**Taps are exactly one sample.** Anything deeper is a DelayLine's job.
This keeps exactly one place where buffers live and one meaning for a
tap: close the loop legally. (Decided 2026-08-31; a depth-N tap would be
a hidden, integer-only, unmodulatable duplicate of DelayLine.)

## 3. Engine changes

### 3.1 RefSource `guard` flag

`RefSource` gains `bool guard{false}`. When set, reads are armored:
NaN/inf scrubbed to 0, value clamped (±8). No new class; guarded-vs-not
is the only behavioral difference between a tap and today's shared-source
ref. Default false keeps every existing patch byte-identical.

The guard makes exploration safe: a runaway drawn curve saturates
audibly instead of poisoning the graph with NaN or blowing float range.

### 3.2 Tap resolution in the loader (second pass)

File form: `{"tap": "<nodeId>"}`, accepted anywhere `{"ref": ...}` is.
Unlike refs, taps may point *forward* in nodeOrder (upstream in the
graph) — that is their purpose. The loader therefore resolves them in a
second pass: pass 1 builds nodes as today, installing a placeholder
guarded RefSource (target null, reads 0) for each tap param; pass 2
binds targets once every node exists. An unresolved tap id is a load
error, same as an unresolved ref.

Tap edges are excluded from the ref-usage counting that drives the
advancer model (§3.3) and from starved-ref promotion's edge enumeration
(a tap never advances, so it can neither be an advancer nor starve a
forward ref; promote_starved_refs treats tap edges as invisible).

### 3.3 Starved-node advancement (the advance list)

Tap edges do not advance their source. A node consumed *only* by tap
edges is never ticked by the pull — the loop is silently dead. Example:
loop `sum → shaper → delay —tap→ sum` with audio taken from the shaper:
the delay's only consumer is the tap.

Fix, same shape as starved-ref promotion: at load/build time, collect
every node whose consumers are all tap edges into a per-patch **advance
list**. The voice ticks these nodes once per sample **after** the root
pull (order matters: their tap consumers must read the previous value
first, and their own forward inputs — already advanced this tick by the
main pull — are reached through the normal multi-consumer RefSource
wrap, so nothing double-advances). `prepare()` is likewise extended to
advance-list nodes, which sit outside the root cone by definition.

Advance-list membership is decided from the JSON edge enumeration (the
proven approach from promote_starved_refs — the built graph is not fully
walkable), in nodeOrder for determinism.

Interaction with starved-ref promotion: an advance-list node ticks every
sample, so it is a *live advancer* even though it is not JSON-reachable
from the output. Promotion's reachability check must count advance-list
nodes as sighted — otherwise a shared source whose advancing consumer
sits on the advance list looks starved, gets promoted, and is then
advanced twice per sample (once by the promoted view, once by the list).

### 3.4 DelayLine node (new)

The tunable resonator backbone. Pure delay: no internal feedback, no
damping — those are the graph's job now.

- Params (pins): `source` (input), `frequency` (hz → length =
  sampleRate/frequency), `ratio` (default 1.0; multiplies the period —
  inharmonic pairs from one Note frequency).
- Fractional read head, linear interpolation; per-sample length
  modulation allowed (mild artifacts accepted in v1).
- Buffer allocated at construction for a floor of 20 Hz at the session
  rate; frequencies below floor clamp. No heap in the render loop.
- Settings: none in v1. Registry + generic wiring; array-free.

### 3.5 Shaper node (new)

Per-sample drawn-curve transfer: `y = shape(drive * x)`.

- Params (pins): `source` (input), `drive` (default 1.0; pre-lookup
  input scale — the "bow pressure" analog: pushing the signal into a
  different region of the curve).
- Shape storage: `values` array of absolute (x, y) breakpoints (NOT the
  SegmentSource delta form), ascending x, domain nominally x ∈ [-2, 2],
  y ∈ [-1.5, 1.5] (drawable margins beyond the working ±1). Evaluation:
  piecewise between breakpoints through SmoothnessInterpolator with a
  `smoothness` pin (default 0.5), clamped to the end values outside the
  drawn range. Identity default: [(-1,-1), (1,1)].
- Open-loop use is free bonus (waveshaper/distortion).
- Deferred, not in v1: morph pin between two drawn curves (physics
  evolving mid-note). Flagged because the wiring (two values arrays)
  touches the array plumbing; revisit after first loop patches exist.

## 4. UI changes

### 4.1 Tap pin and wire

Every node face gets a tap pin, lower right beside the output pin,
visually small/secondary. Tap wires draw in a distinct style (dashed
and/or distinct color — NOT gold, which means dynamic pins). Wiring a
tap creates no visible node: the guarded RefSource is plumbing created
by update_all_dsp, exactly like the auto-RefSource for shared outputs
today. Save writes `{"tap": "<label>"}`; load reconstructs the wire.
Round-trip stays honest: what you wired is what saves.

### 4.2 Cycle validation

Link creation gains the legality check: a normal wire that would close a
cycle of only-normal edges is refused with a status message pointing at
the tap pin ("close loops through a tap"). A tap wire is always legal.
(This is a new check — today nothing in the UI prevents a cycle; the
file format's ordered construction is the only guard.)

### 4.3 Shape editor: third client

The Shaper's curve opens in the shape editor (double-click the node
preview, same as CurveNode/SegmentSource):

- Linear x axis, four-quadrant canvas (negative x and y are the point).
- Points are absolute (x, y) — no delta conversion, no timeMode.
- Same interaction vocabulary; engine overlay evaluates through the same
  SmoothnessInterpolator path the node uses, so drawn vs rendered stay
  distinguishable.
- Node face shows the standard shape preview; Properties shows the
  points table like SegmentSource.

### 4.4 Editor preview semantics

Node waveform previews pull per-node cones; a tap inside the cone reads
current() and does not recurse, so previews of loop members are safe.
Advance-list semantics apply only to voice rendering; a preview rooted
mid-loop may show an unadvanced loop tail. Accepted for v1.

## 5. Non-goals / deferred

- **Depth-N taps** — rejected outright (§2).
- **Pitch compensation** — total loop delay = DelayLine + one sample per
  tap + filter group delay, so naive loops ring slightly flat. Ship
  without compensation; add (as a DelayLine setting subtracting a
  latency estimate) only when a loop patch earns instrument-matching
  treatment. Novelty lane does not care.
- **Shaper morph pin** — deferred (§3.5).
- **Container/group loop node** — grouping an assembled loop into a face
  already works via Groups; a dedicated container adds nothing in v1.
- **Block-rate tier** — unrelated standing question (backlog 26b); taps
  neither need nor advance it.

## 6. Validation

- **Null gate**: guard defaults false, taps absent from every existing
  patch, loader second pass only fires on `"tap"` keys → corpus must
  stay byte-identical. Run the gate pre-commit (standing rule).
- **Unit tests** (engine): (a) tap z⁻¹ — two-node loop, assert one-sample
  delay; (b) starved advancement — audio out mid-loop, assert loop tail
  advances; (c) guard — NaN/inf injected, output stays finite/clamped;
  (d) DelayLine tuning — length tracks frequency pin within interp
  error; (e) Shaper — table lookup matches SmoothnessInterpolator
  reference, clamp past ends; (f) loader round-trip — tap JSON survives
  save/load through both loaders.
- **Reference patches** (patches/baselines/): `loop_bowed.json` — creak
  atom + tap-closed friction-curve loop (the bowed-string skeleton);
  `loop_selfosc.json` — no excitation, curve with gain around zero,
  self-oscillates from the noise floor. Both render via mforce_cli and
  live in the roundtrip gate.

## 7. Build order (increments, shape-editor style)

1. RefSource guard + engine tap resolution + advance list + unit tests
   (a)(b)(c)(f). Null gate frozen before/after.
2. DelayLine node + test (d).
3. Shaper node + test (e); Properties table editing works before the
   editor client exists.
4. UI tap pin, wire style, cycle check, save/load round-trip.
5. Shape editor Shaper client (four-quadrant canvas).
6. Reference patches + first novelty sweep (curve-space batch render,
   existing novelty metric).

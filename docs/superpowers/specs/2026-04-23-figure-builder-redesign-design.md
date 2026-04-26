# FigureBuilder redesign — design spec

Round-1 redesign of `FigureBuilder` and its orbit. Split a monolithic,
drifted struct into three purpose-driven pieces: `RandomFigureBuilder`
(stochastic generation), `ShapeFigures` (pure shape templates), and
`FigureTransforms` (operators on existing figures).

---

## Context

Current `FigureBuilder` (`engine/include/mforce/music/figures.h:726`) is a
monolithic struct conflating three jobs:
1. Stochastic figure generation from constraints.
2. Named shape templates (scalar runs, arcs, neighbor motions, etc.).
3. Transforms on existing figures (invert, retrograde, stretch, combine, etc.).

All render paths for *generated* (non-literal) melody go through it, and
non-literal renders are not yet musical. The stochastic path in particular
has regressed: it does its own inline pulse randomization, duplicating
`PulseGenerator`'s richer musical-duration alphabet. Several shape
templates encode walker-specific assumptions (triadic-skipping, leap-outline
character) that only make sense under a diatonic walker. Some shapes are
phrase-level intent masquerading as figure-level shape. And the
`units[0].step = 0` convention — required by `FigureConnector::leadStep`
at phrase-assembly time — is violated by the stochastic path.

Round 1 replaces the monolith cleanly while preserving existing behaviors
at phrase-assembly and rendering layers. Future rounds iterate on the
randomized transforms (`complexify`, `vary`) and tune the weighted
shape-selection pool.

---

## Bible rules

Cross-cutting discipline for the whole Figure subsystem:

1. **Figures never use the word "note."** Figures are abstract — pulse +
   step. Notes are realized pitches that appear after a Figure is walked
   against a Scale/Chord. In Figure-land, the rhythmic atom is a `pulse`
   (or a `FigureUnit`), not a note.
2. **Fail fast on contradictions.** Impossible or conflicting constraints
   throw. No silent relax, no "best effort."
3. **`step` is an `int`, period.** Interpretation belongs to the stepper
   (scale-degree, chord-tone, microtonal). The Figure layer is
   walker-free at the data level.
4. **"Pure shape" principle for Figures.** Meter, downbeat, chord
   context, preceding context, destination, voice-leading — all belong
   at Phrase level, not Figure level. A Figure is a pattern of motion.
5. **`units[0].step == 0` always.** The first unit of a figure has
   `step = 0`. Connectors (`FigureConnector::leadStep`) add to it at
   phrase-assembly time (`composer.h:1272`). Every figure producer —
   shape builders, random generators, transforms, DURN parser,
   motif-JSON loader — must honor this.

---

## Structural split

`FigureBuilder` monolith is replaced by three purpose-driven pieces:

| Component | Role | Form |
|---|---|---|
| `RandomFigureBuilder` | Stochastic figure generation from constraints | Class; owns `PulseGenerator` + `StepGenerator` |
| `ShapeFigures`        | Named pure-shape templates (run, neighbor, etc.) | Free functions in `shape_figures::` namespace |
| `FigureTransforms`    | Operators on existing figures (invert, combine, complexify, etc.) | Free functions in `figure_transforms::` namespace; `Randomizer&` passed in to randomized ones |

Rationale for `FigureTransforms` as free functions (not a class):
- Pure transforms (`invert`, `retrograde_steps`, etc.) need no state.
- Randomized transforms (`complexify`, `embellish`, `vary`) need only a
  `Randomizer&`, not a `PulseGenerator` or `StepGenerator` — they
  subdivide/insert within the figure's own vocabulary, not from a musical
  pulse alphabet. No duplicate-generator-holder problem.

Rationale for `ShapeFigures` as free functions:
- Shape templates are pure — no state, no rng, no walker assumptions in
  the emitted step values.
- Stateless namespace is friendlier to call sites and tests than a
  dedicated class.

---

## Generator hierarchy

Restores the legacy separation of concerns that current code regressed:

```
PulseGenerator      — rhythm source       — owns rng
StepGenerator       — pitch-shape source  — owns rng
RandomFigureBuilder — assembler           — owns a PulseGenerator + StepGenerator
```

### Rng ownership

| Holds rng? | Type | Role |
|---|---|---|
| ❌ | `StepSequence`   | data container (list of ints) |
| ❌ | `PulseSequence`  | data container (list of floats) |
| ✔️ | `StepGenerator`  | produces StepSequences |
| ✔️ | `PulseGenerator` | produces PulseSequences |

`RandomFigureBuilder` does **not** hold a standalone `Randomizer`. It owns
a `PulseGenerator` and a `StepGenerator`; their rngs collectively are its
state. One seed feeds both deterministically at construction time.

### Restoration steps

- Remove the inline `rng.range(minPulse, maxPulse)` pulse randomization
  currently in `FigureBuilder::build(noteCount)`. Route through
  `PulseGenerator`.
- `PulseGenerator` gains a `generate_count(int count, ...)` method
  paralleling its existing `generate(float totalBeats, ...)`.
- `StepGenerator` is already the authoritative step source; continue
  using its existing methods.
- `RandomFigureBuilder` stops carrying three ad-hoc rngs
  (`fb.rng`, `fb.stepGen.rng`, anything else); two generators, two rngs,
  done.

---

## `Constraints` struct

All constraint axes live here. Fields are optional — only set what you
want to pin down. The generator satisfies everything or throws.

```cpp
struct Constraints {
    std::optional<int>   count;          // number of FigureUnits
    std::optional<float> length;         // total beats
    std::optional<int>   net;            // net step movement
    std::optional<int>   ceiling;        // running step-position ceiling
    std::optional<int>   floor;          // running step-position floor
    std::optional<float> defaultPulse;   // bias center for pulse generator
    std::optional<float> minPulse;       // smallest permitted pulse
    std::optional<float> maxPulse;       // largest permitted pulse
    // future: preferStepwise, preferSkips, maxLeap, maxStep, targetContour
};
```

Note on naming: `maxFloor` and `maxPeak` on the old struct were confused
(a floor is already a minimum; a peak is already a maximum). Replaced by
`ceiling` and `floor`.

---

## `RandomFigureBuilder` — method set

```cpp
class RandomFigureBuilder {
    PulseGenerator pulseGen;
    StepGenerator  stepGen;
public:
    explicit RandomFigureBuilder(uint32_t seed);

    MelodicFigure build           (const Constraints& c);
    MelodicFigure build_by_count  (int count,               const Constraints& c = {});
    MelodicFigure build_by_length (float length,            const Constraints& c = {});
    MelodicFigure build_by_steps  (const StepSequence& ss,  const Constraints& c = {});
    MelodicFigure build_by_rhythm (const PulseSequence& ps, const Constraints& c = {});
    MelodicFigure build_singleton (const Constraints& c = {});
};
```

- `build(c)` is the authoritative entry point — satisfy all set
  constraints or throw.
- Convenience methods are sugar that merge their arg into a local
  `Constraints`, set the named field, and delegate to `build(c)`.
- Constructor takes one seed and shards it deterministically to seed
  the two generators.

---

## `build(c)` algorithm (round 1)

Authoritative entry point. Algorithm:

1. **Resolve count.** From `c.count` if set, else derive from
   `c.length / c.defaultPulse` (rounded), else random in a default range
   (e.g. 4-8).
2. **Feasibility check.** E.g. `count * c.minPulse > c.length` → throw.
   `count < 1` → throw. Any other structurally impossible combo → throw.
3. **Filter feasible shapes by count (and other structural constraints).**
   Not all shapes apply at all counts. Minimum unit counts:
   - `run`: 2
   - `repeats`: 1
   - `neighbor`: 3
   - `leap_and_fill`: 2
   - `arc`: 3
   - `zigzag`: 3
   - `wander` (fall-through): always feasible

   For count=2, the candidate pool reduces to `run`, `repeats`,
   `leap_and_fill` (no fill), `wander`. For count=1, only `repeats` or
   delegation to `build_singleton`.
4. **Weighted random selection** among feasible shapes. `wander` is the
   **fall-through** — it sits at the end of the coin-flip chain and runs
   if no shape branch is selected or if the filter rejects all shapes.
5. **Parameterize + build.** Each `try_<shape>(count, c)` helper picks
   shape parameters to meet `c` (direction from `net` sign, extent from
   count, pulse from `defaultPulse`, etc.), then calls the corresponding
   `shape_figures::` function.
6. **Post-validate** against `c.ceiling`/`c.floor`/`c.net`. On violation,
   **retry up to 3 times**. After 3 failures, throw. (`wander` is the
   honest random-walk; same retry discipline applies.)

### Round-1 suggested weights

| Shape | Weight |
|---|---|
| arc            | 0.30 |
| run            | 0.20 |
| zigzag         | 0.10 |
| neighbor-pair  | 0.10 |
| leap_and_fill  | 0.10 |
| wander (fall-through) | 0.20 |

Weights are initial guesses; tune with rendering feedback. Shape weights
will become a first-class tuning surface later (likely via `Constraints`
extensions or a separate `GenerationPreferences` struct); not exposed in
round 1.

---

## Convenience-method strictness

If the caller's `c` has any field that the convenience method would set,
**throw — regardless of whether values would match.** Presence of the
redundant field is itself the error signal. If the caller wants to set
the axis via `c`, they should call `build(c)` directly.

| Method | Throws if `c` has |
|---|---|
| `build_by_count(n, c)`       | `c.count` set |
| `build_by_length(L, c)`      | `c.length` set |
| `build_by_steps(ss, c)`      | `c.count` OR `c.net` set |
| `build_by_rhythm(ps, c)`     | `c.count` OR `c.length` set |
| `build_singleton(c)`         | `c.count` set |
| `build(c)`                   | never (accepts any combination) |

Impossible-constraint checks (e.g. `count=6, length=2.0, minPulse=0.5` →
infeasible) live inside `build(c)` and throw there.

---

## `FigureTransforms` — method set

Free functions in a `figure_transforms::` namespace. Deterministic
transforms are pure. Randomized transforms take `Randomizer&` as an
explicit parameter. All return a new `MelodicFigure`; input is always
`const MelodicFigure&`.

```cpp
namespace figure_transforms {

  // --- Deterministic (no rng) ---

  MelodicFigure invert           (const MelodicFigure& fig);
  MelodicFigure retrograde_steps (const MelodicFigure& fig);

  MelodicFigure combine   (const MelodicFigure& a, const MelodicFigure& b,
                           const FigureConnector& fc = {});
  MelodicFigure combine   (const MelodicFigure& a, const MelodicFigure& b,
                           int leadStep, bool elide = false);         // sugar

  MelodicFigure replicate (const MelodicFigure& fig, int repeats,
                           int leadStep = 0, bool elide = false);
  MelodicFigure replicate (const MelodicFigure& fig,
                           const std::vector<int>& connectorSteps);   // total copies = 1 + size

  MelodicFigure prune             (const MelodicFigure& fig, int count,
                                   bool from_start = false);
  MelodicFigure set_last_pulse    (const MelodicFigure& fig, float duration);  // absolute
  MelodicFigure adjust_last_pulse (const MelodicFigure& fig, float delta);     // relative

  // --- Specialty / composite (deterministic) ---

  MelodicFigure replicate_and_prune(const MelodicFigure& fig,
                                    const std::vector<int>& connectorSteps,
                                    int pruneAt1,
                                    int pruneAt2 = 0);

  // --- Micro-insertions / subdivisions (deterministic) ---
  // Building blocks intended for use by complexify / vary.

  MelodicFigure split        (const MelodicFigure& fig, int splitAt,
                              int repeats);
  MelodicFigure add_neighbor (const MelodicFigure& fig, int addAt,
                              bool down = false);
  MelodicFigure add_turn     (const MelodicFigure& fig, int addAt,
                              bool down = false);

  // --- Randomized (Randomizer& passed in) ---

  MelodicFigure complexify (const MelodicFigure& fig, Randomizer& rng, float amount);
  MelodicFigure embellish  (const MelodicFigure& fig, Randomizer& rng, int count);
  MelodicFigure vary       (const MelodicFigure& fig, Randomizer& rng, float amount);

} // namespace figure_transforms
```

### Semantics

- **`invert`** — negate all steps. `units[0].step = 0` is preserved
  (−0 = 0).
- **`retrograde_steps`** — reverse pitch sequence in time; pulses do
  *not* reverse (hence the `_steps` suffix). Algorithm: new
  `units[0].step = 0`; for `i ≥ 1`, new `step[i] = -(old step[count-i])`.
  Pulses keep their order (new pulse[i] = old pulse[i]).
  - Example: `[0,+1,+1,-2]` → `[0,+2,-1,-1]`.
- **`combine(a, b, fc)`** — canonical. Composes from atomic transforms:
  1. If `fc.elideCount > 0`: `a = prune(a, fc.elideCount)`.
  2. If `fc.adjustCount != 0`: `a = adjust_last_pulse(a, fc.adjustCount)`.
  3. Set `b.units[0].step = fc.leadStep`.
  4. Concatenate `a` then `b`.
- **`combine(a, b, leadStep, elide)`** — sugar for the common case.
  `elide=true` means elide 1 unit. Builds a `FigureConnector` and
  delegates.
- **`replicate(fig, repeats, leadStep, elide)`** — iteratively combines.
- **`replicate(fig, connectorSteps)`** — produces
  `1 + connectorSteps.size()` copies; `connectorSteps[i]` is the
  leadStep joining copy `i` onto what precedes it.
- **`prune(fig, count, from_start)`** — remove `count` units from the
  end (default) or the start. When `from_start=true`, the new first
  unit's original step value is lost (forced to 0 to honor the
  convention).
- **`set_last_pulse(fig, duration)`** — set last unit's duration
  absolutely.
- **`adjust_last_pulse(fig, delta)`** — change last unit's duration by
  `delta` (signed). Clamps at 0 to avoid negative durations (matches
  `FigureConnector::adjustCount` behavior).
- **`replicate_and_prune(fig, connectorSteps, pruneAt1, pruneAt2=0)`** —
  specialty composite. Replicates `fig` per `connectorSteps` (total
  copies = `1 + connectorSteps.size()`), then prunes the last unit from
  the copy at index `pruneAt1` (1-indexed) and, if `pruneAt2 != 0`, also
  from the copy at index `pruneAt2`. `0` in either slot means "no prune
  at this slot." Musical use: acceleration toward a climax — shortening
  copies near the end tightens the rhythmic pace as the sequence
  ascends/descends.
  - Throws if a prune index exceeds total copies.
  - Throws if pruning would leave a copy with 0 units.
- **`split(fig, splitAt, repeats)`** — replace unit at `splitAt` with
  `repeats` sub-units, each of duration `original.duration / repeats`.
  The first sub-unit inherits the original's step; the rest have
  `step=0`. Length preserved; unit count grows by `repeats - 1`.
  Semantics: a held pulse becomes a series of rapid repeated pulses
  (tremolo-like subdivision without changing pitch contour).
- **`add_neighbor(fig, addAt, down=false)`** — replace unit at `addAt`
  with a 3-unit neighbor motion of equal total duration:
  - sub-unit 0: half duration, original step (arrives at the same pitch)
  - sub-unit 1: quarter duration, `step = down ? -1 : +1` (neighbor)
  - sub-unit 2: quarter duration, `step = down ? +1 : -1` (return)

  Length preserved; unit count grows by 2. Collision note: unrelated to
  the `Neighbor` ornament type (ornaments are metadata on a single unit;
  this inserts motion).
- **`add_turn(fig, addAt, down=false)`** — replace unit at `addAt` with
  a turn motion. Candidate round-1 implementation: a 4-unit figure
  realizing upper-neighbor / main / lower-neighbor / main around the
  original pitch (durations distributed to preserve total length).
  Length preserved; unit count grows. Collision note: unrelated to the
  `Turn` ornament type.
- **`complexify(fig, rng, amount)`** — length preserved. Target unit
  count ≈ `(1 + amount) * original count`. Built on top of the
  deterministic micro-insertions (`split`, `add_neighbor`, `add_turn`):
  rng picks which units to complexify and which mechanism to apply.
  Round-1 implementation chooses uniformly among the three mechanisms;
  tuning deferred.
- **`embellish(fig, rng, count)`** — attach `Ornament` and/or
  `Articulation` to `count` existing units (chosen by rng). Does not
  change unit count or length. Operates on the per-unit fields
  `FigureUnit::articulation` and `FigureUnit::ornament`
  (`figures.h:474-475`).
- **`vary(fig, rng, amount)`** — jitter existing durations and/or steps
  by `amount`. Round-1 implementation: conservative — small
  perturbations on a random subset. Exact behavior tuned with rendering
  feedback.

---

## `ShapeFigures` — method set

Free functions in a `shape_figures::` namespace. Pure ordinal shape
templates — no state, no rng, no walker assumptions in the emitted step
values. Returns `MelodicFigure`. All shapes honor `units[0].step = 0`.

### Catalog review outcome

Reviewed the 14 shape functions currently on `FigureBuilder`. Kept 6;
culled 8.

**Culled** (8):
- `triadic_outline`, `fanfare` — walker-specific (emit step values whose
  *musical character* only emerges under a diatonic walker).
- `held_note` — redundant with `RandomFigureBuilder::build_singleton`.
- `cadential_approach`, `anacrusis`, `sigh`, `suspension`, `cambiata` —
  phrase-level intent masquerading as shape. The shapes themselves are
  either trivially reproducible from the surviving primitives
  (e.g. `sigh` = 2-unit descending `run`) or are better expressed at
  phrase level via `Constraints.net` targeting and rhythmic decisions. A
  phrase wanting a PAC arrival computes `net = target_degree - cursor`
  and asks RFB to produce a figure meeting that constraint.

**Kept** (6):

```cpp
namespace shape_figures {

  MelodicFigure run           (int direction, int count,
                               float pulse = 1.0f);
  MelodicFigure repeats       (int count, float pulse = 1.0f);
  MelodicFigure neighbor      (bool upper, float pulse = 1.0f,
                               bool doublePulseMain = false);
  MelodicFigure leap_and_fill (int leapSize, bool leapUp,
                               int fillSteps = 0, float pulse = 1.0f);
  MelodicFigure arc           (int direction, int extent,
                               int returnExtent = 0, float pulse = 1.0f);
  MelodicFigure zigzag        (int direction, int cycles,
                               int stepSize = 2, int skipSize = 1,
                               float pulse = 1.0f);

} // namespace shape_figures
```

### Renames applied (bible rule #1)

- `scalar_run` → **`run`** (terser).
- `scalar_return` → **`arc`** (terser, more musical).
- `repeated_note` → **`repeats`**.
- `neighbor_tone` → **`neighbor`**.

### Semantics

- **`run(direction, count, pulse)`** — emits `count` units, first with
  `step=0`, rest with `step=sign(direction)`.
- **`repeats(count, pulse)`** — emits `count` units, all `step=0`.
- **`neighbor(upper, pulse, doublePulseMain)`** — 3 units: main,
  neighbor, return. `upper=true` emits `0, +1, -1`; `false` emits
  `0, -1, +1`. If `doublePulseMain`, the main and return units are
  `2*pulse`.
- **`leap_and_fill(leapSize, leapUp, fillSteps, pulse)`** — emits
  `1 + 1 + fillSteps` units. First unit `step=0`; second unit
  `step = leapSize * (leapUp ? +1 : -1)`; remaining `fillSteps` units
  `step = -sign(leap)` (stepwise recovery in opposite direction).
  `fillSteps=0` defaults to `leapSize - 1`.
- **`arc(direction, extent, returnExtent, pulse)`** — arch or inverted
  arch. `1 + extent + returnExtent` units. First `step=0`; `extent`
  units in `sign(direction)` direction; `returnExtent` units in opposite
  direction. `returnExtent=0` defaults to `extent` (full return).
- **`zigzag(direction, cycles, stepSize, skipSize, pulse)`** — emits
  `1 + 2 * cycles` units. Alternates `step=stepSize*dir`,
  `step=-skipSize*dir` per cycle.

---

## Migration plan (summary)

Round-1 implementation expected shape:

1. Add `PulseGenerator::generate_count(int count, float defaultPulse)` to
   parallel its existing `generate(float totalBeats, ...)`.
2. Create `shape_figures::` namespace and port the 6 kept shapes, renamed
   and honoring `units[0].step = 0`.
3. Create `figure_transforms::` namespace with the 11 deterministic + 3
   micro-insertion + 3 randomized functions.
4. Create `RandomFigureBuilder` class owning `PulseGenerator` +
   `StepGenerator`; implement `build(c)` with the feasibility-filter
   + weighted-coin-flip + 3-retry algorithm.
5. Migrate call sites off `FigureBuilder` onto the new API.
   - `shape_strategies.h` (the 16 `Shape*Strategy` classes wrapping FB
     shapes) becomes redundant — delete.
   - `default_strategies.h` and `templates.h` sites that instantiate
     `FigureBuilder fb(seed+N)` migrate to the new API.
6. Delete the old `FigureBuilder` struct.
7. Re-baseline goldens (K467 renders will drift — the pulse-alphabet
   change alone guarantees this; different seed lineages add to it).
   This is expected and acceptable: the old rhythms were not musical
   targets worth preserving bit-for-bit.

### Parked / follow-on

- **`note` → `pulse` / `unit` rename sweep** — `single_note`,
  `note_count`, `FigureUnit` comment references, etc. across the
  codebase. Not in round 1 scope; do as a dedicated cleanup pass.
- **`pitch_reader.h` vs `pitch_walker.h` consolidation** — one redundant
  file introduced 2026-04-22. Fold into a future cleanup.
- **`runningReader` naming** — revisit when we get to Phrase/Passage
  strategy redesign.
- **Dual-constraint algorithm** — for cases like `build_by_count(n,
  {.length=L})`, approach TBD: (a) generate count then adjust durations,
  or (b) generate length then adjust count by splitting/joining/deleting.
  Implementation detail; decide when coding.
- **Hard constraints vs preferences** — as `Constraints` grows, consider
  splitting *must-satisfy* from *hints*.
- **Shape weight tuning** — round-1 weights are initial guesses; next
  round iterates from listening.
- **Phrase-level redesign** — the real musicality problem lives at
  Phrase level, not Figure level. This redesign is the foundation; the
  Phrase pass follows.

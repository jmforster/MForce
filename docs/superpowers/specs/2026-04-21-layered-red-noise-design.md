# LayeredRedNoiseSource — Design Spec

**Status:** Draft for review.
**Author:** Claude (DSP/UI).
**Date:** 2026-04-21.

---

## Problem

Authoring a rich random modulator currently requires N separate
`RedNoiseSource` nodes plus a summing/combining node. Tedious enough
that users (Matt) will skip it and just use one RedNoise layer,
missing out on the richer character of multi-scale noise (slow drift
+ medium wobble + fast flutter, all summed).

Also, when used as the `var` input on a `VarSource` for pitch/param
modulation, a single RedNoise at one frequency produces predictable
periodic-ish variation. Summing multiple RedNoises at different
frequencies gives a more organic, "section"-style humanization.

## Goal

One convenience DSP node — `LayeredRedNoiseSource` — that internally
owns N `RedNoiseSource` children and emits their straight sum per
sample. Table-inspector-edited `frequency` + `amplitude` per layer.
No other exposed knobs beyond the table and total row count.

Specifically targeted at driving modulation inputs (e.g. `VarSource`,
`Range`, `MultiplexSource`-internal variation). Not primarily for
audio-rate use.

## Non-goals

- Per-layer density / smoothness / ramp-variation / boost / other
  RedNoise params. Fixed internal defaults. If a user needs per-row
  control over those, they should build the layered graph manually
  with N `RedNoiseSource` nodes.
- Rendering as a "convenience expansion" that writes out N nodes +
  a summer on save. The node stays monolithic on disk.
- Automatic headroom / peak guard. Matt's note: "headroom not an issue
  (if user uses it for noise they'll have to pick appropriate
  amplitudes)". Direct sum; caller's problem if peaks matter.

## Design

### Shape

Single DSP class `LayeredRedNoiseSource` in
`engine/include/mforce/source/layered_red_noise_source.h`.

```cpp
struct LayeredRedNoiseSource final : ValueSource {
    const char* type_name() const override { return "LayeredRedNoiseSource"; }
    SourceCategory category() const override { return SourceCategory::Generator; }

    // No input_descriptors (no wire-in pins)
    // No param_descriptors (no single-valued params)

    std::span<const ConfigDescriptor> config_descriptors() const override {
        static constexpr ConfigDescriptor descs[] = {
            {"count", ConfigType::Int, 3.0f, 1.0f, 16.0f},
        };
        return descs;
    }

    std::span<const ArrayDescriptor> array_descriptors() const override {
        static constexpr ArrayDescriptor descs[] = {
            {"frequency", "layers", 2.0f, 0.01f, 100.0f},
            {"amplitude", "layers", 1.0f, 0.0f,  10.0f},
        };
        return descs;
    }
    // ... set_array, get_array, set_config for count, etc.

    void prepare(const RenderContext& ctx, int frames) override {
        if (layersDirty_) rebuild_layers_(ctx);
        for (auto& l : layers_) l->prepare(ctx, frames);
    }

    float next() override {
        float sum = 0.0f;
        for (auto& l : layers_) sum += l->next();
        cur_ = sum;
        return cur_;
    }
    float current() const override { return cur_; }

private:
    uint32_t baseSeed_{0xLAYEREDu};
    int count_{3};
    std::vector<float> frequencies_;   // size == count_
    std::vector<float> amplitudes_;    // size == count_
    std::vector<std::shared_ptr<RedNoiseSource>> layers_;
    bool layersDirty_{true};
    float cur_{0.0f};
};
```

### Row-count semantics

- `count` config is the number of layers (1..16). Default 3.
- Arrays `frequency[]` and `amplitude[]` must stay synced to `count`.
  Standard grouped-array behavior (same pattern as ExplicitPartials'
  `mult1/mult2/ampl1/ampl2` grouping).
- When `count` changes via inspector: grow arrays with the default
  values (freq=2.0, amp=1.0) or shrink (truncate).
- When array size mismatches count on load: re-sync to count by
  truncating/padding.

### Defaults (3 rows)

Sensible starter values that give audible layered character:
- Row 0: `frequency = 0.5 Hz, amplitude = 1.0` (slow drift)
- Row 1: `frequency = 2.0 Hz, amplitude = 1.0` (medium wobble)
- Row 2: `frequency = 8.0 Hz, amplitude = 1.0` (fast flutter)

### Per-layer internal RedNoise construction

On `rebuild_layers_(ctx)`:
- Clear `layers_`.
- For `i in 0..count_`: construct a `RedNoiseSource(sampleRate,
  layerSeed_i)` where `layerSeed_i = baseSeed_ ^ (i * 0x9E3779B9u)`.
- Set `layer_i.amplitude = ConstantSource(amplitudes_[i])` and
  `layer_i.frequency = ConstantSource(frequencies_[i])`.
- Leave density/smoothness/rampVariation/etc. at their RedNoise
  defaults.
- Push into `layers_` vector.

### Seed handling

- Constructor takes a `baseSeed` (from registry factory, same pattern
  as `RedNoiseSource` and friends).
- Derive per-layer seed via `baseSeed ^ (i * 0x9E3779B9)` — same
  golden-ratio hash used by `MultiplexSource`'s per-instance
  perturbation. Within one `LayeredRedNoiseSource` instance, each
  layer gets a distinct stream.

### Multiplex compatibility

When a `LayeredRedNoiseSource` sits in a Multiplex's template subtree,
each clone gets a perturbed `baseSeed` via the existing
`build_subgraph_with_seed_perturbation` path. Each clone's layered
noise has distinct streams (different clone-level seed) AND distinct
layers within each clone (different layer-level derivation).

No special-case loader code needed — the generic path handles
`LayeredRedNoiseSource` like any other seedable node.

### Dirty-flag rebuild

Any `set_array` / `set_config("count")` call sets `layersDirty_ = true`.
Next `prepare()` rebuilds the layers. Cheap (N constructors for small N).

## JSON on disk

```json
{
  "id": "layered1",
  "type": "LayeredRedNoiseSource",
  "params": {
    "count": 3,
    "frequency": [0.5, 2.0, 8.0],
    "amplitude": [1.0, 1.0, 1.0]
  }
}
```

No `"seed"` field unless the patch wants reproducibility — matches
other seedable nodes. Loader's injection for Multiplex cloning works
automatically.

## UI integration

- **Menu:** register under the "Noise" submenu (same place as White /
  Pink / Red / Blue / Violet etc.). Menu label: "Layered Red".
- **On-canvas node appearance:** one output pin, no input pins. Node
  body shows the title (via `type_name()` → "Layered Red" display
  name via the existing UI title-case conversion).
- **Inspector table:** grouped array descriptor `"layers"` containing
  columns `frequency` and `amplitude` — the existing table-render code
  for grouped arrays handles it identically to how `ExplicitPartials`
  renders its mult/ampl table.
- **Count editor:** standard Int config slider for `count` (1..16).
  Inspector drawing for configs already handles this.
- **Add/remove row buttons:** inherited from the array-table UI
  (`ExplicitPartials`-style). Changing row count also updates the
  `count` config.

### Inspector "count vs array length" coherence

Two sources of truth: the `count` config and the array lengths. They
must stay in sync. Simpler rule: `count` is the display-level control,
but the array lengths are what `rebuild_layers_` reads. On load:
- If `count` and `frequency.size()` disagree, truncate/pad arrays to
  match `count` using defaults. (Or: treat array size as authoritative
  and update `count` from it. Either works; pick one.)
- On UI "+Add row" / "-Remove row" via array-table buttons: update
  `count` to match new array size.
- On `set_config("count", N)`: resize arrays to N, padding with defaults.

My lean: array length is authoritative at load time; `count` is a
convenience Int config that mirrors the array length. Simpler invariant.

## Validation

1. **Divergence.** Render a LayeredRed with count=3, explicit amps
   [1.0, 1.0, 1.0]. RMS should be visibly higher than a single
   RedNoise at equivalent total amplitude (because three uncorrelated
   streams sum quasi-independently; rms scales by ~√3 vs single).
2. **Reproducibility.** Same patch rendered twice → bit-identical.
3. **Multiplex compatibility.** `[LayeredRed] → [Multiplex:2] → [Out]`:
   ratio(count=1)/ratio(count=2) ≈ √2 (each clone produces a
   differently-seeded layered stream, and they decorrelate on
   summation).
4. **UI integration.** Menu item appears under Noise. Creating a
   `LayeredRed` node: shows one output pin, inspector shows count + a
   2-column "frequency/amplitude" table with 3 default rows. Edit a
   row's values → `s_graphDirty = true` → next render reflects edit.

## Open questions

1. **Default row count = 3?** Per your message, yes.
2. **Max rows = 16?** I'd say 16 is plenty for textural layering. Any
   preference?
3. **Array length vs count authoritative at load?** I lean
   array-length-authoritative (simpler invariant). OK?
4. **Per-row defaults** when adding a row mid-edit: freq=2.0, amp=1.0.
   Fine?
5. **Should the per-layer RedNoises' other params (density/smoothness/
   rampVariation/boost/continuity/zeroCrossTendency) be exposed per-
   row in a future revision?** Or is the "nothing else on the node"
   principle firm? I'd vote firm for MVP — if a user needs finer
   control, they build the graph manually. Confirming.

## Known follow-ups (parked separately)

- `CombineOp::Sum` missing from `CombinedSource`. Direct-sum LayeredRed
  works around it but the hole in `CombinedSource`'s semantics should
  be plugged for its own sake. Park.

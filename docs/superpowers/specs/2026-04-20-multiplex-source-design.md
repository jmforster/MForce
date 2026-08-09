# MultiplexSource — Design Spec

**Status:** Draft for review.
**Author:** Claude (DSP/UI).
**Date:** 2026-04-20.

---

## Problem

Authoring a "section" sound (e.g. 10-violin ensemble) today requires
manually authoring 10 copies of the template source in JSON. No
shared-source "fan-out by N" primitive exists. Legacy had
`MultiplexSource(source, count)` built on top of a pervasive `Clone()`
interface; the C++ port skipped clone (too error-prone: you forgot to
clone a child and got silently shared state) and never replaced the
fan-out affordance.

## Goal

Add a `MultiplexSource` node that takes one template source + a count,
internally holds N independent instances built from the template, and
emits `sum(instances) / N` per sample. Instances diverge because each
is seeded with a deterministic perturbation of the template's base
seed; the underlying sources' built-in randomness does the rest.

Single node in the graph from the user's perspective; N parallel
runtime instances under the hood.

## Non-goals

- Realtime CPU budget. `60-partial Additive × 10 multiplex` moves
  offline-only. No vectorization / SIMD heroics in MVP.
- Per-copy detune/spread parameters. Built-in randomness of the
  template sources is sufficient — if a copy wants wider pitch
  spread, the template should include its own `VarSource`/`RangeSource`.
- Replacing `MultiSource` or the voice pool. MultiSource stays as the
  polyphony/voice-pool primitive (distinct sources, not copies of one).
- Adding `clone()` to every ValueSource. Fan-out uses the same
  rebuild-from-JSON path the voice pool already uses.

## Design

### Node shape

One input pin + one config:

| Pin / config | Type | Meaning |
|---|---|---|
| `template` (input) | `ValueSource*` | Single source whose subtree is replicated |
| `count` (config) | Int, default 10, range 1..64 | Number of instances |

Input pin is `inputOnly` (receives a ref, isn't a connectable output
elsewhere). JSON on disk looks like any other node + ref:

```json
{"id": "add1",   "type": "FullAdditive", "params": {...}},
{"id": "multi1", "type": "MultiplexSource",
                  "params": {"count": 10, "template": {"ref": "add1"}}},
{"id": "out1",   "type": "SoundChannel",
                  "inputs": {"source": "multi1"}}
```

### Runtime semantics

```cpp
struct MultiplexSource final : ValueSource {
    int count_{10};
    uint32_t baseSeed_{0};
    std::vector<std::shared_ptr<ValueSource>> instances_;
    bool templateDirty_{true};  // rebuild on next prepare if set

    void prepare(const RenderContext& ctx, int frames) override {
        if (templateDirty_) { rebuild_(ctx); templateDirty_ = false; }
        for (auto& i : instances_) i->prepare(ctx, frames);
    }
    float next() override {
        float sum = 0.0f;
        for (auto& i : instances_) sum += i->next();
        cur_ = instances_.empty() ? 0.0f : sum / float(instances_.size());
        return cur_;
    }
    // ...
};
```

`instances_` is the vector of N independent template copies. `next()`
is the legacy formula: sum + divide by count. `prepare()` forwards to
each — each handles its own state reset, which is what gives
consecutive notes a consistent (per-instance) timbre.

### Building the instances

Loader extracts the subgraph reachable from the `template` input's
target node and builds it N times. Each build produces an independent
universe, exactly like the voice pool does today (`patch_loader.cpp:
611-626`).

Two paths to the N instances:

1. **At load time.** Loader's special-case branch for
   `MultiplexSource`: resolve `template` ref to a node id, walk the
   nodemap to collect all transitively-referenced ids (the "template
   subgraph"), then call `build_subgraph(templateNodes, seed_i)` for
   `i = 0..N-1`. Store results in the Multiplex's `instances_`. Set
   `templateDirty_ = false`.

2. **On rebuild (live-edit path).** Multiplex stores a copy of the
   template subgraph JSON as a member. When the UI marks it dirty
   (template params changed since last build) and the next `prepare`
   fires, Multiplex re-invokes `build_subgraph` from the stored JSON
   to refresh `instances_`. The JSON is stored as
   `std::string` (serialized) rather than `nlohmann::json` to keep
   header transitive includes minimal.

A new utility in the patch loader:
```cpp
std::shared_ptr<ValueSource> build_subgraph(
    const std::string& json_subtree_str,
    uint32_t instanceSeed,
    int sampleRate);
```
Returns the root source of a freshly-built subgraph with every seedable
node's seed perturbed by `instanceSeed`.

### Seed perturbation for divergence

Without this, 10 instances of a template with `WhiteNoiseSource`
(default seed `0xFACE0000`) all produce the identical stream, and their
sum = 10× one stream. No section character.

Rule: for instance `i`, every seedable node in its subtree gets
`effectiveSeed = baseSeed ^ (i * 0x9E3779B9u)`. `baseSeed` is whatever
the JSON specifies (or the factory default, captured at load). XOR with
golden-ratio hash gives good decorrelation; deterministic given
`baseSeed`.

Implementation shape: `build_subgraph` takes an additional
`seedPerturbation` arg. When the internal loader builds each node, if
the node's JSON specifies `seed`, XOR it with `seedPerturbation`; if
the JSON lacks `seed`, use `factoryDefault ^ seedPerturbation`.

Reproducibility is preserved: same patch file + same master seed →
same rendered output, because perturbation is a pure function of
`baseSeed` and `instanceIdx`.

### Dirty-flag rebuild (live UI edit path)

The problem: user loads a patch, edits `RangeSource.min` on the
template, expects next note trigger to reflect the edit. But `instances_`
is stale — built from JSON at load.

Minimal machinery:
- Multiplex has `templateDirty_` bool (defaults to true; cleared after
  first build).
- UI maintains `s_graphDirty` flag. Any `set_param`/`set_config`/
  `set_array` on any UI graph node sets it.
- Before a note-trigger / Generate action in the UI, the trigger
  handler walks `s_nodes`, finds any Multiplex, re-serializes its
  template subgraph from the current UI state, updates Multiplex's
  stored JSON, sets `templateDirty_ = true`. Then clears `s_graphDirty`.
- Multiplex's next `prepare()` sees `templateDirty_`, rebuilds, emits
  the edited version.

Cost: ~10 ms blip on the first note after a burst of edits. Zero cost
when no edits happened.

**CLI path is unaffected** — no live editing; Multiplex is built once
at load, never rebuilt.

### UI integration

- `MultiplexSource` is registered in the source registry. Appears in
  the "Combiner" menu category (new name for the grouping; was
  previously implicit).
- `template` pin: rendered as a single-input pin accepting any
  `ValueSource` output. Visually marked (color or label suffix) so the
  user can see "that wire owns a template — editing its tail affects
  multiplex behavior."
- `count` config: an Int slider, 1–64.
- No per-instance inspector — the user edits the template once, the
  instances are hidden.

## Known issues / follow-ups (parked, not in this spec)

- **Voice-pool seed correlation.** The current `build_graph`-per-voice
  pattern suffers from the same seed-collision issue: all N voices
  use the patch's JSON seed, producing correlated noise streams across
  voices. Fix: apply the same per-instance seed perturbation in the
  voice-pool build loop. Same code path, same rule. Park as follow-up
  — would inherit from MultiplexSource's implementation.
- **Shared children across template boundary.** If the template refs a
  node that's ALSO referenced from outside the template (e.g. a
  shared Envelope), Multiplex's instance clones have their own copy
  while the outside reference still points to the original. This
  matches legacy Clone semantics; might surprise a user who expected
  sync. Not a blocker for MVP.
- **Nested MultiplexSource.** A template that itself contains a
  Multiplex produces `N * M` instances. Combinatorial. Works correctly
  in principle, expensive in practice. Park as "known to work, don't
  panic."

## Validation

1. **Reproducibility.** Render the same Multiplex patch twice with the
   same seed → bit-identical output.
2. **Divergence.** Render a Multiplex:2 of a WhiteNoiseSource template.
   Compare to Multiplex:1. RMS of :2 should be ≈ RMS(:1)/√2 (two
   independent noise streams averaged), not = RMS(:1) (which would
   indicate correlated streams).
3. **Live rebuild.** In UI: load a Multiplex patch, trigger a note,
   edit a template param, trigger again. Sidecar metrics (`check.py`)
   should differ between the two renders.
4. **No regression.** `pluck_sanity` and `fadd_formant_test` renders
   (no Multiplex) remain bit-identical.

## Open questions (resolve before plan)

1. **Pin naming.** Call the input `template` or something else?
   `source` would be confusing with SoundChannel's pin. I lean
   `template`.
2. **Count range cap.** 1–64 arbitrary; 64 is 6 octaves of doubling. If
   you'd prefer 128 or unbounded, say so.
3. **UI category.** Register under `SourceCategory::Combiner` (new?)
   or `Generator`? The legacy category list in `dsp_value_source.h`
   doesn't obviously fit; I'd add a `Utility` or keep in `Combiner`.
4. **Rebuild trigger granularity.** UI-side "any edit marks all
   Multiplexes dirty" is crude but simple; finer-grained (only mark
   Multiplexes whose template subtree was actually affected) is more
   complex. Crude is probably fine for MVP.
5. **Should `MultiplexSource.count` changes be instant?** If user
   changes count from 10 to 20 mid-session, do we rebuild with the
   new count immediately (via dirty flag), or wait until something
   else triggers a rebuild? I'd say rebuild on count change too —
   same dirty-flag machinery.

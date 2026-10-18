# Part 2, checkpoint 2.2: `prepare(ctx)` / `start(frames)` — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One call, `prepare(ctx, frames)`, becomes two: `prepare(const RenderContext&)` once per build (may allocate; the only way a node learns the sample rate) and `start(int frames)` once per note (may not allocate). Constructors and the registry factory lose their sample-rate argument; `RenderContext` is created where the instrument or the plain sound is built, not at every render call. Renders stay byte-identical.

**Architecture:** `ValueSource` owns both calls as template methods, the way it owns `next()`: `prepare(ctx)` stores the context, forwards to every Auto pin's source and calls `on_prepare(ctx)`; `start(frames)` forwards to every Auto pin's source and calls `on_start(frames)`. A node overrides only the hook it needs and reads the rate through `sample_rate()`; its own copy of the rate goes. During the migration the old call and the old hook stay as a shim so the families convert one per commit under the null gate; the close deletes the shim and switches every host (instrument, mixer, loader, CLI, UI, tests) to prepare-once-then-start, and the compiler's `override` audit proves no old hook remains.

**Tech Stack:** C++20 (MSVC, CMake 3.20+), the existing `engine_tests` harness, `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/ab_render_set.py`.

**Spec:** `docs/architecture/part2-valuesource-contract.md` sections 4 (the split), 5 (the rate and `RenderContext`) and 7 (the 2.2 row: expected render changes none, unless a node that re-draws randomness in `prepare` per note turns out to differ from a reset; any such patch is listed); `docs/architecture/target-architecture.md` section 2 principle 6 (structure and behaviour never change in the same checkpoint).

## Global Constraints

- No heap allocation, lock or throw in any `next()` / `compute()` (CLAUDE.md non-negotiables). `start(frames)` may not allocate by contract (Part 2 §4); this checkpoint moves allocation out of `start` wherever the split makes the move natural (buffer sizing, sub-node construction) and **does not** hunt the per-note allocations that remain inside today's per-note bodies (Partials' per-note arrays, HybridKS's table, DelayLineSource's walk). Those are checkpoint 2.6's test and Part 3's reshaping; the close lists them (section 7 of the report).
- Explicit registries, no reflection (CLAUDE.md).
- Byte-identical renders: after every task `python tools/null_gate_perform_source.py --jobs 3` reports `80/80 manifest entries identical`. No refreeze is expected in this checkpoint; a difference is a bug in the task until proven otherwise and ruled on in the Ledger.
- No new file over 600 lines, no new function over 80 (`tools/structure/limits.json`); `python tools/structure/check.py --delta <pre-checkpoint sha>` is pasted into the report.
- No dated changelog, backlog id or REVIEW number in code comments; a comment that explains a migration shim says "deleted at the close of checkpoint 2.2", nothing dated.
- Commit after every task; `python tools/gates.py --fast` before every commit (its roundtrip smoke fails on `perform/wiring_setting.json` for the pre-existing backlog-79 reason; every other gate must pass).
- Work on main, no worktree (Ruling 1 of checkpoint 2.1 stands). The in-tree build is `build/`; the UI exe may be locked by a running UI — rename-then-link (CLAUDE.md).
- Every family implementer A/Bs the stored patches that use its types against the pre-checkpoint binary (`renders/scratch/ab_tree/cli_pre22.exe`, Task 1 step 0), not only the manifest: library, baselines, audition, pending and every third sweep patch that names the type. The recipe's step 7 gives the command.

## Review Focus

The five input classes the spec implies but no existing test exercises, most likely to bite first. Each line names the task whose tests pin it.

1. **`start()` on a node nobody prepared** (a host that forgot `prepare(ctx)`): the rate is 0, every `x / sample_rate()` divides by zero, and the render is silent or NaN instead of failing loudly. Task 1 adds the debug assert in `ValueSource::start()`; the fault flag that makes it visible in Release is checkpoint 2.5's.
2. **`prepare(ctx)` called again with the same context** (the migration shim does it per note; the UI does it per play): a node that rebuilt on every call would reset state today's code carries across notes (the Butterworth filters' histories, a RedNoise's ramp). Task 1's registry-wide equivalence test fails any type whose second `prepare` is not a no-op.
3. **`prepare(ctx)` called again with a different rate** (a host opening the device at 44100 after 48000): rate-sized buffers must follow the rate, not keep the first size. Task 7 pins it on Limiter, Reverb, DelayFilter and DelayLineSource.
4. **A setting changed after `prepare`** (a per-note push binding, or a UI edit): a node whose structure derives from settings (Vibrato's LFO chain, FM's decimation sections, a preset envelope's stage list) must rebuild on the change, and the rebuilt node must keep the context it was prepared with. Task 2 pins the envelope (the assignment rule), Task 7 pins Vibrato and FM.
5. **A `start()` that still allocates** (Partials' per-note arrays, HybridKS's table fill, an evolution's `assign` in `adjust`): byte-identity cannot see it, and a live note-on would still touch the heap. No test here can catch it; the close records every such body with file and line (Global Constraints, first bullet) so checkpoint 2.6's allocation test starts from a list, not a search.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `engine/include/mforce/core/render_context.h` (modify) | `RenderContext{ int sampleRate{0} }`: the host's facts, one field today; 0 means "not prepared". |
| `engine/include/mforce/core/dsp_value_source.h` (modify) | `ValueSource` owns `prepare(ctx)` and `start(frames)` as template methods, the stored context, `sample_rate()` / `render_context()` / `prepared()`, and the hooks `on_prepare(ctx)` / `on_start(frames)`. Assignment keeps the context (the registry rule extended). `RefSource` and `ConstantSource` adapt. |
| `engine/include/mforce/core/pins.h` (modify) | `PinBase::prepare(ctx)` / `start(frames)`; `Param` and `Input` forward both to their source. |
| `engine/include/mforce/core/source_registry.h`, `engine/src/source_registry.cpp`, `engine/src/source_registrations.cpp` (modify) | `SourceFactory(std::optional<uint32_t> seed)`; `create(type, seed)`. 43 lambdas lose `sr`. |
| every node header under `core/`, `source/`, `filter/` and the four engine `.cpp` node files (modify) | the recipe, one family per task |
| `engine/include/mforce/render/mixer.h`, `engine/src/mixer.cpp` (modify) | `MonoSource::prepare(ctx)` / `render(out, frames)`; `StereoMixer::prepare(ctx)` prepares gains, volumes, pans and sources once; `render` starts them per call. |
| `engine/include/mforce/render/perform_source.h` (modify) | `PerformSource::prepare(ctx)` computes the smoother coefficient; `set_instrument_state` loses the rate. |
| `engine/include/mforce/render/instrument.h` (modify) | `Instrument` holds `RenderContext ctx`; `PitchedInstrument::prepare(ctx)` prepares every voice once; every per-note site calls `start`. The per-note envelopes it builds (bend, glide, gate) are prepared from `ctx`. |
| `engine/include/mforce/render/patch_loader.h`, `engine/src/patch_loader.cpp` (modify) | `build_graph` and the Multiplex builder lose the rate; the loader is the one place a `RenderContext` is made for a patch: `inst->prepare(RenderContext{sampleRate})` after the voice pool is built, `mixer->prepare(RenderContext{sampleRate})` after the mixer is built. |
| `engine/include/mforce/core/envelope_json.h`, `engine/include/mforce/music/pitch_bend.h` (modify) | `envelope_from_preset_json(p)`, `compile_pitch_curve(curve)`: factories that return unprepared nodes. |
| `tools/mforce_cli/main.cpp`, `tools/mforce_cli/explore.cpp`, `tools/test_figures/main.cpp` (modify) | render calls lose the context they only forwarded. |
| `tools/mforce_ui/main.cpp` (modify) | one `g_renderContext` from the opened stream's rate; `prepare_graph` prepares then starts; `DSP_SAMPLE_RATE` goes; saves write the stream's rate. |
| `tools/engine_tests/main.cpp` (modify) | the split-equivalence harness and its registry sweep (Task 1); family tests; the mechanical rewrite of 116 `prepare(ctx, n)` calls and every rate-taking constructor at the close. |

---

## The contract (what Task 1 writes; every later task codes against it)

```cpp
// render_context.h
struct RenderContext {
  int sampleRate{0};          // 0: no host has prepared this node yet
};

// dsp_value_source.h
struct ValueSource : Component {
  // Once per build, from the host, never per note and never on the audio
  // thread; again only when the host changes the rate or has rebuilt the
  // graph. May allocate. Calling it twice with the same context changes
  // nothing a render can see (the idempotence rule, recipe step 3).
  void prepare(const RenderContext& ctx) {
    ctx_ = ctx;
    for (auto* pin : pins()) if (pin->pull == Pull::Auto) pin->prepare(ctx);
    on_prepare(ctx);
  }
  // A span of `frames` samples begins now: reset per-note state and lay
  // out anything that depends on the length. May not allocate.
  void start(int frames) {
    assert(prepared() && "start() before prepare(ctx)");
    for (auto* pin : pins()) if (pin->pull == Pull::Auto) pin->start(frames);
    on_prepare(ctx_, frames);            // migration shim: the old hook (deleted at the close)
    on_start(frames);
  }
  // Migration shim: today's one call, as the two. Deleted at the close.
  void prepare(const RenderContext& ctx, int frames) { prepare(ctx); start(frames); }

  virtual void on_prepare(const RenderContext&) {}
  virtual void on_start(int) {}
  virtual void on_prepare(const RenderContext&, int) {}   // old hook; migration shim, deleted at the close
  ...
protected:
  int sample_rate() const { return ctx_.sampleRate; }
  const RenderContext& render_context() const { return ctx_; }
  bool prepared() const { return ctx_.sampleRate > 0; }
  // A node assigning itself (Envelope::replace_stages) assigns its own
  // state; the context the host gave this object stays, like the registry.
  ValueSource& operator=(const ValueSource& o) { Component::operator=(o); cur_ = o.cur_; return *this; }
private:
  RenderContext ctx_{};
};

// pins.h
struct PinBase { ...
  virtual void prepare(const RenderContext&) {}
  virtual void start(int) {}
};
// Param and Input each declare:
//   void prepare(const RenderContext& ctx) override;   // if (source) source->prepare(ctx)
//   void start(int frames) override;                   // if (source) source->start(frames)
//   void prepare(const RenderContext& ctx, int frames) { prepare(ctx); start(frames); }  // migration shim, deleted at the close
// The shim sits on Param and Input, not PinBase: a derived class that declares
// prepare(ctx) hides every base overload of that name, so a shim on PinBase
// would be unreachable through a Param (C++ name hiding).
```

`prepare` and `start` are not virtual: a node cannot run its own sweep, forget a pin or skip the hook, the same argument as `next()` (checkpoint 2.1, Lesson 2). `RefSource` needs no override: it has no Auto pins and no hook, so both calls do nothing but store the context, which is what its empty `prepare` override did.

## The recipe (one node)

1. **Constructor.** Delete the rate parameter and the node's own copy of the rate (`sampleRate_`, `sr_`, `sampleRate`, `sr_` in a `float`); every read becomes `sample_rate()` (an `int`; wrap in `float()` where the old code did). A node may keep a *derived* float it sets in `on_prepare` (Partials' `rate_`, Mesh2D's `sr_`), because that is a computed value, not a second copy of the input. Anything the constructor computed *from the rate* (buffer sizes, section coefficients) moves to `on_prepare(ctx)`.
2. **Split the hook.** Today's `on_prepare(const RenderContext& ctx, int frames)` becomes two overrides. Into `on_prepare(const RenderContext&)`: the rate capture; sizing of rate-dependent buffers; construction of sub-nodes (and `prepare(ctx)` on them); `prepare(ctx)` on every **Manual** pin. Into `on_start(int frames)`: everything else, **in today's order**, with `start(frames)` on every Manual pin where the old body called `prepare(ctx, frames)` on it, and `start(n)` on every owned sub-node or list entry where the old body prepared it with `n`. A node whose old hook only reset state gets `on_start` alone. A node whose old hook was empty gets nothing.
3. **Idempotence.** `on_prepare(ctx)` must leave a prepared node unchanged when called again with the same rate, because the shim calls it per note until the close and the UI calls it per play: size a buffer only when its size is wrong (`if (int(buf_.size()) != want) buf_.assign(want, 0.0f)`), build a sub-structure only when it is absent or stale (`if (!lfo_ || lfoDirty_)`), never clear state that today's per-note hook did not clear. "Build if absent or if the rate changed" is the whole rule.
4. **`start` is today's per-note effect, not a fresh node.** A RedNoise does not reseed per note today (its `rng_` runs on across the notes a voice slot plays); its `on_start` must not reseed either. The one place that needs *fresh* semantics is a node that **constructed** its sub-nodes per note (Vibrato, Task 7), and it gets them by an explicit reset of the sub-nodes it owns, never by changing the sub-node type's own `on_start`.
5. **Settings that shape structure.** Where `on_prepare` builds from settings (Vibrato's chain, FM's decimation sections, HybridKS's target array), `on_setting_changed` rebuilds when `prepared()`; a per-note push binding or a UI edit then takes effect exactly as today's per-note rebuild did. The build may allocate: `on_setting_changed` runs at Setup or on the UI thread, never in the sample loop.
6. **Traps.** (a) Every constructor of the form `(int sampleRate, uint32_t seed = …)` becomes `(uint32_t seed = …)`, so a leftover call `X(48000)` **compiles** with 48000 as the seed. After converting a type, run `git grep -nE "\b<Type>\((48000|sr|sampleRate|DSP_SAMPLE_RATE)\b" -- engine tools` and fix every hit; the close repeats the grep for every type. (b) `Vibrato`'s constructor takes only its seed from now on (its seven floats were setting initialisers; the loader sets them through `set_setting`), so no float can slide into the rate's old slot. (c) `Envelope`'s protected constructor takes a tag (`Envelope::SustainDefault{1.0f}`), so `Envelope(48000)` is a compile error, not a 48000 sustain level. (d) `WaveSource` subclasses that call `WaveSource::on_prepare(ctx, frames)` call `WaveSource::on_start(frames)` instead (Task 2 does the six).
7. **Prove it.** `engine_tests` (the registry sweep of Task 1 covers every registered type at its defaults; add a wired case where the default is degenerate, e.g. a processor with no input); `python tools/null_gate_perform_source.py --jobs 3` 80/80; and the family A/B: `git grep -l '"type": *"<Type>"' -- patches/library patches/baselines > set.txt`, append the gitignored hits (`grep -rl '"type": *"<Type>"' patches/audition patches/pending`, and every third of `grep -rl … patches/sweep`), then `python tools/ab_render_set.py render set.txt renders/scratch/ab22 old` with the CLI swapped for `cli_pre22.exe`, `… new` with the current build, `python tools/ab_render_set.py compare renders/scratch/ab22 old new` → `0 differ`. The script takes the arm's exe through `--cli <path>` (added in Task 1 step 0); never copy an old exe over the build's.

---

### Task 1: The contract, the shim, the equivalence harness

Inline (controller). Everything after this task codes against these names.

**Files:**
- Modify: `engine/include/mforce/core/render_context.h`, `engine/include/mforce/core/dsp_value_source.h:35-62,145-176`, `engine/include/mforce/core/pins.h:39,61,80`
- Modify: `engine/include/mforce/source/wander_noise_source.h:33-36,107-110` (the two `prepare(...) override`s become the legacy hook `on_prepare(const RenderContext& ctx, int frames) override { amplitude.prepare(ctx, frames); }` — `prepare` is no longer virtual, so `override` on it is a compile error; Task 3 migrates them properly)
- Modify: `tools/ab_render_set.py` (a `--cli` option)
- Test: `tools/engine_tests/main.cpp` (new section `run_split_equivalence_tests`)

**Interfaces:**
- Produces: `ValueSource::prepare(const RenderContext&)`, `ValueSource::start(int)`, `on_prepare(const RenderContext&)`, `on_start(int)`, `sample_rate()`, `render_context()`, `prepared()`; `PinBase/Param/Input::prepare(ctx)` and `::start(frames)`; the shims `prepare(ctx, frames)` on both; `split_equivalent(make, frames, notes, sampleRate)` in the tests.

- [ ] **Step 0: Keep the pre-checkpoint binary.** Confirm the build is current (`python tools/gates.py --fast` passes, `mforce_ui --stamp` clean), then `copy build\tools\mforce_cli\Release\mforce_cli.exe renders\scratch\ab_tree\cli_pre22.exe`. Add `--cli <path>` to `tools/ab_render_set.py` (`render` only; default stays the build path). Record the sha (`git rev-parse HEAD`) in the Ledger as the A/B base.

- [ ] **Step 1: Write the failing tests** (`tools/engine_tests/main.cpp`, a new section after the pin/registry tests):

```cpp
// The split is exact when a node that is prepared once and started per note
// renders the same bytes as one that is prepared and started before every
// note (what today's prepare(ctx, frames) did). Both arms throw or neither.
static bool split_equivalent(const std::function<std::shared_ptr<ValueSource>()>& make,
                             int frames = 2048, int notes = 3, int sampleRate = 48000) {
  RenderContext ctx{sampleRate};
  auto a = make(), b = make();
  std::vector<float> ra, rb;
  auto render = [&](ValueSource& s, std::vector<float>& out, bool prepareEachNote) {
    if (!prepareEachNote) s.prepare(ctx);
    for (int n = 0; n < notes; ++n) {
      if (prepareEachNote) s.prepare(ctx);
      s.start(frames);
      for (int i = 0; i < frames; ++i) out.push_back(s.advance());
    }
  };
  bool threwA = false, threwB = false;
  try { render(*a, ra, true);  } catch (...) { threwA = true; }
  try { render(*b, rb, false); } catch (...) { threwB = true; }
  if (threwA || threwB) return threwA == threwB;
  return ra.size() == rb.size() &&
         std::memcmp(ra.data(), rb.data(), ra.size() * sizeof(float)) == 0;
}

static void run_split_equivalence_tests() {
  register_all_sources();
  auto& reg = SourceRegistry::instance();
  for (auto& [name, cat] : reg.registered_types()) {
    const std::string n = name;
    bool ok = split_equivalent([&] { return reg.create(n, 48000); });   // 2-arg create until Task 9
    if (!ok) std::printf("  split differs: %s\n", n.c_str());
    CHECK(ok);
  }
  // prepared() is false before prepare(ctx): a ConstantSource reports 0 Hz
  struct Probe final : Generator { float compute() override { return 0.0f; }
                                   bool p() const { return prepared(); } int r() const { return sample_rate(); } };
  Probe p; CHECK(!p.p()); CHECK(p.r() == 0);
  p.prepare(RenderContext{22050}); CHECK(p.p()); CHECK(p.r() == 22050);
  // the context survives assignment (the registry rule extended)
  Probe q; q = p; CHECK(q.r() == 0); p = q; CHECK(p.r() == 22050);
}
```

Call it from `main()` with the others.

- [ ] **Step 2: Run to verify it fails** — `cmake --build build --config Release --target engine_tests` fails to compile (`prepare(const RenderContext&)` with one argument does not exist; `prepared()` does not exist).

- [ ] **Step 3: Implement the contract** exactly as "The contract" above: `render_context.h` (default 0; rewrite the header comment without the dated line); `pins.h` (`PinBase::prepare(ctx)` / `start(frames)` virtuals, the shim, `Param` and `Input` declarations); `dsp_value_source.h` (`ValueSource` as shown; move the pin method bodies at the bottom of the file to the new names; delete `RefSource`'s empty `prepare(...) override` — it needs none; `ConstantSource` unchanged); the two WanderNoise edits listed in Files. `#include <cassert>`. Every existing node still compiles: its `on_prepare(ctx, frames)` override now binds to the shim hook, which `start()` calls.

- [ ] **Step 4: Run the tests** — `ctest --test-dir build -C Release` passes (`engine_tests`: the sweep passes for every type because, for an unmigrated node, both arms run the old hook per note). `python tools/null_gate_perform_source.py --jobs 3` → 80/80 (the shim reproduces today's call exactly).

- [ ] **Step 5: Commit** — `git add engine/include/mforce/core/render_context.h engine/include/mforce/core/dsp_value_source.h engine/include/mforce/core/pins.h tools/engine_tests/main.cpp tools/ab_render_set.py` ; `git commit -m "core: prepare(ctx) and start(frames) are two calls; the old one is a shim until every family is cut"`.

---

### Task 2: Core — WaveSource and the oscillators, Envelope and its presets, Curve, Range/Var, Multi, NameGate

**Files:**
- Modify: `core/dsp_wave_source.h`, `source/sine_source.h`, `source/saw_source.h`, `source/triangle_source.h`, `source/pulse_source.h`, `source/fm_source.h`, `core/envelope.h`, `core/envelope_presets.h`, `core/envelope_json.h`, `music/pitch_bend.h`, `core/curve_node.h`, `core/multi_source.h`, `core/name_gate.h`, `core/range_source.h`, `core/var_source.h` (all under `engine/include/mforce/`); `engine/src/source_registrations.cpp` (this family's lambdas → `[](int, auto seed)`); the construction sites outside the registry: `engine/src/patch_loader.cpp:681,716,791`, `engine/include/mforce/render/instrument.h:325,422,477`, `tools/mforce_ui/main.cpp:411,564,1368,1863`, `tools/engine_tests/main.cpp` (every `Envelope`, `*Envelope`, `SineSource(48000)`-class construction and `make_ar/make_adsr(sr, …)` call)
- Modify: the six `WaveSource::on_prepare(ctx, frames)` calls in unmigrated subclasses — `source/additive/basic_additive_source.h:33`, `engine/src/additive_source2.cpp:84`, `engine/src/full_additive_source.cpp:12`, `engine/src/hybrid_ks_source.cpp:16`, `engine/src/wavetable_source.cpp:14`, `source/fm_source.h:37` — become `WaveSource::on_start(frames)` (trap d).

**Interfaces:**
- Produces: `WaveSource()`; `Envelope()`, `Envelope::SustainDefault`, `explicit Envelope(SustainDefault)` (protected); `Envelope::make_ar(float attackPct, float attackMin = 0.0f, float attackMax = 1.0f)`, `make_adsr(…)` and `make_adsr_abs(…)` with the rate parameter removed and every other parameter as today; `envelope_from_preset_json(const nlohmann::json&)`; `compile_pitch_curve(const PitchCurve&)`; `NameGate()`; `BWLPSection::reset()`, `BWHPSection::reset()`.

| Type | Constructor after | `on_prepare(ctx)` | `on_start(frames)` | Note |
|---|---|---|---|---|
| WaveSource | `WaveSource()` | — | `ptr_ = -1` | `sampleRate_` deleted; `currPhaseIncr_ = currFreq_ / float(sample_rate())` |
| Sine/Saw/Triangle/Pulse | `()` | — | — | |
| FMSource | `()` | build `decimSections_` from `oversample.value` and `sample_rate()` when empty or stale (`oversample` changed since the build: keep `builtOversample_`); `update(cutoff)` on each | `carrierPhase_ = modPhase_ = 0`; `for (auto& s : decimSections_) s.reset()` | `on_setting_changed("oversample")`: rebuild if `prepared()`. This task adds `BWLPSection::reset()` and `BWHPSection::reset()` to `filter/filters.h` (zero `fir.vals` and `iir.vals` with `std::fill`); a fresh section had zero history, so `reset()` after the one-time build is today's per-note rebuild. |
| Envelope | `Envelope()`; protected `explicit Envelope(SustainDefault s)` → `sustainLevel{this, "sustainLevel", s.level, 0, 1}` | `stageCounts_.resize(stages_.size(), 0)` | today's body without the resize | `sampleRate_` deleted (`gate_release`, the layout use `sample_rate()`); `add_stage` also does `stageCounts_.push_back(0)`; `replace_stages` ends with `stageCounts_.assign(stages_.size(), 0)` (the move-assignment brought `fresh`'s empty vector). The context survives `*this = std::move(fresh)` by Task 1's rule — test it. |
| AR/AS/ASR/ADS/ADR/ADSR | `()` : `Envelope()` or `Envelope(SustainDefault{…})` as today's second argument | — | — | `sr_` deleted; `rebuild()` calls the rate-less factories |
| CurveNode | — | — | `lastX_ = NaN` | |
| MultiSource | — | `for (auto& e : entries) e.source->prepare(ctx)` | `ptr_ = -1; for (auto& e : entries) e.source->start(frames + e.delaySamples)` | entries are a list, not pins |
| NameGate | `NameGate()` | — | — | loader 791 |
| RangeSource, VarSource | no rate today | — | — | nothing to do; listed so the family is complete |

- [ ] **Step 1: Write the failing tests** (`tools/engine_tests/main.cpp`): (a) a preset envelope keeps its context through a setting change — `AREnvelope e; e.prepare({22050}); e.set_setting("attack", 0.5f); e.start(22050);` renders 22050 samples bit-equal to `AREnvelope f; f.set_setting("attack", 0.5f); f.prepare({22050}); f.start(22050);`; (b) `Envelope` built by `make_adsr(0.2f, 0.1f, 0.7f, 0.0f)` then `prepare({48000}); start(4800)` renders bit-equal to the same envelope after a second `prepare({48000})` (idempotence); (c) FM with `oversample = 2`: `split_equivalent([]{ auto f = std::make_shared<FMSource>(); f->set_setting("oversample", 2.0f); return f; })`; (d) MultiSource with two entries (a Sine and an Envelope, delay 100): `split_equivalent`.
- [ ] **Step 2: Run to verify they fail** — compile errors on the rate-less constructors.
- [ ] **Step 3: Convert the family** per the table and the recipe; update the construction sites listed in Files (they keep calling the two-argument `prepare(ctx, frames)` shim for now — only the constructors and factories change); the six `WaveSource::on_start(frames)` edits.
- [ ] **Step 4: Verify** — recipe step 7: `engine_tests` (the sweep now exercises these types for real), null gate 80/80, A/B of the stored patches naming `FMSource`, `Envelope`, `AREnvelope`…`ADSREnvelope`, `CurveNode`, `MultiSource`, `NameGate` (expect every library patch: this family is in all of them) → `0 differ`. `git grep -nE "Envelope\((48000|sr|sampleRate|DSP_SAMPLE_RATE)\)|make_(ar|adsr|adsr_abs)\((48000|sr|sampleRate|DSP_SAMPLE_RATE)" -- engine tools` → nothing.
- [ ] **Step 5: Commit** — `git commit -m "core(wave, envelope): the rate arrives through prepare(ctx); the note through start(frames)"`.

---

### Task 3: The noise family

**Files:**
- Modify: `source/red_noise_source.h`, `engine/src/red_noise_source.cpp`, `source/white_noise_source.h`, `source/pink_noise_source.h`, `source/noise_sources.h` (Blue, Violet, Velvet, Perlin, Crackle, Murmuration), `source/wander_noise_source.h` (three types), `source/layered_red_noise_source.h`; `engine/src/source_registrations.cpp` lines 123-168 lambdas; tests constructing these.

**Interfaces:**
- Produces: `RedNoiseSource(uint32_t seed = 0xC0FFEEu)`; `RedNoiseSource::reset()` — the constructed state: `rampSize_ = 1, sampleCount_ = 0, lastValue_ = 0, lastSign_ = -1, nextValue_ = 0, zeroRamp_ = false, rng_.reanchor()` (Task 7's Vibrato is its only caller); `VelvetNoiseSource(uint32_t seed = …)`, `PerlinNoiseSource(uint32_t seed = …)`, `MurmurationNoiseSource(uint32_t seed = …)`, `WanderNoiseSource(uint32_t seed = …)`, `WanderNoise2Source(uint32_t seed = …)`, `WanderNoise3Source()`, `LayeredRedNoiseSource(uint32_t seed = …)`.

| Type | Constructor after | `on_prepare(ctx)` | `on_start(frames)` | Note |
|---|---|---|---|---|
| RedNoise | `(seed)` | — | — (WaveSource's `ptr_`) | add `reset()`; no reseed in start (recipe step 4) |
| White, Crackle | unchanged | — | Crackle: today's draws | |
| Pink, Blue, Violet | unchanged | Blue: `pink_.prepare(ctx)` | Pink, Violet: today's; Blue: `pink_.start(frames); prev_ = 0` | Blue owns a Pink by value and drives it with `next()` |
| Velvet | `(seed)` | — | `counter_ = 0; recompute_gap()` | `recompute_gap` uses `sample_rate()` |
| Perlin | `(seed)` | — | `pos_ = 0` | the `perm_` table stays in the constructor (seed only) |
| Murmuration | `(seed)` | — | today's | |
| Wander, Wander2 | `(seed)` | `amplitude.prepare(ctx)` | `amplitude.start(frames)` | they overrode `prepare` itself to put the Manual pin first; the two hooks keep that order |
| Wander3 | `()` | — | today's | |
| LayeredRed | `(seed)` | `if (layersDirty_) rebuild_layers_(); for layers: l->prepare(ctx)` | `for layers: l->start(frames)` | `rebuild_layers_` loses its unused `ctx` parameter and builds `RedNoiseSource(layerSeed)`; `on_array_changed` already marks dirty — a UI edit is picked up by the UI's per-play `prepare` |

- [ ] **Step 1: Write the failing tests**: `RedNoiseSource r; r.prepare({48000}); r.start(100); run 50 samples; r.reset(); r.start(100);` renders bit-equal to a fresh `RedNoiseSource` prepared and started the same way; `split_equivalent` for LayeredRed (default three layers) is in the sweep already — add Wander with `amplitude` wired to a constant 0.5.
- [ ] **Step 2: Run to verify they fail.**
- [ ] **Step 3: Convert the family.**
- [ ] **Step 4: Verify** — recipe step 7; the A/B set names every noise type (the winds and the piano use RedNoise; `fm_matrix2` audition patches use three RedNoise each — include them). Trap grep for every type in the table.
- [ ] **Step 5: Commit** — `git commit -m "nodes(noise): constructors lose the rate; prepare/start split"`.

---

### Task 4: Wavetable, hybrid KS, the evolutions

**Files:**
- Modify: `source/wavetable_source.h`, `engine/src/wavetable_source.cpp`, `source/hybrid_ks_source.h`, `engine/src/hybrid_ks_source.cpp`, `source/wave_evolution.h` (the `WaveEvolution` interface, its implementations and the twelve holder nodes); registrations 524, 573; loader 934, 980 (`WavetableSource(seed)`, `HybridKSSource(seed)`); tests.

**Interfaces:**
- Produces: `WavetableSource(uint32_t inputSeed = 0xC0FFEEu)`, `HybridKSSource(uint32_t seed = …)`; `WaveEvolution::prepare(const RenderContext&)` (renamed from its one-argument `on_prepare`; same body: the rate capture).

| Type | Constructor after | `on_prepare(ctx)` | `on_start(frames)` | Note |
|---|---|---|---|---|
| WavetableSource | `(seed)` | `speedFactor.prepare(ctx); inputSource.prepare(ctx); evolution.prepare(ctx); if (evolution_) evolution_->prepare(ctx)` | `WaveSource::on_start(frames); speedFactor.start(frames); inputSource.start(frames); evolution.start(frames)` | `fill_table` keeps its throw (checkpoint 2.5) |
| HybridKSSource | `(seed)` | `inputSource.prepare(ctx)`; `if (int(targetAmpl_.size()) < numPartials.value) targetAmpl_.resize(numPartials.value, 0.0f)` | the rest of today's body, with `inputSource.start(frames)` | `on_setting_changed("numPartials")`: the same resize if `prepared()` |
| WaveEvolution impls (BowedString, Brass) | — | `prepare(ctx)`: `sampleRate_ = ctx.sampleRate` | — | they are parts, not ValueSources; their `sampleRate_` stays (Part 3 moves them under Component) |
| the holder nodes (Reed, BowedString, Brass, BitRotate, …) | — | each Manual pin `.prepare(ctx)` | each Manual pin `.start(frames)` | today's bodies are pin forwards only |

- [ ] **Step 1: Failing tests** — `split_equivalent` for a WavetableSource with `inputSource` wired to a WhiteNoiseSource and a PluckEvolution holder wired to `evolution` (the loader's shape; see `engine_tests` for an existing construction to copy), and for HybridKS with `numPartials` set to 12 after `prepare`.
- [ ] **Step 2: Run to verify they fail.** **Step 3: Convert.** **Step 4: Verify** — recipe step 7; A/B set: every patch naming `WavetableSource`, `HybridKSSource`, `*EvolutionSource`. **Step 5: Commit** — `git commit -m "nodes(wavetable, evolutions): prepare/start split"`.

---

### Task 5: The additive family

**Files:**
- Modify: `source/additive/basic_additive_source.h`, `source/additive/additive_source2.h`, `engine/src/additive_source2.cpp`, `source/additive/full_additive_source.h`, `engine/src/full_additive_source.cpp`, `source/additive/partials.h` (`IPartials`, `Partials`, `CompositePartials`, `ExplicitPartials`…), `source/additive/formant.h` (`IFormant` and the four formant nodes); registrations 86, 92, 470; loader 891; tests.

**Interfaces:**
- Produces: `BasicAdditiveSource(uint32_t seed = …)`, `AdditiveSource2(uint32_t seed = …)`, `FullAdditiveSource(uint32_t seed = …)`; `IPartials::partials_prepare(const RenderContext&)` + `partials_start(int frames)`; `IFormant::fmt_prepare(const RenderContext&)` + `fmt_start(int frames)`.

| Type | Constructor after | `on_prepare(ctx)` | `on_start(frames)` | Note |
|---|---|---|---|---|
| BasicAdditive | `(seed)` | — | today's clears | `sampleRate_` reads → `sample_rate()` |
| AdditiveSource2 | `(seed)` | `for e in freqEnvs_, amplEnvs_: e->prepare(ctx)`; `reserve(endIdx_.size())` on `endFreq_, startFreq_, partialPos_, freqOffset_, amplOffset_, freqVarDir_, amplVarDir_` | today's body with the env loops as `e->start(frames)` | the per-note `resize(activeCount_)` stays (within capacity it allocates nothing); `activeCount_` still reads the previous note's `currFreq_` — today's quirk, preserved |
| FullAdditive | `(seed)` | `if (partials_) partials_->partials_prepare(ctx); if (formant_) formant_->fmt_prepare(ctx); formantWeight.prepare(ctx); formantFloor.prepare(ctx)` | the same four as `start`, then today's bed state (uses `sample_rate()`) | |
| Partials | — | `partials_prepare(ctx)`: `rate_ = float(ctx.sampleRate)`; the eight env pins `.prepare(ctx)` | `partials_start(frames)`: today's body from the note counter on, env pins `.start(frames)` | the per-note `assign(n, …)` allocations stay and are listed in the report (Global Constraints) |
| CompositePartials, ExplicitPartials and the rest of `IPartials` | — | forward `partials_prepare(ctx)` | forward `partials_start(frames)` | |
| Formant, FormantSequence, FormantBank, FormantBlend (the four in `formant.h`) | — | `fmt_prepare(ctx)`: their pins / members `.prepare(ctx)` | `fmt_start(frames)`: `.start(frames)` | the ValueSource hooks forward to these |

- [ ] **Step 1: Failing tests** — the sweep covers the defaults; add `split_equivalent` for a FullAdditiveSource with a `Partials` in its `partials` slot carrying `mult1 = {1,2,3}`, `ampl1 = {1,0.5,0.25}` and `bandwidthHz = 2` (the per-note stream seeds depend on the note ordinal — both arms advance it identically).
- [ ] **Step 2: Run to verify they fail.** **Step 3: Convert.** **Step 4: Verify** — recipe step 7; A/B set: every patch naming `AdditiveSource`, `AdditiveSource2`, `BasicAdditiveSource`, `Partials`, `*Formant*` (the `ml_ears` viola family and the additive audition queues). **Step 5: Commit** — `git commit -m "nodes(additive): prepare/start split; the rate through prepare only"`.

---

### Task 6: The physical models

**Files:**
- Modify: `source/ks_string.h`, `source/allpass_resonator.h`, `source/delay_line_source.h`, `source/mesh2d_source.h`, `source/pierce_filter.h`, `source/bow_table_source.h`, `source/wormhole_source.h`, `source/shaper_source.h`; registrations; tests.

| Type | Constructor after | `on_prepare(ctx)` | `on_start(frames)` | Note |
|---|---|---|---|---|
| KSString | `()` | — (buffers are `kBufLen`, not rate-sized) | today's | `sampleRate_` → `sample_rate()` (`init_note`) |
| AllpassResonator | `()` | — | today's | same |
| DelayLineSource | `()` | `const int want = sample_rate() / 20 + 4; if (int(buf_.size()) != want) buf_.assign(want, 0.0f)` | today's (fill, walk) | the walk allocates nothing today; keep |
| Mesh2D | unchanged | `sr_ = float(ctx.sampleRate)` | `clear_mesh(); cur_ = 0` | |
| PierceFilter, BowTable, Wormhole | unchanged | — | today's | |
| ShaperSource | unchanged | the five Manual pins + `input` `.prepare(ctx)` | the same `.start(frames)`, then `stuck_ = true; lastSign_ = 0; cur_ = 0` | keeps its read timing (2.1 ruling 7); untouched |

- [ ] **Step 1: Failing tests** — `split_equivalent` for KSString and AllpassResonator with `input` wired to a WhiteNoiseSource, DelayLineSource with `input` wired to a Sine; DelayLineSource prepared at 22050 then 48000 has `buf_.size() == 48000/20 + 4` (expose through a test-only read of `phase_delay_at`? No — check behaviour: the max delay reachable differs; simpler: add `int capacity() const` returning `buf_.size()`; it is a diagnostic, like `tracks_frequency_live`).
- [ ] **Step 2: Run to verify they fail.** **Step 3: Convert.** **Step 4: Verify** — recipe step 7; A/B set: patches naming each type (the KS pianos, `feedback/` baselines, the string and brass libraries). **Step 5: Commit** — `git commit -m "nodes(physical): prepare/start split"`.

---

### Task 7: The filters and Vibrato

**Files:**
- Modify: `filter/filters.h` (`BWLPSection`, `BWHPSection`, the three BW filters, `DelayFilter`), `filter/biquad_source.h`, `filter/svf_source.h`, `filter/limiter.h`, `filter/reverb.h`, `filter/hammer_bank.h`, `filter/vibrato.h`; registrations; loader 828-845 (Vibrato, BW); tests.

**Interfaces:**
- Consumes: `BWLPSection::reset()`, `BWHPSection::reset()` from Task 2.
- Produces: `BWLowpassFilter(int sectionCount)`, `BWHighpassFilter(int sectionCount)`, `BWBandpassFilter(int sectionsPerFilter)`; `DelayFilter()`, `BiquadSource()`, `SVFSource()`, `Limiter()`, `Reverb()`, `HammerBank()`; `Vibrato(uint32_t seed = 0xF1B0'0000u)`.

| Type | Constructor after | `on_prepare(ctx)` | `on_start(frames)` | Note |
|---|---|---|---|---|
| BW lowpass / highpass / bandpass | `(int sectionCount)` stored | build the sections with `float(sample_rate())` when empty or the rate changed (keep `builtRate_`), and only then set `lastCutoff_` (the bandpass pair) to its constructed value so the first sample runs `update`; a repeat call with the same rate touches nothing | — (no reset today: the histories carry across notes; keep that) | the sections' `rate` field is set by the section constructor as today |
| DelayFilter | `()` | `if (int(buffer_.size()) != sample_rate()) buffer_.assign(sample_rate(), 0.0f)` | `std::fill(buffer_…); ptr_ = -1` | |
| Biquad | `()` | `frequency.prepare(ctx); radius.prepare(ctx)` | the two `.start(frames)`, then today's resets | |
| SVF, HammerBank | `()` | — | today's | |
| Limiter | `()` | `lookaheadSamples_ = max(1, lround(sample_rate() * kLookaheadSec)); if (int(lookahead_.size()) != lookaheadSamples_) lookahead_.assign(lookaheadSamples_, 0.0f)` | today's | |
| Reverb | `()` | today's constructor body, each `assign` guarded by a size compare | today's | |
| Vibrato | `(seed)`; the seven floats go: the loader (828) calls `set_setting` for `speed, depth, attack, threshold, speedVar, depthVar, zeroCrossTendency` with the same `p.value(key, default)` it passes today | `if (!lfo_ || lfoDirty_) { build_lfo(); lfo_->prepare(ctx); lfoDirty_ = false; }` | `enabled_ = float(frames) / float(sample_rate()) > threshold.value; if (enabled_) { lfo_->reset(); lfo_->start(frames); }` | `build_lfo` builds `RedNoiseSource(seed_)` and `ASEnvelope()`s; `on_setting_changed(name)`: `lfoDirty_ = true; if (prepared()) on_prepare(render_context())`. Today a fresh chain is built per note; `reset()` + `start()` is that chain's fresh state (Task 3's `reset`, Envelope's `on_start` reseeds and re-lays out, RangeSource and ConstantSource hold no per-note state). `reseed()` unchanged. |

- [ ] **Step 1: Failing tests** — (a) `split_equivalent` for Vibrato with `frequency` wired to a constant 440 (the sweep's default; add it explicitly with `notes = 5`); (b) Vibrato prepared, then `set_setting("speed", 7.0f)`, then started, renders bit-equal to a fresh Vibrato with speed 7 prepared then started; (c) Limiter and DelayFilter prepared at 22050 then at 48000: the lookahead / buffer length follows (`capacity()` diagnostics as in Task 6, or assert through behaviour: a DelayFilter with `delayTime = 0.9` s at 48000 must not read past its buffer — render 48000 samples with a Sine input without a NaN); (d) `split_equivalent` for each BW filter and the Limiter/Reverb with `input` wired to a Sine.
- [ ] **Step 2: Run to verify they fail.** **Step 3: Convert.** **Step 4: Verify** — recipe step 7; A/B set: every patch naming the types (Vibrato is in the string and wind libraries: `cello_full_range`, `viola_default`, `bassoon1`, `oboe1`, `oboe2`, and the audition queues). Trap grep for `Vibrato\(` with any numeric first argument. **Step 5: Commit** — `git commit -m "nodes(filter, vibrato): prepare/start split; Vibrato builds its LFO once and resets it per note"`.

---

### Task 8: Combiners, modulators and the rest of `source/`

**Files:**
- Modify: `source/combined_source.h` (CombinedSource, CrossfadeSource, StaticVarSource, StaticRangeSource), `source/multiplex_source.h`, `source/repeating_source.h`, `source/phased_value_source.h`, `source/segment_source.h`; `engine/src/patch_loader.cpp` (`build_subgraph_with_seed_perturbation` loses `int sampleRate`; the Multiplex builder lambda loses `sr`; SegmentSource at 820); registrations 381, 394; tests.

**Interfaces:**
- Produces: `RepeatingSource(uint32_t seed = …)`, `PhasedValueSource(float overlap)`, `SegmentSource(std::vector<float> initial, bool oneShot = false, uint32_t seed = …)`; `MultiplexSource::InstanceBuilder` unchanged in type (it never took the rate); `build_subgraph_with_seed_perturbation(const std::string&, uint32_t, const PerformContext*)`.

| Type | Constructor after | `on_prepare(ctx)` | `on_start(frames)` | Note |
|---|---|---|---|---|
| CombinedSource | — | — | `totalFrames_ = frames; ptr_ = -1` | |
| CrossfadeSource | — | `source1.prepare(ctx); source2.prepare(ctx)` | today's layout, then `source1.start(s1Count + overlapCount_); source2.start(s2Count + overlapCount_)` | Manual pins |
| StaticVar, StaticRange | — | — | today's draw | |
| MultiplexSource | — | `if (templateDirty_) rebuild_(); if instances: each `inst->prepare(ctx)` else `input.prepare(ctx)`` | if instances: `inst->start(frames)` else `input.start(frames)` | the builder returns unprepared clones; the Multiplex prepares them. `mark_dirty` from the UI is honoured by the UI's per-play `prepare`. |
| RepeatingSource | `(seed)` | `input.prepare(ctx)` | `prepare_repetition()` | `ctx_` deleted; `prepare_repetition` calls `input.start(samples)` (legal on the audio thread by contract); `sampleRate` → `sample_rate()` |
| PhasedValueSource | `(float overlap)` | `for (auto& s : stages_) s.source->prepare(ctx)` | today's layout with `stages_[i].source->start(stageCounts_[i] + extra)`; `boundaries_.resize` stays (sized at first start; within capacity after) | `sampleRate` → `sample_rate()` |
| SegmentSource | `(initial, oneShot, seed)` | — | today's | `sampleRate` → `sample_rate()` in `update_segments` |

- [ ] **Step 1: Failing tests** — `split_equivalent` for Repeating with `input` wired to an Envelope (`make_ar(0.3f)`), Crossfade with two Sines of different frequency, Phased with two stages (each a Sine; `add_stage` as the registry configurator does), and a Multiplex loaded through `load_instrument_patch_json` with a two-node template and `count = 3` (render two notes through `prepare_voice_at` is Task 9's; here compare the voice source directly: `prepare(ctx)` once vs per note, `start`, 2048 samples, three notes).
- [ ] **Step 2: Run to verify they fail.** **Step 3: Convert.** **Step 4: Verify** — recipe step 7; A/B set: every patch naming the types (`multiplex` baselines, `repeating`, `crossfade`, the segment patches). **Step 5: Commit** — `git commit -m "nodes(combine, modulate): prepare/start split; the Multiplex builder loses the rate"`.

---

### Task 9: The hosts — prepare once, start per note; the shim goes

Inline (controller): this task crosses every host and must land as one buildable change. The compiler drives it: deleting the shim and the old hook makes every remaining caller and override an error.

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (delete the two-argument `prepare`, the two-argument `on_prepare`, and the pins' two-argument shim in `pins.h`), `engine/include/mforce/core/source_registry.h`, `engine/src/source_registry.cpp`, `engine/src/source_registrations.cpp` (43 lambdas → `[](auto seed)` / `[](auto)`), `engine/include/mforce/render/mixer.h`, `engine/src/mixer.cpp`, `engine/include/mforce/render/perform_source.h`, `engine/include/mforce/render/instrument.h`, `engine/include/mforce/render/patch_loader.h`, `engine/src/patch_loader.cpp`, `tools/mforce_cli/main.cpp`, `tools/mforce_cli/explore.cpp`, `tools/test_figures/main.cpp`, `tools/mforce_ui/main.cpp`, `tools/engine_tests/main.cpp`

**Interfaces:**
- Produces: `SourceFactory = std::function<std::shared_ptr<ValueSource>(std::optional<uint32_t> seed)>`; `SourceRegistry::create(const std::string& type_name, std::optional<uint32_t> seed = {})`; `MonoSource::prepare(const RenderContext&) = 0` and `MonoSource::render(float* out, int frames) = 0`; `StereoMixer::prepare(const RenderContext&)` and `StereoMixer::render(float* outLR, int frames)`; `PerformSource::prepare(const RenderContext&)` and `set_instrument_state(std::shared_ptr<InstrumentState>)`; `Instrument::ctx` (a `RenderContext`, replacing `int sampleRate`), `Instrument::prepare(const RenderContext&) override` (stores), `PitchedInstrument::prepare(const RenderContext&) override` (stores, then for every voice: `vg.source->prepare(ctx)`, each `advanceList` entry, `vg.performSource->prepare(ctx)`), `DrumKit::prepare(const RenderContext&) override` (stores, each drum source); `build_graph(nodeMap, nodeOrder, perf = nullptr, onsets = nullptr)`; `make_perform_context(vg, instrumentState)`.

- [ ] **Step 1: Write the failing tests** — (a) `tools/engine_tests/main.cpp`: a scoreless baseline patch loaded with `load_instrument_patch`, two `PerformedNote`s played, rendered with `ip.instrument->render(buf.data(), n)` (no context) and compared bit-for-bit against the same patch loaded a second time; (b) the registry sweep now calls `reg.create(n)` (one argument); (c) `PerformSource ps; ps.set_instrument_state(is); ps.prepare({48000});` — `tick()` smooths the wheel toward 1.0 by `1 - exp(-1/(0.010*48000))` on the first tick (`CHECK_NEAR`).
- [ ] **Step 2: Delete the shims** — in `dsp_value_source.h` remove the two-argument `prepare`, the two-argument `on_prepare` and the `on_prepare(ctx_, frames)` line in `start()`; in `pins.h` remove the two-argument `prepare`. Build: every error is a site this task owns. `git grep -n "on_prepare(const RenderContext&[^)]*, *int" -- engine` must be empty when the build is green — any hit is an unmigrated node (the `override` audit, checkpoint 2.1 Lesson 2) and goes back to its family's task.
- [ ] **Step 3: The registry** — the two signatures above; every lambda; `register_all_sources` unchanged otherwise.
- [ ] **Step 4: The render layer** — `mixer.h` / `mixer.cpp`: `StereoMixer::prepare(ctx)` = `gainL/gainR->prepare(ctx)`, per channel `volume->prepare(ctx); pan->prepare(ctx); source->prepare(ctx)`; `render(outLR, frames)` = today's body with `prepare(ctx, frames)` → `start(frames)` and `ch.source->render(mono.data(), frames)`. `patch_loader.cpp`'s `WaveSourceMono` / `ValueSourceMono`: `prepare(ctx)` forwards; `render(out, frames)` = `src->start(frames)` then the loop. `perform_source.h`: as the Interfaces say; `smoothA_` keeps its default. `instrument.h`: `RenderContext ctx{}` replaces `int sampleRate{48000}` and every `sampleRate` read becomes `ctx.sampleRate`; `Instrument::render(float*, int)`; the three `prepare` overrides; every per-note site — `prepare_voice_at`, the two paths in `play_note`, `play_hit`, `make_glide`, the gate envelope at 421-427, the bend at 325 — becomes `start(durSamples)` on the voice and advance list, and `prepare(ctx); start(durSamples)` on an envelope built in that function (bend, glide, gate: fresh objects, so both calls, from the member `ctx`). `compile_pitch_curve(*curve)` with one argument.
- [ ] **Step 5: The loader** — `build_graph` and `make_perform_context` lose the rate parameter (every call site); `load_patch_file`: after the voice-pool loop closes at line 1573 and **before** the embedded score schedules a note (`play_note` at 1624 renders at load), `inst->prepare(RenderContext{sampleRate});` and, after `patch.mixer` is assembled, `patch.mixer->prepare(RenderContext{sampleRate});` (the mixer forwards to the instrument again — idempotent); `load_instrument_patch_json`: `inst->prepare(RenderContext{sampleRate});` before `return {std::move(inst), sampleRate};`. `InstrumentPatch::sampleRate` and `Patch::sampleRate` stay: they are the file's render default, which the CLI uses for frame arithmetic and the WAV header (Part 2 §5).
- [ ] **Step 6: The CLI, explore, test_figures** — every `{ RenderContext _ctx{…}; x->render(_ctx, …); }` becomes `x->render(…)`; nothing else changes (the rate they use for `frames` is the file's).
- [ ] **Step 7: The UI** — `static RenderContext g_renderContext{AUDIO_SAMPLE_RATE};`, set from `g_audio->getStreamSampleRate()` right after `openStream` succeeds (4061); delete `DSP_SAMPLE_RATE` (230) — its uses become `g_renderContext` (`prepare_graph`: for each node `prepare(g_renderContext); start(samples)`; 4965 the same for `prepExtra`), or go (`reg.create(typeName)` at 461, 610, 1489; `Envelope tmp;` 564; `env = Envelope();` 1368; `Envelope::make_adsr(0.05f, 0.1f, 0.7f, 0.2f)` 411; `envelope_from_preset_json(params)` 1863); the two saves (3144, 3447) write `g_renderContext.sampleRate`; `g_outputSpectrumSR` (3611, 11893) initialises from it; the three render sites (4437, 8152, 8281) lose their context and `kit.sampleRate = AUDIO_SAMPLE_RATE` (8239) becomes `kit.prepare(g_renderContext)` after the kit's sources are built. `AUDIO_SAMPLE_RATE` stays as the one requested-device-rate constant.
- [ ] **Step 8: The tests** — the mechanical rewrite: every `x.prepare(ctx, n)` / `x->prepare(ctx, n)` becomes `x.prepare(ctx); x.start(n)` (a 20-line Python script over the file is acceptable; review the diff); every rate-taking construction loses the argument (the trap grep over `tools/engine_tests/main.cpp` for every type in Tasks 2-8); `ip.instrument->render(ctx, …)` → `render(…)`. The sweep's `reg.create(n)`.
- [ ] **Step 9: Verify** — build all three targets; `python tools/gates.py --fast`; `python tools/null_gate_perform_source.py --jobs 3` 80/80; the UI opens a patch, plays a key, Generates, and the headless `--stamp` passes; `git grep -nE "\bRenderContext\s*(ctx|\{)" -- engine` lists exactly five: the loader's three creations, `ValueSource::ctx_`'s default and `Instrument::ctx`'s default (tests and tools are hosts and may create their own). Trap grep across `engine tools` for every converted type.
- [ ] **Step 10: Commit** — `git commit -m "render, loader, hosts: prepare once at build, start per note; the rate reaches a node only through prepare(ctx)"`.

---

### Task 10: Close the checkpoint

Inline (controller), with one Fable refuter.

**Files:**
- Modify: `tools/structure/metrics.json` (snapshot), `docs/autonomy/refactor/reports/2026-10-xx-checkpoint-2.2.md` (new), `docs/autonomy/refactor/REVIEW.md` (R3 awaiting), `docs/autonomy/refactor/BACKLOG.md`, `docs/autonomy/STATUS.md`, `docs/architecture/part2-valuesource-contract.md` (section 4's "today" column notes are history; leave; correct any name the cut changed).

- [ ] **Step 1: Confirm nothing old remains** — `git grep -nE "prepare\([^)]*,\s*(int|frames|durSamples|samples|n)\b" -- engine tools` lists nothing in engine code (tests may keep `prepare(ctx); start(n)` on one line); `git grep -n "sampleRate_\|int sr_;" -- engine/include` lists only the two `WaveEvolution` parts (Task 4's note) and `partials.h`'s derived float if kept; `git grep -nE "\((int sampleRate|int sr)\b" -- engine/include` lists nothing.
- [ ] **Step 2: Every gate** — `python tools/gates.py` (all): everything passes except the known roundtrip set (8 render diffs, 0 id changes, same names). `python tools/null_gate_wiring.py` 80/80.
- [ ] **Step 3: Whole-tree A/B** — the Appendix A procedure of report R2 with `cli_pre22.exe` against the final binary: library, baselines, audition, pending in full; sweep and old every third. Expected 0 differ everywhere. Any difference is ruled on in the Ledger before the report is written (a listed, explained change is the spec's allowance; an unexplained one is a bug).
- [ ] **Step 4: Render time** — `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre22.exe build/tools/mforce_cli/Release/mforce_cli.exe 7` on oboe1, piano_default, viola_default: the checkpoint should be neutral (the per-sample path is untouched); record the ranges in section 5 either way.
- [ ] **Step 5: Meter and delta** — `python tools/structure/check.py --snapshot`; `python tools/structure/check.py --delta <sha from Task 1 step 0>`.
- [ ] **Step 6: Independent review** — a fresh Fable reviewer on the checkpoint's commits with `docs/architecture/REVIEW_BRIEF.md`, given the diff, `MODULES.md` and Part 2 sections 4-5, told to refute; fix what is right in a fix wave; re-review the fixes. Its findings go into section 8 verbatim.
- [ ] **Step 7: Architecture report R3** — `docs/autonomy/refactor/reports/<date>-checkpoint-2.2.md` in the ten-section format: entries for the changed `ValueSource` contract, `RenderContext`, `MonoSource`/`StereoMixer`, `PerformSource`, `Instrument`, the registry factory, the loader's two creation sites and the UI's one; section 3 (interfaces) lists the 44 constructors, the three envelope factories, `compile_pitch_curve`, `envelope_from_preset_json`, `build_graph`; section 5 the delta, the A/B and the timing; section 6 the Ledger's rulings; section 7 the debt (the `start` bodies that still allocate, with file and line, for 2.6; `WaveEvolution`'s own rate copy for Part 3; anything the refuter listed and the wave left); section 10 two lessons (template method with two phases and why neither is virtual; what assignment copies and what it must not — the registry rule and the context rule, against C#'s reference assignment). REVIEW R3 awaiting; BACKLOG 2.2 done, 2.3 next; STATUS handoff.
- [ ] **Step 8: Commit** — `git commit -m "docs: checkpoint 2.2 report (R3), REVIEW R3 awaiting, backlog, STATUS handoff, meter snapshot"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **The base holds the context; nodes drop their copies.** Nineteen node files kept an `int` copy of the rate (Part 2 §5). One protected `RenderContext` in `ValueSource`, read through `sample_rate()`, replaces them all; a node may keep a *derived* float. Alternative: no storage, every rate-dependent constant computed in `on_prepare` — rejected because a dozen nodes divide by the rate per sample.
2. **The context survives assignment.** `Envelope::replace_stages` assigns a fresh, unprepared envelope over a prepared one; without this rule every preset rebuild would zero the rate. The rule is the registry rule from checkpoint 2.1 extended: what the host attached to this object is not node state.
3. **`prepare(ctx)` is idempotent, "build if absent or if the rate changed".** Required by the migration shim (per note until the close) and by the UI (per play). It also makes a rate change at the host a plain re-`prepare`.
4. **`start` reproduces today's per-note effect, not a fresh node.** RedNoise and the Butterworth filters carry state across the notes a voice slot plays today; the split keeps that. Vibrato alone needs fresh semantics (it rebuilt its chain per note) and gets them by resetting the chain it owns (`RedNoiseSource::reset()`), not by changing RedNoise's `on_start`.
5. **`MonoSource::render` loses the context** it only forwarded to `prepare`; `MonoSource::prepare(ctx)` is the new door. The spec's "created in one place instead of forty" is not met while every render call makes one.
6. **The UI prepares at every play** (stream start, Generate), from one `g_renderContext` taken from the opened stream. A play is not a note; prepare is idempotent; and the editor graph is re-wired between plays. The live keyboard's voices are prepared once by the loader and only started per note.
7. **The factory loses the rate at the close, not per family.** Migrated types ignore the parameter until Task 9; one signature change, one commit.
8. **Allocation at Setup is tolerated through 2.6; allocation inside `start` is not removed here.** A per-note push binding may rebuild a preset envelope or Vibrato's chain (as today's per-note prepare did, earlier in the same note). The per-note `assign`s inside Partials, HybridKS's `fill_table` and AdditiveSource2's resizes stay where they are, listed in section 7 for checkpoint 2.6's test.
9. **`Envelope`'s protected constructor takes a tag**, `SustainDefault{level}`, because `Envelope(float)` would accept the old `Envelope(48000)` silently; **Vibrato's constructor takes only its seed** for the same reason, its seven floats being setting initialisers the loader now sets by name.
10. **`PerformSource` gets `prepare(ctx)`** rather than keeping the rate argument on `set_instrument_state`: it is a per-voice object that outlives notes, prepared by the instrument with the voice.

(Execution rulings follow here.)

- **Task 2 (`1a55d88`), from its review:** an Envelope's stage counts are now sized as stages are added, so a never-started envelope holds zero counts rather than an empty vector; `next_raw_` reaches 0 through the stage walk instead of the early return, and `retrigger` / `gate_release` no longer early-return on one. Output is 0 either way and nothing in the engine starts without `start`; the UI's display cascade still reads 0. Recorded, not changed. FMSource also rebuilds its sections when the rate changes (recipe step 3), which the table did not spell out.
- **A/B base (Task 1 step 0):** `renders/scratch/ab_tree/cli_pre22.exe` is the CLI built from the tree at `1145a52` (engine last changed at `9f2b4a8`; the UI stamp read "stale: no" before the copy). Every family A/B and the close's whole-tree A/B compare against it.

## Self-review notes

- **Spec coverage.** Part 2 §4 "the split": Task 1 (the two calls, the hooks, the non-allocating `start`), Tasks 2-8 (every node), Task 9 (block streaming becomes possible: `StereoMixer::render` no longer re-prepares; RepeatingSource restarts its child with `start`; Vibrato's rebuild moves to `prepare`). §4 "byte-identity": the registry sweep (Task 1) plus the per-family and whole-tree A/B; the one class the spec names (fresh draws in a per-note rebuild) is Vibrato, handled by ruling 4. §5 "the rule": constructors and the factory lose the rate (Tasks 2-9), `RenderContext` stays and is created in the loader (Task 9 step 5), the file's rate is the CLI's render default (unchanged), the UI tells the engine the stream's rate (Task 9 step 7). §5 "backlog 69 stays a list": no 48k-baked constant is touched. §7's 2.2 row: expected render changes none; the close lists any.
- **Step scan.** Every table row names the constructor, the two hook bodies and the reads that change; the two places the implementer must look up are named (FIR/IIR `vals` for `reset()`, the `engine_tests` construction to copy for the wavetable case).
- **Type consistency.** `prepare(const RenderContext&)`, `start(int)`, `on_prepare(const RenderContext&)`, `on_start(int)`, `sample_rate()`, `render_context()`, `prepared()`, `RedNoiseSource::reset()`, `BWLPSection::reset()`, `partials_prepare/partials_start`, `fmt_prepare/fmt_start`, `WaveEvolution::prepare`, `MonoSource::prepare/render`, `StereoMixer::prepare/render`, `PerformSource::prepare`, `Instrument::ctx`, `Envelope::SustainDefault`, `split_equivalent` are used with those exact names throughout.
- **Review Focus.** Items 1-4 have their tests in Tasks 1, 1, 7 and 2/7; item 5 is a documentation obligation on Task 10 by design (no mechanical test exists until 2.6).
- **Proportion.** Code appears for the contract (Task 1) and the harness; every node is a table row. The tables are the plan; the compiler enforces the rest.

# Feedback Loops Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make feedback loops first-class in the ValueSource graph — tap edges (guarded RefSource, implicit z⁻¹), a starved-node advance list, DelayLine and Shaper nodes, UI tap wiring, and a Shaper client in the shape editor.

**Architecture:** Cycles become legal when broken by a "tap" edge that reads `current()` (last tick's value) instead of pulling `next()`. Taps are guarded RefSources (NaN scrub + clamp). Nodes consumed only by taps go on a per-voice advance list ticked after the root pull. Two new registry nodes (DelayLine, Shaper) supply the resonator and the drawn nonlinearity.

**Tech Stack:** C++17 header-per-node engine (`engine/include/mforce/`), nlohmann::json loaders, ImGui/imnodes UI (`tools/mforce_ui/main.cpp`), assert-style tests in `tools/engine_tests/main.cpp`, render-based validation via `mforce_cli` + `tools/null_test_manifest.py`.

**Spec:** `docs/feedback_loop_design.md` — read it first; this plan implements it section by section.

## Global Constraints

- No heap allocation in hot render loops (buffers sized at construction).
- Null gate must stay clean: `guard` defaults false, `{"tap": ...}` absent from all existing patches → every existing render byte-identical. Frozen manifest: `tools/null_gate_manifest.json`. Run the gate before each engine-touching commit (standing rule: once per commit batch, not per tweak).
- Registry pattern: explicit registration in `engine/src/source_registrations.cpp`, self-describing descriptors, generic wiring. No reflection.
- Seeds: none of the new nodes are stochastic; no seed plumbing needed.
- Build: `cmake --build build --target <t> --config Release` from repo root. CLI at `build\tools\mforce_cli\Release\mforce_cli.exe`, tests at `build\tools\engine_tests\Release\engine_tests.exe`.
- The UI exe may be running (Matt); if the link step fails with LNK1104, rename the running exe (`mforce_ui_running.exe`) and relink.
- Commit messages: repo style (`engine(...)`, `ui:`, `docs:` prefixes), `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 1: RefSource guard flag

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h:131-142` (RefSource)
- Test: `tools/engine_tests/main.cpp`

**Interfaces:**
- Produces: `RefSource(std::shared_ptr<ValueSource> src, bool g = false)` with public `bool guard`; guarded reads scrub non-finite to 0 and clamp to ±8.0. `source` member stays public (Task 3's pass-2 bind assigns it directly).

- [ ] **Step 1: Write the failing test.** Append to `tools/engine_tests/main.cpp` (before `main`), and add `run_tap_guard_tests();` inside `main` alongside the existing run_* calls:

```cpp
static void run_tap_guard_tests() {
    // Unguarded RefSource: passes values through untouched (existing behavior)
    auto c = std::make_shared<ConstantSource>(42.0f);
    RefSource plain(c);
    CHECK_NEAR(plain.next(), 42.0f, 1e-6f);

    // Guarded: clamp to +-8
    RefSource g1(c, true);
    CHECK_NEAR(g1.next(), 8.0f, 1e-6f);
    c->set(-100.0f); c->next();
    CHECK_NEAR(g1.next(), -8.0f, 1e-6f);
    c->set(3.5f); c->next();
    CHECK_NEAR(g1.next(), 3.5f, 1e-6f);

    // Guarded: NaN/inf scrub to 0
    c->set(std::nanf("")); c->next();
    CHECK_NEAR(g1.next(), 0.0f, 1e-6f);
    c->set(INFINITY); c->next();
    CHECK_NEAR(g1.next(), 0.0f, 1e-6f);

    // Null source still reads 0 (pass-2 placeholder state)
    RefSource empty(nullptr, true);
    CHECK_NEAR(empty.next(), 0.0f, 1e-6f);
}
```

The test file needs `#include "mforce/core/dsp_value_source.h"` (already pulled in via curve_node.h, but add explicitly if the build complains) and `<cmath>` for `nanf`/`INFINITY` (already included).

- [ ] **Step 2: Run test to verify it fails.**

Run: `cmake --build build --target engine_tests --config Release` then `build\tools\engine_tests\Release\engine_tests.exe`
Expected: compile FAILURE — `RefSource` has no two-argument constructor. (A compile failure is this step's "red".)

- [ ] **Step 3: Implement.** Replace the RefSource struct body in `dsp_value_source.h`:

```cpp
// Transparent wrapper for shared sources with multiple consumers.
// The primary consumer calls next() on the real source; secondary consumers
// use a RefSource which just reads current() without advancing.
// Created automatically by the UI when multiple inputs wire to the same output.
// guard=true is the TAP form (feedback_loop_design.md): reads are armored —
// non-finite scrubbed to 0, clamped to +-8 — so a runaway loop saturates
// audibly instead of poisoning the graph. The z-1 a tap provides is
// positional (tap consumers evaluate before their source each tick), not
// implemented here.
struct RefSource final : ValueSource {
  std::shared_ptr<ValueSource> source;
  bool guard{false};

  explicit RefSource(std::shared_ptr<ValueSource> src, bool g = false)
    : source(std::move(src)), guard(g) {}

  void prepare(const RenderContext& /*ctx*/, int /*frames*/) override {} // primary consumer prepares the real source
  float next() override { return read(); }
  float current() const override { return read(); }

  const char* type_name() const override { return "RefSource"; }
  SourceCategory category() const override { return SourceCategory::Utility; }

private:
  float read() const {
    float v = source ? source->current() : 0.0f;
    if (!guard) return v;
    if (!std::isfinite(v)) return 0.0f;
    return v < -8.0f ? -8.0f : (v > 8.0f ? 8.0f : v);
  }
};
```

`<cmath>` must be included by the header for `std::isfinite` — add `#include <cmath>` to the header's include block if not present.

- [ ] **Step 4: Run test to verify it passes.** Rebuild `engine_tests`, run; expected `0 fails`.

- [ ] **Step 5: Null gate + commit.** Run `python tools/null_test_manifest.py <scratchpad>\null_t1.txt`, then diff against `tools/null_gate_manifest.json` (`git diff --no-index tools/null_gate_manifest.json <scratchpad>\null_t1.txt`). Expected: identical. Commit:

```bash
git add engine/include/mforce/core/dsp_value_source.h tools/engine_tests/main.cpp
git commit -m "engine(refsource): guard flag for tap edges - NaN scrub + clamp"
```

---

### Task 2: Positional z⁻¹ semantics test (no production code)

**Files:**
- Test: `tools/engine_tests/main.cpp`

**Interfaces:**
- Consumes: Task 1's guarded RefSource.
- Produces: nothing — this task pins the semantic the whole subsystem rests on, at the object level, before any loader work.

- [ ] **Step 1: Write the test.** A self-referencing accumulator: `counter = CombinedSource(Add, Constant(1), tap-of-counter)`. Each tick reads its own previous value plus one → 1, 2, 3, ... clamped at 8 by the guard. Append and register `run_tap_cycle_tests();` in `main`:

```cpp
#include "mforce/source/combined_source.h"

static void run_tap_cycle_tests() {
    auto counter = std::make_shared<CombinedSource>(
        std::make_shared<ConstantSource>(1.0f),
        std::make_shared<ConstantSource>(0.0f),   // replaced by the tap below
        CombineOp::Add, 0.0f);
    auto tap = std::make_shared<RefSource>(counter, true);
    counter->set_param("source2", tap);

    // Tick 1: tap reads counter's initial current() (0) -> 1. Then 2, 3...
    CHECK_NEAR(counter->next(), 1.0f, 1e-6f);
    CHECK_NEAR(counter->next(), 2.0f, 1e-6f);
    CHECK_NEAR(counter->next(), 3.0f, 1e-6f);
    for (int i = 0; i < 20; ++i) counter->next();
    CHECK_NEAR(counter->current(), 8.0f, 1e-6f);   // guard ceiling, not inf
}
```

If `CombinedSource`'s param names differ (check `param_descriptors()` in `engine/include/mforce/source/combined_source.h` — the pins are named `source1`/`source2` there; verify before writing), use the names the descriptor declares. If CombinedSource's initial `current()` is not 0, adjust the expected ramp accordingly — the invariant under test is *previous-tick read + no runaway*, i.e. strictly increasing by 1 until the clamp.

- [ ] **Step 2: Run.** Rebuild `engine_tests`, run. Expected: PASS (Task 1 already supplied the mechanics; if this fails, the positional-z⁻¹ premise is broken — stop and re-read spec §2 before touching anything).

- [ ] **Step 3: Commit.**

```bash
git add tools/engine_tests/main.cpp
git commit -m "engine(tests): pin tap z-1 semantics - self-referencing counter"
```

---

### Task 3: Loader tap resolution (two-pass) + advance list

**Files:**
- Modify: `engine/src/patch_loader.cpp` — `resolve_param` (:85), `build_graph`, the voice-pool build (:1276) and DrumKit build (:1508), `promote_starved_refs` (:148)
- Modify: `engine/include/mforce/render/instrument.h` — `VoiceGraph` (:97), `StreamingVoice` (:154), `play_note` (:241), `prepare_voice_at` (:220), `DrumKit` (:286)
- Test: `patches/baselines/feedback/loop_tap_counter.json` (new) + null gate

**Interfaces:**
- Consumes: `RefSource(src, true)`, public `source` member (Task 1).
- Produces: JSON form `{"tap": "<nodeId>"}` accepted anywhere `{"ref": ...}` is; `VoiceGraph::advanceList` and `StreamingVoice::advanceList` (both `std::vector<std::shared_ptr<ValueSource>>`); streaming callers tick the list once per sample after `source->next()`.

- [ ] **Step 1: Write the failing test patch.** Create `patches/baselines/feedback/loop_tap_counter.json` — the Task 2 counter as a patch. The graph: Constant 1 → CombinedSource `source1`; CombinedSource taps itself into `source2`; output = the CombinedSource. Follow the exact JSON shape of an existing baseline patch (open one from `patches/baselines/` for the envelope: `graph.nodes[]` with `id`/`type`/`params`, `graph.output`, an `instrument` block with polyphony 1, and a minimal embedded score playing one note ~0.2 s). The node entry that matters:

```json
{ "id": "counter", "type": "CombinedSource",
  "params": { "source1": 1.0, "source2": { "tap": "counter" },
              "operation": "add" } }
```

- [ ] **Step 2: Run to verify it fails.** `build\tools\mforce_cli\Release\mforce_cli.exe patches\baselines\feedback\loop_tap_counter.json <scratchpad>\tap_counter.wav`
Expected: FAIL — "Param must be a number or {\"ref\":\"...\"}" from `resolve_param`.

- [ ] **Step 3: Implement tap resolution.** In `patch_loader.cpp`:

(a) Thread a pass-2 bind list. Define near `resolve_param`:

```cpp
// Tap edges ({"tap": "<id>"}, feedback_loop_design.md §3.2) may point
// FORWARD in nodeOrder — that is their purpose — so they resolve in a
// second pass: pass 1 installs a guarded RefSource with a null target
// (reads 0), pass 2 binds targets once every node exists.
struct TapBind { std::shared_ptr<RefSource> ref; std::string targetId; };
```

(b) In `resolve_param`, before the final `throw`, add (and give `resolve_param` + `resolve_param_or` + the `ResolveParamFn` lambdas that wrap them an extra `std::vector<TapBind>* taps = nullptr` parameter, threaded from `build_graph`):

```cpp
    if (val.is_object() && val.contains("tap")) {
        auto rs = std::make_shared<RefSource>(nullptr, true);
        if (taps) taps->push_back({rs, val.at("tap").get<std::string>()});
        return rs;
    }
```

Update the error message to `"Param must be a number, {\"ref\":\"...\"} or {\"tap\":\"...\"}"`.

(c) At the end of `build_graph`, after all nodes are constructed, bind:

```cpp
    for (auto& tb : g.tapBinds) {
        auto it = g.valueNodes.find(tb.targetId);
        if (it == g.valueNodes.end())
            throw std::runtime_error("Unresolved tap target: " + tb.targetId);
        tb.ref->source = it->second;
    }
```

(`build_graph`'s result struct gains `std::vector<TapBind> tapBinds;`. Note: tap edges must NOT increment the ref-usage counter in `resolve_param` — a tap never advances, so it must never claim the advancing-consumer role.)

- [ ] **Step 4: Implement the advance list.** Still in `patch_loader.cpp`:

(a) A helper that decides membership from the JSON (same evidence base as `promote_starved_refs` — read its header comment at :120 before writing this; the built graph is not fully walkable):

```cpp
// Advance list (feedback_loop_design.md §3.3): nodes consumed ONLY by tap
// edges are never ticked by the pull — collect them, in nodeOrder, for the
// voice to tick after the root pull. Consumers are enumerated from the
// JSON: every {"ref":id}/{"tap":id} occurrence in every node's params.
static std::vector<std::string> collect_advance_ids(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::vector<std::string>& nodeOrder,
    const std::string& outputId);
```

Implementation: walk every node's `params` recursively (mirror the existing JSON edge walk inside `promote_starved_refs`, which already handles nested arrays/objects); build two sets per node id: `hasNormalConsumer`, `hasTapConsumer`. Result = ids where `hasTapConsumer && !hasNormalConsumer && id != outputId`, in nodeOrder.

(b) In the voice-pool build (:1276) and the DrumKit build (:1508), after `promote_starved_refs`:

```cpp
            for (const auto& id : collect_advance_ids(nodeMap, nodeOrder, outputId))
                vg.advanceList.push_back(g.valueNodes.at(id));
```

(c) `promote_starved_refs` sighted-ness fix (spec §3.3 hazard): its JSON-reachability walk from `outputId` must ALSO start from every advance-list id — an advance-list node ticks every sample, so its advancing consumers are live. Pass `collect_advance_ids(...)` results in as extra walk roots.

(d) In `instrument.h`: add to `VoiceGraph` and `StreamingVoice`:

```cpp
    // Nodes consumed only by tap edges — ticked once per sample after the
    // root pull (feedback_loop_design.md §3.3). Streaming callers MUST tick
    // these like performSource->tick(); play_note does it internally.
    std::vector<std::shared_ptr<ValueSource>> advanceList;
```

In `play_note`'s render loop (:261) after `buf[i] = vg.source->next() * gain;`:

```cpp
      for (auto& a : vg.advanceList) a->next();
```

In `prepare_voice_at` (:236) after `vg.source->prepare(ctx, durSamples);`:

```cpp
    for (auto& a : vg.advanceList) a->prepare(ctx, durSamples);
```

and include `vg.advanceList` in the returned StreamingVoice: `return { vg.source, durSamples, gain, vg.performSource, vg.advanceList };` (append the member after `performSource` so aggregate init order matches).

`DrumKit` has no VoiceGraph; give `DrumSource` its own `advanceList` filled at :1508's build, prepare it in `play_hit`, and tick it in the `play_hit` loop the same way.

(e) In `tools/mforce_ui/main.cpp`, find the audio callback's per-sample loop (grep `performSource->tick()` — the comment at instrument.h:163 names the contract) and add the same advance-list tick after each `source->next()` for live voices.

- [ ] **Step 4½: Starved-advancement test patch.** The counter patch does NOT exercise the advance list (its tapped node IS the output, which the pull advances). Create `patches/baselines/feedback/loop_tap_starved.json`: node `b` = CombinedSource(add, source1 = 1.0, source2 = `{"tap": "b"}`) — a self-counter; node `a` = CombinedSource(add, source1 = 0.0, source2 = `{"tap": "b"}`); output = `a`. Node `b` is consumed ONLY by tap edges → advance list = {b}; the render ramps 1, 2, 3... at the output only if the list ticks. Before the advance list exists this renders all-zeros — that is this task's second red. Verify with the same ramp check as Step 5.

- [ ] **Step 5: Run to verify it passes.** Render the counter patch again. Then verify content: the counter ramps 1..8 then holds 8 (times amplitude scaling downstream); assert with a quick python check that the wav's first nonzero samples are strictly increasing then flat — write it inline in the scratchpad, don't commit it:

```python
import wave, struct
w = wave.open(r"<scratchpad>\tap_counter.wav"); n = w.getnframes()
raw = struct.unpack("<%dh" % (n * w.getnchannels()), w.readframes(n))
nz = [s for s in raw if s != 0][:400]
assert nz and all(b >= a for a, b in zip(nz, nz[1:])), "not a clamped ramp"
print("ok: ramp then clamp,", len(nz), "nonzero samples checked")
```

Expected: `ok`. (If the patch clips at the instrument's soft_clip before the guard's 8.0, lower the embedded score velocity — the shape, not the exact ceiling, is the assertion.)

- [ ] **Step 6: Null gate + commit.** Gate as in Task 1 — the new baseline ADDS a line to the regenerated manifest; every pre-existing line must be unchanged. Re-freeze `tools/null_gate_manifest.json` with the new line included (copy the regenerated file over it) — that is the established re-freeze pattern. Commit:

```bash
git add engine/src/patch_loader.cpp engine/include/mforce/render/instrument.h tools/mforce_ui/main.cpp patches/baselines/feedback/ tools/null_gate_manifest.json
git commit -m "engine(loader): tap edges + advance list - cycles legal via guarded refs"
```

---

### Task 4: DelayLine node

**Files:**
- Create: `engine/include/mforce/source/delay_line_source.h`
- Modify: `engine/src/source_registrations.cpp` (register "DelayLine")
- Test: `tools/engine_tests/main.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `DelayLineSource(int sr)`, registry name `"DelayLine"`, category Oscillator. Pins: `source` (input), `frequency` (hz, default 440), `ratio` (default 1.0). Delay length = sampleRate / frequency * ratio samples, fractional (linear interp), floor 20 Hz. Distinct from the existing `DelayFilter` (fixed-time effect delay) — this one is the pitch-tracked resonator backbone.

- [ ] **Step 1: Write the failing test.** Append + register `run_delay_line_tests();`:

```cpp
#include "mforce/source/delay_line_source.h"

static void run_delay_line_tests() {
    // sr 48000, freq 480 -> exactly 100 samples of delay
    auto dl = std::make_shared<DelayLineSource>(48000);
    dl->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    auto in = std::make_shared<ConstantSource>(0.0f);
    dl->set_param("source", in);
    RenderContext ctx{48000};
    dl->prepare(ctx, 48000);

    // Impulse in at tick 1
    in->set(1.0f);
    float first = dl->next();
    in->set(0.0f);
    CHECK_NEAR(first, 0.0f, 1e-6f);          // empty line
    float out = 0.0f;
    for (int i = 0; i < 99; ++i) out = dl->next();
    CHECK_NEAR(out, 0.0f, 1e-6f);            // still inside the line (tick 100)
    out = dl->next();                        // tick 101: impulse emerges
    CHECK_NEAR(out, 1.0f, 1e-3f);
    out = dl->next();
    CHECK_NEAR(out, 0.0f, 1e-3f);            // and passes

    // ratio doubles the period: 200 samples
    auto dl2 = std::make_shared<DelayLineSource>(48000);
    dl2->set_param("frequency", std::make_shared<ConstantSource>(480.0f));
    dl2->set_param("ratio", std::make_shared<ConstantSource>(2.0f));
    auto in2 = std::make_shared<ConstantSource>(0.0f);
    dl2->set_param("source", in2);
    dl2->prepare(ctx, 48000);
    in2->set(1.0f); dl2->next(); in2->set(0.0f);
    for (int i = 0; i < 199; ++i) out = dl2->next();
    out = dl2->next();
    CHECK_NEAR(out, 1.0f, 1e-3f);
}
```

- [ ] **Step 2: Run to verify it fails.** Rebuild `engine_tests`. Expected: compile failure (missing header).

- [ ] **Step 3: Implement.** New header `engine/include/mforce/source/delay_line_source.h`:

```cpp
#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>
#include <algorithm>

namespace mforce {

// Pitch-tracked delay line — the resonator backbone for feedback loops
// (feedback_loop_design.md §3.4). Pure delay: no internal feedback, no
// damping; those are the graph's job. Length = sampleRate/frequency*ratio
// samples, fractional read (linear interp), modulatable per sample (mild
// artifacts accepted in v1). Buffer sized at construction for a 20 Hz
// floor — no heap in the render loop; frequencies below floor clamp.
struct DelayLineSource final : ValueSource {
  int sampleRate{48000};

  explicit DelayLineSource(int sr)
    : sampleRate(sr),
      source_(std::make_shared<ConstantSource>(0.0f)),
      frequency_(std::make_shared<ConstantSource>(440.0f)),
      ratio_(std::make_shared<ConstantSource>(1.0f)),
      buf_(size_t(sr / 20 + 4), 0.0f) {}

  const char* type_name() const override { return "DelayLine"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"source",    0.0f, -10.0f, 10.0f, nullptr},
      {"frequency", 440.0f, 20.0f, 20000.0f, "hz"},
      {"ratio",     1.0f, 0.05f, 20.0f, "ratio"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    { source_ = std::move(src); return; }
    if (name == "frequency") { frequency_ = std::move(src); return; }
    if (name == "ratio")     { ratio_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "frequency") return frequency_;
    if (name == "ratio")     return ratio_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    source_->prepare(ctx, frames);
    frequency_->prepare(ctx, frames);
    ratio_->prepare(ctx, frames);
    std::fill(buf_.begin(), buf_.end(), 0.0f);
    writeIdx_ = 0;
    cur_ = 0.0f;
  }

  float next() override {
    const float in = source_->next();
    frequency_->next();
    ratio_->next();
    const float f = std::max(frequency_->current(), 20.0f);
    const float len = std::clamp(float(sampleRate) / f * ratio_->current(),
                                 1.0f, float(buf_.size()) - 2.0f);
    buf_[size_t(writeIdx_)] = in;
    float rp = float(writeIdx_) - len;
    if (rp < 0.0f) rp += float(buf_.size());
    const int i0 = int(rp);
    const int i1 = (i0 + 1) % int(buf_.size());
    const float frac = rp - float(i0);
    cur_ = buf_[size_t(i0)] * (1.0f - frac) + buf_[size_t(i1)] * frac;
    writeIdx_ = (writeIdx_ + 1) % int(buf_.size());
    return cur_;
  }

  float current() const override { return cur_; }

private:
  std::shared_ptr<ValueSource> source_, frequency_, ratio_;
  std::vector<float> buf_;
  int writeIdx_{0};
  float cur_{0.0f};
};

} // namespace mforce
```

Register in `source_registrations.cpp` (KS piano blocks section, after AllpassResonator):

```cpp
    reg.register_type("DelayLine", SourceCategory::Oscillator,
        [](int sr, auto) { return std::make_shared<DelayLineSource>(sr); });
```

with `#include "mforce/source/delay_line_source.h"` in the include block. NOTE: if the write-then-read order in `next()` makes the measured delay off by one against the test (read-before-write vs write-before-read), fix the CODE to match the test's contract (impulse at tick T emerges at tick T+len), not the test.

- [ ] **Step 4: Run to verify it passes.** Rebuild + run `engine_tests`; expected `0 fails`.

- [ ] **Step 5: Commit.** (Registry touched, no existing-node behavior change — gate deferred to Task 5's freeze.)

```bash
git add engine/include/mforce/source/delay_line_source.h engine/src/source_registrations.cpp tools/engine_tests/main.cpp
git commit -m "engine(delayline): pitch-tracked fractional delay - loop resonator backbone"
```

---

### Task 5: Shaper node

**Files:**
- Create: `engine/include/mforce/source/shaper_source.h`
- Modify: `engine/src/source_registrations.cpp` (register "Shaper")
- Test: `tools/engine_tests/main.cpp`

**Interfaces:**
- Consumes: `SmoothnessInterpolator` (`mforce/core/smoothness_interpolator.h`), same construction as SegmentSource's (`{smooth, false}`).
- Produces: `ShaperSource`, registry name `"Shaper"`, category Modulator. Pins: `source`, `drive` (default 1.0), `smoothness` (default 0.5). Array `values` = absolute (x, y) breakpoint pairs ascending in x, default identity `[-1,-1, 1,1]`. Transfer: `y = curve(drive * x)`, clamped to end values outside the drawn range.

- [ ] **Step 1: Write the failing test.** Append + register `run_shaper_tests();`:

```cpp
#include "mforce/source/shaper_source.h"

static void run_shaper_tests() {
    RenderContext ctx{48000};
    // Default identity curve passes input through
    auto sh = std::make_shared<ShaperSource>();
    auto in = std::make_shared<ConstantSource>(0.5f);
    sh->set_param("source", in);
    sh->prepare(ctx, 100);
    CHECK_NEAR(sh->next(), 0.5f, 1e-3f);

    // Clamp past the drawn ends
    in->set(3.0f); in->next();
    CHECK_NEAR(sh->next(), 1.0f, 1e-3f);
    in->set(-3.0f); in->next();
    CHECK_NEAR(sh->next(), -1.0f, 1e-3f);

    // drive scales input BEFORE the lookup: drive 2 pushes 0.4 to 0.8
    sh->set_param("drive", std::make_shared<ConstantSource>(2.0f));
    in->set(0.4f); in->next();
    CHECK_NEAR(sh->next(), 0.8f, 1e-3f);

    // A drawn dead-zone curve: flat 0 across [-0.5, 0.5], ramps outside
    auto dz = std::make_shared<ShaperSource>();
    dz->set_array("values", {-1.0f, -1.0f, -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f});
    dz->set_param("smoothness", std::make_shared<ConstantSource>(0.0f));
    auto in2 = std::make_shared<ConstantSource>(0.2f);
    dz->set_param("source", in2);
    dz->prepare(ctx, 100);
    float mid = dz->next();
    CHECK(std::fabs(mid) < 0.05f);           // inside the dead zone
    in2->set(0.75f); in2->next();
    float hi = dz->next();
    CHECK(hi > 0.2f);                        // on the outer ramp
}
```

- [ ] **Step 2: Run to verify it fails.** Rebuild; expected compile failure.

- [ ] **Step 3: Implement.** New header `engine/include/mforce/source/shaper_source.h`:

```cpp
#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/smoothness_interpolator.h"
#include <memory>
#include <vector>
#include <algorithm>

namespace mforce {

// Per-sample drawn-curve transfer: y = curve(drive * x) — the junction
// nonlinearity for feedback loops (feedback_loop_design.md §3.5), and a
// free waveshaper open-loop. values[] holds ABSOLUTE (x, y) breakpoints
// ascending in x (NOT the SegmentSource delta form); evaluation is
// piecewise between breakpoints through SmoothnessInterpolator, clamped
// to the end values outside the drawn range. Default = identity.
struct ShaperSource final : ValueSource {
  ShaperSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      drive_(std::make_shared<ConstantSource>(1.0f)),
      smoothness_(std::make_shared<ConstantSource>(0.5f)),
      values_{-1.0f, -1.0f, 1.0f, 1.0f} {}

  const char* type_name() const override { return "Shaper"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"source",     0.0f, -10.0f, 10.0f, nullptr},
      {"drive",      1.0f, 0.0f, 20.0f, "ratio"},
      {"smoothness", 0.5f, 0.0f, 1.0f, "0-1"},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "drive")      { drive_ = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")     return source_;
    if (name == "drive")      return drive_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  std::span<const ArrayDescriptor> array_descriptors() const override {
    static constexpr ArrayDescriptor descs[] = {
      {"values", nullptr, 0.0f, -2.0f, 2.0f},
    };
    return descs;
  }
  void set_array(std::string_view name, std::vector<float> v) override {
    if (name == "values" && v.size() >= 4 && v.size() % 2 == 0)
      values_ = std::move(v);
  }
  std::vector<float> get_array(std::string_view name) const override {
    if (name == "values") return values_;
    return {};
  }

  void prepare(const RenderContext& ctx, int frames) override {
    source_->prepare(ctx, frames);
    drive_->prepare(ctx, frames);
    smoothness_->prepare(ctx, frames);
    cur_ = 0.0f;
  }

  float next() override {
    const float x = source_->next() * drive_->next();
    smoothness_->next();
    interp_.setSmoothness(smoothness_->current());
    cur_ = map(x);
    return cur_;
  }

  float current() const override { return cur_; }

  // Public for the UI preview/editor overlay (mirrors CurveNode::map).
  float map(float x) {
    const size_t n = values_.size() / 2;
    auto px = [&](size_t i) { return values_[i * 2]; };
    auto py = [&](size_t i) { return values_[i * 2 + 1]; };
    if (n == 0) return x;
    if (x <= px(0))     return py(0);
    if (x >= px(n - 1)) return py(n - 1);
    for (size_t i = 1; i < n; ++i) {
      if (x <= px(i)) {
        const float w = px(i) - px(i - 1);
        const float pos = w > 0.0f ? (x - px(i - 1)) / w : 1.0f;
        return interp_.interpolate(py(i - 1), py(i), pos);
      }
    }
    return py(n - 1);
  }

private:
  std::shared_ptr<ValueSource> source_, drive_, smoothness_;
  std::vector<float> values_;
  SmoothnessInterpolator interp_{0.5f, false};
  float cur_{0.0f};
};

} // namespace mforce
```

Register (Modulators section):

```cpp
    reg.register_type("Shaper", SourceCategory::Modulator,
        [](int, auto) { return std::make_shared<ShaperSource>(); });
```

Check `SmoothnessInterpolator`'s actual setter name in `smoothness_interpolator.h` (`setSmoothness` is used at `tools/mforce_ui/main.cpp` — mirror whatever the header declares).

- [ ] **Step 4: Run to verify it passes.** Rebuild + run `engine_tests`; `0 fails`.

- [ ] **Step 5: Null gate + commit.** Full gate (two new registrations since last freeze); every pre-existing line unchanged; re-freeze including the Task 3 baseline if not already done.

```bash
git add engine/include/mforce/source/shaper_source.h engine/src/source_registrations.cpp tools/engine_tests/main.cpp tools/null_gate_manifest.json
git commit -m "engine(shaper): per-sample drawn-curve transfer - the loop junction"
```

---

### Task 6: UI tap pin, wiring, cycle check, save/load

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — `Pin` struct (:85), `GraphNode::build_pins` (:512), `update_all_dsp` (:863-880 wiring), `IsLinkCreated` handler (:11890), save (`save_patch_graph`, params emission ~:2689), load (pin-value ingestion ~:1628)

**Interfaces:**
- Consumes: `RefSource(src, true)` (Task 1); `{"tap": "<label>"}` file form (Task 3).
- Produces: every DSP node face gets one extra output pin named `tap` (`Pin::isTap == true`); tap wires save as `{"tap": ...}` and reload as tap links; a normal wire that would close an all-normal cycle is refused with a status message.

- [ ] **Step 1: Pin flag + build_pins.** Add `bool isTap{false};` to `Pin` (:85 block). In `build_pins`'s generic branch (:578-583), after the `out` pin:

```cpp
        outputs.emplace_back("tap", PinKind::Output);
        outputs.back().isTap = true;
```

Do the same for the Envelope branch (:541-548). Special UI-only types (mixer, output, Parameter, PerformNode) get NO tap pin. Render the tap pin visually small/secondary — find where output pins draw their imnodes attribute and give tap pins a dimmer color (follow the gold-pin precedent for styling hooks; NOT gold itself — pick a dashed/teal style distinct from both normal wires and gold dynamic-pin wires).

- [ ] **Step 2: Cycle check on link creation.** In the `IsLinkCreated` handler (:11890), before accepting a NORMAL (non-tap) link, run a DFS from the destination node following only existing normal links; if it reaches the source node, refuse:

```cpp
                // Cycle legality (feedback_loop_design.md §2): a normal wire
                // may not close a cycle — loops close through a tap pin.
                if (!outPinIsTap && would_close_normal_cycle(outNode, inNode)) {
                    transport_set_status(
                        "Cycle - close loops through a tap pin", true);
                    accept = false;
                }
```

`would_close_normal_cycle(from, to)`: standard visited-set DFS over `s_links` (skip links whose start pin `isTap`), starting at `from`'s consumers... i.e. does a normal-edge path exist `to → ... → from`? Write it next to the other graph helpers (`find_source_node` etc.). Tap wires skip the check entirely.

- [ ] **Step 3: Wiring.** In `update_all_dsp`'s per-pin wiring (:878 region) and the link-resolution path (:898 region), when the link's source pin `isTap`, wire the consumer with `std::make_shared<RefSource>(srcNode->dspSource, true)` instead of the raw source, and do NOT count it toward the shared-output auto-RefSource usage logic.

- [ ] **Step 4: Save/load.** Save: where a connected input pin currently emits `{"ref": "<label>"}` into `params[pin.name]` (~:2689 and the second site ~:2977), emit `{"tap": "<label>"}` when the feeding link is a tap link. Load: where `{"ref"}` pin values reconstruct links (~:1628 region and JSON-extras ingestion), accept `{"tap"}` and mark the created link/pin association as tap (link stores tap-ness by its start pin — loading must target the source node's tap pin, not `out`). Round-trip test is Step 6.

- [ ] **Step 5: Build.** `cmake --build build --target mforce_ui --config Release` (rename-then-link if locked). Expected: clean.

- [ ] **Step 6: Round-trip verification.** Launch is Matt's side; the scriptable check: load `patches/baselines/feedback/loop_tap_counter.json` through the UI's loader and re-save, then diff — the repo has a round-trip harness (`tools/test_stable_roundtrip.py`); run it on the feedback baseline:

Run: `python tools/test_stable_roundtrip.py patches/baselines/feedback/loop_tap_counter.json` (check the script's usage line first; if it takes a directory, point it at `patches/baselines/feedback/`).
Expected: round-trip stable, tap edge preserved.

- [ ] **Step 7: Commit.**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "ui: tap pins + cycle-legal wiring - feedback loops in the editor"
```

---

### Task 7: Shape editor Shaper client

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — `ShapeEditorState` (:5197, `Client` enum), `draw_shape_editor` (:5337), open-dispatch (double-click handling near the SegmentSource preview, ~:8015 region), Properties points table (reuse SegmentSource's pair table pattern)

**Interfaces:**
- Consumes: `ShaperSource::map(float)` (Task 5), `values` array via `node.arrayValues` (same storage path SegmentSource uses, `shape_ed_segment_values` pattern at :5287).
- Produces: double-clicking a Shaper node's preview opens the editor: linear x, four-quadrant, absolute points, engine overlay via `ShaperSource::map`.

- [ ] **Step 1: Add the client.** `Client::Shaper` in the enum; `shape_editor_open_shaper(GraphNode&)` mirroring `shape_editor_open_segment` (:5275) with `logX = false`. In `draw_shape_editor`: Shaper points load straight from the values array as absolute pairs (no delta conversion — write `shape_ed_shaper_load`/`apply` as trivial pair<->flat converters), apply pushes via `node.push_array("values")` + `mark_graph_dirty()` exactly like `shape_editor_apply_segment` (:5306) minus the delta math and minus any timeMode logic. Fit: do NOT force x0=0 (the segment-only branch at :5447) — Shaper fits the drawn x-range with margins, y expanded to at least [-1, 1].

- [ ] **Step 2: Engine overlay.** In the overlay section (:5547 region) add the Shaper branch: instantiate a `ShaperSource` probe, `set_array("values", flat)`, set a ConstantSource smoothness from the node's `smoothness` pin `defaultValue` (NOT `constantSrc->current()` — see the stale-read comments added 2026-08-31), then plot `probe.map(fx(sx))` across the canvas like the CurveNode branch does. Interaction rules: same as curve client (insert sorted by x, drag clamps between neighbors); y clamped to the array descriptor's ±2.

- [ ] **Step 3: Node preview + open dispatch.** Give Shaper nodes the same shape-preview-above-points-table treatment SegmentSource has (:8015): draw `map()` across [-1.5, 1.5] input domain. Wire the double-click to `shape_editor_open_shaper`.

- [ ] **Step 4: Build + verify.** Rebuild `mforce_ui`. Scripted verification is limited here (interactive canvas); the check that IS scriptable: create-save-reload a patch with a non-default Shaper curve via the round-trip harness and confirm the values array survives. Manual polish pass is Matt's.

- [ ] **Step 5: Commit.**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "ui: shape editor Shaper client - four-quadrant drawn transfer curves"
```

---

### Task 8: Reference loop patches

**Files:**
- Create: `patches/baselines/feedback/loop_bowed.json`
- Create: `patches/baselines/feedback/loop_selfosc.json`
- Modify: `tools/null_gate_manifest.json` (re-freeze with the two new lines)

**Interfaces:**
- Consumes: everything above.
- Produces: the two spec §6 reference patches, rendering non-silent, gate-frozen.

- [ ] **Step 1: loop_bowed.json.** The bowed-string skeleton from spec §1: excitation (a short SegmentSource thump, oneShot) + tap of the DelayLine → CombinedSource(add) → Shaper holding a friction-ish curve (steep through zero, compressive shoulders — e.g. values `[-1, -0.85, -0.3, -0.75, -0.05, -0.6, 0.05, 0.6, 0.3, 0.75, 1, 0.85]`) → DelayLine (frequency from the Note face / instrument frequency param, ratio 1) → output. Loop gain must sit just under unity through the shaper's slope, or it runs away to the guard — start compressive (slope < 1 at the extremes) and iterate until the render RINGS and DECAYS. Instrument block, polyphony 2, embedded score: three notes across an octave, ~1.5 s each.
- [ ] **Step 2: loop_selfosc.json.** No excitation at all: Shaper with gain through zero (e.g. `[-1, -0.9, -0.2, -0.9, 0, 0, 0.2, 0.9, 1, 0.9]` — slope ≈ 4.5 near origin, saturating shoulders) → DelayLine → tap back into the Shaper's source. Render must be NON-silent from the noise floor... which in a digital loop is exactly zero — so seed it: a WhiteNoiseSource through a very small gain (CombinedSource add, ~0.001 amplitude) summed into the loop input. That stays honest to the spec's "blooming from the noise floor."
- [ ] **Step 3: Render both.** `mforce_cli <patch> <scratchpad>\<name>.wav` — verify non-silent (python: peak > 0.05) and finite. Iterate curve values until both behave as described (ring-and-decay; sustained self-oscillation). These curves are STARTING POINTS — the sound is Matt's to judge; the gate freezes reproducibility, not aesthetics. Copy renders to `renders/dsp/pending/feedback/` for audition (standing rule: leave audition material in pending, never promote).
- [ ] **Step 4: Null gate + re-freeze + commit.**

```bash
git add patches/baselines/feedback/ tools/null_gate_manifest.json
git commit -m "patches: feedback reference loops - bowed skeleton + self-oscillator"
```

---

### Task 9: Documentation close-out

**Files:**
- Modify: `docs/feedback_loop_design.md` (STATUS line → shipped + increment ledger)
- Modify: `docs/autonomy/STATUS.md` (dsp lane row)
- Modify: `docs/autonomy/dsp/BACKLOG.md` (log the novelty-sweep follow-on as a backlog item; note 34/34a relationship — the mode family now has a second engine to build on)

- [ ] **Step 1:** Update the three docs (one commit). The novelty sweep itself (batch-generated curves + novelty metric over renders) is a RUN activity, not part of this plan — the backlog item is its handle.

```bash
git add docs/feedback_loop_design.md docs/autonomy/STATUS.md docs/autonomy/dsp/BACKLOG.md
git commit -m "docs: feedback loop v1 shipped - status + sweep backlog item"
```

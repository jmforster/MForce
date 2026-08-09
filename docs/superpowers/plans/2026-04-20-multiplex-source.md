# MultiplexSource Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `MultiplexSource` node that fans one template source out into N independent, seed-perturbed instances and emits `sum(instances) / N`. "One source in, 10-voice section out." No clone machinery — reuses the existing `build_graph`-from-JSON pattern already used by the voice pool. Also reclassifies 5 waveguide-evolution types from `Generator` to `Combiner` while in the neighborhood.

**Architecture:** New header `engine/include/mforce/source/multiplex_source.h` for the class. Loader (`patch_loader.cpp`) gains `extract_subgraph_json()` + `build_subgraph()` helpers; a new special-case branch handles `MultiplexSource` at load time by building N instances with seed perturbation `base_seed ^ (i * 0x9E3779B9u)`. UI side: one `s_graphDirty` global plus a one-liner that sets it on any graph-node edit; on note-trigger, walk `s_nodes` for multiplexes, reserialize their template subtree from the current UI state, mark them dirty so the next `prepare()` rebuilds.

**Tech Stack:** C++17/20, existing CMake build. No new deps.

**Spec reference:** `docs/superpowers/specs/2026-04-20-multiplex-source-design.md` (resolutions applied: pin name `source`, count cap 50, category `Combiner`, crude "any edit → all multiplexes dirty", count changes also trigger rebuild).

**Validation philosophy:** CLI render-path testable on its own: build a test patch, verify bit-identical with same seed (reproducibility) and RMS of Mux:2 ≈ RMS of Mux:1 / √2 on WhiteNoise (divergence proof). UI-path testable via Matt loading a patch, triggering a note, tweaking a template param, triggering again, confirming audio changes.

**Not in scope:** Applying the seed-perturbation trick to the voice pool (parked as follow-up). Nested-MultiplexSource optimization (works in principle). Per-instance inspector UI.

---

## File Structure

**New files:**
- `engine/include/mforce/source/multiplex_source.h` — the class.

**Modified files:**
- `engine/src/source_registrations.cpp` — register `MultiplexSource`; reclassify 5 waveguides.
- `engine/include/mforce/source/wave_evolution.h` — change `category()` return on `EKSEvolutionSource`, `BlownTubeEvolutionSource`, `ReedEvolutionSource`, `BowedStringEvolutionSource`, `BrassEvolutionSource` from `Generator` to `Combiner`.
- `engine/src/patch_loader.cpp` — add `extract_subgraph_json` + `build_subgraph` helpers; add `MultiplexSource` special-case branch.
- `tools/mforce_ui/main.cpp` — `s_graphDirty` flag; note-trigger preflight that reserializes & dirties any Multiplex.

---

## Task 1: Reclassify 5 waveguides from Generator → Combiner

**Files:**
- Modify: `engine/include/mforce/source/wave_evolution.h`
- Modify: `engine/src/source_registrations.cpp`

- [ ] **Step 1: Change `category()` return in the 5 WaveEvolution sources**

In `wave_evolution.h`, locate the five holder classes:
`EKSEvolutionSource`, `BlownTubeEvolutionSource`, `ReedEvolutionSource`, `BowedStringEvolutionSource`, `BrassEvolutionSource`. Each has:
```cpp
SourceCategory category() const override { return SourceCategory::Generator; }
```
Change the return to `SourceCategory::Combiner` for these five.

Leave `PluckEvolutionSource` and `AveragingEvolutionSource` as `Generator` (they're wavetable-modifier evolutions, not exciter-resonator models).

- [ ] **Step 2: Update the registrations**

In `engine/src/source_registrations.cpp`, lines 476–498, change the category arg passed to `reg.register_type` for those same five types from `SourceCategory::Generator` to `SourceCategory::Combiner`.

- [ ] **Step 3: Build clean on Release**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine mforce_cli 2>&1 | tail -5
```

Expected: PASS. If UI is running and the UI build fails on file lock, skip `mforce_ui` target.

- [ ] **Step 4: Regression render — confirm no behavior change**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/pluck_sanity.json renders/_t1_pluck.wav 2>&1 | tail -2
build/tools/mforce_cli/Release/mforce_cli.exe patches/fadd_formant_test.json renders/_t1_fadd.wav 2>&1 | tail -2
rm renders/_t1_*.wav
```

Expected: both bit-identical (category is a UI grouping, not a runtime behavior).
- pluck_sanity: `peak=0.593099 rms=0.0525044`
- fadd_formant_test: `peak=0.179758 rms=0.0151559`

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/source/wave_evolution.h engine/src/source_registrations.cpp
git commit -m "refactor(waveguide): reclassify exciter-resonator evolutions as Combiner

EKS, BlownTube, Reed, BowedString, Brass are all exciter + resonator
combinations (breath/bow/noise driving a tube or string). Category
Combiner fits their graph role better than Generator. Pluck and
Averaging stay as Generator — they're simple wavetable-modifier
evolutions. Runtime behavior unchanged (category is UI grouping only).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: MultiplexSource class

**Files:**
- Create: `engine/include/mforce/source/multiplex_source.h`

- [ ] **Step 1: Write the class**

```cpp
#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <string>
#include <vector>

namespace mforce {

// MultiplexSource: fans one template subgraph into N independent instances,
// each built with a perturbed seed, and emits sum(instances) / N per sample.
// The loader special-cases this node to populate instances_ at build time
// from an extracted JSON subtree (stored in templateJson_).
//
// Live-edit rebuild (UI path): external code mutates templateJson_ to the
// current UI serialization and sets templateDirty_; next prepare() rebuilds.
struct MultiplexSource final : ValueSource {
  const char* type_name() const override { return "MultiplexSource"; }
  SourceCategory category() const override { return SourceCategory::Combiner; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source", false},  // single template source; not connectable from elsewhere
    };
    return descs;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"count", ConfigType::Int, 10.0f, 1.0f, 50.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "count") {
      int newCount = std::max(1, std::min(50, int(value)));
      if (newCount != count_) {
        count_ = newCount;
        templateDirty_ = true;
      }
    }
  }
  float get_config(std::string_view name) const override {
    if (name == "count") return float(count_);
    return 0.0f;
  }

  // Loader hook: invoked at patch load with the extracted subtree JSON,
  // base seed captured from the subtree's JSON, and rebuild function.
  // The rebuild function takes (instance_index) and returns a freshly built
  // root source; the loader supplies it via a closure over its local state.
  // After set_template, rebuild_() can be invoked on demand.
  using InstanceBuilder = std::function<std::shared_ptr<ValueSource>(int instanceIdx)>;
  void set_template(std::string json, uint32_t baseSeed, InstanceBuilder builder) {
    templateJson_ = std::move(json);
    baseSeed_ = baseSeed;
    builder_ = std::move(builder);
    templateDirty_ = true;
  }

  // External dirty signal (UI sets this after editing template nodes).
  void mark_dirty() { templateDirty_ = true; }

  void prepare(const RenderContext& ctx, int frames) override {
    if (templateDirty_) rebuild_(ctx);
    for (auto& inst : instances_)
      if (inst) inst->prepare(ctx, frames);
  }

  float next() override {
    float sum = 0.0f;
    int n = int(instances_.size());
    for (auto& inst : instances_)
      if (inst) sum += inst->next();
    cur_ = (n > 0) ? sum / float(n) : 0.0f;
    return cur_;
  }

  float current() const override { return cur_; }

 private:
  void rebuild_(const RenderContext& /*ctx*/) {
    instances_.clear();
    if (!builder_) { templateDirty_ = false; return; }
    instances_.reserve(count_);
    for (int i = 0; i < count_; ++i) {
      instances_.push_back(builder_(i));
    }
    templateDirty_ = false;
  }

  int count_{10};
  uint32_t baseSeed_{0};
  std::string templateJson_;
  InstanceBuilder builder_;
  std::vector<std::shared_ptr<ValueSource>> instances_;
  bool templateDirty_{true};
  float cur_{0.0f};
};

} // namespace mforce
```

Note: `InstanceBuilder` is a `std::function` closure. The loader supplies it; it captures whatever loader-side state is needed (nodemap, seed perturbation rule, etc.). Multiplex just calls it N times.

- [ ] **Step 2: Include `<functional>` and `<algorithm>` guard**

Add at the top:
```cpp
#include <functional>
#include <algorithm>
```

(For `std::function` and `std::max`/`std::min`.)

- [ ] **Step 3: Build engine; expect header-only compile**

The header isn't used anywhere yet; just compile-check via engine build:

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine 2>&1 | tail -3
```

Expected: PASS.

(We'll commit this with the registration in Task 4, since header alone isn't useful.)

---

## Task 3: Loader utilities — subtree extraction + per-instance build

**Files:**
- Modify: `engine/src/patch_loader.cpp`

- [ ] **Step 1: Add `extract_subgraph_json` helper**

Near the top of `patch_loader.cpp` (after the `resolve_param` helpers, before `build_graph`), add:

```cpp
// Walk the node map starting from rootId, collecting all transitively-
// referenced node ids. Returns a self-contained JSON subtree:
//   { "nodes": [...], "output": "<rootId>" }
// suitable for passing to build_graph() after parsing with from_json.
static json extract_subgraph_json(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::string& rootId)
{
    std::unordered_set<std::string> visited;
    std::vector<std::string> order;

    std::function<void(const std::string&)> walk = [&](const std::string& id) {
        if (visited.count(id)) return;
        if (!nodeMap.count(id)) return;
        visited.insert(id);
        const auto& node = nodeMap.at(id);
        // Recurse into all "ref" fields within params.
        if (node.contains("params")) {
            std::function<void(const json&)> scanRefs = [&](const json& v) {
                if (v.is_object()) {
                    if (v.size() == 1 && v.contains("ref") && v["ref"].is_string())
                        walk(v["ref"].get<std::string>());
                    else
                        for (auto it = v.begin(); it != v.end(); ++it) scanRefs(it.value());
                } else if (v.is_array()) {
                    for (const auto& item : v) scanRefs(item);
                }
            };
            scanRefs(node["params"]);
        }
        order.push_back(id);
    };

    walk(rootId);

    json nodes = json::array();
    for (const auto& id : order) nodes.push_back(nodeMap.at(id));

    json subtree;
    subtree["nodes"] = nodes;
    subtree["output"] = rootId;
    return subtree;
}
```

Ensure `#include <unordered_set>` and `#include <functional>` are present at the file top.

- [ ] **Step 2: Add `build_subgraph_with_seed_perturbation` helper**

Add after `extract_subgraph_json`:

```cpp
// Build a fresh root source from a subtree JSON string, with every seedable
// node's seed XOR'd by seedPerturbation. Uses the existing build_graph
// machinery internally.
static std::shared_ptr<ValueSource> build_subgraph_with_seed_perturbation(
    const std::string& subtreeJsonStr,
    uint32_t seedPerturbation,
    int sampleRate)
{
    json subtree = json::parse(subtreeJsonStr);

    // Reconstruct nodeMap + nodeOrder for build_graph. Perturb any "seed"
    // field in node params by the perturbation value; nodes without an
    // explicit seed fall back to factory defaults XOR perturbation (we
    // inject a "seed" field so the factory picks it up).
    std::unordered_map<std::string, json> nodeMap;
    std::vector<std::string> nodeOrder;
    for (auto& jnode : subtree["nodes"]) {
        json perturbed = jnode;
        if (!perturbed.contains("params")) perturbed["params"] = json::object();
        auto& params = perturbed["params"];
        if (params.contains("seed") && params["seed"].is_number()) {
            uint32_t s = uint32_t(params["seed"].get<int64_t>());
            params["seed"] = int64_t(s ^ seedPerturbation);
        } else {
            // Inject a seed of 0 ^ perturbation, so factories that have
            // a default get our perturbed override instead.
            params["seed"] = int64_t(seedPerturbation);
        }
        std::string id = perturbed["id"].get<std::string>();
        nodeMap[id] = perturbed;
        nodeOrder.push_back(id);
    }

    auto g = build_graph(nodeMap, nodeOrder, sampleRate);

    std::string outputId = subtree["output"].get<std::string>();
    auto it = g.valueNodes.find(outputId);
    if (it == g.valueNodes.end())
        throw std::runtime_error("build_subgraph: output '" + outputId + "' not found");
    return it->second;
}
```

Note: this function is called AFTER `build_graph` is declared, so forward declare `build_graph` at the top if compilation complains about ordering. Simpler fix: put both helpers AFTER `build_graph`.

- [ ] **Step 3: Build engine**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine 2>&1 | tail -5
```

Expected: PASS. Fix ordering issues if any.

---

## Task 4: Loader special-case for MultiplexSource + register it

**Files:**
- Modify: `engine/src/patch_loader.cpp`
- Modify: `engine/src/source_registrations.cpp`

- [ ] **Step 1: Register the type**

In `source_registrations.cpp`, after the CombinedSource / CrossfadeSource registrations (or wherever the Combiner group lives), add:

```cpp
reg.register_type("MultiplexSource", SourceCategory::Combiner,
    [](int, auto) { return std::make_shared<MultiplexSource>(); });
```

Include the header at the top of the file:
```cpp
#include "mforce/source/multiplex_source.h"
```

- [ ] **Step 2: Add the loader special-case**

In `patch_loader.cpp`, find `build_graph`'s main type-dispatch chain (the long `if (type == "X") … else if (type == "Y") …`). Add before the generic-path fallback:

```cpp
else if (type == "MultiplexSource") {
    auto mux = std::make_shared<MultiplexSource>();
    if (pp) {
        // Apply count config via the generic path.
        wire_params_generic(*mux, *pp, valueNodes);

        // Resolve the "source" input to its target node id.
        if (pp->contains("source") && (*pp)["source"].is_object()
            && (*pp)["source"].contains("ref")) {
            std::string tmplRootId = (*pp)["source"]["ref"].get<std::string>();

            // Extract the template subtree as JSON.
            json subtree = extract_subgraph_json(nodeMap, tmplRootId);
            std::string subtreeStr = subtree.dump();

            // Capture the template's base seed (from the root node's params),
            // or 0 if absent.
            uint32_t baseSeed = 0;
            if (nodeMap.count(tmplRootId)) {
                const auto& rootNode = nodeMap.at(tmplRootId);
                if (rootNode.contains("params") && rootNode["params"].contains("seed")
                    && rootNode["params"]["seed"].is_number()) {
                    baseSeed = uint32_t(rootNode["params"]["seed"].get<int64_t>());
                }
            }

            // Build the instance-builder closure. Captures subtreeStr + baseSeed
            // by value (closure outlives loader's stack).
            int sr = sampleRate;
            MultiplexSource::InstanceBuilder builder =
                [subtreeStr, baseSeed, sr](int instanceIdx) {
                    uint32_t perturbation = baseSeed ^ (uint32_t(instanceIdx) * 0x9E3779B9u);
                    return build_subgraph_with_seed_perturbation(
                        subtreeStr, perturbation, sr);
                };

            mux->set_template(subtreeStr, baseSeed, std::move(builder));
        }
    }
    valueNodes[id] = mux;
    add_mono(g, id, mux);
}
```

Include at top:
```cpp
#include "mforce/source/multiplex_source.h"
```

- [ ] **Step 3: Build — all targets**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_engine mforce_cli 2>&1 | tail -5
```

Expected: PASS.

- [ ] **Step 4: Create a minimal test patch**

Write to `patches/mux_noise_test.json`:

```json
{
  "sampleRate": 48000,
  "seconds": 2,
  "graph": {
    "nodes": [
      { "id": "env1", "type": "Envelope",
        "params": { "preset": "adsr", "attack": 0.01, "decay": 0.01,
                    "sustainLevel": 1.0, "release": 0.05 } },
      { "id": "wn1", "type": "WhiteNoiseSource",
        "params": { "amplitude": { "ref": "env1" }, "seed": 0 } },
      { "id": "mux1", "type": "MultiplexSource",
        "params": { "count": 2, "source": { "ref": "wn1" } } },
      { "id": "ch1", "type": "SoundChannel",
        "inputs": { "source": "mux1" },
        "params": { "volume": 1.0, "pan": 0.0 } },
      { "id": "mix", "type": "StereoMixer",
        "inputs": { "channels": ["ch1"] },
        "params": { "gainL": 1.0, "gainR": 1.0 } }
    ],
    "output": "mix"
  }
}
```

- [ ] **Step 5: Validate reproducibility + divergence**

Render with count=2 and count=1 (by editing the patch):

```bash
# count=2
build/tools/mforce_cli/Release/mforce_cli.exe patches/mux_noise_test.json renders/_t4_mux2.wav 2>&1 | tail -3
python tools/metrics/check.py renders/_t4_mux2.wav 2>&1 | head -2

# count=1 (temporarily)
sed -i 's/"count": 2/"count": 1/' patches/mux_noise_test.json
build/tools/mforce_cli/Release/mforce_cli.exe patches/mux_noise_test.json renders/_t4_mux1.wav 2>&1 | tail -3
python tools/metrics/check.py renders/_t4_mux1.wav 2>&1 | head -2
sed -i 's/"count": 1/"count": 2/' patches/mux_noise_test.json

# Reproducibility: render count=2 twice, expect bit-identical
build/tools/mforce_cli/Release/mforce_cli.exe patches/mux_noise_test.json renders/_t4_mux2b.wav 2>&1 | tail -3
cmp renders/_t4_mux2.wav renders/_t4_mux2b.wav && echo "REPRODUCIBILITY OK" || echo "REPRODUCIBILITY FAILED"
```

Expected:
- Reproducibility: `REPRODUCIBILITY OK` (cmp returns 0).
- Divergence: RMS of count=2 render ≈ RMS of count=1 render / √2 (within ~10%). If RMS is IDENTICAL or close to it, the seed perturbation isn't working — the two streams are still correlated.

Print the RMS ratio explicitly:
```bash
python -c "
import json
r1 = json.load(open('renders/_t4_mux1.features.json'))['features']['rms']
r2 = json.load(open('renders/_t4_mux2.features.json'))['features']['rms']
import math
expected = r1 / math.sqrt(2)
print(f'rms(mux1)={r1:.4f}  rms(mux2)={r2:.4f}  expected_mux2={expected:.4f}')
print(f'ratio r2/r1 = {r2/r1:.3f}  (expect ~0.707 for divergent, ~1.0 for correlated)'
"
```

Expected: ratio 0.65-0.78 → divergent. Ratio > 0.9 → still correlated (bug in perturbation).

- [ ] **Step 6: Cleanup + commit**

```bash
rm renders/_t4_mux*.wav renders/_t4_mux*.features.json
git add engine/include/mforce/source/multiplex_source.h \
        engine/src/source_registrations.cpp \
        engine/src/patch_loader.cpp \
        patches/mux_noise_test.json
git commit -m "feat(source): MultiplexSource — fan-out template into N independent instances

Takes one template source + count (1..50), internally holds N instances
built via build_subgraph_with_seed_perturbation(). Each instance gets
seed = base ^ (i * 0x9E3779B9) to guarantee divergence. Output is
sum(instances) / count.

Reuses the voice-pool's build_graph-from-JSON pattern rather than
introducing a clone() interface — matches Matt's preference (legacy
Clone was error-prone).

Validation: count=2 WhiteNoise render has RMS ≈ RMS(count=1)/√2,
confirming the two instances produce uncorrelated streams.
Reproducibility: two back-to-back renders are bit-identical.

Also registers MultiplexSource in SourceCategory::Combiner.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: UI integration — dirty-flag rebuild on live edits

**Files:**
- Modify: `tools/mforce_ui/main.cpp`

- [ ] **Step 1: Add `s_graphDirty` flag**

Near the other `s_*` globals (around line 399):
```cpp
static bool s_graphDirty = false;
```

- [ ] **Step 2: Set the flag on any graph edit**

Inspector panel code (around `draw_properties_panel`) updates `pin.defaultValue`, `node.configValues`, `node.arrayValues`, and calls `set_param`/`set_config`/`set_array` on the DSP source. Add a flag-bump at every such edit site.

Simplest: at the top of `draw_properties_panel`, save all values; at the bottom, compare against new values; if anything changed, set `s_graphDirty = true`. That's intrusive. Cleaner:

Grep for `set_config(`, `set_array(`, `pin.constantSrc->set(` in `main.cpp`; after each call that's inside an `ImGui::DragFloat` / `ImGui::InputText` / `ImGui::Checkbox` truthy branch, add `s_graphDirty = true;`. Expected count: 5–10 spots.

Also set it when a link is created or broken (inside the `ImNodes::IsLinkCreated` / `IsLinkDestroyed` handlers).

Also set it on `formantRows` edits in the FormantSpectrum inline table.

Concretely (examples; actual line numbers will vary):

```cpp
// After pin.constantSrc->set(pin.defaultValue):
s_graphDirty = true;

// After node.configValues change + apply_config():
s_graphDirty = true;

// Inside ImNodes::IsLinkCreated and IsLinkDestroyed handlers:
s_graphDirty = true;

// After formantRows edit + rebuild_formant_spectrum():
s_graphDirty = true;
```

Don't worry about being surgical; false-positives (spurious rebuilds) are cheap (one rebuild on next note), false-negatives (missed dirty signal) are silent bugs.

- [ ] **Step 3: Preflight on note trigger**

Before every point in `main.cpp` where a note is triggered (keyboard press, Generate button, transport Play), add a preflight:

```cpp
static void refresh_multiplex_before_play() {
    if (!s_graphDirty) return;
    // Walk s_nodes; for each MultiplexSource, reserialize its template
    // subgraph from current UI state and mark it dirty.
    for (auto& n : s_nodes) {
        if (n.typeName != "MultiplexSource") continue;
        auto mux = std::dynamic_pointer_cast<MultiplexSource>(n.dspSource);
        if (!mux) continue;
        // Find what's wired to the "source" input.
        Pin* srcPin = n.find_input("source");
        if (!srcPin) continue;
        GraphNode* tmplNode = find_source_node(srcPin->id);
        if (!tmplNode) continue;
        // Reserialize: use the same serialization logic as save_node_graph,
        // but only for the subtree rooted at tmplNode.
        json subtreeJson = serialize_subgraph(*tmplNode);  // new helper — see Step 4
        uint32_t baseSeed = 0;  // UI doesn't track a base seed explicitly; 0 is fine — perturbation still diverges instances.
        // Update the Multiplex's template JSON and rebuild builder via the
        // patch_loader utility. Problem: patch_loader is C++ side; we'd need
        // to call build_subgraph_with_seed_perturbation from the UI.
        // See Step 4 for the exposed helper.
        mux->set_template(subtreeJson.dump(), baseSeed,
                          make_ui_instance_builder(subtreeJson.dump(), baseSeed));
        mux->mark_dirty();
    }
    s_graphDirty = false;
}
```

Call `refresh_multiplex_before_play()` before each play-path entry in the UI (keyboard handler, Generate, transport Play). Grep for existing note-trigger sites (`play_note`, `kit.render`, `ip.instrument->render`) and add the call just before.

- [ ] **Step 4: Expose loader helpers for UI use**

The `serialize_subgraph(GraphNode&)` and `make_ui_instance_builder(subtreeJson, baseSeed)` helpers don't exist yet. Factor them out of existing code:

**For `serialize_subgraph`**: in `main.cpp`, factor out the per-node serialization block from `save_patch_graph` / `save_node_graph` into a reusable helper:
```cpp
// Emits one GraphNode as JSON (params, configValues, arrayValues,
// formantRows, etc.) — extracted from save_patch_graph.
static json serialize_node(const GraphNode& node, const std::unordered_map<int, std::string>& nodeIds);

// Walks the subgraph rooted at `root`, returning a self-contained
// { "nodes": [...], "output": "<rootId>" } JSON object.
static json serialize_subgraph(GraphNode& root);
```

**For `make_ui_instance_builder`**: factor out of patch_loader by adding a non-static exported function:
```cpp
// In engine/include/mforce/render/patch_loader.h (or a new public header):
std::shared_ptr<ValueSource> build_subgraph_from_json(
    const std::string& subtreeJsonStr,
    uint32_t seedPerturbation,
    int sampleRate);
```
Then in the UI:
```cpp
static MultiplexSource::InstanceBuilder make_ui_instance_builder(
    const std::string& subtreeJsonStr, uint32_t baseSeed)
{
    int sr = DSP_SAMPLE_RATE;
    return [subtreeJsonStr, baseSeed, sr](int instanceIdx) {
        uint32_t perturbation = baseSeed ^ (uint32_t(instanceIdx) * 0x9E3779B9u);
        return build_subgraph_from_json(subtreeJsonStr, perturbation, sr);
    };
}
```

Implementation of `build_subgraph_from_json` in patch_loader.cpp is a one-liner wrapper around the existing `build_subgraph_with_seed_perturbation`.

- [ ] **Step 5: Build — UI target**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_ui 2>&1 | tail -10
```

Expected: PASS. (Kill running mforce_ui.exe if locked.)

- [ ] **Step 6: Commit**

```bash
git add tools/mforce_ui/main.cpp \
        engine/include/mforce/render/patch_loader.h \
        engine/src/patch_loader.cpp
git commit -m "feat(ui): mark-dirty rebuild of Multiplex on live edits

UI graph edits set s_graphDirty; preflight before every play path
walks s_nodes, reserializes each Multiplex's template subgraph from
current UI state, and marks the Multiplex dirty so its next prepare()
rebuilds N instances with the new params.

Exposes build_subgraph_from_json from patch_loader for UI use.

Edit template param, press key: hear the edit. No patch save/reload
needed.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: End-to-end validation + handoff

**Files:** none. Validation + user handoff.

- [ ] **Step 1: Build a "10-voice section" test patch**

Use the UI to build: `[FullAdditive] → [Multiplex:10] → [Output]` with sensible partial/envelope settings. Save as `patches/mux_section_test.json`.

Or write the JSON by hand — whatever's faster. The MVP point is to have one audible real-world patch using Multiplex.

- [ ] **Step 2: CLI render + metrics**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe patches/mux_section_test.json renders/_mux_section.wav 2>&1 | tail -3
python tools/metrics/check.py renders/_mux_section.wav
```

Expected: OK, clean render. Section character audible if Matt listens.

- [ ] **Step 3: UI live-edit test**

Launch `build/tools/mforce_ui/Release/mforce_ui.exe`, load the test patch, trigger a note via keyboard. Edit a template param. Trigger another note. Confirm the second note sounds different.

If no audible change: dirty flag isn't firing, or rebuild isn't happening, or reserialization is missing a field. Debug.

- [ ] **Step 4: Report to Matt**

Summarize:
- Reproducibility ✓ (bit-identical)
- Divergence ✓ (RMS ratio sqrt(2))
- Live rebuild ✓ (edit → press key → hear change)
- Known issue flagged: voice-pool seed correlation can be fixed by reusing this perturbation (parked follow-up).

Await his A/B subjective listen.

---

## Self-review

**Spec coverage:**
- Node shape (source pin + count config) → Task 2 ✓
- Runtime semantics (sum/count, prepare-to-all) → Task 2 ✓
- Build N instances at load → Task 4 ✓
- Seed perturbation with 0x9E3779B9 → Tasks 3, 4 ✓
- Dirty-flag rebuild → Tasks 2, 5 ✓
- UI category Combiner + waveguide reclassification → Task 1 ✓
- UI live-edit rebuild → Task 5 ✓
- Validation (reproducibility, divergence, UI responsiveness) → Tasks 4, 6 ✓

**No placeholders:** every step has concrete code or commands. One slight fuzziness in Task 5 Step 2 — "find every set_param/set_config/set_array site and add s_graphDirty = true" is mechanical but requires locating them; acceptable for a task step since the pattern is clear.

**Type consistency:** `MultiplexSource::InstanceBuilder` defined in Task 2, used by Tasks 4 & 5. `build_subgraph_with_seed_perturbation` (static in patch_loader, Task 3) vs `build_subgraph_from_json` (exported, Task 5) — the exported one is a wrapper that calls the static; named explicitly so no conflict.

**Known friction:** Task 5 Step 4 requires factoring helper functions out of existing long codepaths (save_patch_graph). That's invasive relative to the rest of the plan. If the UI work gets gnarly, it's OK to punt Task 5 for now and commit Tasks 1-4 (CLI-only Multiplex), then do UI integration as a follow-up PR.

# Curve Unification, Per-Segment Interp, Shaper Morph — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One shared piecewise-curve evaluator; per-segment interp overrides (incl. Hold, replacing Stage holdPct); hardcoded curve presets; a point-space morph pin on Shaper with an A/B editor workflow.

**Architecture:** Extract `Ramp` to its own header, then a `Curve` static evaluator both `CurveNode::map` and `ShaperSource::map` delegate to (JSON and audio byte-identical). Per-segment overrides and morph land as new generic-wired arrays/params on ShaperSource. UI work stays inside the existing shape-editor client model in `tools/mforce_ui/main.cpp`.

**Tech Stack:** C++20 headers-first engine, nlohmann::json loaders, Dear ImGui UI, assert-style tests in `tools/engine_tests/main.cpp`, corpus null gate `tools/null_gate_perform_source.py`.

**Spec:** docs/superpowers/specs/2026-09-05-curve-morph-design.md

## Global Constraints

- No heap allocation in hot render loops (`next()`/`map()` paths).
- Null gate must pass unchanged at every commit: `python tools/null_gate_perform_source.py` (compares render hashes against the frozen manifest; run pre-commit per batch, standing rule).
- Existing patch JSON parses unchanged; saves of untouched patches stay byte-identical (check with `python tools/test_stable_roundtrip.py` where indicated).
- UI copy says "points"/"segment" — the word "knots" NEVER appears in UI strings (code identifiers may keep it).
- Build from repo root: `cmake --build build --config Release`. Executables: `build/tools/mforce_cli/Release/mforce_cli.exe`, `build/tools/engine_tests/Release/engine_tests.exe`, `build/tools/mforce_ui/Release/mforce_ui.exe` (if a target path differs, locate with `ls build/tools`). If `mforce_ui.exe` is running/locked, build the engine targets and defer the UI link — never stall engine work (see memory: rename-then-link).
- Commit after every task; messages end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- Two Claudes share this working copy: run `git branch --show-current` before each commit (must be `main`) and stage ONLY the files each task names — the tree carries Matt's unrelated uncommitted patch edits.

---

### Task 1: Extract Ramp into ramp.h (pure move)

**Files:**
- Create: `engine/include/mforce/core/ramp.h`
- Modify: `engine/include/mforce/core/envelope.h:11-62` (delete moved block, add include)

**Interfaces:**
- Produces: `mforce/core/ramp.h` defining `enum class RampType { Linear, Expo, InverseExpo, Sine }` and `struct Ramp { float startVal, endVal; RampType type; float power; float holdPct; float value(float pos) const; }` — exactly today's semantics. Tasks 2 and 4 include this header.

- [ ] **Step 1: Create ramp.h with the moved code**

Copy `envelope.h` lines 11-62 (the `RampType` enum and `struct Ramp`, comments included) verbatim into:

```cpp
#pragma once
#include <cmath>

namespace mforce {

// Ported from C# MForce.Utility.RampType
enum class RampType { Linear, Expo, InverseExpo, Sine };

// ... (struct Ramp verbatim from envelope.h, including holdPct —
//      holdPct is removed in Task 4, not here; this task is a pure move)

} // namespace mforce
```

- [ ] **Step 2: Replace the block in envelope.h**

Delete lines 11-62 of `envelope.h`; add `#include "mforce/core/ramp.h"` after the existing includes.

- [ ] **Step 3: Build**

Run: `cmake --build build --config Release`
Expected: clean build, all targets.

- [ ] **Step 4: Engine tests + null gate**

Run: `build/tools/engine_tests/Release/engine_tests.exe` → expect `0 fails`.
Run: `python tools/null_gate_perform_source.py` → expect all-pass, zero DIFF.

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/core/ramp.h engine/include/mforce/core/envelope.h
git commit -m "refactor: extract Ramp/RampType into core/ramp.h (pure move)"
```

---

### Task 2: Curve evaluator + parity tests

**Files:**
- Create: `engine/include/mforce/core/curve.h`
- Modify: `tools/engine_tests/main.cpp` (new `run_curve_shared_tests()`, call it from `main`)

**Interfaces:**
- Consumes: `Ramp`/`RampType` from Task 1; `SmoothnessInterpolator` (existing).
- Produces: `struct Curve` with `enum class Domain : uint8_t { Linear, LogX, LogLog }`, `struct Seg { bool overridden{false}; RampType type{RampType::Linear}; float power{2.0f}; }`, and two static entry points used by Tasks 3/5/8:
  - `static float eval(std::span<const std::pair<float,float>> pts, std::span<const Seg> segs, Domain domain, float smoothness, float x)`
  - `static float eval_flat(std::span<const float> xy, std::span<const Seg> segs, Domain domain, float smoothness, float x)` (xy = [x0,y0,x1,y1,...])

- [ ] **Step 1: Write failing parity tests**

In `tools/engine_tests/main.cpp` add (and call from `main()`):

```cpp
#include "mforce/core/curve.h"
#include "mforce/source/shaper_source.h"

static void run_curve_shared_tests() {
    // Parity vs CurveNode::map — Linear domain, smoothness 0.5 == exact lerp
    CurveNode lin;
    lin.knots = {{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}};
    for (float x : {-1.0f, 0.0f, 0.1f, 0.2f, 0.5f, 0.925f, 1.0f, 2.0f})
        CHECK_NEAR(Curve::eval(lin.knots, {}, Curve::Domain::Linear, 0.5f, x),
                   lin.map(x), 1e-7f);

    // Parity vs CurveNode::map — LogX and LogLog domains
    CurveNode logx; logx.interp = CurveNode::CurveInterp::LogX;
    logx.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    CHECK_NEAR(Curve::eval(logx.knots, {}, Curve::Domain::LogX, 0.5f, 1000.0f),
               logx.map(1000.0f), 1e-6f);
    CurveNode ll; ll.interp = CurveNode::CurveInterp::LogLog;
    ll.knots = {{10.0f, 100.0f}, {100.0f, 10000.0f}};
    CHECK_NEAR(Curve::eval(ll.knots, {}, Curve::Domain::LogLog, 0.5f, 31.6227766f),
               ll.map(31.6227766f), 0.5f);

    // Parity vs ShaperSource::map — smoothness 0.6, asymmetric curve
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-0.76f, -0.87f,-0.73f, -0.1f,-0.2f,
                             0.0f,0.0f, 0.11f,0.19f, 0.78f,0.57f, 0.93f,0.66f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.6f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    auto vals = sh.get_array("values");
    for (float x : {-1.5f, -0.9f, -0.3f, 0.0f, 0.05f, 0.5f, 0.9f, 1.5f})
        CHECK_NEAR(Curve::eval_flat(vals, {}, Curve::Domain::Linear, 0.6f, x),
                   sh.map(x), 1e-6f);

    // Identity on empty
    CHECK_NEAR(Curve::eval({}, {}, Curve::Domain::Linear, 0.5f, 123.0f),
               123.0f, 1e-9f);
}
```

Note: `sh.map(x)` needs one `sh.next()` first so the interpolator picks up smoothness — call `sh.prepare(ctx, 8); sh.next();` with a default `RenderContext` before the loop (copy the RenderContext setup idiom already used in this file).

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build build --config Release --target engine_tests` — expected: FAILS to compile (`curve.h` missing).

- [ ] **Step 3: Implement curve.h**

```cpp
#pragma once
#include "mforce/core/ramp.h"
#include "mforce/core/smoothness_interpolator.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace mforce {

// Shared piecewise-curve math (docs/superpowers/specs/2026-09-05-curve-
// morph-design.md §2). A curve is breakpoints + an interpolation policy;
// hosts keep their own storage and quirks (CurveNode expressions mode,
// SegmentSource deltas) and delegate evaluation here. UI copy says
// "points", never "knots".
struct Curve {
  enum class Domain : uint8_t { Linear, LogX, LogLog };

  // Per-segment connection override ("how this point connects to the
  // next"), stored on the segment's left point. overridden=false =>
  // host default (SmoothnessInterpolator at the given smoothness; 0.5
  // is exact lerp, which is CurveNode's fixed behavior).
  struct Seg {
    bool overridden{false};
    RampType type{RampType::Linear};
    float power{2.0f};
  };

  static float eval_seg(float y0, float y1, float t, const Seg& seg,
                        float smoothness) {
    if (!seg.overridden)
      return SmoothnessInterpolator(smoothness, false).interpolate(y0, y1, t);
    Ramp r{y0, y1, seg.type, seg.power};
    return r.value(t);
  }

  template <class PX, class PY, class SG>
  static float eval_core(size_t n, PX px, PY py, SG seg_at, Domain domain,
                         float smoothness, float x) {
    if (n == 0) return x;                       // identity (both hosts)
    if (x <= px(0))     return py(0);
    if (x >= px(n - 1)) return py(n - 1);
    for (size_t i = 1; i < n; ++i) {
      if (x <= px(i)) {
        if (domain == Domain::Linear) {
          const float w = px(i) - px(i - 1);
          const float pos = w > 0.0f
              ? std::clamp((x - px(i - 1)) / w, 0.0f, 1.0f) : 1.0f;
          return eval_seg(py(i - 1), py(i), pos, seg_at(i - 1), smoothness);
        }
        const float lf = std::log(x / px(i - 1)) /
                         std::log(px(i) / px(i - 1));
        if (domain == Domain::LogLog && py(i - 1) > 0.0f && py(i) > 0.0f)
          return py(i - 1) * std::pow(py(i) / py(i - 1), lf);
        return py(i - 1) + (py(i) - py(i - 1)) * lf;
      }
    }
    return py(n - 1);
  }

  static float eval(std::span<const std::pair<float, float>> pts,
                    std::span<const Seg> segs, Domain domain,
                    float smoothness, float x) {
    return eval_core(pts.size(),
        [&](size_t i) { return pts[i].first; },
        [&](size_t i) { return pts[i].second; },
        [&](size_t i) { return i < segs.size() ? segs[i] : Seg{}; },
        domain, smoothness, x);
  }

  static float eval_flat(std::span<const float> xy,
                         std::span<const Seg> segs, Domain domain,
                         float smoothness, float x) {
    return eval_core(xy.size() / 2,
        [&](size_t i) { return xy[i * 2]; },
        [&](size_t i) { return xy[i * 2 + 1]; },
        [&](size_t i) { return i < segs.size() ? segs[i] : Seg{}; },
        domain, smoothness, x);
  }
};

} // namespace mforce
```

Parity caveat (document in a comment): old CurveNode Linear had no `w > 0` guard — a curve with duplicate knot x's div-by-zeroed before and now returns the right endpoint. No patch has duplicate x's; the null gate arbitrates.

- [ ] **Step 4: Run tests to verify pass**

Run: `cmake --build build --config Release --target engine_tests && build/tools/engine_tests/Release/engine_tests.exe`
Expected: PASS, 0 fails.

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/core/curve.h tools/engine_tests/main.cpp
git commit -m "feat: shared Curve evaluator with parity tests"
```

---

### Task 3: CurveNode and ShaperSource delegate to Curve

**Files:**
- Modify: `engine/include/mforce/core/curve_node.h:84-117` (`map` body)
- Modify: `engine/include/mforce/source/shaper_source.h` (`map`, `next`, remove `interp_` member)

**Interfaces:**
- Consumes: `Curve::eval` / `Curve::eval_flat` (Task 2).
- Produces: `ShaperSource` gains members `float smoothCur_{0.5f};` and `std::vector<Curve::Seg> segs_{};` (empty until Task 5) — Tasks 5/8 extend these.

- [ ] **Step 1: Rewrite CurveNode::map**

```cpp
  float map(float x) const {
    if (exprMode) return map_expr(x);
    const Curve::Domain d =
        interp == CurveInterp::LogLog ? Curve::Domain::LogLog
      : interp == CurveInterp::LogX   ? Curve::Domain::LogX
      :                                 Curve::Domain::Linear;
    return Curve::eval(knots, {}, d, 0.5f, x);
  }
```

Add `#include "mforce/core/curve.h"`. Delete the now-dead hand-rolled interp loops (the ParamSlot-verbatim comment blocks go with them; note in the commit message that curve.h §eval_core carries the semantics forward).

- [ ] **Step 2: Rewrite ShaperSource**

In `shaper_source.h`: add `#include "mforce/core/curve.h"`; replace the `interp_` member with `float smoothCur_{0.5f};` and add `std::vector<Curve::Seg> segs_{};`; `next()` becomes:

```cpp
  float next() override {
    const float x = (source_ ? source_->next() : 0.0f) * drive_->next();
    smoothness_->next();
    smoothCur_ = smoothness_->current();
    cur_ = map(x);
    return cur_;
  }

  // Public for the UI preview/editor overlay.
  float map(float x) {
    return Curve::eval_flat(values_, segs_, Curve::Domain::Linear,
                            smoothCur_, x);
  }
```

Delete the old `map` body and the `SmoothnessInterpolator interp_` member.

- [ ] **Step 3: Build, tests, null gate**

Run: `cmake --build build --config Release && build/tools/engine_tests/Release/engine_tests.exe`
Expected: PASS (the Task 2 parity tests now exercise the delegating paths).
Run: `python tools/null_gate_perform_source.py`
Expected: zero DIFF — this is the load-bearing check of the whole refactor.

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/core/curve_node.h engine/include/mforce/source/shaper_source.h
git commit -m "refactor: CurveNode and Shaper evaluate through shared Curve"
```

---

### Task 4: RampType::Hold; remove Stage holdPct

**Files:**
- Modify: `engine/include/mforce/core/ramp.h` (add Hold, remove holdPct, add pos<=0 guard)
- Modify: `engine/include/mforce/core/envelope.h:261` (drop `s.ramp.holdPct = 0.0f;` in `add_stage_default`)
- Modify: `engine/src/patch_loader.cpp:580-585` (drop holdPct read; add "Hold" to the type map)
- Modify: `tools/mforce_ui/main.cpp:1263` (stage save: drop holdPct entry, add "Hold" to the type-string mapping used there), `:1286` (stage load: drop holdPct), `:8943` (stage editor: delete the `##hold` DragFloat row; add "Hold" to the stage-type combo items in the same editor block)
- Modify: `tools/engine_tests/main.cpp` (new `run_ramp_hold_tests()`)

**Interfaces:**
- Produces: `RampType::Hold` — `Ramp::value` returns `startVal` for every pos. Used by Curve::Seg (Task 5 onward) and Envelope stages.

- [ ] **Step 1: Write failing tests**

```cpp
#include "mforce/core/ramp.h"

static void run_ramp_hold_tests() {
    Ramp h{0.7f, 0.2f, RampType::Hold, 0.0f};
    CHECK_NEAR(h.value(0.0f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(0.5f), 0.7f, 1e-9f);
    CHECK_NEAR(h.value(1.0f), 0.7f, 1e-9f);
    // Byte-identity guard: every type returns startVal at exactly pos==0
    // (the old holdPct==0 branch did this; Expo with power 0 would
    // otherwise flip to endVal at pos 0).
    Ramp e{0.3f, 0.9f, RampType::Expo, 0.0f};
    CHECK_NEAR(e.value(0.0f), 0.3f, 1e-9f);
}
```

- [ ] **Step 2: Verify failure** — build engine_tests; expected: compile FAIL (`Hold` not in enum / Ramp still takes 5 initializers where code uses 4).

- [ ] **Step 3: Implement**

In `ramp.h`: `enum class RampType { Linear, Expo, InverseExpo, Sine, Hold };`. In `Ramp`: delete `float holdPct{0.0f};`. New `value` head:

```cpp
  float value(float pos) const {
    if (type == RampType::Hold) return startVal;
    if (pos <= 0.0f) return startVal;   // preserves the old holdPct==0
                                        // boundary for every type
    float t = pos;
    float range = endVal - startVal;
    // ... rest unchanged (Linear/Expo/InverseExpo/Sine bodies verbatim,
    //     with `t` in place of the old `(pos - holdPct) / (1 - holdPct)`)
```

Fix every brace-initializer that passed 5 values to Ramp (search `RampType::` in envelope.h preset factories — they pass 4, unchanged). In `patch_loader.cpp:580` delete the holdPct line; extend the type map: `: (t == "Hold") ? RampType::Hold`. In `mforce_ui/main.cpp` make the three edits listed above; the stage-type combo and save mapping must both carry `"Hold"`; UI label is "Hold" (a stage type — no holdPct spinner anymore).

- [ ] **Step 4: Tests + null gate**

Run: engine_tests → PASS. `python tools/null_gate_perform_source.py` → zero DIFF (the pos<=0 guard is what keeps Expo-power-0 stages identical).
Run: `python tools/test_stable_roundtrip.py` → stage JSON no longer writes holdPct but loads files that still carry it (nlohmann ignores unknown keys); expect pass.

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/core/ramp.h engine/include/mforce/core/envelope.h engine/src/patch_loader.cpp tools/mforce_ui/main.cpp tools/engine_tests/main.cpp
git commit -m "feat: RampType::Hold stage type; remove unused Stage holdPct"
```

---

### Task 5: Shaper per-segment overrides (engine + serialization)

**Files:**
- Modify: `engine/include/mforce/source/shaper_source.h` (segs array descriptor + decode)
- Modify: `tools/engine_tests/main.cpp` (new `run_shaper_seg_tests()`)

**Interfaces:**
- Produces: ShaperSource array `"segs"`: flat floats `[typeIdx, power]` per segment (length `2 * (npts - 1)`, or empty = all default). typeIdx: 0 = default, else `1 + int(RampType)` (1 Linear, 2 Expo, 3 InverseExpo, 4 Sine, 5 Hold). `set_array("segs", …)` decodes into `segs_`; `get_array("segs")` re-encodes (empty when all default). Loader/UI plumbing is automatic: patch_loader.cpp:418 iterates `array_descriptors()` generically and the UI mirrors arrays via `GraphNode::arrayValues`.

- [ ] **Step 1: Write failing tests**

```cpp
static void run_shaper_seg_tests() {
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-1.0f, 0.0f,0.0f, 1.0f,1.0f});
    // Segment 1 (0->1) overridden to Hold: y stays 0 across it.
    sh.set_array("segs", {0.0f,0.0f, 5.0f,0.0f});
    CHECK_NEAR(sh.map(0.5f), 0.0f, 1e-9f);
    // Segment 0 default: smoothness 0.5 == lerp
    CHECK_NEAR(sh.map(-0.5f), -0.5f, 1e-6f);
    // Expo override with power 2 on segment 1 (typeIdx 2 = 1 + RampType::Expo)
    sh.set_array("segs", {0.0f,0.0f, 2.0f,2.0f});
    CHECK_NEAR(sh.map(0.5f), 0.25f, 1e-6f);   // t^2 at t=0.5
    // Round-trip: get_array returns what was set; a fresh node stays empty
    auto back = sh.get_array("segs");
    CHECK(back.size() == 4);
    // (test order note: run the size check while the Expo override from
    // above is still set)
    ShaperSource fresh;
    CHECK(fresh.get_array("segs").empty());
    // Smoothness must NOT bend an overridden Linear segment
    sh.set_array("segs", {0.0f,0.0f, 1.0f,0.0f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(1.0f));
    /* prepare + next() once, then: */
    CHECK_NEAR(sh.map(0.5f), 0.5f, 1e-6f);
}
```

(Delete the stray duplicate `set_array` line when transcribing — the Expo call is `{0,0, 2,2}`.)

- [ ] **Step 2: Verify failure** — engine_tests compile/run: FAIL (`segs` array unknown, `get_array("segs")` returns `{}` is fine but `map` ignores overrides).

- [ ] **Step 3: Implement in shaper_source.h**

Extend descriptors and accessors:

```cpp
  std::span<const ArrayDescriptor> array_descriptors() const override {
    static constexpr ArrayDescriptor descs[] = {
      {"values",  nullptr, 0.0f, -2.0f, 2.0f},
      {"segs",    nullptr, 0.0f,  0.0f, 40.0f},
      {"values2", nullptr, 0.0f, -2.0f, 2.0f},   // Task 8 uses these two;
      {"segs2",   nullptr, 0.0f,  0.0f, 40.0f},  // declared now, inert
    };
    return descs;
  }

  static std::vector<Curve::Seg> decode_segs(const std::vector<float>& v) {
    std::vector<Curve::Seg> out;
    for (size_t i = 0; i + 1 < v.size(); i += 2) {
      Curve::Seg s;
      const int t = int(v[i]);
      if (t >= 1 && t <= 5) {
        s.overridden = true;
        s.type = RampType(t - 1);
        s.power = v[i + 1];
      }
      out.push_back(s);
    }
    return out;
  }
  static std::vector<float> encode_segs(const std::vector<Curve::Seg>& segs) {
    bool any = false;
    for (auto& s : segs) any |= s.overridden;
    if (!any) return {};
    std::vector<float> v;
    for (auto& s : segs) {
      v.push_back(s.overridden ? float(1 + int(s.type)) : 0.0f);
      v.push_back(s.power);
    }
    return v;
  }
```

In `set_array`: `if (name == "segs") { segs_ = decode_segs(v); segsRaw_ = std::move(v); return; }` (and accept `values2`/`segs2` into `valuesB_`/`segs2Raw_`+`segsB_` members now, unused until Task 8). In `get_array`: return `segsRaw_` etc. Keep `values` behavior identical.

- [ ] **Step 4: Tests + null gate + roundtrip**

engine_tests → PASS. `python tools/null_gate_perform_source.py` → zero DIFF. `python tools/test_stable_roundtrip.py` → verify no patch file gained a `"segs"` key (encode_segs returns empty ⇒ the UI save must skip empty arrays — check the arrayValues save loop at `tools/mforce_ui/main.cpp:2899`/`:3168`; if it writes empty arrays, add `if (vec.empty()) continue;` there and include that edit in this task).

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/source/shaper_source.h tools/engine_tests/main.cpp tools/mforce_ui/main.cpp
git commit -m "feat: Shaper per-segment interp overrides (segs array, generic-wired)"
```

---

### Task 6: Shaper editor — override gesture, power dot, clear

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — shape editor block (`ShapeEditorState` ~5357, `draw_shape_editor` ~5565, apply helpers ~5480-5548)

**Interfaces:**
- Consumes: `"segs"` array via `GraphNode::arrayValues` + `node.push_array("segs")`; `ShaperSource::decode_segs`/`encode_segs` (make them usable from the UI — they are static on ShaperSource).
- Produces: editing UX per spec §4. No new globals beyond fields added to `ShapeEditorState`.

- [ ] **Step 1: State + load/apply plumbing**

Add to `ShapeEditorState`: `int segDragIdx{-1};` (segment whose power dot is being dragged), `float segDragStartPower{2.0f};`. In `draw_shape_editor`, when `isShaper`, load segs alongside points:

```cpp
    std::vector<Curve::Seg> segs;
    if (isShaper) {
        for (auto& [d, v] : node.arrayValues)
            if (std::string_view(d.name) == "segs")
                segs = ShaperSource::decode_segs(v);
        segs.resize(pts.empty() ? 0 : pts.size() - 1);
    }
```

Add `shape_editor_apply_segs(GraphNode&, const std::vector<Curve::Seg>&)` mirroring `shape_editor_apply_shaper` (find the `"segs"` entry in `arrayValues`, overwrite with `ShaperSource::encode_segs(segs)`, `node.push_array("segs")`, `mark_graph_dirty()`).

Structural-edit sync: in the existing add-point branch (`pts.insert(it, …)`, ~5957) and delete branch (`pts.erase`, ~5963), when `isShaper`, insert/erase the corresponding `segs` entry (insert a default `Seg{}` at the new segment index; on delete, erase `segs[hot-1<0?0:…]` — deleting point i removes segment `min(i, segs.size()-1)`), then call `shape_editor_apply_segs`.

- [ ] **Step 2: Draw override curves + power dots**

After the skeleton lines (~5903), for `isShaper` draw each overridden segment's true shape (sample `Curve::eval_seg` at 24 steps between the endpoints) in a distinct color, and a mid-segment dot:

```cpp
    if (isShaper) {
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            if (!segs[i].overridden) continue;
            ImVec2 prev(tx(pts[i].first), ty(pts[i].second));
            for (int k = 1; k <= 24; ++k) {
                float t = float(k) / 24.0f;
                float xx = pts[i].first + (pts[i+1].first - pts[i].first) * t;
                float yy = Curve::eval_seg(pts[i].second, pts[i+1].second,
                                           t, segs[i], 0.5f);
                ImVec2 p(tx(xx), ty(yy));
                dl->AddLine(prev, p, IM_COL32(255, 150, 60, 220), 1.6f);
                prev = p;
            }
            float mx = 0.5f * (pts[i].first + pts[i+1].first);
            float my = Curve::eval_seg(pts[i].second, pts[i+1].second,
                                       0.5f, segs[i], 0.5f);
            dl->AddCircleFilled(ImVec2(tx(mx), ty(my)), 5.0f,
                                IM_COL32(255, 150, 60, 255));
        }
    }
```

- [ ] **Step 3: Gestures**

Ahead of the existing right-click-deletes-point branch (~5961), add Shaper-only handling, careful with precedence: (a) right-click ON a power dot (10 px pick) clears that override; (b) right-click NOT on a point and within 8 px of a segment's skeleton line starts an override drag: set `segs[i] = {true, RampType::Expo, 2.0f}`, `segDragIdx = int(i)`, record `segDragStartPower`; (c) while right-button dragging with `segDragIdx >= 0`, map vertical drag to power: `power = std::clamp(segDragStartPower * std::pow(2.0f, -ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).y / 60.0f), 0.1f, 20.0f)`; release ends the drag. Left-drag on a power dot (add dots to the hot-test before points) adjusts power the same way without re-creating. Segment hit-test: point-to-segment distance in screen space between consecutive pts. Existing right-click point-delete keeps priority when `hot >= 0`.

Toolbar: add `Clear overrides` button (Shaper only), active only when any `segs[i].overridden`; wrap in the existing confirm idiom (`ImGui::OpenPopup` + modal, matching other confirms in this file) — this stands in for the spec's global-reset rule until a non-Shaper client gets overrides; note the deviation in the commit message.

All UI strings: "segment", "points", "override" — never "knots".

- [ ] **Step 4: Manual verification**

Build; run `build/tools/mforce_ui/Release/mforce_ui.exe`; open `patches/library/winds/oboe_default.json`; double-click the Junction preview. Verify: right-click-drag mid-segment creates an orange curve + dot; dragging changes curvature audibly on keyboard playback; right-click the dot restores; save + reload round-trips the overrides (`segs` appears in the saved JSON only when overrides exist).

- [ ] **Step 5: Commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "ui: Shaper editor per-segment overrides - gesture, power dot, clear"
```

---

### Task 7: Preset curves menu

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — shape editor toolbar (Shaper client), preset table near `ShapeEdGenState` (~5376)

**Interfaces:**
- Consumes: `shape_editor_apply_shaper`, `shape_editor_apply_segs` (Task 6).
- Produces: `static const ShaperPreset kShaperPresets[]` — name + points + segs.

- [ ] **Step 1: Preset data**

```cpp
struct ShaperPreset {
    const char* name;
    std::vector<std::pair<float, float>> pts;
    // (segment index, typeIdx 1..5, power) — sparse overrides
    std::vector<std::tuple<int, int, float>> segs;
};
static const ShaperPreset kShaperPresets[] = {
    {"bow",      {{-1.00f,-0.33f},{-0.10f,-0.52f},{-0.015f,-0.95f},
                  { 0.015f, 0.95f},{ 0.10f, 0.52f},{ 1.00f, 0.33f}},
                 {{0,2,0.5f},{4,2,2.0f}}},          // hyperbolic slip flanks
    {"reed1",    {{-1.00f,-0.90f},{-0.55f,-0.80f},{-0.12f,-0.42f},
                  { 0.00f, 0.00f},{ 0.12f, 0.42f},{ 0.40f, 0.70f},
                  { 0.70f, 0.45f},{ 1.00f, 0.10f}}, {}},
    {"reed2",    {{-1.00f,-0.95f},{-0.50f,-0.85f},{-0.10f,-0.48f},
                  { 0.00f, 0.00f},{ 0.10f, 0.50f},{ 0.28f, 0.72f},
                  { 0.50f, 0.15f},{ 0.62f, 0.00f},{ 1.00f, 0.00f}},
                 {{7,5,0.0f}}},                      // Hold: closed reed
    {"lip",      {{-1.00f,-0.45f},{-0.50f,-0.30f},{-0.20f,-0.10f},
                  {-0.05f,-0.03f},{ 0.06f, 0.04f},{ 0.25f, 0.12f},
                  { 0.55f, 0.50f},{ 0.80f, 0.92f},{ 1.00f, 0.98f}}, {}},
    {"jet",      {{-1.00f,-0.82f},{-0.45f,-0.78f},{-0.18f,-0.50f},
                  { 0.00f, 0.00f},{ 0.18f, 0.50f},{ 0.45f, 0.78f},
                  { 1.00f, 0.82f}},
                 {{0,4,0.0f},{1,4,0.0f},{2,4,0.0f},
                  {3,4,0.0f},{4,4,0.0f},{5,4,0.0f}}},// Sine everywhere
    {"hard",     {{-0.52f,-0.60f},{ 0.52f, 0.60f}}, {}},
    {"sine",     {{-1.00f, 0.00f},{-0.50f,-1.00f},{ 0.00f, 0.00f},
                  { 0.50f, 1.00f},{ 1.00f, 0.00f}},
                 {{0,4,0.0f},{1,4,0.0f},{2,4,0.0f},{3,4,0.0f}}},
    {"saw",      {{-1.00f,-1.00f},{-0.002f, 1.00f},{ 0.002f,-1.00f},
                  { 1.00f, 1.00f}},
                 {{0,1,0.0f},{1,1,0.0f},{2,1,0.0f}}},
    {"triangle", {{-1.00f, 0.00f},{-0.50f,-1.00f},{ 0.00f, 0.00f},
                  { 0.50f, 1.00f},{ 1.00f, 0.00f}},
                 {{0,1,0.0f},{1,1,0.0f},{2,1,0.0f},{3,1,0.0f}}},
};
```

- [ ] **Step 2: Toolbar combo + revert stash**

Shaper-client toolbar: a `Preset` combo listing the names plus a leading em-dash blank. On selection: stash current pts+segs into `ShapeEditorState` (`std::vector<...> revertPts, revertSegsRaw; bool hasRevert;`), then build pts/segs from the preset (expand sparse tuples into a `std::vector<Curve::Seg>` of size npts-1), call `shape_editor_apply_shaper` + `shape_editor_apply_segs`. Show a `Revert` button while `hasRevert` (restores the stash, one level — the file has no undo stack; this delivers the spec's "undoable" as one-shot revert; flag to Matt at review).

- [ ] **Step 3: Manual verification**

Open a Shaper; insert each preset; verify shapes match docs (bow = spike + falling flanks, reed2 flat-zero tail via Hold, jet rounded via Sine). Play the UI keyboard against a loop patch to confirm each loads without NaN/blowup (guarded taps clamp regardless).

- [ ] **Step 4: Commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "ui: Shaper preset curves - nature five + hard/sine/saw/triangle"
```

---

### Task 8: Morph pin (engine + serialization)

**Files:**
- Modify: `engine/include/mforce/source/shaper_source.h`
- Modify: `engine/src/patch_loader.cpp` (values2 validation — see Step 3)
- Modify: `tools/engine_tests/main.cpp` (new `run_shaper_morph_tests()`)

**Interfaces:**
- Consumes: `valuesB_`/`segsB_` members accepted in Task 5.
- Produces: Shaper param `"morph"` (default 0, range 0-1); point-space blend. m=0 ≡ curve A exactly; m=1 ≡ curve B exactly.

- [ ] **Step 1: Write failing tests**

```cpp
static void run_shaper_morph_tests() {
    ShaperSource sh;
    sh.set_array("values",  {-1.0f,-1.0f, 0.4f,0.2f, 1.0f,1.0f});
    sh.set_array("values2", {-1.0f,-1.0f, 0.1f,0.6f, 1.0f,1.0f});
    auto morph = std::make_shared<ConstantSource>(0.0f);
    sh.set_param("morph", morph);
    /* prepare + next() idiom */
    // m=0: exactly curve A (knee at 0.4)
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-7f);
    // m=1: exactly curve B (knee at 0.1)
    /* set morph const to 1.0, next() */
    CHECK_NEAR(sh.map(0.1f), 0.6f, 1e-7f);
    // m=0.5: knee SLIDES to x=0.25, y=0.4 — point-space, not output blend
    /* set morph const to 0.5, next() */
    CHECK_NEAR(sh.map(0.25f), 0.4f, 1e-6f);
    // Mismatched point count: morph ignored (B degenerates to A)
    sh.set_array("values2", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-6f);
    // No values2 at all: byte-identity path
    ShaperSource plain;
    plain.set_array("values", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(plain.map(0.3f), 0.3f, 1e-7f);
}
```

- [ ] **Step 2: Verify failure** — build+run: FAIL (`morph` param unknown).

- [ ] **Step 3: Implement**

Param descriptors gain `{"morph", 0.0f, 0.0f, 1.0f, "0-1"}`; `set_param`/`get_param` handle `morph`; `prepare` prepares it; `next()` advances it into `float morphCur_{0.0f};`. `map` becomes:

```cpp
  float map(float x) {
    const bool canMorph = !valuesB_.empty()
                       && valuesB_.size() == values_.size();
    const float m = canMorph ? std::clamp(morphCur_, 0.0f, 1.0f) : 0.0f;
    if (m <= 0.0f)
      return Curve::eval_flat(values_, segs_, Curve::Domain::Linear,
                              smoothCur_, x);
    const auto& A = values_;
    const auto& B = valuesB_;
    auto lerp1 = [m](float a, float b) { return a + (b - a) * m; };
    return Curve::eval_core(A.size() / 2,
        [&](size_t i) { return lerp1(A[i*2],     B[i*2]);     },
        [&](size_t i) { return lerp1(A[i*2 + 1], B[i*2 + 1]); },
        [&](size_t i) {
            Curve::Seg s = i < segs_.size() ? segs_[i] : Curve::Seg{};
            if (s.overridden && i < segsB_.size() && segsB_[i].overridden)
                s.power = lerp1(s.power, segsB_[i].power);
            return s;
        },
        Curve::Domain::Linear, smoothCur_, x);
  }
```

(Ascending-x is preserved under lerp of two ascending lists — note as a comment.) Loader: nothing needed for wiring (arrays generic, `morph` generic param) — but add validation right after the generic array pass at `patch_loader.cpp:418`: if a node's `values2` is non-empty and its length differs from `values`, `throw std::runtime_error(id + ": values2 length must match values")`. Seg types shared: loader also rejects `segs2` whose typeIdx column differs from `segs` (compare every even index; powers may differ).

- [ ] **Step 4: Tests + null gate**

engine_tests → PASS. `python tools/null_gate_perform_source.py` → zero DIFF (no patch carries values2). Render `patches/library/winds/oboe_default.json` via mforce_cli and hash-compare against a pre-task render — byte-identical.

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/source/shaper_source.h engine/src/patch_loader.cpp tools/engine_tests/main.cpp
git commit -m "feat: Shaper morph pin - point-space A/B curve blend"
```

---

### Task 9: Editor A/B morph workflow

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — shape editor (Shaper client)

**Interfaces:**
- Consumes: `values2`/`segs2` via `arrayValues` (auto-mirrored), Task 6's segs plumbing.
- Produces: spec §5 workflow: + morph, A/B toggle, ghost, structural sync, blend overlay.

- [ ] **Step 1: State + birth/delete**

`ShapeEditorState` gains `int activeCurve{0};`. Toolbar (Shaper client): if `values2` is empty show `+ morph` — on click copy `values`→`values2` and `segs`→`segs2` in `arrayValues`, `push_array` both. If non-empty show `A`/`B` radio (sets `activeCurve`) and a `remove morph` button (confirm modal; keeps the ACTIVE curve: if B active, copy values2/segs2 into values/segs first; then clear values2/segs2 and push).

- [ ] **Step 2: Route edits to the active curve**

When `activeCurve == 1`, the editor's `pts`/`segs` load from `values2`/`segs2` and apply back to them (parameterize the load/apply helpers with the array names: `"values"`/`"segs"` vs `"values2"`/`"segs2"`). Geometric edits (drag point, power drag) touch only the active arrays. Structural edits (add point, delete point, create/clear override TYPE) apply to BOTH: for add-point on the active curve at x, insert into the inactive curve a point ON its current polyline at the same segment fraction (evaluate the inactive curve's segment at the same t); for delete, erase the same index from both; for a type change, set the same `type`/`overridden` on both (each keeps its own power).

- [ ] **Step 3: Ghost + blend overlay**

Draw the inactive curve's polyline faint (`IM_COL32(140,140,150,90)`) behind the skeleton. If the node's live morph value is available (`node.dspSource->get_param("morph")` non-null → `->current()`), draw the blended curve thin in the engine-overlay color, sampled via the same lerped-points walk (24 steps per segment).

- [ ] **Step 4: Manual verification**

On a copy of oboe_default in `patches/scratch/`: + morph; edit B's near-origin points steeper; wire an Envelope to the Junction's morph pin (gold dynamic-pin flow as with any param); keyboard-play and confirm the attack morphs; save/reload; confirm A/B and overrides survive; `remove morph` while B active keeps B's shape as the single curve. Confirm saved JSON of a never-morphed Shaper gains no keys (roundtrip: `python tools/test_stable_roundtrip.py`).

- [ ] **Step 5: Commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "ui: Shaper A/B morph workflow - birth-as-copy, ghost, structural sync"
```

---

### Task 10: Final gates + docs

**Files:**
- Modify: `docs/autonomy/STATUS.md` (dsp row: spec+plan landed, what shipped)

- [ ] **Step 1: Full validation sweep**

Run in order; all must pass:
`build/tools/engine_tests/Release/engine_tests.exe` (0 fails);
`python tools/null_gate_perform_source.py` (zero DIFF);
`python tools/test_stable_roundtrip.py`;
`python tools/rt_smoke.py` (live-path smoke).

- [ ] **Step 2: One audible artifact**

Render a morph demonstrator into the audition queue: copy oboe_default to `patches/audition/curve_morph/oboe_morph_demo.json`, add a steeper-origin curve B (the oboe_dv_junc_tweaked junction values), wire Drive_env (or a dedicated envelope) to `morph`, render 5-note format to `renders/dsp/audition/curve_morph/`. This validates the whole stack end-to-end and gives Matt something to hear. NOT auto-promoted anywhere.

- [ ] **Step 3: Update STATUS.md dsp row + commit**

```bash
git add docs/autonomy/STATUS.md
git commit -m "docs: curve/morph implementation landed - status note"
```

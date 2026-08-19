# PerformSource P1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Phase 1 of docs/perform_source_design.md — PerformSource as the
per-voice performance store, CurveNode, Envelope minValue/maxValue +
Stage.nominal, paramMap load-conversion, ParamSlot retirement — with
**bit-identical audio**, proven by a whole-library null gate.

**Architecture:** Baked semantics only: PerformSource outputs are constants
between `set_note` calls. paramMap JSON is converted at load into wires
(ordinary pins), push bindings (configs + Multiplex fans), and direct-freq
bindings (bend-graft swap targets). No liveness, no UI work (that's P2/P3).

**Tech Stack:** C++20 (MSVC), CMake (VS-bundled:
`C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`),
nlohmann::json, Python 3 for the null gate.

## Global Constraints

- **Null gate is the law:** after Tasks 6, 7, 8 the render hash of every
  patch in `patches/library` + `patches/baselines` must equal the frozen
  manifest. A diff = stop and diagnose, never rationalize.
- No heap allocation in `next()` paths (CLAUDE.md non-negotiable).
- Build from repo root; check `mforce_ui.exe` is NOT running before any
  build (locked exe blocks relink): `Get-Process mforce_ui` must fail.
- Verify current branch is `main` live before each commit
  (`git branch --show-current`) — two Claudes share this working copy.
- Commit trailer: `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- Port formulas **verbatim** where bit-identity is claimed (ParamSlot::map
  and ::vmap into CurveNode) — same expressions, same float ops, same order.
- New JSON fields are all optional with defaults that reproduce current
  behavior exactly.

---

### Task 0: Freeze the null-gate manifest (BEFORE any engine change)

**Files:**
- Create: `tools/null_gate_perform_source.py`
- Create (generated): `tools/null_gate_manifest.json`

**Interfaces:**
- Produces: `python tools/null_gate_perform_source.py --freeze` (writes
  manifest) and `python tools/null_gate_perform_source.py` (verifies; exit
  0 = all identical, 1 = diffs listed on stdout). Tasks 6-8 consume this.

- [ ] **Step 1: Write the script**

```python
"""Null gate for the PerformSource P1 conversion (plan_perform_source_p1.md).

--freeze : render every library/baselines patch with the CURRENT binary and
           record SHA256 of the WAV bytes (or RENDER_FAIL:<code>).
(default): re-render and compare against the frozen manifest.

Render failures are recorded and compared like hashes: a patch that failed
before must fail identically after (no silent regressions either way).
"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
SETS = ["patches/library", "patches/baselines"]
MANIFEST = ROOT / "tools/null_gate_manifest.json"
OUT = ROOT / "renders/scratch/null_gate.wav"

def render_hash(patch: Path) -> str:
    if OUT.exists():
        OUT.unlink()
    r = subprocess.run([str(CLI), str(patch), str(OUT)],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0 or not OUT.exists():
        return f"RENDER_FAIL:{r.returncode}"
    return hashlib.sha256(OUT.read_bytes()).hexdigest()

def main():
    patches = [p for s in SETS for p in sorted((ROOT / s).rglob("*.json"))]
    if "--freeze" in sys.argv:
        manifest = {}
        for p in patches:
            key = str(p.relative_to(ROOT)).replace("\\", "/")
            manifest[key] = render_hash(p)
            print("froze", key)
        MANIFEST.write_text(json.dumps(manifest, indent=1))
        print(f"Manifest: {len(manifest)} entries")
        return
    ref = json.loads(MANIFEST.read_text())
    bad = 0
    for p in patches:
        key = str(p.relative_to(ROOT)).replace("\\", "/")
        h = render_hash(p)
        if ref.get(key) != h:
            bad += 1
            print(f"DIFF {key}: {ref.get(key)} -> {h}")
    print(f"{len(patches) - bad}/{len(patches)} identical")
    sys.exit(1 if bad else 0)

main()
```

- [ ] **Step 2: Build current HEAD so the frozen binary is current**

Run: `& "<cmake>" --build build --config Release --target mforce_cli`
Expected: `mforce_cli.exe` link line, no errors.

- [ ] **Step 3: Freeze**

Run: `python tools/null_gate_perform_source.py --freeze`
Expected: one `froze <path>` line per patch, final count. Takes minutes
(hundreds of renders) — run in background, wait for completion.

- [ ] **Step 4: Sanity-verify the gate passes against itself**

Run: `python tools/null_gate_perform_source.py`
Expected: `N/N identical`, exit 0.

- [ ] **Step 5: Commit**

```bash
git add tools/null_gate_perform_source.py tools/null_gate_manifest.json
git commit -m "test: null-gate manifest frozen for PerformSource P1"
```

---

### Task 1: engine_tests scaffold

**Files:**
- Create: `tools/engine_tests/main.cpp`
- Create: `tools/engine_tests/CMakeLists.txt`
- Modify: `CMakeLists.txt` (root — add subdirectory after `tools/stamp_test`)

**Interfaces:**
- Produces: `engine_tests.exe` returning 0 on success, printing
  `ALL PASS (<n> checks)`; `CHECK(cond)` and `CHECK_NEAR(a,b,eps)` macros;
  a `void run_<area>_tests()` convention — later tasks append test
  functions and call them from `main()`.

- [ ] **Step 1: Write the harness with one trivial failing check**

`tools/engine_tests/main.cpp`:
```cpp
// Assert-style engine unit tests (PerformSource P1, plan_perform_source_p1.md).
// House testing is render-based; this exe covers the value-level semantics
// renders can't isolate (curve interp identity, envelope mapping, adapters).
#include <cstdio>
#include <cmath>
#include <cstdlib>

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { ++g_checks; float _a=(a), _b=(b); \
  if (std::fabs(_a-_b) > (eps)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)

int main() {
    CHECK(1 == 2);  // scaffold sanity: must FAIL until Step 3
    if (g_fails) { std::printf("%d/%d FAILED\n", g_fails, g_checks); return 1; }
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
}
```

`tools/engine_tests/CMakeLists.txt`:
```cmake
add_executable(engine_tests main.cpp)
target_link_libraries(engine_tests PRIVATE mforce_engine)
```

Root `CMakeLists.txt`: after `add_subdirectory(tools/stamp_test)` add:
```cmake
add_subdirectory(tools/engine_tests)
```

- [ ] **Step 2: Build and verify the scaffold FAILS**

Run: `& "<cmake>" --build build --config Release --target engine_tests` then
`build/tools/engine_tests/Release/engine_tests.exe`
Expected: `FAIL ... 1 == 2`, exit 1.

- [ ] **Step 3: Replace the sanity check with `CHECK(1 == 1);`, rebuild, verify pass**

Expected: `ALL PASS (1 checks)`, exit 0.

- [ ] **Step 4: Commit**

```bash
git add tools/engine_tests CMakeLists.txt
git commit -m "test: engine_tests assert harness"
```

---

### Task 2: CurveNode

**Files:**
- Create: `engine/include/mforce/core/curve_node.h`
- Modify: `engine/src/source_registrations.cpp` (include + register)
- Modify: `engine/src/patch_loader.cpp` (build_graph special case, near the
  `type == "Envelope"` case at ~line 328)
- Modify: `tools/engine_tests/main.cpp` (tests)

**Interfaces:**
- Produces: `struct CurveNode final : ValueSource` with public
  `std::vector<std::pair<float,float>> knots;`,
  `enum class CurveInterp { Linear, LogX, LogLog };` member `interp`
  (default Linear), `float map(float x) const`, input param `"source"`.
  JSON: `{"type":"CurveNode","params":{"knots":[[x,y],...],"interp":
  "linear|logx|loglog","source":{"ref":"..."}}}`.
- CRITICAL: `map()` for LogX/LogLog is ParamSlot::map VERBATIM
  (instrument.h:121-143), and for Linear is ParamSlot::vmap VERBATIM
  (instrument.h:106-119) — bit-identity for the Task 6 conversion depends
  on this. Copy the loop bodies; do not "improve" them.

- [ ] **Step 1: Write failing tests** (append to main.cpp; call from main)

```cpp
#include "mforce/core/curve_node.h"
#include "mforce/core/constant_source.h"   // adjust to the actual header name
using namespace mforce;

static void run_curve_node_tests() {
    // Linear (vcurve semantics): clamp + linear interp
    CurveNode lin;
    lin.knots = {{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}};
    CHECK_NEAR(lin.map(-1.0f), 0.3f, 1e-6f);   // clamp low
    CHECK_NEAR(lin.map(0.1f),  0.3f, 1e-6f);   // flat segment
    CHECK_NEAR(lin.map(1.0f),  1.6f, 1e-6f);   // clamp high
    CHECK_NEAR(lin.map(0.925f), 1.3f, 1e-3f);  // midpoint of last segment

    // LogX (default paramMap curve semantics): value linear in log(x)
    CurveNode logx; logx.interp = CurveNode::CurveInterp::LogX;
    logx.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    CHECK_NEAR(logx.map(1000.0f), 1.0f, 1e-4f);  // geometric midpoint

    // LogLog: 2 knots == exact power law (y = x^2 through (10,100),(100,10000))
    CurveNode ll; ll.interp = CurveNode::CurveInterp::LogLog;
    ll.knots = {{10.0f, 100.0f}, {100.0f, 10000.0f}};
    CHECK_NEAR(ll.map(31.6227766f), 1000.0f, 0.5f);

    // Pull path: source wired, evaluates on change, caches on repeat
    auto cs = std::make_shared<ConstantSource>(0.5f);
    CurveNode wired;
    wired.knots = {{0.0f, 0.0f}, {1.0f, 10.0f}};
    wired.set_param("source", cs);
    wired.next();
    CHECK_NEAR(wired.current(), 5.0f, 1e-6f);

    // Empty knots = identity (matches ParamSlot::map empty-curve behavior)
    CurveNode ident;
    CHECK_NEAR(ident.map(123.0f), 123.0f, 1e-6f);
}
```

- [ ] **Step 2: Build, verify FAIL** (compile error: no such header) —
  expected.

- [ ] **Step 3: Implement `curve_node.h`**

```cpp
#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>
#include <limits>
#include <memory>
#include <vector>

namespace mforce {

// Transfer node: y = curve(x), knots sorted by x, clamped at end knots.
// The runtime (pullable) home of the paramMap curve/vcurve data
// (docs/perform_source_design.md §2.2). Interp modes:
//   Linear — y linear in x               (legacy ParamSlot::vmap, verbatim)
//   LogX   — y linear in log(x)          (legacy ParamSlot::map,  verbatim)
//   LogLog — log(y) linear in log(x): a 2-knot segment is exactly y=k*x^n
struct CurveNode final : ValueSource {
  enum class CurveInterp { Linear, LogX, LogLog };

  std::vector<std::pair<float, float>> knots;
  CurveInterp interp{CurveInterp::Linear};

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    lastX_ = std::numeric_limits<float>::quiet_NaN();  // force re-eval
  }

  float next() override {
    float x = source_ ? (source_->next(), source_->current()) : 0.0f;
    if (x != lastX_) { lastX_ = x; cur_ = map(x); }
    return cur_;
  }
  float current() const override { return cur_; }

  float map(float x) const {
    if (knots.empty()) return x;  // identity — matches empty-curve ParamSlot
    if (interp == CurveInterp::Linear) {
      // === ParamSlot::vmap body, verbatim (instrument.h:106-119) ===
      if (x <= knots.front().first) return knots.front().second;
      if (x >= knots.back().first)  return knots.back().second;
      for (size_t i = 1; i < knots.size(); ++i) {
        if (x <= knots[i].first) {
          float t = (x - knots[i - 1].first) /
                    (knots[i].first - knots[i - 1].first);
          return knots[i - 1].second +
                 (knots[i].second - knots[i - 1].second) * t;
        }
      }
      return knots.back().second;
    }
    // === ParamSlot::map body, verbatim (instrument.h:121-143); LogLog is
    // its loglog branch ===
    if (x <= knots.front().first) return knots.front().second;
    if (x >= knots.back().first)  return knots.back().second;
    for (size_t i = 1; i < knots.size(); ++i) {
      if (x <= knots[i].first) {
        float lf = std::log(x / knots[i - 1].first) /
                   std::log(knots[i].first / knots[i - 1].first);
        if (interp == CurveInterp::LogLog &&
            knots[i - 1].second > 0.0f && knots[i].second > 0.0f) {
          return knots[i - 1].second *
                 std::pow(knots[i].second / knots[i - 1].second, lf);
        }
        return knots[i - 1].second +
               (knots[i].second - knots[i - 1].second) * lf;
      }
    }
    return knots.back().second;
  }

  const char* type_name() const override { return "CurveNode"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"source", 0.0f, -1.0e9f, 1.0e9f},
    };
    return descs;
  }
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source") source_ = std::move(src);
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source") return source_;
    return nullptr;
  }

private:
  std::shared_ptr<ValueSource> source_;
  float cur_{0.0f};
  float lastX_{std::numeric_limits<float>::quiet_NaN()};
};

} // namespace mforce
```

NOTE: before finalizing, open instrument.h:106-143 and diff the two loop
bodies against this file character-by-character (modulo variable renames
curve→knots, freq/vel→x). Fix any drift toward instrument.h, not away.
If ParamDescriptor's field layout differs from `{name, default, min, max}`,
mirror an existing Modulator source (var_source.h:50-57).

- [ ] **Step 4: Register + loader case**

`source_registrations.cpp`: add `#include "mforce/core/curve_node.h"` with
the other core includes; register alongside the other Modulators:
```cpp
    reg.register_type("CurveNode", SourceCategory::Modulator,
        [](int sr, auto) { (void)sr; return std::make_shared<CurveNode>(); });
```
(Mirror the exact lambda signature of the adjacent registrations.)

`patch_loader.cpp`, in build_graph next to the `type == "Envelope"` case
(~line 328) add:
```cpp
        else if (type == "CurveNode") {
            auto cn = std::make_shared<CurveNode>();
            if (pp) {
                const auto& p = *pp;
                if (p.contains("knots"))
                    for (const auto& k : p["knots"])
                        cn->knots.emplace_back(k.at(0).get<float>(),
                                               k.at(1).get<float>());
                const std::string in = p.value("interp", "linear");
                cn->interp = in == "loglog" ? CurveNode::CurveInterp::LogLog
                           : in == "logx"   ? CurveNode::CurveInterp::LogX
                           :                  CurveNode::CurveInterp::Linear;
            }
            valueNodes[id] = cn;
        }
```
(`"source"` wires through the generic param pass like every other node.
Match the surrounding case style exactly — `pp`/`p` naming per the Envelope
case actually present in the file.)

- [ ] **Step 5: Build engine_tests + mforce_cli, run tests**

Expected: `ALL PASS`, and mforce_cli builds clean.

- [ ] **Step 6: Commit**

```bash
git add engine/include/mforce/core/curve_node.h engine/src/source_registrations.cpp engine/src/patch_loader.cpp tools/engine_tests/main.cpp
git commit -m "feat(engine): CurveNode transfer source — paramMap curve semantics, pullable"
```

---

### Task 3: Envelope minValue / maxValue (pullable)

**Files:**
- Modify: `engine/include/mforce/core/envelope.h`
- Modify: `tools/engine_tests/main.cpp`

**Interfaces:**
- Produces: Envelope params `"minValue"` / `"maxValue"`
  (shared_ptr<ValueSource>, null defaults). Output becomes
  `min + (max - min) * raw` where raw is the unmapped 0..1-domain stage
  value. Null defaults short-circuit to raw — bit-identical.
- INVARIANT: all internal state stays in the RAW domain — specifically the
  gate re-anchor (`gateFrom_ = cur_` at envelope.h:141) must capture the
  raw value, and stage startVal/endVal stay 0..1-domain. Only the value
  RETURNED by next()/current() is mapped.

- [ ] **Step 1: Write failing tests**

```cpp
#include "mforce/core/envelope.h"

static void run_envelope_range_tests() {
    // A 2-stage envelope 0->1 then 1->1; nominal render at 100 samples.
    RenderContext ctx{48000};
    auto mk = []() {
        auto env = std::make_shared<Envelope>(48000);
        Envelope::Stage a; a.ramp.startVal = 0.0f; a.ramp.endVal = 1.0f;
        a.percent = 0.5f;
        Envelope::Stage b; b.ramp.startVal = 1.0f; b.ramp.endVal = 1.0f;
        b.percent = 0.5f;
        env->stages().push_back(a);   // use the actual stage-adding API —
        env->stages().push_back(b);   // if stages are ctor/setter-fed, adapt
        return env;
    };
    // Default (no min/max): final value 1.0 — unchanged behavior
    auto e1 = mk(); e1->prepare(ctx, 100);
    float last = 0; for (int i = 0; i < 100; ++i) last = e1->next();
    CHECK_NEAR(last, 1.0f, 1e-4f);

    // minValue 80, maxValue 500 (constants): final value 500, first sample
    // near 80 (raw ~0)
    auto e2 = mk();
    e2->set_param("minValue", std::make_shared<ConstantSource>(80.0f));
    e2->set_param("maxValue", std::make_shared<ConstantSource>(500.0f));
    e2->prepare(ctx, 100);
    float first = e2->next();
    CHECK(first < 120.0f && first >= 80.0f);
    for (int i = 1; i < 100; ++i) last = e2->next();
    CHECK_NEAR(last, 500.0f, 1e-2f);
}
```
(Adapt stage construction to the real API — read envelope.h first; stages
may be a public vector or ctor argument. Keep the two-stage 0→1,1→1 shape.)

- [ ] **Step 2: Build, verify FAIL** (`set_param` unknown / minValue
  ignored).

- [ ] **Step 3: Implement**

In envelope.h:
1. Add members near the other shared state:
```cpp
  // Output range mapping (PerformSource P1, perform_source_design.md §2.4):
  // out = lo + (hi - lo) * raw. Null = 0/1 = identity (bit-compatible).
  // Pullable params per the credo — velocity->maxValue is the garden
  // velocity wiring once PerformSource lands.
  std::shared_ptr<ValueSource> minValue_;
  std::shared_ptr<ValueSource> maxValue_;
```
2. In `prepare(...)`, after existing child prep (if any):
```cpp
    if (minValue_) minValue_->prepare(ctx, frames);
    if (maxValue_) maxValue_->prepare(ctx, frames);
```
3. In `next()`, find the single point where the computed stage value is
   stored into the member returned by `current()` (the `cur_` write at the
   end of the stage evaluation, envelope.h ~line 340-360 region). Rename
   the stored raw value path so the raw member (`rawCur_`, new) holds the
   unmapped value, keep `gateFrom_ = ` capture reading `rawCur_`, then:
```cpp
    if (minValue_ || maxValue_) {
      if (minValue_) minValue_->next();
      if (maxValue_) maxValue_->next();
      const float lo = minValue_ ? minValue_->current() : 0.0f;
      const float hi = maxValue_ ? maxValue_->current() : 1.0f;
      cur_ = lo + (hi - lo) * rawCur_;
    } else {
      cur_ = rawCur_;
    }
    return cur_;
```
4. Add/extend `param_descriptors` / `set_param` / `get_param` (Envelope may
   not override these today — add overrides following var_source.h:50-70):
```cpp
  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"minValue", 0.0f, -100000.0f, 100000.0f},
      {"maxValue", 1.0f, -100000.0f, 100000.0f},
    };
    return descs;
  }
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "minValue") { minValue_ = std::move(src); return; }
    if (name == "maxValue") { maxValue_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "minValue") return minValue_;
    if (name == "maxValue") return maxValue_;
    return nullptr;
  }
```
(If Envelope already overrides these, merge the new names in.)
5. Loader: nothing — `"minValue": 80.0` / `{"ref": "..."}` in an Envelope's
   params flows through the generic param-wiring pass automatically once
   descriptors exist. Verify in Step 5.

- [ ] **Step 4: Build + run engine_tests** — expected `ALL PASS`.

- [ ] **Step 5: JSON smoke: hand-edit a scratch copy of
  `patches/library/keys/Piano_bright.json`'s env1 to add
  `"minValue": 0.0, "maxValue": 2.0`, render via CLI, confirm it renders
  louder excitation (peak differs) then DELETE the scratch copy. Then run
  the null gate (unmodified patches must be untouched):**

Run: `python tools/null_gate_perform_source.py`
Expected: `N/N identical` — the mapping defaults are bit-transparent.

- [ ] **Step 6: Commit**

```bash
git add engine/include/mforce/core/envelope.h tools/engine_tests/main.cpp
git commit -m "feat(engine): Envelope minValue/maxValue pullable range mapping"
```

---

### Task 4: Stage.nominal

**Files:**
- Modify: `engine/include/mforce/core/envelope.h` (Stage struct + layout)
- Modify: `engine/src/patch_loader.cpp` (stage JSON: parse `"nominal"`)
- Modify: `tools/engine_tests/main.cpp`

**Interfaces:**
- Produces: `Envelope::Stage::nominal` (float seconds, default 0 = absent).
  Rule: **in gated mode only** (`gated_ == true`), a stage with
  `nominal > 0` lays out as `nominal` seconds (then the existing
  minSec/maxSec clamp path applies); non-gated behavior and nominal==0
  stages are unchanged. Loader: `s.nominal = sj.value("nominal", 0.0f);`
  beside the other stage fields (patch_loader.cpp ~line 335).

- [ ] **Step 1: Write failing test**

```cpp
static void run_stage_nominal_tests() {
    RenderContext ctx{48000};
    auto env = std::make_shared<Envelope>(48000);
    // One 0->1 stage with nominal 0.25 s, then expand (percent 0), gated.
    Envelope::Stage a; a.ramp.startVal = 0.0f; a.ramp.endVal = 1.0f;
    a.percent = 0.001f; a.nominal = 0.25f;
    Envelope::Stage hold; hold.ramp.startVal = 1.0f; hold.ramp.endVal = 1.0f;
    hold.percent = 0.0f;   // expand stage
    env->stages().push_back(a); env->stages().push_back(hold);
    env->set_gated(true);
    env->prepare(ctx, 48000);  // nominal duration 1 s
    // With nominal honored, the attack lasts 0.25 s = 12000 samples: value
    // at sample 6000 is ~0.5; WITHOUT nominal the 0.001-pct attack would
    // have finished within ~48 samples.
    float v = 0; for (int i = 0; i < 6000; ++i) v = env->next();
    CHECK(v > 0.3f && v < 0.7f);
}
```

- [ ] **Step 2: Build, verify FAIL** (`nominal` not a member).

- [ ] **Step 3: Implement**

Stage struct gains `float nominal{0.0f};   // seconds; Live-mode stage
length when gated (perform_source_design.md §2.4). 0 = unset.`

In the stage-length computation inside `prepare` (the loop that fills
`stageCounts_`, envelope.h ~line 280-300 region — where percent/timeMode
resolve to sample counts): before the existing percent-based computation
for a stage, insert:
```cpp
      if (gated_ && st.nominal > 0.0f) {
        stageSec = st.nominal;   // then fall through to the existing
                                  // minSec/maxSec clamp exactly as percent
                                  // stages do
      }
```
adapting `stageSec` to the actual local variable the clamp operates on.
The expand stage (percent == 0) is NOT affected by nominal — it still
expands.

Loader stage parse (patch_loader.cpp, the `Envelope::Stage s;` block at
~line 335): add `s.nominal = sj.value("nominal", 0.0f);` beside minSec.

- [ ] **Step 4: Build + run engine_tests** — `ALL PASS`.

- [ ] **Step 5: Null gate** (no patch has `nominal`; must be transparent):
  `python tools/null_gate_perform_source.py` → `N/N identical`.

- [ ] **Step 6: Commit**

```bash
git add engine/include/mforce/core/envelope.h engine/src/patch_loader.cpp tools/engine_tests/main.cpp
git commit -m "feat(engine): Stage.nominal — patch-intrinsic live stage layout"
```

---

### Task 5: PerformSource + NoteState + PerformOut

**Files:**
- Create: `engine/include/mforce/render/perform_source.h`
- Modify: `tools/engine_tests/main.cpp`

**Interfaces:**
- Produces (Task 6 consumes exactly these):
```cpp
struct NoteState { float frequency{440.0f}; float velocity{0.8f}; int durSamples{0}; };
class PerformSource {                       // NOT a ValueSource
  void set_note(float freqHz, float velocity, int durSamples);
  const NoteState& note() const;
};
struct PerformOut final : ValueSource {     // leaf adapter
  enum class Field { Frequency, Velocity };
  PerformOut(std::shared_ptr<PerformSource> ps, Field f);
  // next()/current() report the field; prepare() is a no-op.
};
```
- InstrumentState is deliberately NOT built in P1 (YAGNI — first consumer
  is the P3 wheel).

- [ ] **Step 1: Write failing tests**

```cpp
#include "mforce/render/perform_source.h"

static void run_perform_source_tests() {
    auto ps = std::make_shared<PerformSource>();
    ps->set_note(220.0f, 0.9f, 48000);
    PerformOut f(ps, PerformOut::Field::Frequency);
    PerformOut v(ps, PerformOut::Field::Velocity);
    f.next(); v.next();
    CHECK_NEAR(f.current(), 220.0f, 1e-6f);
    CHECK_NEAR(v.current(), 0.9f, 1e-6f);
    ps->set_note(440.0f, 0.5f, 24000);      // re-strike: adapters follow
    f.next(); v.next();
    CHECK_NEAR(f.current(), 440.0f, 1e-6f);
    CHECK_NEAR(v.current(), 0.5f, 1e-6f);
    CHECK(ps->note().durSamples == 24000);
}
```

- [ ] **Step 2: Build, verify FAIL** (missing header).

- [ ] **Step 3: Implement `perform_source.h`**

```cpp
#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>

namespace mforce {

// Per-voice performance backing store (docs/perform_source_design.md §2.1).
// NOT a ValueSource — its outputs (PerformOut adapters) are. One write per
// note at Realization/Setup; everything downstream pulls. Heap-owned via
// shared_ptr so adapters stay valid when voicePool vectors reallocate.
struct NoteState {
  float frequency{440.0f};   // Hz, base (bend rides the legacy graft in P1)
  float velocity{0.8f};      // 0..1
  int   durSamples{0};       // actual (Piece) or nominal (Live)
};

class PerformSource {
public:
  void set_note(float freqHz, float velocity, int durSamples) {
    note_.frequency  = freqHz;
    note_.velocity   = velocity;
    note_.durSamples = durSamples;
  }
  const NoteState& note() const { return note_; }
private:
  NoteState note_;
};

// Leaf adapter: pulls nothing, reports one NoteState field. Constant
// between set_note calls (baked semantics, P1); P3 liveness changes what
// these return, not who calls them.
struct PerformOut final : ValueSource {
  enum class Field { Frequency, Velocity };
  PerformOut(std::shared_ptr<PerformSource> ps, Field f)
      : ps_(std::move(ps)), field_(f) {}

  void prepare(const RenderContext&, int) override {}
  float next() override {
    cur_ = field_ == Field::Frequency ? ps_->note().frequency
                                      : ps_->note().velocity;
    return cur_;
  }
  float current() const override { return cur_; }
  const char* type_name() const override { return "PerformOut"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

private:
  std::shared_ptr<PerformSource> ps_;
  Field field_;
  float cur_{0.0f};
};

} // namespace mforce
```
(If ValueSource::prepare is not pure-virtual-with-that-signature, mirror
an existing no-input source like ConstantSource.)

- [ ] **Step 4: Build + run engine_tests** — `ALL PASS`.

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/render/perform_source.h tools/engine_tests/main.cpp
git commit -m "feat(engine): PerformSource + NoteState + PerformOut leaf adapters"
```

---

### Task 6: Loader conversion + prepare/play rework (THE core task)

**Files:**
- Modify: `engine/include/mforce/render/instrument.h` (VoiceGraph fields,
  prepare_voice_at / prepare_voice / play_note rework)
- Modify: `engine/src/patch_loader.cpp` (build_bindings replaces
  resolve_param_map at BOTH call sites — lines ~883 and ~1075)

**Interfaces:**
- Consumes: PerformSource/PerformOut (Task 5), CurveNode (Task 2).
- Produces on VoiceGraph:
```cpp
struct PushBinding {
  std::shared_ptr<ValueSource>     consumer;
  std::string                      paramName;
  std::string                      targetNodeId;  // Multiplex clone fan
  std::shared_ptr<ValueSource>     chain;         // evaluated at set-note
  std::shared_ptr<ConstantSource>  cs;            // non-config delivery
  bool                             isConfig{false};
};
// VoiceGraph gains:
std::shared_ptr<PerformSource> performSource;
std::shared_ptr<ValueSource>   freqOut, velOut;
std::vector<PushBinding>       pushBindings;
std::vector<std::pair<std::shared_ptr<ValueSource>, std::string>> directFreq;
```
- **Conversion decision matrix** (build_bindings, per "frequency" entry):
  1. `isConfig` target → PushBinding (isConfig=true, chain = curve/vcurve
     chain rooted at freqOut/velOut).
  2. voice has `topMultiplex` → EVERY entry becomes a PushBinding
     (bit-safe: Multiplex keeps push semantics wholesale).
  3. bare target, no curve, no vcurve → `directFreq` entry; pin wired to
     `freqOut` at load.
  4. otherwise → pin wired ONCE at load to the chain
     (`consumer->set_param(paramName, chain)`).
- **Chain construction** (shared helper):
  - start: `chain = vg.freqOut`
  - curve present: CurveNode{knots=curve, interp = loglog ? LogLog : LogX,
    source=vg.freqOut}; chain = that node.
  - vcurve present: CurveNode{knots=vcurve, interp=Linear, source=vg.velOut},
    then multiply with chain via CombinedSource — construct the
    CombinedSource EXACTLY as build_graph's `type == "CombinedSource"` case
    does for a multiply (find that case in patch_loader.cpp, reuse its
    construction/op-setting calls verbatim).
- paramMap names other than `"frequency"`: keep parsing, emit
  `std::fprintf(stderr, "[loader] paramMap name '%s' ignored (post-PerformSource)\n", ...)`
  and skip — the old engine only ever consumed "frequency"
  (`vg.params.find("frequency")`), so this is behavior-preserving.

- [ ] **Step 1: instrument.h — add PushBinding + VoiceGraph fields**
  (declarations above; place PushBinding beside the current ParamSlot,
  which survives until Task 7).

- [ ] **Step 2: patch_loader.cpp — write `build_bindings`**

Signature and skeleton (place directly below resolve_param_map; port the
JSON-shape parsing — target/dot-split, curve/vcurve arrays with >=2
breakpoint validation, `interp == "loglog"` — from resolve_param_map
verbatim):
```cpp
static void build_bindings(const json& paramMapJson, const GraphResult& g,
                           PitchedInstrument::VoiceGraph& vg)
{
    auto make_chain = [&](const std::vector<std::pair<float,float>>& curve,
                          const std::vector<std::pair<float,float>>& vcurve,
                          bool loglog) -> std::shared_ptr<ValueSource> {
        std::shared_ptr<ValueSource> chain = vg.freqOut;
        if (!curve.empty()) {
            auto cn = std::make_shared<CurveNode>();
            cn->knots = curve;
            cn->interp = loglog ? CurveNode::CurveInterp::LogLog
                                : CurveNode::CurveInterp::LogX;
            cn->set_param("source", vg.freqOut);
            chain = cn;
        }
        if (!vcurve.empty()) {
            auto vn = std::make_shared<CurveNode>();
            vn->knots = vcurve;
            vn->interp = CurveNode::CurveInterp::Linear;
            vn->set_param("source", vg.velOut);
            // multiply: construct as build_graph's CombinedSource multiply
            // case does (reuse its exact construction here)
            chain = make_multiply(chain, vn);   // local helper mirroring it
        }
        return chain;
    };
    // ... per-entry loop: resolve target node/param exactly as
    // resolve_one_target does (valueNodes lookup, ConstantSource check for
    // params, config_descriptors scan for configs), then apply the
    // decision matrix above, building PushBinding / directFreq / wire.
}
```
For matrix case 4 the load-time wire replaces the pin's ConstantSource:
`consumer->set_param(paramName, chain)`. For PushBindings on non-config
targets, keep the pin's original ConstantSource in `b.cs` (the same
`dynamic_pointer_cast<ConstantSource>` fetch resolve_one_target uses).

- [ ] **Step 3: Call sites** — at BOTH `vg.params = resolve_param_map(...)`
  sites (~883 in load_patch_file, ~1075 in load_instrument_patch): first
  create the per-voice performance objects, then convert:
```cpp
            vg.performSource = std::make_shared<PerformSource>();
            vg.freqOut = std::make_shared<PerformOut>(vg.performSource,
                             PerformOut::Field::Frequency);
            vg.velOut  = std::make_shared<PerformOut>(vg.performSource,
                             PerformOut::Field::Velocity);
            if (instJson.contains("paramMap"))
                build_bindings(instJson["paramMap"], g, vg);
```
(`vg.params = resolve_param_map(...)` lines are DELETED here; the ParamSlot
type itself is deleted in Task 7. `topMultiplex` is captured a few lines
earlier — build_bindings reads `vg.topMultiplex` for matrix case 2, so
keep the capture ABOVE the build_bindings call.)

- [ ] **Step 4: prepare_voice_at rework** (instrument.h:177+; play_note
  gets the IDENTICAL rework of its ParamSlot loop):

```cpp
  StreamingVoice prepare_voice_at(int slot, float noteNumber, float velocity,
                                  float duration,
                                  const PitchCurve* curve = nullptr) {
    auto& vg = voicePool[slot];
    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    if (vg.performSource)
      vg.performSource->set_note(freq, velocity, durSamples);

    // Push bindings (configs + Multiplex fans): evaluate the chain once —
    // inputs are constants this note — and deliver.
    for (auto& b : vg.pushBindings) {
      b.chain->next();
      float v = b.chain->current();
      if (b.isConfig) {
        b.consumer->set_config(b.paramName, v);
      } else {
        b.cs->set(v);
        b.consumer->set_param(b.paramName, b.cs);
        if (vg.topMultiplex && !b.targetNodeId.empty())
          vg.topMultiplex->set_clone_param(b.targetNodeId, b.paramName, v);
      }
    }

    // Bend graft (P1: legacy PitchBendSource path, retires in P3): swap
    // bare-frequency pins between freqOut and a compiled bend.
    if (curve) {
      auto env = compile_pitch_curve(*curve, sampleRate);
      auto pbs = std::make_shared<PitchBendSource>(freq, std::move(env));
      for (auto& [consumer, name] : vg.directFreq)
        consumer->set_param(name, pbs);
    } else {
      for (auto& [consumer, name] : vg.directFreq)
        consumer->set_param(name, vg.freqOut);
    }

    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = velocity * (1.0f + boost) * volume;

    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);
    return { vg.source, durSamples, gain };
  }
```
Preserved invariants (verify against the old body before deleting it):
push-deliveries happen BEFORE `vg.source->prepare` (configs rebuild state
in prepare); the frequency-driven-config branch (`slot.isConfig` today) is
matrix case 1; the `curve && slot.curve.empty()` graft targets are exactly
`directFreq`; hiBoost/gain formula unchanged.

- [ ] **Step 5: Build ALL targets** (engine change → cli + ui + tests):
  no errors. UI must not be running.

- [ ] **Step 6: Run engine_tests** — `ALL PASS`.

- [ ] **Step 7: THE null gate**

Run: `python tools/null_gate_perform_source.py`
Expected: `N/N identical`. Any DIFF: diagnose against the invariants list
(most likely suspects: interp-mode mix-up LogX-vs-Linear, chain multiply
order `curve*vcurve` vs `vcurve*curve` — float multiplication IS
commutative bit-wise for finite values so order is safe, but
CombinedSource construction differences are not; and the Multiplex matrix
case). Fix and re-run to green BEFORE committing.

- [ ] **Step 8: Multiplex line item** — identify the Multiplex-bearing
  patches (`Select-String -Path patches\library\*,patches\baselines\* -Pattern "Multiplex"` recursively),
  confirm each appears in the gate output as identical; list them in the
  commit message.

- [ ] **Step 9: Commit**

```bash
git add engine/include/mforce/render/instrument.h engine/src/patch_loader.cpp
git commit -m "feat(engine): paramMap load-conversion to PerformSource bindings (null-gated)"
```

---

### Task 7: ParamSlot retirement

**Files:**
- Modify: `engine/include/mforce/render/instrument.h` (delete ParamSlot,
  `VoiceGraph::params`, map/vmap)
- Modify: `engine/src/patch_loader.cpp` (delete resolve_param_map)

- [ ] **Step 1: Grep for consumers first** —
  `Grep "ParamSlot|vg.params|\.params\.find|resolve_param_map" engine/ tools/`
  Expected consumers: only the code deleted in this task. The UI
  (tools/mforce_ui/main.cpp) does NOT touch vg.params (it calls
  prepare_voice/collect_envelopes only) — verify, and if a hit exists,
  STOP and reassess rather than deleting through it.
- [ ] **Step 2: Delete** ParamSlot struct (instrument.h:82-145 region),
  the `std::unordered_map<std::string, std::vector<ParamSlot>> params;`
  member on VoiceGraph, and `resolve_param_map` (patch_loader.cpp:711-815).
  The paramMap JSON-shape comments worth keeping migrate to
  build_bindings' header comment.
- [ ] **Step 3: Build ALL targets** — clean. (mforce_keys is pre-broken —
  ignore its known ParamSlot-vector errors ONLY if identical to its
  pre-existing failure; it does not link mforce_engine's instrument path...
  actually mforce_keys fails on ParamSlot API already. Confirm its error
  list is unchanged vs. before this task; if this task ADDS errors there,
  note it in the commit message — the tool is already broken and out of
  scope.)
- [ ] **Step 4: engine_tests + null gate** — `ALL PASS`, `N/N identical`.
- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/render/instrument.h engine/src/patch_loader.cpp
git commit -m "refactor(engine): ParamSlot machinery retired — bindings are the one model"
```

---

### Task 8: Final validation + docs

- [ ] **Step 1: Full build, all targets.**
- [ ] **Step 2: engine_tests** — `ALL PASS`.
- [ ] **Step 3: Null gate** — `N/N identical` (this is the P1 exit
  criterion from the spec).
- [ ] **Step 4: Live smoke:** launch
  `build/tools/mforce_ui/Release/mforce_ui.exe patches/library/keys/Piano_bright.json`,
  confirm MIDI/QWERTY notes sound (velocity variation audible on the MIDI
  keyboard), close the app.
- [ ] **Step 5: Bend smoke:** render one bend-using score/patch (grep
  `articulation` in patches/baselines; any hit) and confirm it still
  renders — bends ride the PitchBendSource graft unchanged in P1.
- [ ] **Step 6: Update docs:** perform_source_design.md §7 — mark P1 ✓
  with date + commit hashes; docs/autonomy/dsp/BACKLOG.md — note P1 landed
  (new item or STATUS pointer, matching house style).
- [ ] **Step 7: Commit docs; report to Matt** — exactly what changed,
  gate numbers (N/N), and the P2/P3 doorstep.

---

## Self-Review Notes (kept honest)

- **Spec coverage:** §2.1 store+adapters = T5/T6; §2.2 CurveNode+policy =
  T2 (RangeSource retirement is P4, correctly absent); §2.4 = T3/T4;
  §5 conversion+null gate = T0/T6; ParamSlot retirement = T7; Multiplex
  line item = T6 S8. InstrumentState intentionally deferred to P3 (YAGNI,
  noted in T5). UI work correctly absent (P2).
- **Known unknowns an executor must resolve in-file (flagged in-task):**
  Envelope's stage-storage API and exact cur_-write location (T3/T4);
  ValueSource base signatures (T2/T5); CombinedSource multiply
  construction (T6) — each has a named mirror to copy from.
- **Type consistency:** PerformOut::Field, CurveNode::CurveInterp,
  PushBinding fields — spelled identically in T5/T6 interface blocks.

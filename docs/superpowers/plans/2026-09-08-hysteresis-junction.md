# Hysteresis Junction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a stick/slip hysteresis mode to the Shaper — the bowed-string
junction — as specced.

**Architecture:** One bool of discrete state on ShaperSource: while STUCK
evaluate curve A (`values`), on |s|>breakaway switch to SLIPPING and curve B
(`values2`), recapture on zero-cross or |s|<capture. Reuses the morph
work's A/B curve storage, editor, and loader plumbing; two new ValueSource
params (breakaway, capture). hysteresis=false is byte-identical to today.

**Tech Stack:** header-only C++ engine (engine/include/mforce), nlohmann
JSON loader (engine/src/patch_loader.cpp), ImGui editor
(tools/mforce_ui/main.cpp), python render harnesses in tools/, mforce_cli
for all rendering.

**Spec:** docs/superpowers/specs/2026-09-08-hysteresis-junction-design.md

**Execution delta from spec (flagged for Matt):** spec §3 said hysteresis
needs no values2/values length match. The editor's point-correspondence
gestures assume matched counts, so the loader's existing length rule is
KEPT (matched counts are no musical restriction — add points). Everything
else per spec.

## Global Constraints

- No heap allocation in the render loop (next()/prepare() at note-on run
  in the audio callback) — state is a bool + a float; params are
  shared_ptr set at load.
- hysteresis absent/false must be byte-identical: null gate over
  patches/baselines + patches/library must match before/after.
- Build: `cmake --build C:/@dev/repos/mforce/build --config Release
  --target mforce_cli` (and `--target mforce_ui` for Task 4 only).
- CLI: `build/tools/mforce_cli/Release/mforce_cli.exe <patch.json> <out.wav>`
- Loops are probed/measured ONLY at the shipped root/output (backlog 65).
- New renders/patches go under a family folder, never a tree root.
  Audition queues: renders/dsp/audition/<family>/ + patches/audition/<family>/.
- Commit messages end with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- Never combine cd with git — use `git -C C:/@dev/repos/mforce`.

---

### Task 1: Before-manifest, then the engine state machine

**Files:**
- Create: `tools/test_hysteresis_openloop.py`
- Modify: `engine/include/mforce/source/shaper_source.h`

**Interfaces:**
- Produces: Shaper JSON contract used by every later task:
  `"hysteresis": true` (bool setting), `"breakaway"` / `"capture"`
  (ValueSource params, scalars or `{"ref": id}`), `values` = stick curve,
  `values2` = slip curve (same length as values). Engine behavior:
  stuck→slip when |drive·x| > breakaway; slip→stuck on sign change of
  drive·x or |drive·x| < capture; note start = stuck; morph ignored
  while hysteresis on.

- [ ] **Step 1: Render the null-gate BEFORE manifest with the unmodified binary**

Run (repo root is the cwd):
```bash
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli && python tools/null_test_manifest.py C:/@dev/repos/mforce/renders/scratch/hyst_null_before.txt
```
Expected: `195 patches, 193 rendered, 2 FAIL` (FormantSequence1 and
NATest1 are known pre-existing FAILs; a FAIL matching a FAIL is a pass).
This must run BEFORE any engine edit.

- [ ] **Step 2: Write the failing open-loop harness**

Create `tools/test_hysteresis_openloop.py`. The test makes the state
machine directly observable: curve A is the constant line y=+1, curve B
the constant line y=−1, so the output IS the state. Input: an Envelope
triangle ramp −1→+1→−1 over 2 s feeding the Shaper. Expected output with
breakaway 0.6, capture 0: starts stuck, but |−1|>0.6 slips on sample one
(−1); zero-cross going up recaptures (+1); +0.6 slips (−1); ramp peak,
descends, zero-cross recaptures (+1); −0.6 slips (−1). With capture 0.3
the recaptures happen earlier, at |s|=0.3. Transitions are asserted by
SIGN (velocity/volume scaling can't break sign).

```python
"""Open-loop hysteresis state-machine test (spec 6.2, plan Task 1).
Curve A = +1 line, curve B = -1 line -> output sign IS the state.
Input: Envelope triangle -1 -> +1 -> -1 over SECONDS, linear, exact.
Asserts transition times for (breakaway, capture) cases against the
analytic ramp. Exit 0 = pass."""
import json
import os
import struct
import subprocess
import sys
import wave

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
OUT = os.path.join(ROOT, "renders", "scratch", "hyst_openloop")
SECONDS = 2.0
SR = 48000
TOL = 0.01 * SR   # 10 ms slack on each transition


def patch(breakaway, capture):
    return {
        "sampleRate": SR,
        "seconds": SECONDS,
        "instrument": {"polyphony": 1, "volume": 1.0},
        "score": [{"note": 60, "time": 0.0, "duration": SECONDS,
                   "velocity": 1.0}],
        "graph": {
            "output": "Shaper",
            "nodes": [
                {"id": "Ramp", "type": "Envelope", "params": {
                    "minValue": -1.0, "maxValue": 1.0,
                    "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
                    "timeMode": "fraction", "timeScale": 1.0,
                    "stages": [
                        {"type": "Linear", "startVal": 0.0, "endVal": 1.0,
                         "percent": 0.5, "power": 0.0,
                         "minSec": 0.0, "maxSec": 0.0},
                        {"type": "Linear", "startVal": 1.0, "endVal": 0.0,
                         "percent": 0.5, "power": 0.0,
                         "minSec": 0.0, "maxSec": 0.0}]}},
                {"id": "Shaper", "type": "Shaper", "params": {
                    "source": {"ref": "Ramp"},
                    "drive": 1.0, "smoothness": 0.0, "morph": 0.0,
                    "hysteresis": True,
                    "breakaway": breakaway, "capture": capture,
                    "values":  [-2.0, 1.0, 2.0, 1.0],
                    "values2": [-2.0, -1.0, 2.0, -1.0]}},
            ],
        },
    }


def sign_trace(path):
    w = wave.open(path)
    n, ch = w.getnframes(), w.getnchannels()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    mono = [sum(raw[i * ch:(i + 1) * ch]) for i in range(n)]
    return [1 if s > 0 else (-1 if s < 0 else 0) for s in mono]


def transitions(tr):
    out = []
    prev = tr[0]
    for i, s in enumerate(tr):
        if s != 0 and prev != 0 and s != prev:
            out.append((i, prev, s))
        if s != 0:
            prev = s
    return out


def ramp_time(v_from_minus1):
    """Sample index where the triangle (-1 at t=0, +1 at t=1s, -1 at 2s)
    first reaches value v on the way up."""
    return (v_from_minus1 + 1.0) / 2.0 * SR


def expect(breakaway, capture):
    """(sample, newsign) transition list for the analytic ramp."""
    up_zero = ramp_time(0.0) if capture == 0.0 else ramp_time(-capture)
    down_zero = 2.0 * SR - (ramp_time(0.0) if capture == 0.0
                            else ramp_time(-capture))  # symmetric descent
    # NOTE: on descent the recapture threshold is +capture approached from
    # above; by symmetry its time is 2*SR - ramp_time(-capture).
    return [
        (up_zero, +1),                       # slip->stick near zero (up)
        (ramp_time(breakaway), -1),          # stick->slip at +breakaway
        (down_zero, +1),                     # slip->stick near zero (down)
        (2.0 * SR - ramp_time(-breakaway), -1),  # stick->slip at -breakaway
    ]


def run_case(breakaway, capture):
    os.makedirs(OUT, exist_ok=True)
    tag = f"b{int(breakaway*100)}_c{int(capture*100)}"
    pj = os.path.join(OUT, f"case_{tag}.json")
    pw = os.path.join(OUT, f"case_{tag}.wav")
    json.dump(patch(breakaway, capture), open(pj, "w"))
    r = subprocess.run([CLI, pj, pw], capture_output=True, timeout=120)
    if r.returncode != 0:
        print(f"FAIL {tag}: render error:",
              r.stderr.decode(errors="replace")[:200])
        return False
    tr = transitions(sign_trace(pw))
    exp = expect(breakaway, capture)
    ok = True
    if len(tr) != len(exp):
        print(f"FAIL {tag}: {len(tr)} transitions, expected {len(exp)}: "
              f"{[(i, b) for i, _, b in tr]}")
        return False
    for (i, _, newsign), (ei, esign) in zip(tr, exp):
        if newsign != esign or abs(i - ei) > TOL:
            print(f"FAIL {tag}: transition at {i} -> {newsign}, "
                  f"expected ~{int(ei)} -> {esign}")
            ok = False
    if ok:
        print(f"ok {tag}: {len(tr)} transitions at expected times")
    return ok


ok = run_case(0.6, 0.0) and run_case(0.6, 0.3)
sys.exit(0 if ok else 1)
```

- [ ] **Step 3: Run harness, verify it FAILS on the unmodified engine**

Run: `python tools/test_hysteresis_openloop.py`
Expected: FAIL — the engine ignores the unknown `hysteresis` key, curve A
(+1 line) is evaluated always, output never goes negative, so case
b60_c0 reports 0 transitions vs 4 expected. (A render error instead of a
transition-count failure means the harness patch JSON is malformed — fix
the harness before touching the engine.)

- [ ] **Step 4: Implement the state machine in ShaperSource**

In `engine/include/mforce/source/shaper_source.h`:

4a. Constructor gains the two param defaults — replace the existing
initializer list:
```cpp
  ShaperSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      drive_(std::make_shared<ConstantSource>(1.0f)),
      smoothness_(std::make_shared<ConstantSource>(0.5f)),
      breakaway_(std::make_shared<ConstantSource>(0.6f)),
      capture_(std::make_shared<ConstantSource>(0.0f)),
      values_{-1.0f, -1.0f, 1.0f, 1.0f} {}
```

4b. Extend `param_descriptors()` (inside the existing `descs[]`):
```cpp
      // Hysteresis thresholds, in post-drive curve-x units (spec §2).
      // breakaway ~ bow pressure; capture 0 = zero-cross-only recapture.
      {"breakaway",  0.6f, 0.0f, 2.0f, "curve-x"},
      {"capture",    0.0f, 0.0f, 2.0f, "curve-x"},
```

4c. Extend `set_param` / `get_param` with the two names:
```cpp
    if (name == "breakaway")  { breakaway_ = std::move(src); return; }
    if (name == "capture")    { capture_ = std::move(src); return; }
```
```cpp
    if (name == "breakaway")  return breakaway_;
    if (name == "capture")    return capture_;
```

4d. Add setting plumbing (ShaperSource has none today — new overrides,
placed after `get_param`):
```cpp
  std::span<const SettingDescriptor> setting_descriptors() const override {
    // Stick/slip junction mode (hysteresis spec §3): curve A = stick,
    // curve B = slip, one bit of state. Mutually exclusive with morph
    // (morph is ignored while on; loader warns).
    static constexpr SettingDescriptor descs[] = {
      {"hysteresis", SettingType::Bool, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }
  void set_setting(std::string_view name, float v) override {
    if (name == "hysteresis") hysteresis_ = (v != 0.0f);
  }
  float get_setting(std::string_view name) const override {
    return (name == "hysteresis" && hysteresis_) ? 1.0f : 0.0f;
  }
```

4e. `prepare()` gains (before `cur_ = 0.0f;`):
```cpp
    breakaway_->prepare(ctx, frames);
    capture_->prepare(ctx, frames);
    stuck_ = true;      // bow resting on the string at note start
    prevS_ = 0.0f;
```

4f. `next()` — after `smoothCur_ = smoothness_->current();` and the morph
advance, replace `cur_ = map(x);` with:
```cpp
    if (hysteresis_) {
      breakaway_->next();
      capture_->next();
      const float ba = breakaway_->current();
      const float cap = capture_->current();
      if (stuck_) {
        if (std::fabs(x) > ba) stuck_ = false;
      } else if ((prevS_ != 0.0f && (x > 0.0f) != (prevS_ > 0.0f))
                 || std::fabs(x) < cap) {
        stuck_ = true;   // zero-cross (or capture band) re-sticks
      }
      prevS_ = x;
      // Slip curve = values2; absent/short values2 leaves the mode inert
      // (loader warns at load).
      const bool useB = !stuck_ && valuesB_.size() >= 4;
      cur_ = useB
        ? Curve::eval_flat(valuesB_, segsB_, Curve::Domain::Linear,
                           smoothCur_, x)
        : Curve::eval_flat(values_, segs_, Curve::Domain::Linear,
                           smoothCur_, x);
      return cur_;
    }
    cur_ = map(x);
```
(`x` is already the post-drive value in the existing code; `<cmath>` is
already included via curve.h — if the build disagrees, add `#include
<cmath>` at the top.)

4g. Private members — extend the member block:
```cpp
  std::shared_ptr<ValueSource> breakaway_, capture_;
  bool hysteresis_{false};
  bool stuck_{true};
  float prevS_{0.0f};
```

- [ ] **Step 5: Rebuild and run the harness to verify it passes**

Run:
```bash
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli && python tools/test_hysteresis_openloop.py
```
Expected: `ok b60_c0: 4 transitions at expected times`, `ok b60_c30: ...`,
exit 0. If b60_c30's recapture transitions land outside tolerance,
re-check the descent expectation in `expect()` before touching the
engine — the harness math is the likelier bug.

- [ ] **Step 6: Commit**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/source/shaper_source.h tools/test_hysteresis_openloop.py
git -C C:/@dev/repos/mforce commit -m "engine: Shaper hysteresis mode - stick/slip state machine (spec 2026-09-08)

curve A = stick, curve B = slip; |s|>breakaway slips, zero-cross or
|s|<capture re-sticks; note start stuck. breakaway/capture are
ValueSource params. hysteresis=false path untouched. Open-loop
state-machine test: tools/test_hysteresis_openloop.py (flat +1/-1
curves make output sign = state; transition times asserted).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: Loader invariants and warnings

**Files:**
- Modify: `engine/src/patch_loader.cpp` (the ShaperSource block, ~line 431)
- Create: `tools/test_hysteresis_loader.py`

**Interfaces:**
- Consumes: Task 1's JSON contract and `get_setting("hysteresis")`.
- Produces: load-time stderr warnings `"Shaper: morph is ignored while
  hysteresis is on"` and `"Shaper: hysteresis without values2 is inert"`;
  the existing values2-length error stays as-is.

- [ ] **Step 1: Write the failing loader test**

Create `tools/test_hysteresis_loader.py`:
```python
"""Loader warnings for hysteresis Shapers (plan Task 2). Renders three
tiny patches and asserts on CLI stderr. Exit 0 = pass."""
import copy
import json
import os
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
OUT = os.path.join(ROOT, "renders", "scratch", "hyst_loader")

BASE = {
    "sampleRate": 48000, "seconds": 0.2,
    "instrument": {"polyphony": 1, "volume": 1.0},
    "score": [{"note": 60, "time": 0.0, "duration": 0.2, "velocity": 1.0}],
    "graph": {"output": "Shaper", "nodes": [
        {"id": "Shaper", "type": "Shaper", "params": {
            "drive": 1.0, "smoothness": 0.0, "hysteresis": True,
            "values":  [-2.0, 1.0, 2.0, 1.0],
            "values2": [-2.0, -1.0, 2.0, -1.0]}},
    ]},
}


def run(mutate):
    p = copy.deepcopy(BASE)
    mutate(p["graph"]["nodes"][0]["params"])
    os.makedirs(OUT, exist_ok=True)
    pj = os.path.join(OUT, "case.json")
    json.dump(p, open(pj, "w"))
    r = subprocess.run([CLI, pj, os.path.join(OUT, "case.wav")],
                       capture_output=True, timeout=60)
    return r.returncode, r.stderr.decode(errors="replace")


def check(name, cond):
    print(("ok " if cond else "FAIL ") + name)
    return cond


ok = True
rc, err = run(lambda q: q.update({"morph": 0.5}))
ok &= check("morph+hysteresis warns", rc == 0 and "ignored" in err)
rc, err = run(lambda q: q.pop("values2"))
ok &= check("missing values2 warns inert", rc == 0 and "inert" in err)
rc, err = run(lambda q: q.update(
    {"values2": [-2.0, -1.0, 0.0, 0.0, 2.0, -1.0]}))
ok &= check("length mismatch still errors", rc != 0)
sys.exit(0 if ok else 1)
```

- [ ] **Step 2: Run it, verify the two warning cases FAIL**

Run: `python tools/test_hysteresis_loader.py`
Expected: `FAIL morph+hysteresis warns`, `FAIL missing values2 warns
inert`, `ok length mismatch still errors` (that rule already exists).

- [ ] **Step 3: Extend the loader's Shaper block**

In `engine/src/patch_loader.cpp`, inside the `if
(dynamic_cast<ShaperSource*>(&src))` block, AFTER the existing segs2
type checks, add:
```cpp
        // Hysteresis invariants (hysteresis spec §3). The generic
        // settings pass above has already applied the flag.
        if (src.get_setting("hysteresis") != 0.0f) {
            if (params.contains("morph"))
                std::fprintf(stderr,
                    "[load] Shaper: morph is ignored while hysteresis "
                    "is on\n");
            if (vals2.empty())
                std::fprintf(stderr,
                    "[load] Shaper: hysteresis without values2 is "
                    "inert (no slip curve)\n");
        }
```
(`vals2` is already in scope from the existing checks.)

- [ ] **Step 4: Rebuild, run the loader test to verify it passes**

Run:
```bash
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli && python tools/test_hysteresis_loader.py
```
Expected: all three `ok`, exit 0. Also re-run
`python tools/test_hysteresis_openloop.py` — still exit 0 (the morph
warning case must not have broken evaluation).

- [ ] **Step 5: Commit**

```bash
git -C C:/@dev/repos/mforce add engine/src/patch_loader.cpp tools/test_hysteresis_loader.py
git -C C:/@dev/repos/mforce commit -m "loader: hysteresis Shaper warnings - morph ignored, missing slip curve inert

values2 length rule kept (editor point-correspondence relies on it;
spec relaxation dropped - flagged in the plan header for Matt).

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 3: Null gate

**Files:** none modified — verification only.

**Interfaces:**
- Consumes: `renders/scratch/hyst_null_before.txt` from Task 1 Step 1.

- [ ] **Step 1: Render the AFTER manifest on the finished engine+loader**

Run:
```bash
python tools/null_test_manifest.py C:/@dev/repos/mforce/renders/scratch/hyst_null_after.txt
```
Expected: `195 patches, 193 rendered, 2 FAIL`.

- [ ] **Step 2: Diff**

Run:
```bash
diff C:/@dev/repos/mforce/renders/scratch/hyst_null_before.txt C:/@dev/repos/mforce/renders/scratch/hyst_null_after.txt && echo NULL-GATE-CLEAN
```
Expected: `NULL-GATE-CLEAN` (no output from diff). Any hash difference =
STOP: the hysteresis=false path changed; bisect Task 1's edits (the
prime suspect is 4f — confirm `map(x)` is still reached verbatim when
`hysteresis_` is false) before proceeding.

---

### Task 4: Editor — Stick/Slip labels

**Files:**
- Modify: `tools/mforce_ui/main.cpp:5881-5886` (the A/B radio buttons)

**Interfaces:**
- Consumes: `node.dspSource->get_setting("hysteresis")` (Task 1).
- Produces: UI-only label change; no data or gesture changes. The
  hysteresis checkbox and breakaway/capture pins appear on the Shaper
  face automatically from the descriptors (registry-driven UI) — no code
  for those.

- [ ] **Step 1: Relabel the radios when hysteresis is on**

At `tools/mforce_ui/main.cpp` ~line 5881, the block currently reads:
```cpp
            } else {
                int ac = s_shapeEd.activeCurve;
                if (ImGui::RadioButton("A", ac == 0)) s_shapeEd.activeCurve = 0;
                ImGui::SameLine();
                if (ImGui::RadioButton("B", ac == 1)) s_shapeEd.activeCurve = 1;
```
Replace the two RadioButton lines with:
```cpp
                // Hysteresis mode reuses the A/B workflow; the curves ARE
                // the stick and slip curves, so say so (hysteresis spec §3).
                const bool hyst = node.dspSource
                    && node.dspSource->get_setting("hysteresis") != 0.0f;
                if (ImGui::RadioButton(hyst ? "Stick" : "A", ac == 0))
                    s_shapeEd.activeCurve = 0;
                ImGui::SameLine();
                if (ImGui::RadioButton(hyst ? "Slip" : "B", ac == 1))
                    s_shapeEd.activeCurve = 1;
```

- [ ] **Step 2: Rebuild the UI**

Run: `cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_ui`
Expected: clean build. (If the link fails with a file-lock error the UI
is running — rename the old exe out of the way, link, and note that Matt
must restart the UI: rename-then-link is the standing convention.)

- [ ] **Step 3: Commit**

```bash
git -C C:/@dev/repos/mforce add tools/mforce_ui/main.cpp
git -C C:/@dev/repos/mforce commit -m "ui: Shaper A/B radios read Stick/Slip in hysteresis mode

Checkbox and breakaway/capture pins come free from descriptors.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: Bow round — baseline patch, probe sweep, audition queue

**Files:**
- Create: `tools/gen_hysteresis_bow1.py`
- Create (by running it): `patches/baselines/feedback/loop_hysteresis_bow.json`,
  `patches/sweep/hysteresis_bow1/*`, `patches/audition/hysteresis_bow1/*`,
  `renders/dsp/{sweep,audition}/hysteresis_bow1/*`
- Create: `renders/dsp/audition/hysteresis_bow1/README.md` (copied to the
  patch queue by the tool)

**Interfaces:**
- Consumes: Task 1's JSON contract; `patches/library/winds/oboe1.json` as
  the loop skeleton; the probe/bisect/normalize machinery pattern from
  `tools/gen_feedback_valve2.py` (copy the functions — do not import).
- Produces: Matt's listening queue. Cells `hyb_b{BA}_c{CAP}_{s|h}.wav`
  plus `x_memoryless.wav` (same slip curve, hysteresis off — the spec §6.4
  kill-test control) and `x_control.wav` (stock oboe1).

- [ ] **Step 1: Write the generator**

Create `tools/gen_hysteresis_bow1.py`, modeled line-for-line on
`tools/gen_feedback_valve2.py` (same ROOT/CLI/paths pattern, same
`window_rms`, `oscillates`-at-shipped-root, `measure_critical`,
`normalize_copy`, manifest, and queue-copy structure), with these
differences:

Output dirs use family `hysteresis_bow1`. Axes:
```python
BREAKAWAY = [0.4, 0.6, 0.9]
CAPTURE = [0.0, 0.3]
HEADROOM = {"s": None, "h": 2.0}
```

`make_patch(base, ba, cap)` (ba None = stock control) transforms oboe1:
```python
STICK = [-1.0, -1.0, -0.6, -1.0, -0.15, -0.3, 0.0, 0.0,
         0.15, 0.3, 0.6, 1.0, 1.0, 1.0]          # slope 2 through origin
SLIP = [-1.0, -0.25, -0.6, -0.35, -0.15, -1.0, 0.0, 0.0,
        0.15, 1.0, 0.6, 0.35, 1.0, 0.25]         # friction falling flank
# 7 points each — matched count (loader rule), editor-safe.

def make_patch(base, ba, cap):
    patch = copy.deepcopy(base)
    nodes_by_id(patch)["Pitch_vibrato"]["params"]["depth"] = 0.003
    if ba is None:
        return patch
    j = nodes_by_id(patch)["Junction"]["params"]
    j["values"] = list(STICK)
    j["values2"] = list(SLIP)
    j["hysteresis"] = True
    j["breakaway"] = ba
    j["capture"] = cap
    j["morph"] = 0.0
    return patch
```

`make_memoryless(base)` = the §6.4 control: same transform but
`hysteresis: False` and `values = list(SLIP)`, `values2 = []` — the best
single-curve rendering of the same friction shape.

Criticals: bisect per (ba, cap) at the SHIPPED topology and root exactly
as valve2 does (breath seeded 0.001, drive min=max, output untouched).
Drive transplant: same min/max-as-ratios-to-stock-critical scheme;
headroom `h` = max 2.0×crit. Cells = 3×2×2 = 12 + the two controls.

After rendering, the tool also writes
`patches/baselines/feedback/loop_hysteresis_bow.json`: the (ba=0.6,
cap=0.0) patch at stock-ratio drive — the tracked regression baseline.

Add one measurement the valve rounds lacked (spec §6.5): for each ok
cell, on the raw C4 note window (t 2.6–3.8 s), count waveform corners
per period — zero crossings of the first difference, divided by periods
in the window — and store it in the manifest as `corners_per_period`
(report only, no assertion; Helmholtz motion trends LOW — a sawtooth has
2 — while reed equilibria run higher).

- [ ] **Step 2: Run the round**

Run: `python tools/gen_hysteresis_bow1.py`
Expected: stock critical near 0.526 (matches valve2's measurement of the
same skeleton); 12 cells + 2 controls rendered, 0 failures. Cells whose
critical bisection fails record a skip in the manifest — 12/12 is not
required (a breakaway the loop can't reach at drive ≤ 10 is data, not a
tool failure), but BOTH controls must render.

- [ ] **Step 3: Verify pitch and fire per note (round-3 lesson — before
  any queue announcement)**

Run a per-note check over `renders/dsp/sweep/hysteresis_bow1/*.wav`
using the same measurement as the valve rounds (RMS > 0.02 per 5 note
windows; FFT peak folded to the nearest harmonic vs C3..C7, flag beyond
±30c on C3–C5). The asymmetric slip curve deposits DC (spec §4), so run
the pitch check on the existing renders — dcblock handles DC, but if
C3–C5 pitch drifts beyond ±30c or cells are silent, STOP and record the
numbers in the manifest and README rather than shipping the queue;
that outcome is finding, not failure.

- [ ] **Step 4: Write the queue README**

`renders/dsp/audition/hysteresis_bow1/README.md` (the tool copies it to
the patch queue): state the axes (breakaway = bow pressure, capture 0 =
zero-cross-only vs 0.3 = early recapture, s/h headroom), the two
controls and why x_memoryless is THE comparison that decides the
feature (spec §6.4: hysteresis-on must clearly beat the same curve
memoryless or the feature failed), the measured tuning table from Step
3, and the corners_per_period numbers. Mechanism facts only, no
predictions about how cells will sound.

- [ ] **Step 5: Commit**

```bash
git -C C:/@dev/repos/mforce add tools/gen_hysteresis_bow1.py patches/baselines/feedback/loop_hysteresis_bow.json
git -C C:/@dev/repos/mforce commit -m "bow round 1: hysteresis junction on the oboe1 loop skeleton

breakaway x capture x headroom grid + the memoryless kill-test control
(same slip curve, hysteresis off) per spec 6.4. Queue in
{renders,patches}/audition/hysteresis_bow1/.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

## Self-review notes

- Spec coverage: §2 model → Task 1; §3 Shaper mode/params/exclusivity →
  Tasks 1+2 (length-rule deviation flagged in header); §4 aliasing/DC →
  no code by design, DC observed via Task 5 Step 3; §5 exclusions → none
  implemented (correct); §6.1 → Task 3, §6.2 → Task 1, §6.3+§6.5 → Task
  5, §6.4 → Task 5's x_memoryless. §7 open questions resolved by Matt's
  spec approval of the stated defaults (magnitude breakaway;
  breakaway/capture names; no extra face badge — checkbox only).
- Types: `hysteresis_`/`stuck_`/`prevS_`/`breakaway_`/`capture_` names
  consistent across Tasks 1, 2 (via get_setting), 4.
- The harness's `expect()` descent-side times rely on the triangle's
  symmetry; tolerance is 10 ms against a 2 s ramp.

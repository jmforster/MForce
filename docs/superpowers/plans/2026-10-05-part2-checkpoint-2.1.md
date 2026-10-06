# Part 2, checkpoint 2.1: pins declared once, Component and the two bases — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every pin on every node is declared once as a self-registering member; `Component` becomes the root with `ValueSource`, `Generator` and `Processor` under it; and the two measurements that checkpoints 2.3 and 2.4 need are taken with today's engine. Renders stay byte-identical except where a constructor default and a descriptor default disagree today.

**Architecture:** Five member types (`Param`, `Input`, `Slot<T>`, `Setting<T>`, `Array`) register themselves with their owning `Component` in its constructor; the base class serves the descriptor spans and `set_param` / `get_param` / `set_setting` / `set_array` from that registry and forwards `prepare` to every pin, so a node keeps only its declarations and its `compute()`. `next()` stays as today (advance every pin, then compute); the memo is checkpoint 2.3. The migration is one family per task, each gated by the null gate.

**Tech Stack:** C++20 (MSVC, CMake 3.20+), the existing `engine_tests` harness, `python tools/gates.py`, `python tools/null_gate_perform_source.py`.

**Spec:** `docs/architecture/part2-valuesource-contract.md` sections 3 and 7 (checkpoint 2.1), `docs/architecture/part3-nodes.md` section 2 (`Component`), `docs/architecture/target-architecture.md` section 2 (principle 6: structure and behaviour never change in the same checkpoint).

## Global Constraints

- No heap allocation, lock or throw in any `next()` / `compute()`; pin registration happens in constructors only (CLAUDE.md non-negotiables; the registry vector is built when the patch is built, never on the audio thread).
- Explicit registries, no reflection (CLAUDE.md).
- Byte-identical renders: after every task, `python tools/null_gate_perform_source.py --jobs 3` reports `80/80 manifest entries identical` (or the listed exceptions from step 2 of the recipe, with their new hashes refrozen in the same commit and named in the commit message).
- No new file over 600 lines, no new function over 80 (`tools/structure/limits.json`); `python tools/structure/check.py --delta HEAD~1` is pasted into the checkpoint's architecture report.
- No dated changelog, backlog id or REVIEW number in code comments.
- Commit after every task; run `python tools/gates.py --fast` before every commit (its roundtrip smoke fails on `perform/wiring_setting.json` today for the pre-existing backlog-79 reason; every other gate must pass).
- Declaration order of pins = the old advance order (recipe step 4), until checkpoint 2.3.

---

## File structure

| File | Responsibility |
|---|---|
| `engine/include/mforce/core/pins.h` (new) | `PinKind`, `PinBase`, `Param`, `Input`, `Slot<T>`, `Setting<T>`, `Array`: a pin knows its name, kind, default/range, and how to read its value; it registers with its owner on construction. |
| `engine/include/mforce/core/component.h` (new) | `Component`: the root. Owns the pin registry (a vector of `PinBase*`), serves the four descriptor spans from it, implements `set_param`/`get_param`/`add_param`/`clear_param`/`set_setting`/`get_setting`/`set_array`/`get_array` by name, `type_name()`, `category()`. |
| `engine/include/mforce/core/dsp_value_source.h` (modify) | `ValueSource : Component` keeps `prepare`/`next`/`current`/`reseed`/`phase_delay_at`/`tracks_frequency_live`; `next()` becomes non-virtual: advance every registered pin, then `cur_ = compute()`. `compute()` is the new pure virtual. `ConstantSource`, `RefSource` adapt. New `Generator : ValueSource` (adds `range()`) and `Processor : ValueSource` (adds `Input input`). |
| `engine/include/mforce/core/dsp_wave_source.h` (modify) | `WaveSource : Generator`, three `Param`s, `compute()` with the phase accumulator calling `wave()` (the renamed `compute_wave_value()`). |
| every node header under `core/`, `source/`, `filter/`, `render/perform_source.h` (modify) | the recipe, one family per task |
| `engine/src/patch_loader.cpp` (modify, Task 0 only) | temporary sharing instrumentation behind an environment variable, removed in checkpoint 2.3 |
| `tools/engine_tests/main.cpp` (modify) | tests for pins and the registry |
| `docs/architecture/measurements/2026-10-xx-sharing-lag.md` (new) | the two measured lists |

---

### Task 0: Measure the wiring-order lag and the same-tick taps

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h:155-176` (RefSource)
- Modify: `engine/include/mforce/render/instrument.h:433-470` (the per-sample render loop in `render_chunk`), `engine/src/patch_loader.cpp` (mixer render loop for plain sounds)
- Create: `tools/measure_sharing.py`, `docs/architecture/measurements/2026-10-05-sharing-lag.md`

**Interfaces:**
- Produces: two lists, by patch path: (a) shared sources with a wrapped consumer that reads the previous sample ("lag reads"), (b) taps that read the current sample ("same-tick taps"). Both are the expected-change lists for checkpoints 2.3 and 2.4.

- [ ] **Step 1: Add a per-voice sample counter the instrumentation can read**

In `dsp_value_source.h`, inside `namespace mforce` before `struct ValueSource`:

```cpp
// Temporary instrumentation for the per-tick-memo migration (removed in
// checkpoint 2.3). The render driver bumps it once per sample; RefSource
// compares its source's last-advanced sample with it.
struct SharingTrace {
  static inline thread_local long long sample = 0;
  static inline thread_local long long lagReads = 0;       // wrapped consumer read a previous-sample value
  static inline thread_local long long sameTickTaps = 0;   // guarded (tap) read saw this sample's value
  static inline bool enabled = false;                       // set from MFORCE_TRACE_SHARING=1
};
```

In `ValueSource` add `long long lastAdvancedSample_{-1};` and one non-virtual
hook that a consumer calls in place of `next()` on a pin it advances:

```cpp
  // instrumentation hook: records the sample at which this node last advanced
  float advance() { if (SharingTrace::enabled) lastAdvancedSample_ = SharingTrace::sample; return next(); }
```

`RefSource::read()` then knows whether its source has already advanced in
the current sample:

```cpp
  float read() const {
    float v = source ? source->current() : 0.0f;
    if (SharingTrace::enabled && source) {
      const bool advancedThisSample = source->lastAdvancedSample_ == SharingTrace::sample;
      if (guard && advancedThisSample) ++SharingTrace::sameTickTaps;
      if (!guard && !advancedThisSample) ++SharingTrace::lagReads;
    }
    if (!guard) return v;
    if (!std::isfinite(v)) return 0.0f;
    return v < -8.0f ? -8.0f : (v > 8.0f ? 8.0f : v);
  }
```

Then in `WaveSource::next()` (dsp_wave_source.h:33-35) and in `Envelope::next()` for its two pins replace `x_->next()` with `x_->advance()`. Those two classes cover every shared source in the library patches (a shared node is wired into an oscillator or envelope pin in all 80 manifest patches; confirm with `python tools/measure_sharing.py --consumers`, Step 3, before trusting the lists). If the confirmation shows other consumer types, add `advance()` to them the same way.

- [ ] **Step 2: Bump the sample counter in the two render loops**

In `instrument.h` `render_chunk` (the per-sample loop that calls `vg.source->next()`), before the pull: `++SharingTrace::sample;`. In `patch_loader.cpp` the plain-sound mixer path renders through `Channel`/`StereoMixer::render`; in `engine/src/mixer.cpp` inside `StereoMixer::render`'s per-frame loop add the same line. Counts accumulate for the whole process (one patch per process in the tool below) and are printed at exit. Add to `mforce_cli/main.cpp` at the end of `main()`:

```cpp
    if (mforce::SharingTrace::enabled)
        std::fprintf(stderr, "[sharing] lagReads=%lld sameTickTaps=%lld\n",
                     mforce::SharingTrace::lagReads, mforce::SharingTrace::sameTickTaps);
```

and at the start of `main()`: `mforce::SharingTrace::enabled = std::getenv("MFORCE_TRACE_SHARING") != nullptr;`.

- [ ] **Step 3: Write the measurement tool**

`tools/measure_sharing.py`:

```python
"""Render every manifest patch with MFORCE_TRACE_SHARING=1 and collect the
[sharing] line: lag reads (a wrapped consumer that read a previous-sample
value) and same-tick taps. Writes the two lists checkpoints 2.3 and 2.4
expect to change.  python tools/measure_sharing.py [--consumers]"""
import json, os, re, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
MANIFEST = ROOT / "tools/null_gate_manifest.json"

def main():
    env = dict(os.environ, MFORCE_TRACE_SHARING="1")
    lag, taps = [], []
    for key in sorted(json.loads(MANIFEST.read_text())):
        out = ROOT / "renders/scratch/measure_sharing.wav"
        r = subprocess.run([str(CLI), str(ROOT / key), str(out)], capture_output=True, text=True, env=env, cwd=ROOT)
        m = re.search(r"\[sharing\] lagReads=(\d+) sameTickTaps=(\d+)", r.stderr)
        if not m:
            print("NO TRACE", key); continue
        if int(m.group(1)): lag.append((key, int(m.group(1))))
        if int(m.group(2)): taps.append((key, int(m.group(2))))
    doc = ["# Sharing measurements (checkpoints 2.3 and 2.4)", "",
           f"{len(lag)} patches with wiring-order lag reads (expected to change under the memo):", ""]
    doc += [f"- `{k}`: {n} lag reads" for k, n in lag] or ["- none"]
    doc += ["", f"{len(taps)} patches with same-tick tap reads (expected to change under reading B):", ""]
    doc += [f"- `{k}`: {n} same-tick tap reads" for k, n in taps] or ["- none"]
    p = ROOT / "docs/architecture/measurements/2026-10-05-sharing-lag.md"
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text("\n".join(doc) + "\n", encoding="utf-8")
    print("\n".join(doc))

if __name__ == "__main__":
    sys.exit(main())
```

The `--consumers` confirmation from Step 1: `git grep -n "_->next()" -- engine/include | grep -v "advance()"` lists every remaining untraced pin advance; any node in that list that can hold a shared source in a library patch gets `advance()` too.

- [ ] **Step 4: Build, run, and record**

Run: `cmake --build build --config Release --target mforce_cli` then `python tools/measure_sharing.py`.
Expected: the document lists the patches; the null gate (`python tools/null_gate_perform_source.py --jobs 3`) still reads 80/80 identical, because tracing changes no arithmetic.

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/core/dsp_value_source.h engine/include/mforce/core/dsp_wave_source.h engine/include/mforce/core/envelope.h engine/include/mforce/render/instrument.h engine/src/mixer.cpp tools/mforce_cli/main.cpp tools/measure_sharing.py docs/architecture/measurements/2026-10-05-sharing-lag.md
git commit -m "measure: wiring-order lag reads and same-tick taps per manifest patch (for checkpoints 2.3/2.4)"
```

---

### Task 1: The pin types

**Files:**
- Create: `engine/include/mforce/core/pins.h`
- Test: `tools/engine_tests/main.cpp` (new function `run_pin_tests()` registered in `main()` next to the others)

**Interfaces:**
- Produces, for Task 2:

```cpp
namespace mforce {
struct Component;                       // Task 2
enum class PinKind { Param, Input, Slot, Setting, Array };

struct PinBase {
  const char* name;
  PinKind kind;
  explicit PinBase(Component* owner, const char* name, PinKind kind);   // registers with owner
  virtual ~PinBase() = default;
  virtual void prepare(const RenderContext& ctx, int frames) {}
  virtual void advance() {}
};

struct Param final : PinBase {
  Param(Component* owner, const char* name, float def, float min, float max, const char* hint = nullptr);
  float current() const;                               // source->current(), or the constant
  void set(std::shared_ptr<ValueSource> src);          // nullptr restores the constant
  std::shared_ptr<ValueSource> get() const;
  ParamDescriptor descriptor() const;
  float constant;                                      // the value when unwired
  float min, max; const char* hint;
  std::shared_ptr<ValueSource> source;                 // null = constant
};

struct Input final : PinBase {
  enum Required { RequiredInput, Optional };
  Input(Component* owner, const char* name, Required r = RequiredInput, const char* hint = nullptr, bool multi = false);
  float current() const;                               // source->current(), or 0
  void set(std::shared_ptr<ValueSource> src);
  std::shared_ptr<ValueSource> get() const;
  InputDescriptor descriptor() const;
  std::shared_ptr<ValueSource> source;
};

template <class T> struct Setting final : PinBase {
  Setting(Component* owner, const char* name, T def, T min, T max, const char* const* labels = nullptr);
  T value; T min, max; const char* const* labels;
  SettingDescriptor descriptor() const;
  void set_from_float(float v); float as_float() const;
};

struct Array final : PinBase {
  Array(Component* owner, const char* name, const char* group, float def, float min, float max);
  std::vector<float> values; const char* group; float def, min, max;
  ArrayDescriptor descriptor() const;
};
}
```

`Slot<T>` is declared here too but used only from checkpoint 3.1:

```cpp
template <class T> struct Slot final : PinBase {
  Slot(Component* owner, const char* name, const char* hint = nullptr, bool multi = false);
  std::vector<std::shared_ptr<Component>> parts;      // the owning handles
  std::vector<T*> views;                              // the same parts as T*
  bool set(std::shared_ptr<Component> part);          // false if part is not a T
  void clear();
  InputDescriptor descriptor() const;
};
```

- [ ] **Step 1: Write the failing tests**

Append to `tools/engine_tests/main.cpp` before `main()`:

```cpp
#include "mforce/core/pins.h"

namespace {
struct PinProbe : mforce::Component {
  mforce::Param freq{this, "frequency", 440.0f, 0.01f, 20000.0f, "hz"};
  mforce::Param amp{this, "amplitude", 1.0f, 0.0f, 10.0f};
  mforce::Input in{this, "source", mforce::Input::Optional};
  mforce::Setting<int> mode{this, "mode", 2, 0, 3};
  mforce::Array table{this, "weights", nullptr, 0.5f, 0.0f, 1.0f};
  const char* type_name() const override { return "PinProbe"; }
};
}

static void run_pin_tests() {
    PinProbe p;
    // declaration order is registry order
    auto pd = p.param_descriptors();
    CHECK(pd.size() == 2);
    CHECK(std::string(pd[0].name) == "frequency");
    CHECK_NEAR(pd[0].default_value, 440.0f, 1e-6f);
    CHECK(std::string(pd[0].hint) == "hz");
    CHECK(p.input_descriptors().size() == 1);
    CHECK(p.setting_descriptors().size() == 1);
    CHECK(p.array_descriptors().size() == 1);
    // unwired param reads its constant; wired reads the source
    CHECK_NEAR(p.freq.current(), 440.0f, 1e-6f);
    p.set_param("frequency", std::make_shared<mforce::ConstantSource>(220.0f));
    p.freq.source->next();
    CHECK_NEAR(p.freq.current(), 220.0f, 1e-6f);
    CHECK(p.get_param("frequency") != nullptr);
    CHECK(p.get_param("nosuch") == nullptr);
    // settings and arrays by name
    p.set_setting("mode", 3.0f);
    CHECK(p.mode.value == 3);
    CHECK_NEAR(p.get_setting("mode"), 3.0f, 1e-6f);
    p.set_array("weights", {0.1f, 0.2f});
    CHECK(p.get_array("weights").size() == 2);
    // an optional input unwired reads 0
    CHECK_NEAR(p.in.current(), 0.0f, 1e-6f);
}
```

and call `run_pin_tests();` in `main()`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Release --target engine_tests`
Expected: compile error, `mforce/core/pins.h` not found.

- [ ] **Step 3: Write pins.h**

```cpp
#pragma once
#include <memory>
#include <string_view>
#include <vector>
#include "mforce/core/dsp_value_source.h"   // ParamDescriptor & co., ValueSource

namespace mforce {

struct Component;

enum class PinKind { Param, Input, Slot, Setting, Array };

// Every pin registers itself with its owner when constructed, so a node's
// member declarations are its pin table. Order of declaration is the
// order of registration, which is the order pins are advanced.
struct PinBase {
  const char* name;
  PinKind kind;
  PinBase(Component* owner, const char* name, PinKind kind);
  virtual ~PinBase() = default;
  virtual void prepare(const RenderContext&, int) {}
  virtual void advance() {}
};

struct Param final : PinBase {
  float constant, min, max;
  const char* hint;
  std::shared_ptr<ValueSource> source;
  Param(Component* owner, const char* n, float def, float mn, float mx, const char* h = nullptr)
    : PinBase(owner, n, PinKind::Param), constant(def), min(mn), max(mx), hint(h) {}
  float current() const { return source ? source->current() : constant; }
  void set(std::shared_ptr<ValueSource> s) { source = std::move(s); }
  std::shared_ptr<ValueSource> get() const { return source; }
  ParamDescriptor descriptor() const { return {name, constant, min, max, hint}; }
  void prepare(const RenderContext& ctx, int frames) override { if (source) source->prepare(ctx, frames); }
  void advance() override { if (source) source->next(); }
};

struct Input final : PinBase {
  enum Required { RequiredInput, Optional };
  Required required; const char* hint; bool multi;
  std::shared_ptr<ValueSource> source;
  Input(Component* owner, const char* n, Required r = RequiredInput, const char* h = nullptr, bool m = false)
    : PinBase(owner, n, PinKind::Input), required(r), hint(h), multi(m) {}
  float current() const { return source ? source->current() : 0.0f; }
  void set(std::shared_ptr<ValueSource> s) { source = std::move(s); }
  std::shared_ptr<ValueSource> get() const { return source; }
  InputDescriptor descriptor() const { return {name, multi, hint}; }
  void prepare(const RenderContext& ctx, int frames) override { if (source) source->prepare(ctx, frames); }
  void advance() override { if (source) source->next(); }
};

template <class T>
struct Setting final : PinBase {
  T value, min, max;
  const char* const* labels;
  Setting(Component* owner, const char* n, T def, T mn, T mx, const char* const* lb = nullptr)
    : PinBase(owner, n, PinKind::Setting), value(def), min(mn), max(mx), labels(lb) {}
  SettingDescriptor descriptor() const {
    constexpr SettingType t = std::is_same_v<T, bool> ? SettingType::Bool
                           : std::is_same_v<T, int> ? SettingType::Int : SettingType::Float;
    return {name, t, float(value_default_), float(min), float(max), labels};
  }
  void set_from_float(float v) { value = T(v); }
  float as_float() const { return float(value); }
private:
  T value_default_ = value;
};

struct Array final : PinBase {
  std::vector<float> values;
  const char* group; float def, min, max;
  Array(Component* owner, const char* n, const char* g, float d, float mn, float mx)
    : PinBase(owner, n, PinKind::Array), group(g), def(d), min(mn), max(mx) {}
  ArrayDescriptor descriptor() const { return {name, group, def, min, max}; }
};

template <class T>
struct Slot final : PinBase {
  std::vector<std::shared_ptr<Component>> parts;
  std::vector<T*> views;
  const char* hint; bool multi;
  Slot(Component* owner, const char* n, const char* h = nullptr, bool m = false)
    : PinBase(owner, n, PinKind::Slot), hint(h), multi(m) {}
  bool set(std::shared_ptr<Component> part);   // defined in component.h (needs Component complete)
  void clear() { parts.clear(); views.clear(); }
  InputDescriptor descriptor() const { return {name, multi, hint}; }
};

} // namespace mforce
```

`Setting::value_default_` is captured after `value` so the descriptor reports the declared default even after `set_from_float`. `PinBase`'s constructor body and `Slot::set` live in `component.h` (Task 2), because they need `Component` complete; `pins.h` declares them.

- [ ] **Step 4: Run the tests**

Not yet green: `Component` does not exist until Task 2. Proceed to Task 2 without committing; Task 2's commit covers both files.

---

### Task 2: Component, and ValueSource / Generator / Processor on top of it

**Files:**
- Create: `engine/include/mforce/core/component.h`
- Modify: `engine/include/mforce/core/dsp_value_source.h` (the whole `ValueSource` section, lines 71-131, and `ConstantSource` / `RefSource`)
- Test: `tools/engine_tests/main.cpp` (`run_pin_tests` from Task 1, plus `run_base_tests` below)

**Interfaces:**
- Consumes: the pin types of Task 1.
- Produces:

```cpp
struct Component {
  virtual ~Component() = default;
  virtual const char* type_name() const { return "Unknown"; }
  virtual SourceCategory category() const { return SourceCategory::Utility; }
  // descriptor spans served from the registry (per instance, built at registration)
  std::span<const ParamDescriptor> param_descriptors() const;
  std::span<const InputDescriptor> input_descriptors() const;
  std::span<const SettingDescriptor> setting_descriptors() const;
  std::span<const ArrayDescriptor> array_descriptors() const;
  // by-name access: unknown names are ignored in this checkpoint (checkpoint 2.5 makes them errors)
  void set_param(std::string_view name, std::shared_ptr<ValueSource> src);
  std::shared_ptr<ValueSource> get_param(std::string_view name) const;
  void add_param(std::string_view name, std::shared_ptr<ValueSource> src);
  void clear_param(std::string_view name);
  void set_setting(std::string_view name, float v);
  float get_setting(std::string_view name) const;
  void set_array(std::string_view name, std::vector<float> values);
  std::vector<float> get_array(std::string_view name) const;
  // hooks for subclasses that need to react (KSString recomputes on a setting)
  virtual void on_setting_changed(std::string_view) {}
  virtual void on_array_changed(std::string_view) {}
  const std::vector<PinBase*>& pins() const;
};

struct ValueSource : Component {
  virtual void prepare(const RenderContext& ctx, int frames);   // forwards to every pin, then on_prepare
  virtual void on_prepare(const RenderContext&, int) {}
  float next();                                                  // advance every pin, cur_ = compute(), return cur_
  virtual float compute() = 0;
  virtual float current() const { return cur_; }
  virtual bool tracks_frequency_live() const { return true; }
  virtual float phase_delay_at(float) { return 0.0f; }
  virtual void reseed() {}
protected:
  float cur_{0.0f};
};

struct Generator : ValueSource {
  enum class Range { Bipolar, Unipolar, None };
  virtual Range range() const { return Range::None; }
};

struct Processor : ValueSource {
  Input input{this, "source"};
  SourceCategory category() const override { return SourceCategory::Filter; }
};
```

`next()` is non-virtual from here on. Two existing classes override `next()` for reasons other than pin bookkeeping and keep a virtual hook: `RefSource` (reads its source on the fly) and `PerformOut` (idempotent read); both become `compute()` bodies that do not depend on `cur_`, with `current()` overridden to recompute, exactly as today.

- [ ] **Step 1: Write the failing base test**

Append to `tools/engine_tests/main.cpp`:

```cpp
namespace {
struct Doubler final : mforce::Processor {
  float compute() override { return 2.0f * input.current(); }
  const char* type_name() const override { return "Doubler"; }
};
struct Ramp final : mforce::Generator {
  float v_{0.0f};
  float compute() override { return v_ += 1.0f; }
  Range range() const override { return Range::Unipolar; }
  const char* type_name() const override { return "Ramp"; }
};
}

static void run_base_tests() {
    auto ramp = std::make_shared<Ramp>();
    Doubler d;
    d.set_param("source", ramp);              // Processor's input is a pin named "source"
    CHECK(d.input_descriptors().size() == 1);
    mforce::RenderContext ctx{48000};
    d.prepare(ctx, 100);
    // next() advances the input (ramp -> 1) then computes 2*1
    CHECK_NEAR(d.next(), 2.0f, 1e-6f);
    CHECK_NEAR(d.next(), 4.0f, 1e-6f);
    CHECK_NEAR(d.current(), 4.0f, 1e-6f);
    CHECK(ramp->range() == mforce::Generator::Range::Unipolar);
}
```

Register `run_base_tests();` in `main()`.

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build build --config Release --target engine_tests`
Expected: compile errors (`Component` undefined, `compute` unknown).

- [ ] **Step 3: Write component.h**

```cpp
#pragma once
#include <string_view>
#include <vector>
#include "mforce/core/pins.h"

namespace mforce {

// The root of every node. Owns the pin registry its members fill in at
// construction and serves the self-description surface from it.
struct Component {
  virtual ~Component() = default;
  virtual const char* type_name() const { return "Unknown"; }
  virtual SourceCategory category() const { return SourceCategory::Utility; }

  std::span<const ParamDescriptor> param_descriptors() const { return paramDescs_; }
  std::span<const InputDescriptor> input_descriptors() const { return inputDescs_; }
  std::span<const SettingDescriptor> setting_descriptors() const { return settingDescs_; }
  std::span<const ArrayDescriptor> array_descriptors() const { return arrayDescs_; }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) {
    if (auto* p = find<Param>(name)) { p->set(std::move(src)); return; }
    if (auto* i = find<Input>(name)) { i->set(std::move(src)); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const {
    if (auto* p = find<Param>(name)) return p->get();
    if (auto* i = find<Input>(name)) return i->get();
    return nullptr;
  }
  void add_param(std::string_view name, std::shared_ptr<ValueSource> src) { set_param(name, std::move(src)); }
  void clear_param(std::string_view name) { set_param(name, nullptr); }

  void set_setting(std::string_view name, float v) {
    for (auto* pin : pins_)
      if (pin->kind == PinKind::Setting && name == pin->name) {
        static_cast<SettingBase*>(pin)->set_from_float(v);
        on_setting_changed(name);
        return;
      }
  }
  float get_setting(std::string_view name) const {
    for (auto* pin : pins_)
      if (pin->kind == PinKind::Setting && name == pin->name) return static_cast<SettingBase*>(pin)->as_float();
    return 0.0f;
  }
  void set_array(std::string_view name, std::vector<float> values) {
    if (auto* a = find<Array>(name)) { a->values = std::move(values); on_array_changed(name); }
  }
  std::vector<float> get_array(std::string_view name) const {
    if (auto* a = find<Array>(name)) return a->values;
    return {};
  }
  virtual void on_setting_changed(std::string_view) {}
  virtual void on_array_changed(std::string_view) {}
  const std::vector<PinBase*>& pins() const { return pins_; }

  // called by PinBase's constructor
  void register_pin(PinBase* pin) {
    pins_.push_back(pin);
    switch (pin->kind) {
      case PinKind::Param:   paramDescs_.push_back(static_cast<Param*>(pin)->descriptor()); break;
      case PinKind::Input:   inputDescs_.push_back(static_cast<Input*>(pin)->descriptor()); break;
      case PinKind::Slot:    inputDescs_.push_back(static_cast<SlotBase*>(pin)->descriptor()); break;
      case PinKind::Setting: settingDescs_.push_back(static_cast<SettingBase*>(pin)->descriptor()); break;
      case PinKind::Array:   arrayDescs_.push_back(static_cast<Array*>(pin)->descriptor()); break;
    }
  }

private:
  template <class P> P* find(std::string_view name) const {
    for (auto* pin : pins_)
      if (name == pin->name)
        if (auto* p = dynamic_cast<P*>(pin)) return p;
    return nullptr;
  }
  std::vector<PinBase*> pins_;
  std::vector<ParamDescriptor> paramDescs_;
  std::vector<InputDescriptor> inputDescs_;
  std::vector<SettingDescriptor> settingDescs_;
  std::vector<ArrayDescriptor> arrayDescs_;
};

inline PinBase::PinBase(Component* owner, const char* n, PinKind k) : name(n), kind(k) { owner->register_pin(this); }

} // namespace mforce
```

`SettingBase` and `SlotBase` are non-template bases the templates derive from (add them to `pins.h`: `struct SettingBase : PinBase { virtual void set_from_float(float) = 0; virtual float as_float() const = 0; virtual SettingDescriptor descriptor() const = 0; }` and `struct SlotBase : PinBase { virtual InputDescriptor descriptor() const = 0; virtual bool set(std::shared_ptr<Component>) = 0; }`), so `Component` can address every `Setting<T>` and `Slot<T>` through one interface. Define `Slot<T>::set` after `Component` in `component.h`:

```cpp
template <class T> bool Slot<T>::set(std::shared_ptr<Component> part) {
  auto* view = dynamic_cast<T*>(part.get());
  if (!view) return false;
  if (!multi) clear();
  views.push_back(view);
  parts.push_back(std::move(part));
  return true;
}
```

`find` uses `dynamic_cast` on a handful of pins at wiring time only (never on the audio thread); it is the one cast this design keeps.

- [ ] **Step 4: Rewrite the ValueSource section of dsp_value_source.h**

Replace lines 71-131 (`struct ValueSource { ... };`) with:

```cpp
#include "mforce/core/component.h"

struct ValueSource : Component {
  virtual void prepare(const RenderContext& ctx, int frames) {
    for (auto* pin : pins()) pin->prepare(ctx, frames);
    on_prepare(ctx, frames);
  }
  virtual void on_prepare(const RenderContext&, int) {}
  // Advance every pin in declaration order, then compute. Non-virtual: a
  // node's work is compute().
  float next() {
    for (auto* pin : pins()) pin->advance();
    cur_ = compute();
    return cur_;
  }
  virtual float compute() = 0;
  virtual float current() const { return cur_; }
  virtual bool tracks_frequency_live() const { return true; }
  virtual float phase_delay_at(float) { return 0.0f; }
  virtual void reseed() {}
protected:
  float cur_{0.0f};
};

struct Generator : ValueSource {
  enum class Range { Bipolar, Unipolar, None };
  virtual Range range() const { return Range::None; }
};

struct Processor : ValueSource {
  Input input{this, "source"};
  SourceCategory category() const override { return SourceCategory::Filter; }
};
```

Keep the descriptor structs and `SourceCategory` above it unchanged (they are what `pins.h` includes; to avoid the include cycle, move `ParamDescriptor`, `InputDescriptor`, `SettingDescriptor`, `ArrayDescriptor` and `SourceCategory` into a new `engine/include/mforce/core/descriptors.h` that both `pins.h` and `dsp_value_source.h` include; `dsp_value_source.h` keeps including it so no other file changes). `ConstantSource` becomes:

```cpp
struct ConstantSource final : Generator {
  explicit ConstantSource(float v) : v_(v) { cur_ = v; }
  void set(float v) { v_ = v; }
  float compute() override { return v_; }
  const char* type_name() const override { return "Constant"; }
private:
  float v_{0.0f};
};
```

and `RefSource` keeps its on-the-fly read:

```cpp
struct RefSource final : ValueSource {
  std::shared_ptr<ValueSource> source;
  bool guard{false};
  explicit RefSource(std::shared_ptr<ValueSource> src, bool g = false) : source(std::move(src)), guard(g) {}
  void prepare(const RenderContext&, int) override {}   // the advancing consumer prepares the real source
  float compute() override { return read(); }
  float current() const override { return read(); }
  const char* type_name() const override { return "RefSource"; }
private:
  float read() const { /* unchanged body */ }
};
```

The Task 0 instrumentation inside `read()` stays as it is.

- [ ] **Step 5: Build and run the tests**

Run: `cmake --build build --config Release --target engine_tests && build\tools\engine_tests\Release\engine_tests.exe`
Expected: compile fails in every node header that still overrides `next()` (a non-virtual `next()` cannot be overridden). That is expected at this step: Task 3 and the family tasks migrate them. To get a green test run for Tasks 1 and 2 alone, build them behind a temporary scope: keep the old `ValueSource` as `ValueSourceLegacy`? No. Instead do Tasks 2 and 3 together with the oscillator family and run the tests then; the commit at the end of Task 3 is the first green commit. Do not commit between Tasks 1, 2 and 3.

---

### Task 3: WaveSource and the oscillators (the prototype migration)

**Files:**
- Modify: `engine/include/mforce/core/dsp_wave_source.h` (all of it)
- Modify: `engine/include/mforce/source/sine_source.h`, `saw_source.h`, `triangle_source.h`, `pulse_source.h`, `fm_source.h`
- Test: `tools/engine_tests/main.cpp` (`run_pin_tests`, `run_base_tests`, and every existing test)

**Interfaces:**
- Consumes: `Generator`, `Param` (Tasks 1-2).
- Produces: the worked example every family task follows.

- [ ] **Step 1: Rewrite WaveSource**

```cpp
#pragma once
#include "mforce/core/dsp_value_source.h"
#include <cmath>

namespace mforce {

struct WaveSource : Generator {
  // Declared in the order the old next() advanced them: amplitude, frequency, phase.
  Param amplitude{this, "amplitude", 1.0f,   0.0f,  10.0f,    "0-1"};
  Param frequency{this, "frequency", 440.0f, 0.01f, 20000.0f, "hz"};
  Param phase    {this, "phase",     0.0f,  -1.0f,  1.0f,     "cycles"};

  explicit WaveSource(int sampleRate) : sampleRate_(sampleRate) {}

  Range range() const override { return Range::Bipolar; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  void on_prepare(const RenderContext&, int) override { ptr_ = -1; }

  float compute() override {
    ++ptr_;
    const float f = frequency.current();
    if (f <= 0.0f) throw std::runtime_error("WaveSource: non-positive frequency");   // removed in checkpoint 2.5
    if (ptr_ == 0) {
      currPos_   = phase.current();
      lastPhase_ = phase.current();
    }
    currAmpl_ = amplitude.current();
    currFreq_ = f;
    currPhase_ = phase.current();
    currPhaseIncr_ = currFreq_ / float(sampleRate_);
    const float w = wave();
    const float out = w * currAmpl_;
    const float phaseDelta = (currPhase_ - lastPhase_);
    lastPhase_ = currPhase_;
    currPos_ = std::fmod(currPos_ + currPhaseIncr_ + phaseDelta, 1.0f);
    if (currPos_ < 0.0f) currPos_ += 1.0f;
    return out;
  }

protected:
  virtual float wave() = 0;     // was compute_wave_value()
  int   sampleRate_;
  int   ptr_{-1};
  float currAmpl_{1.0f}, currFreq_{440.0f}, currPhase_{0.0f};
  float currPos_{0.0f}, currPhaseIncr_{0.0f};
  float lastPhase_{0.0f};
};

} // namespace mforce
```

Note the descriptor table order was frequency, amplitude, phase while the old `next()` advanced amplitude, frequency, phase; declaration order follows the advance order (recipe step 4), so `param_descriptors()` now lists amplitude first. The UI lists pins in descriptor order; that is a cosmetic change in the Properties panel, not a render change. Delete `set_amplitude` / `get_amplitude` and friends; check `git grep -n "set_amplitude\|get_amplitude\|set_frequency\|get_frequency\|set_phase\|get_phase" -- engine tools` first and replace any caller with `node.amplitude.set(...)` / `.get()`.

- [ ] **Step 2: Migrate the five oscillators**

In each of `sine_source.h`, `saw_source.h`, `triangle_source.h`, `pulse_source.h`, `fm_source.h`: rename `compute_wave_value()` to `wave()`; for pins beyond the three (pulse `width`, triangle `bias`/`asymmetric`, FM `modIndex`/`ratio`/`oversample`/... as listed in their descriptor tables) apply the recipe: `Param`/`Setting` members in the old advance order, delete the five lists, read `x.current()`. Their inventories:

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `FMSource` | `source/fm_source.h` | frequency, amplitude, phase, carrierRatio, modRatio, depth | — | unbounded_pos, oversample | — |
| `PulseSource` | `source/pulse_source.h` | frequency, amplitude, phase, dutyCycle, bend | — | — | — |
| `SawSource` | `source/saw_source.h` | — | — | — | — |
| `SineSource` | `source/sine_source.h` | — | — | — | — |
| `TriangleSource` | `source/triangle_source.h` | frequency, amplitude, phase, bias, power | — | asymmetric | — |

- [ ] **Step 3: Build and run every test**

Run: `cmake --build build --config Release --target mforce_cli engine_tests` then `build\tools\engine_tests\Release\engine_tests.exe`
Expected: the build fails only in headers not yet migrated. Migrate those in the family order of Tasks 4-11 before the first green build; the first green build and commit come at the end of the last family task if the tree cannot build in between. If it can (the loader and UI call only `set_param` by name and compile against the new base), commit per family. Determine which at this step with the compiler.

- [ ] **Step 4: Null gate**

Run: `python tools/null_gate_perform_source.py --jobs 3`
Expected: `80/80 manifest entries identical`.

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/core tools/engine_tests/main.cpp engine/include/mforce/source/sine_source.h engine/include/mforce/source/saw_source.h engine/include/mforce/source/triangle_source.h engine/include/mforce/source/pulse_source.h engine/include/mforce/source/fm_source.h
git commit -m "core: Component root, self-registering pins, Generator/Processor; WaveSource and the oscillators migrated (checkpoint 2.1 part 1)"
```

---
### Task 4: Migrate core: envelopes, curve, range/var, multi, name gate

**Files:**
- Modify: `engine/include/mforce/core/curve_node.h`, `engine/include/mforce/core/dsp_value_source.h`, `engine/include/mforce/core/envelope.h`, `engine/include/mforce/core/envelope_presets.h`, `engine/include/mforce/core/multi_source.h`, `engine/include/mforce/core/name_gate.h`, `engine/include/mforce/core/range_source.h`, `engine/include/mforce/core/var_source.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `CurveNode` | `core/curve_node.h` | source | — | — | — |
| `ConstantSource` | `core/dsp_value_source.h` | — | — | — | — |
| `RefSource` | `core/dsp_value_source.h` | — | — | — | — |
| `Envelope` | `core/envelope.h` | minValue, maxValue, trigger | — | stage_accuracy, ramp_accuracy, sustainLevel, timeScale | — |
| `ADREnvelope` | `core/envelope_presets.h` | — | — | attack, attackCurve, attackPower, decay, decayCurve, decayPower, decayLevel, releaseCurve, releasePower, stage_accuracy, ramp_accuracy | — |
| `ADSEnvelope` | `core/envelope_presets.h` | — | — | attack, attackCurve, attackPower, decay, decayCurve, decayPower, sustainLevel, stage_accuracy, ramp_accuracy | — |
| `ADSREnvelope` | `core/envelope_presets.h` | — | — | attack, attackCurve, attackPower, decay, decayCurve, decayPower, sustainLevel, release, releaseCurve, releasePower, stage_accuracy, ramp_accuracy | — |
| `AREnvelope` | `core/envelope_presets.h` | — | — | attack, attackCurve, attackPower, releaseCurve, releasePower, stage_accuracy, ramp_accuracy | — |
| `ASEnvelope` | `core/envelope_presets.h` | — | — | attack, attackCurve, attackPower, sustainLevel, reverse, stage_accuracy, ramp_accuracy | — |
| `ASREnvelope` | `core/envelope_presets.h` | — | — | attack, attackCurve, attackPower, sustainLevel, release, releaseCurve, releasePower, stage_accuracy, ramp_accuracy | — |
| `MultiSource` | `core/multi_source.h` | — | source | — | — |
| `NameGate` | `core/name_gate.h` | in | — | — | — |
| `RangeSource` | `core/range_source.h` | min, max, var | — | normalized | — |
| `VarSource` | `core/var_source.h` | val, var, varPct | — | absolute | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(core): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 5: Migrate the noise family

**Files:**
- Modify: `engine/include/mforce/source/layered_red_noise_source.h`, `engine/include/mforce/source/noise_sources.h`, `engine/include/mforce/source/pink_noise_source.h`, `engine/include/mforce/source/red_noise_source.h`, `engine/include/mforce/source/wander_noise_source.h`, `engine/include/mforce/source/white_noise_source.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `LayeredRedNoiseSource` | `source/layered_red_noise_source.h` | — | — | count | frequency, amplitude |
| `BlueNoiseSource` | `source/noise_sources.h` | amplitude | — | — | — |
| `CrackleNoiseSource` | `source/noise_sources.h` | chaos | — | — | — |
| `MurmurationNoiseSource` | `source/noise_sources.h` | count, cohesion, alignment, separation, chaos, speed | — | — | — |
| `PerlinNoiseSource` | `source/noise_sources.h` | speed, octaves, persistence, lacunarity | — | — | — |
| `VelvetNoiseSource` | `source/noise_sources.h` | density, amplitude | — | — | — |
| `VioletNoiseSource` | `source/noise_sources.h` | amplitude | — | — | — |
| `PinkNoiseSource` | `source/pink_noise_source.h` | amplitude | — | — | — |
| `RedNoiseSource` | `source/red_noise_source.h` | frequency, amplitude, phase, density, smoothness, rampVariation, boost, continuity, zeroCrossTendency | — | — | — |
| `WanderNoise2Source` | `source/wander_noise_source.h` | amplitude, minSpeed, maxSpeed, reverseProb, retraceProb, retracePct | — | — | — |
| `WanderNoise3Source` | `source/wander_noise_source.h` | amplitude, speed, deltaSpeed, slopeLimit | — | — | — |
| `WanderNoiseSource` | `source/wander_noise_source.h` | amplitude, speed, deltaSpeed, slopeLimit | — | — | — |
| `WhiteNoiseSource` | `source/white_noise_source.h` | amplitude, density, boost, continuity, zeroCrossTendency | — | — | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(noise): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 6: Migrate wavetable, hybrid KS and the evolution holders

**Files:**
- Modify: `engine/include/mforce/source/hybrid_ks_source.h`, `engine/include/mforce/source/wave_evolution.h`, `engine/include/mforce/source/wavetable_source.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `HybridKSSource` | `source/hybrid_ks_source.h` | frequency, amplitude, phase | inputSource | holdCycles, morphDuration, numPartials | — |
| `AveragingEvolutionSource` | `source/wave_evolution.h` | — | — | sampleCount, speed, decayFactor, leading, autoAdjust | — |
| `BezierPullEvolutionSource` | `source/wave_evolution.h` | — | — | p0, p1, p2, p3, rate | — |
| `BitRotateEvolutionSource` | `source/wave_evolution.h` | shiftBits, stepEvery | — | — | — |
| `BowedStringEvolutionSource` | `source/wave_evolution.h` | frictionGain | bow | tubeLoss, bowSpeed, bowPosition, brightness | — |
| `BrassEvolutionSource` | `source/wave_evolution.h` | brassiness | breath | tubeLoss, lipTension, lipFreqRatio, lipQ | — |
| `CellularAutomatonEvolutionSource` | `source/wave_evolution.h` | — | — | rule, threshold, levelHi, levelLo, stepEvery | — |
| `EKSEvolutionSource` | `source/wave_evolution.h` | — | — | pickPosition, pickDirection, stiffness, decayStretch, drumBlend | — |
| `HistogramEqualizeEvolutionSource` | `source/wave_evolution.h` | — | — | bins, rate | — |
| `PluckEvolutionSource` | `source/wave_evolution.h` | — | — | muting | — |
| `ReactionDiffusionEvolutionSource` | `source/wave_evolution.h` | — | — | feed, kill, diffU, diffV, dt, subSteps | — |
| `ReedEvolutionSource` | `source/wave_evolution.h` | reedStiffness | breath | tubeLoss, loopFilter | — |
| `SortErosionEvolutionSource` | `source/wave_evolution.h` | — | — | swapsPerSample, descending | — |
| `WavetableSource` | `source/wavetable_source.h` | frequency, amplitude, phase, speedFactor | inputSource, evolution | interpolate | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(wavetable): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 7: Migrate the additive family

**Files:**
- Modify: `engine/include/mforce/source/additive/additive_source2.h`, `engine/include/mforce/source/additive/basic_additive_source.h`, `engine/include/mforce/source/additive/formant.h`, `engine/include/mforce/source/additive/full_additive_source.h`, `engine/include/mforce/source/additive/partials.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `AdditiveSource2` | `source/additive/additive_source2.h` | frequency, amplitude, phase, phaseOffset, freqVarDepth, freqVarSpeed, amplVarDepth, amplVarSpeed | partials | partialCount | — |
| `BasicAdditiveSource` | `source/additive/basic_additive_source.h` | frequency, amplitude, phase, evenWeight, oddWeight, rolloff, freqVarPct, freqVarSpeed, amplVarPct, amplVarSpeed | — | — | — |
| `BandSpectrum` | `source/additive/formant.h` | startFreq, freqIncrement | — | — | gains |
| `FixedSpectrum` | `source/additive/formant.h` | — | — | — | gains |
| `Formant` | `source/additive/formant.h` | frequency, gain, width, power | — | — | — |
| `FormantSequence` | `source/additive/formant.h` | blend | spectra | — | — |
| `FormantSpectrum` | `source/additive/formant.h` | — | — | — | — |
| `FullAdditiveSource` | `source/additive/full_additive_source.h` | frequency, amplitude, phase, formantWeight, formantFloor | formant, partials | noiseBedLevel, noiseBedFreq, noiseBedWidth, noiseBedDelay, noiseBedFadePow, noiseBedFade | — |
| `CompositePartials` | `source/additive/partials.h` | — | partials | — | — |
| `ExpandRuleNode` | `source/additive/partials.h` | spacing1, spacing2, dt1, dt2, loPct1, loPct2, power1, power2, po1, po2 | — | count, recurse | — |
| `ExplicitPartials` | `source/additive/partials.h` | — | — | maxPartials, evolve, unitPO1, unitPO2, rolloff1, rolloff2, detune1, detune2, bandwidth1, bandwidth2, bandwidthHz | mult1, ampl1, mult2, ampl2 |
| `FullPartials` | `source/additive/partials.h` | — | — | maxPartials, minMult, evenWeight1, evenWeight2, oddWeight1, oddWeight2, unitPO1, unitPO2, rolloff1, rolloff2, detune1, detune2, bandwidth1, bandwidth2, bandwidthHz | — |
| `Partials` | `source/additive/partials.h` | multEnv, amplEnv, poEnv, roEnv, dtEnv, bwEnv, motionEnv, shimmerEnv | — | rolloff1, rolloff2, detune1, detune2, bandwidth1, bandwidth2, bandwidthHz | — |
| `SequencePartials` | `source/additive/partials.h` | — | — | maxPartials, minMult1, minMult2, incr1, incr2, unitPO1, unitPO2, rolloff1, rolloff2, detune1, detune2, bandwidth1, bandwidth2, bandwidthHz | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(additive): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 8: Migrate the physical models

**Files:**
- Modify: `engine/include/mforce/source/allpass_resonator.h`, `engine/include/mforce/source/bow_table_source.h`, `engine/include/mforce/source/delay_line_source.h`, `engine/include/mforce/source/ks_string.h`, `engine/include/mforce/source/mesh2d_source.h`, `engine/include/mforce/source/pierce_filter.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `AllpassResonator` | `source/allpass_resonator.h` | frequency, amplitude | source | feedback, damping, stiffness, tension, exciteGain, direct | — |
| `BowTableSource` | `source/bow_table_source.h` | slope, offset | source | minOutput, maxOutput | — |
| `DelayLineSource` | `source/delay_line_source.h` | frequency, ratio, amplitude | source | compensate | — |
| `KSString` | `source/ks_string.h` | frequency, amplitude | source, damper, bow | numCombs, detune, spreadJitter, excStagger, t60, brightness, dispersion, inharmGain, inharmFb, inharmHp, ap1, ap2, ap3, fbCoeff, releaseFb, damperNoise, exciteGain, direct, bowSpeed, frictionGain, bowGain | — |
| `Mesh2DSource` | `source/mesh2d_source.h` | inX, inY, outX, outY, decay, edgeFc, edgeR, coefNeg, coefPos | source | stk, cols, rows, edgeMode | — |
| `PierceFilterSource` | `source/pierce_filter.h` | coefNeg, coefPos | source | — | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(physical): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 9: Migrate the filters

**Files:**
- Modify: `engine/include/mforce/filter/biquad_source.h`, `engine/include/mforce/filter/filters.h`, `engine/include/mforce/filter/hammer_bank.h`, `engine/include/mforce/filter/limiter.h`, `engine/include/mforce/filter/reverb.h`, `engine/include/mforce/filter/svf_source.h`, `engine/include/mforce/filter/vibrato.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `BiquadSource` | `filter/biquad_source.h` | frequency, radius | source | Raw, mode, b0, b1, b2, a1, a2 | — |
| `BWBandpassFilter` | `filter/filters.h` | lowCutoff, highCutoff | source | — | — |
| `BWHighpassFilter` | `filter/filters.h` | cutoffFreq | source | — | — |
| `BWLowpassFilter` | `filter/filters.h` | cutoffFreq | source | — | — |
| `DelayFilter` | `filter/filters.h` | delayTime, delayLevel, feedback | source | — | — |
| `HammerBank` | `filter/hammer_bank.h` | frequency | source | numBands, harm1, harm2, harm3, harm4, resStart, resEnd, resDecay, bandTilt, direct, gain | — |
| `Limiter` | `filter/limiter.h` | threshold, release | source | — | — |
| `Reverb` | `filter/reverb.h` | roomSize, damping, wet, dry | source | — | — |
| `SVFSource` | `filter/svf_source.h` | cutoffFreq, resonance | source | Lowpass, mode, normalize | — |
| `Vibrato` | `filter/vibrato.h` | frequency | — | speed, depth, attack, threshold, speedVar, depthVar, zeroCrossTendency | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(filter): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 10: Migrate combiners, modulators and the rest of source/

**Files:**
- Modify: `engine/include/mforce/source/combined_source.h`, `engine/include/mforce/source/multiplex_source.h`, `engine/include/mforce/source/phased_value_source.h`, `engine/include/mforce/source/repeating_source.h`, `engine/include/mforce/source/segment_source.h`, `engine/include/mforce/source/shaper_source.h`, `engine/include/mforce/source/wormhole_source.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `CombinedSource` | `source/combined_source.h` | — | source1, source2 | Mix, operation, gainAdj | — |
| `CrossfadeSource` | `source/combined_source.h` | amplitude | source1, source2 | ratio, overlap, gainAdj | — |
| `DistortedSource` | `source/combined_source.h` | amplitude, density, gain, shift | source | — | — |
| `StaticRangeSource` | `source/combined_source.h` | — | — | — | — |
| `StaticVarSource` | `source/combined_source.h` | — | — | — | — |
| `MultiplexSource` | `source/multiplex_source.h` | — | source | count | — |
| `PhasedValueSource` | `source/phased_value_source.h` | amplitude | — | — | — |
| `RepeatingSource` | `source/repeating_source.h` | — | source | duration, durVarPct, gapDuration, gapVarPct | — |
| `SegmentSource` | `source/segment_source.h` | amplitude, smoothness, width, widthVarPct, valVarPct, gap, gapVarPct | — | auto, oneShot, timeMode | values |
| `ShaperSource` | `source/shaper_source.h` | drive, smoothness, morph, breakaway, capture | source | hysteresis | values, segs, values2, segs2 |
| `WormholeSource` | `source/wormhole_source.h` | — | source | — | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(combine_mod): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 11: Migrate render: PerformOut

**Files:**
- Modify: `engine/include/mforce/render/perform_source.h`
- Test: `tools/engine_tests/main.cpp` (every existing test; no new test unless a type has none and a one-sample smoke is cheap: `prepare`, ten `next()` calls, finite output)

**Interfaces:**
- Consumes: `Component`, `Generator`, `Processor`, the pin types (Tasks 1-3).
- Produces: nothing new; the types keep their names and their `set_param` names.

The types and their pins (from today's descriptor tables):

| Type | File | Params | Inputs | Settings | Arrays |
|---|---|---|---|---|---|
| `PerformOut` | `render/perform_source.h` | — | — | — | — |

The transformation for each type in the table, exactly as Task 3 did it for `WaveSource`:

1. Delete the hand-written `param_descriptors()`, `input_descriptors()`, `setting_descriptors()`, `array_descriptors()`, `set_param()`, `get_param()`, `add_param()`, `clear_param()`, `set_setting()`, `get_setting()`, `set_array()`, `get_array()` overrides, and every typed `set_x()` / `get_x()` accessor that nothing outside the class calls (check with `git grep -n "->set_x\|\.set_x"` first; the UI and the loader use `set_param` by name, not these).
2. Replace each `std::shared_ptr<ValueSource> x_` member that had a `ParamDescriptor` with `Param x{this, "name", default, min, max, "hint"}`, copying name, default, min, max and hint from the old descriptor row and the old constructor's `ConstantSource` default. If the constructor default and the descriptor default disagree, the descriptor wins and the disagreement is listed in the commit message (that is one of the drift bugs this checkpoint fixes; expect the three noise types the review named).
3. Replace each input with `Input x{this, "name"}` (or `Input x{this, "name", Input::Optional}` when the old code tolerated a null source), each setting with `Setting<float|int|bool> x{this, "name", default, min, max}` (with `labels` for enum settings), each array with `Array x{this, "name", groupName, default, min, max}`. Structural inputs (`partials`, `formant`, `spectra`, `evolution`, `stages`) stay as they are in this checkpoint: `Slot<T>` arrives with Part 3's checkpoint 3.1.
4. **Declare the pins in the order the old `next()` advanced them.** Order matters until the memo lands (checkpoint 2.3): two pins whose sub-graphs share a source read different samples if advanced in a different order.
5. Rename `next()` to `compute()` and delete every `x_->next();` line from it; where it read `x_->current()` write `x.current()`. Keep `cur_ = ...; return cur_;` as the last lines. Delete the `current()` override if it only returned `cur_` (the base provides it).
6. In `prepare(ctx, frames)` delete every `x_->prepare(ctx, frames);` line (the base forwards to every pin); keep what remains if anything does, otherwise delete the override.
7. Build; run `python tools/gates.py --fast`; run `python tools/null_gate_perform_source.py --jobs 3`: every manifest entry identical except the patches named in the commit message under step 2. Commit.

- [ ] **Step 1: Migrate every type in the table** (recipe steps 1-6), keeping each type's base: a type whose old base was `ValueSource` and which has a signal input in the Inputs column becomes a `Processor` (its `source` input is the inherited `input`; delete the duplicate declaration); one with no signal input becomes a `Generator` (declare `range()` as Bipolar for oscillator-like output, Unipolar for envelopes/curves/0..1 sources, None otherwise). A type that today derives from `WaveSource` keeps that.
- [ ] **Step 2: Build** — `cmake --build build --config Release --target mforce_cli mforce_ui engine_tests test_figures`; fix compile errors in this family only.
- [ ] **Step 3: Tests and fast gates** — `build\tools\engine_tests\Release\engine_tests.exe` then `python tools/gates.py --fast`.
- [ ] **Step 4: Null gate** — `python tools/null_gate_perform_source.py --jobs 3`: identical, or only the patches named under recipe step 2 (refreeze those with `--freeze` only after confirming the diff is the default disagreement and nothing else, by rendering the patch with the old binary from `git stash` and diffing the two WAVs' first differing sample against the two defaults).
- [ ] **Step 5: Commit** — `git add` the files above; message `nodes(render): pins declared once (checkpoint 2.1)` with the list of patches whose defaults were corrected, if any.

---

### Task 12: Close the checkpoint

**Files:**
- Modify: `tools/structure/metrics.json` (snapshot), `docs/autonomy/refactor/reports/<date>-checkpoint-2.1.md` (new), `docs/autonomy/refactor/REVIEW.md`, `docs/autonomy/refactor/BACKLOG.md`

- [ ] **Step 1: Confirm nothing hand-written remains** — `git grep -n "param_descriptors() const override\|set_param(std::string_view" -- engine/include engine/src` must list only `component.h`. `git grep -n "compute_wave_value" -- engine tools` must list nothing.
- [ ] **Step 2: Every gate** — `python tools/gates.py` (all, 15-30 minutes): everything passes except the known roundtrip-smoke `wiring` shape.
- [ ] **Step 3: Meter and delta** — `python tools/structure/check.py --snapshot` and `python tools/structure/check.py --delta <commit before Task 1>`; the delta shows the 109 dead accessors gone and no file over 600 lines that was not already.
- [ ] **Step 4: Independent review** — dispatch a fresh reviewer agent on the checkpoint's commits with the ten-item brief from `docs/superpowers/specs/2026-10-04-refactor-gates-design.md` section 3.8, told to refute; fold its findings into the report's section 8 and fix what is right.
- [ ] **Step 5: Architecture report** — write `docs/autonomy/refactor/reports/<date>-checkpoint-2.1.md` in the ten-section format of `2026-10-05-checkpoint0-meter.md`; entries for `Component`, `PinBase`/`Param`/`Input`/`Slot`/`Setting`/`Array`, `Generator`, `Processor`, the changed `ValueSource` and `WaveSource`; the delta pasted into section 5; one or two C++ lessons (self-registering members as the stand-in for reflection; `Slot<T>` as a compile-time-typed plug). Add REVIEW entry R2 and mark BACKLOG step 1's first checkpoint done.
- [ ] **Step 6: Commit** — `git add` the files above; `git commit -m "docs: checkpoint 2.1 report (R2)"`.

## Self-review notes

- Spec coverage: Part 2 section 3 (five kinds, declared once; the two bases; classification rule) is Tasks 1-11; section 7's 2.1 row (109 accessors deleted) is the recipe's step 1 and the closing check; Part 3 section 2's `Component` root is Task 2. `Slot<T>` is declared and tested only through `Component::register_pin`; its use arrives with checkpoint 3.1 as the spec says. `next()` non-virtual with pins advanced in declaration order preserves today's numerics by the argument in Task 3's note; the memo (2.3) removes the order dependence.
- Types: `Param::current()`, `Input::current()`, `Setting<T>::value`, `Array::values`, `Generator::Range`, `Processor::input`, `ValueSource::compute()`, `WaveSource::wave()` are used with those exact names throughout.
- The inventory tables were generated from the headers on 2026-10-05 (`tools/structure` was not used; a 60-line throwaway script parsed the descriptor tables). A type missing from a table is a type with no descriptor table today; the recipe still applies to its `next()`/`prepare()`.

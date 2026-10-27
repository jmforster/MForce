# Part 2, checkpoint 2.5: no throw on a render path; fault flags; a rejected wire says so — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** `start()` and `next()` never throw and never do I/O: an invalid runtime value is clamped to the narrowest value that makes the old throw's condition false and the node sets a fault bit; the host collects the bits after a render and reports them once. A wire the registry cannot place fails the load with the node's id, the pin and the reason, instead of rendering silently wrong. The DelayLine's one-past-the-ring read is fixed. Every valid patch renders byte-identically; the patches that stop loading and the patches whose DelayLine read moves are measured beforehand and listed.

**Architecture:** `ValueSource` gains a per-node fault byte (`std::atomic<unsigned char>`, set with a relaxed OR on the fault path only, never on the per-sample path), five named faults, `faults()` / `clear_faults()`, and a protected `fault(Fault)`. The two render-path throws become clamp-and-fault; the tap-free-cycle `fprintf` becomes a fault bit and the same re-entry guard covers `prepare()` and `start()`; a never-prepared node faults instead of dividing by zero; a clockless pull inside a host faults on its cold branch; `prepare()` throws on a zero rate (prepare may throw) and Mesh2D's silent 48000 fallback goes. `Component::set_param` returns `bool` — false for a pin the node does not have or a part of the wrong kind — and the loader turns false, and an unknown key, into a load failure naming node, pin and reason. `PitchedInstrument::collect_faults` walks every voice's `nodesById`; the CLI prints a `[faults]` block after a render, the UI's Generate writes the status line and the live pool is polled once a frame. `DelayLineSource` wraps its read to slot 0 and drops the extra zero slot.

**Tech Stack:** C++20 (MSVC, CMake 3.20+), the existing `engine_tests` harness, `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/structure/check.py`, `python tools/lint_patches.py` (the pre-check), `tools/measure_25.py` (Task 0 only; deleted in Task 1, rebuilt from history).

**Spec:** `docs/architecture/part2-valuesource-contract.md` section 6 ("No throw on a render path; a rejected wire says so": the two throws reachable from a render path; the rule — `start` and `next` never throw, an invalid value is clamped to the node's declared range and a fault flag set, no I/O on a render path, the flag is a per-node bit set without locking and collected by the control side after a render, `prepare` may throw; `set_pin` fails on an unknown name or a kind mismatch with the reason and the node's id, the builder turns that into a load failure; byte-identity: unchanged for every valid patch, a zero-frequency patch renders with a fault, a bad-wire patch fails to load with the reason) and section 7 row 2.5; `docs/architecture/target-architecture.md` section 2 principle 6 (structure and behaviour never in one commit); the items routed here by the last three reports — 2.2 §7 (the DelayLine one-past-the-ring read, Mesh2D's 48000 fallback, `start()` on a never-prepared node in Release), 2.3 §7 (a clockless pull inside a host, `prepare`/`start` on a hand-wired tap-free cycle, `computing_` after a throw, the `fprintf` on the cycle path), 2.4 §7 (the clockless fallback); REVIEW R3's verdict (the DelayLine zero slot "stays listed for 2.5").

## Global Constraints

- No heap allocation, lock or throw in any `next()`, `step()`, `compute()`, `start()` or fault path (CLAUDE.md non-negotiables). The fault byte is a lock-free atomic; the only operations on it from the audio thread are a relaxed `fetch_or` on a fault and nothing on the common path. `prepare()` may throw and may allocate, as before.
- Explicit registries, no reflection (CLAUDE.md). Fault names are a `switch`, not a table of strings looked up by reflection.
- Byte-identity: `python tools/null_gate_perform_source.py --jobs 3` reports `78/78 manifest entries identical` after Tasks 1, 2 and 3. Task 4 is the one task whose renders change: the differing manifest entries must be exactly the manifest members of Task 0's wrap list, and only then is the manifest refrozen. A patch that stops loading in Task 3 must be on Task 0's unknown-key list or be explained in the Ledger as a kind mismatch (the one class the current loader cannot see). Any other difference, in any task, is a bug until ruled on.
- No new file over 600 lines, no new function over 80 (`tools/structure/limits.json`); `python tools/structure/check.py --delta <Task 0 sha>` is pasted into the report.
- No dated changelog, backlog id or REVIEW number in code comments.
- Commit after every task; `python tools/gates.py --fast` before every commit (its roundtrip smoke fails on `perform/wiring_setting.json` for the pre-existing backlog-79 reason; every other gate must pass; an engine header change needs the UI rebuilt for the stamp gate — rename-then-link if a UI is running).
- Work on main, no worktree (ruling 1 of checkpoint 2.1 stands). The in-tree build is `build/`.
- The A/B base for every task is `renders/scratch/ab_tree/cli_pre25.exe` (Task 0 step 0: the build at the Task 0 base, which `git diff 403269f HEAD --stat -- engine tools/mforce_cli tools/engine_tests tools/mforce_ui` must show is the 2.4 engine). The pre-2.5 arms of the eight set files under `renders/scratch/ab24_tree/<set>/new/hashes.txt` (2.4's sealed arms, re-used as 2.5's `old`; ruling 12).
- Dates: `date` first (the machine is HST); this plan is dated 2026-10-20.

## Review Focus

The five input classes the spec implies but no existing test exercises, most likely to bite first. Each line names the task whose tests pin it.

1. **A modulated frequency that reaches zero or below mid-note** (an envelope-driven `frequency` on a WaveSource with `minValue` 0, a bend that overshoots): today the process terminates from the audio thread. Task 1 pins it: the sample renders with the pin's minimum, the node reports `Fault::FrequencyClamped`, the next sample with a positive frequency is computed normally.
2. **A never-prepared node that is started and pulled** (a host that builds a node after prepare and forgets `ensure_constant`'s three-argument form; the Release build, where the assert is gone): today a division by zero in `on_start` or `compute`. Task 1 pins it: `start()` sets `Fault::NeverPrepared` and skips `on_start`; `next()` serves `cur_` without computing; no crash.
3. **A cycle with no tap, hand-wired after the host calls** (illegal; the UI refuses; a test can): today a once-per-node `fprintf` from `next()`, and a stack overflow if the cycle is wired before `prepare`. Task 2 pins it: `next()` serves the previous value and sets `Fault::CycleWithoutTap`; `prepare()` and `start()` re-entered through the cycle return with the same fault and do not overflow.
4. **A patch whose wire the registry cannot place** (a typo'd key, a `partials` slot given a SineSource): today the key is warned and ignored, the part silently dropped, and the patch renders wrong. Task 3 pins it: `set_param` returns false; `load_instrument_patch_json` throws `std::runtime_error` whose message names the node id, the pin and the reason; the tracked patches on Task 0's list, if any, are repaired first.
5. **A DelayLine read position that rounds to exactly the ring length** (`writeIdx_ - len` a hair negative, `rp += ring_` rounding up in float): today the read lands on the extra zero slot. Task 4 pins it: the read wraps to slot 0; the renders that move are exactly Task 0's wrap list, `double_advance_ok` among them.

Also noted, not a Review Focus line: a clockless pull inside a host (a node prepared with `RenderContext{rate}` alone by a host that has a clock) sets `Fault::Clockless` on the cold branch; a bare node in a test sets it too and nobody reads it there — Task 2 pins the bit and the Ledger records that the host's report is the only reader.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `engine/include/mforce/core/dsp_value_source.h` (modify) | `enum class Fault`, `fault_name()`; `ValueSource::faults()`, `clear_faults()`, protected `fault()`; `prepare()` throws on a zero rate and guards re-entry; `start()` faults a never-prepared node and guards re-entry; `next()` faults a clockless or never-prepared pull on its cold branch; the tap-free cycle sets a bit — `report_cycle_once_`, `cycleReported_` and `<cstdio>` gone. |
| `engine/include/mforce/core/component.h` (modify) | `bool set_param(name, src)` (false: no Param or Input of that name); `add_param` and `clear_param` return what `set_param` returns. |
| the six `set_param` overrides (modify): `multi_source.h`, `additive/additive_source2.h`, `additive/formant.h`, `additive/full_additive_source.h`, `additive/partials.h`, `source/wavetable_source.h` | return `bool`: true when the override consumed the wire (the right kind in the slot), false for the wrong kind, the base's result otherwise. |
| `engine/include/mforce/core/dsp_wave_source.h` (modify) | `compute()` clamps a non-positive frequency to the pin's minimum and faults. |
| `engine/src/wavetable_source.cpp` (modify) | `fill_table()` clamps the table length to one and faults instead of throwing. |
| `engine/include/mforce/source/mesh2d_source.h` (modify) | the 48000 fallback goes; the rate is the context's. |
| `engine/include/mforce/source/delay_line_source.h` (modify, Task 4) | the read wraps to slot 0; the ring has no extra slot. |
| `engine/include/mforce/render/instrument.h` (modify) | `FaultReport`; `Instrument::collect_faults(std::vector<FaultReport>&)` (virtual, default empty); `PitchedInstrument` walks every voice's `nodesById`, ORs per id, clears; `DrumKit` walks its sources. |
| `engine/src/patch_loader.cpp` (modify) | `wire_params_generic` takes the node id; a false `set_param` and an unknown key throw `std::runtime_error("node 'id' (Type): …")`; the unknown-key warning block becomes the failure. |
| `tools/mforce_cli/main.cpp` (modify) | `run_patch` prints a `[faults]` block after the render (one line per node id: type and fault names), or nothing. |
| `tools/mforce_ui/main.cpp` (modify) | Generate and `--gencheck` write the faults to the transport status line; the live pool is polled once a frame and the status line set once per fault set. |
| `tools/engine_tests/main.cpp` (modify) | `run_no_throw_tests`, `run_fault_tests`, `run_wire_rejection_tests`, `run_delay_line_wrap_tests`. |
| `tools/measure_25.py` (new in Task 0; deleted in Task 1) | the unknown-key list from the current loader's warnings, the non-loaders classified by their error line, the DelayLine wrap counter's list. |
| `tools/null_gate_manifest.json` (modify, Task 4) | refrozen for the wrap list's manifest members. |
| `docs/architecture/measurements/2026-10-20-no-throw-and-wires.md` (new, Task 0) | the three lists. |
| `docs/autonomy/refactor/reports/2026-10-20-checkpoint-2.5.md` (new, Task 5), `REVIEW.md`, `BACKLOG.md`, `docs/autonomy/STATUS.md`, `tools/structure/metrics.json` (modify, Task 5) | the seal. |

---

## The contract (what Task 1 and Task 2 write; every later task codes against it)

`engine/include/mforce/core/dsp_value_source.h`, the parts that change. Everything not shown is as it stands at `403269f`.

```cpp
#include <atomic>
#include <stdexcept>
// <cstdio> is gone: nothing in this header prints.

namespace mforce {

// What a node can report from a render path instead of throwing or
// printing. One bit each; a node accumulates them until the host reads
// and clears them after a render (collect_faults).
enum class Fault : unsigned char {
  None             = 0,
  FrequencyClamped = 1,   // a non-positive frequency was clamped to the pin's minimum
  TableClamped     = 2,   // a table fill's length was clamped to one sample
  NeverPrepared    = 4,   // start() or next() on a node no host prepared
  Clockless        = 8,   // next() with a rate but no host clock: every pull computes
  CycleWithoutTap  = 16,  // re-entered through a cycle with no tap; served the previous value
};
constexpr unsigned char operator|(unsigned char a, Fault b) { return a | static_cast<unsigned char>(b); }
const char* fault_name(Fault f);   // "frequency clamped", "table length clamped", ... ; a switch

struct ValueSource : Component {
  // prepare(ctx): ... (comment as today, plus:) A zero rate is a host error
  // and throws here, where throwing is allowed; a node never sees one.
  // Re-entered through a cycle with no tap (hand-wired before the host
  // calls), it returns with the fault set instead of overflowing.
  void prepare(const RenderContext& ctx) {
    if (ctx.sampleRate <= 0) throw std::invalid_argument("prepare: the context has no sample rate");
    if (computing_) { fault(Fault::CycleWithoutTap); return; }
    computing_ = true;
    ctx_ = ctx;
    lastTick_ = -1;
    faults_.store(0, std::memory_order_relaxed);
    refresh_auto_sources();
    for (auto* pin : pins())
      if (pin->pull == Pull::Auto) pin->prepare(ctx);
    on_prepare(ctx);
    computing_ = false;
  }
  // start(frames): ... (comment as today, plus:) On a node no host prepared
  // it sets the fault and does nothing else — the Debug assert stays, the
  // Release build reports instead of dividing by zero.
  void start(int frames) {
    assert(prepared() && "start() before prepare(ctx)");
    if (!prepared()) { fault(Fault::NeverPrepared); return; }
    if (computing_) { fault(Fault::CycleWithoutTap); return; }
    computing_ = true;
    refresh_auto_sources();
    for (auto* pin : pins())
      if (pin->pull == Pull::Auto) pin->start(frames);
    on_start(frames);
    computing_ = false;
  }

  float next() {
    if (computing_) { fault(Fault::CycleWithoutTap); return cur_; }
    if (ctx_.clock) {
      const Tick t = ctx_.clock->now;
      if (t == lastTick_) return cur_;
      lastTick_ = t;
    } else if (!prepared()) {          // the cold branches: no host clock
      fault(Fault::NeverPrepared);
      return cur_;
    } else {
      fault(Fault::Clockless);         // a bare node in a test, or a host that forgot its clock
    }
    prev_ = cur_;
    computing_ = true;
    for (auto* s : auto_sources()) s->next();
    cur_ = compute();
    computing_ = false;
    return cur_;
  }
  // step() unchanged except the never-prepared guard: if (!prepared()) { fault(NeverPrepared); return cur_; }

  // The faults this node has reported since the host last cleared them.
  // Read and cleared by the host after a render (collect_faults), never on
  // the audio thread.
  unsigned char faults() const { return faults_.load(std::memory_order_relaxed); }
  void clear_faults() { faults_.store(0, std::memory_order_relaxed); }

protected:
  // Report from a render path. A relaxed OR: single writer per node on the
  // audio thread, one reader after the render; no lock, nothing on the
  // common path.
  void fault(Fault f) { faults_.fetch_or(static_cast<unsigned char>(f), std::memory_order_relaxed); }

  ValueSource& operator=(const ValueSource& o) {
    Component::operator=(o);
    cur_ = o.cur_;
    prev_ = o.prev_;
    faults_.store(o.faults_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    return *this;
  }

private:
  // report_cycle_once_() and cycleReported_ are gone.
  std::atomic<unsigned char> faults_{0};
};
```

`engine/include/mforce/core/component.h`:

```cpp
  // False when the node has no Param or Input of that name, or (in an
  // override) the part is of the wrong kind for the slot. The builder turns
  // false into a load failure; nothing is ignored any more.
  virtual bool set_param(std::string_view name, std::shared_ptr<ValueSource> src) {
    if (auto* p = find<Param>(name)) { p->set(std::move(src)); return true; }
    if (auto* i = find<Input>(name)) { i->set(std::move(src)); return true; }
    return false;
  }
  virtual bool add_param(std::string_view name, std::shared_ptr<ValueSource> src) { return set_param(name, std::move(src)); }
  virtual bool clear_param(std::string_view name) { return set_param(name, nullptr); }
```

`engine/include/mforce/render/instrument.h`:

```cpp
struct FaultReport { std::string nodeId; const char* type; unsigned char faults; };

struct Instrument : MonoSource {
  // The faults every node of this instrument reported since the last call,
  // one entry per node id (a pooled voice's clones are one node to the
  // reader), cleared as they are read. Control-side only.
  virtual void collect_faults(std::vector<FaultReport>&) {}
};
// PitchedInstrument: for each voice, for each (id, node) in nodesById: OR
// into the entry for id, then node->clear_faults(). DrumKit: its sources.
```

The loader's failure texts, exact: `node 'osc1' (SineSource): unknown key 'frequncy'` (the descriptor check); `node 'add1' (AdditiveSource): 'partials' does not accept a SineSource` (a false `set_param` from the descriptor loop — the source's `type_name()`); `node 'osc1' (SineSource) has no pin 'freq'` (a false `set_param` from one of the loader's direct, hand-named calls) — each thrown as `std::runtime_error` from `wire_params_generic(src, id, params, valueNodes)`, which gains the id as its second argument, or from the direct call's site.

The CLI's block after the stats, exact shape:

```
[faults] osc1 (SineSource): frequency clamped
[faults] tab1 (WavetableSource): table length clamped
```

Nothing is printed when no node reported.

---

### Task 0: Measure; keep the base

**Files:**
- Create: `tools/measure_25.py`, `docs/architecture/measurements/2026-10-20-no-throw-and-wires.md`
- Modify (measurement only, reverted in Task 1): `engine/include/mforce/source/delay_line_source.h` (a counter where `i0 == ring_`), `tools/mforce_cli/main.cpp` (the `[wrap] delayLineWraps=N` printer, the 2.4 pattern)
- Modify (if the list has tracked members): the listed patch files under `patches/library`, `patches/baselines`, `patches/audition`, `patches/pending`

**Interfaces:**
- Produces: **the unknown-key list** (every patch the current loader warns about, with the keys: the patches Task 3 makes unloadable); **the non-loaders classified** (the 157 by error line); **the wrap list** (every patch whose DelayLine read hits the ring length: the renders Task 4 changes); `cli_pre25.exe`.

- [ ] **Step 0: Keep the pre-checkpoint binary.** `git diff 403269f HEAD --stat -- engine tools/mforce_cli tools/engine_tests tools/mforce_ui` prints nothing; `python tools/gates.py --fast` passes (known roundtrip failure only); `copy build\tools\mforce_cli\Release\mforce_cli.exe renders\scratch\ab_tree\cli_pre25.exe`. Record `git rev-parse HEAD` in the Ledger as the A/B base. The eight set files are 2.4's under `renders/scratch/ab24_tree/*.txt`; copy them to `renders/scratch/ab25_tree/` and, for each set, copy `ab24_tree/<set>/new/hashes.txt` to `ab25_tree/<set>/old/hashes.txt` (ruling 12).
- [ ] **Step 1: The wrap counter.** `delay_line_source.h` `compute()`: after `const int i0 = int(rp);` add `if (trace && i0 == ring_) ++wraps;` with `static inline bool trace{false}; static inline long long wraps{0};` on the struct; `mforce_cli/main.cpp` `main()`: the switch from `MFORCE_TRACE_WRAP` and an RAII printer `[wrap] delayLineWraps=%lld` on every exit path (as 2.4's `TapTracePrint`). Build the CLI.
- [ ] **Step 2: The driver.** `tools/measure_25.py --set <file> [--set ...] [--jobs 3] [--cli <exe>] --out <doc>`: renders every patch of the sets with `MFORCE_TRACE_WRAP=1`, keeps stderr, and writes the document with four sections: (a) "N patches the loader warns about" — every patch with a `[load] <Type>: unknown param '<key>'` line, grouped by set, with the (type, key) pairs; (b) "M patches that do not load", each with its first `ERROR:` line, grouped by the error's leading words (`Unknown node type`, `Unresolved node ref`, …) — a patch whose error is a render-path throw (`non-positive frequency`, `invalid frequency for table fill`) is called out: it will render after Task 1 and is on the expected-change list; (c) "K patches whose DelayLine read hits the ring length" with the count — Task 4's list; (d) a "Lint cross-check" heading. The driver reuses 2.4's `measure_taps.py` shape (`git show a8d5aa9:tools/measure_taps.py`).
- [ ] **Step 3: Run.** `python tools/measure_25.py --jobs 3 --cli build/tools/mforce_cli/Release/mforce_cli.exe --set renders/scratch/ab25_tree/{library,baselines,pending,audition,sweep,old,taps,retired}.txt --out docs/architecture/measurements/2026-10-20-no-throw-and-wires.md`. Expected: (a) library 0 and baselines 0 (the gate corpus "loads warning-free" by the loader's own comment); some in sweep and old; (b) the 157 as before, 114 of them `Unknown node type: FitzhughNagumoSource`; the remaining 43 named with their error; (c) `double_advance_ok` on the wrap list (2.2 §7), possibly others. The null gate stays 78/78: the counter is inert without the switch.
- [ ] **Step 4: Lint cross-check.** `python tools/lint_patches.py patches/library`, `patches/baselines`, `patches/audition`, `patches/pending` → the document's section (d): the lint tool's findings against the loader's (a); any patch the lint names and the loader does not, or the reverse, is explained (the lint's scraped allowlist versus the loader's `kStructural`); the loader is the authority.
- [ ] **Step 5: Repair the tracked members of list (a).** For each patch under `patches/library` or `patches/baselines` on list (a) — expected none — and each under `patches/audition` or `patches/pending` (Matt's queues, gitignored): remove the unknown key from the file (it was ignored, so the render cannot move); re-render with `cli_pre25.exe` before and after the edit and confirm the WAV hash is identical; record each in the Ledger. `sweep/` and `old/` are left as they are: they stop loading at Task 3 and are listed (old/ is never deleted; sweep/ is regenerable).
- [ ] **Step 6: Commit** — `git add docs/architecture/measurements/2026-10-20-no-throw-and-wires.md tools/measure_25.py engine/include/mforce/source/delay_line_source.h tools/mforce_cli/main.cpp` plus any repaired tracked patch; `git commit -m "measure: the unknown-key, non-loader and DelayLine-wrap lists for checkpoint 2.5 (the counter and its driver go with the next commit)"`.

---

### Task 1: No throw on a render path

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (`prepare()` throws on a zero rate; the never-prepared guard in `start()`, `next()`, `step()`; `Fault`, `fault_name`, `faults()`, `clear_faults()`, `fault()`, `faults_` — the enum and the byte land here because the clamps need them), `engine/include/mforce/core/dsp_wave_source.h:28-29`, `engine/src/wavetable_source.cpp:26-29`, `engine/include/mforce/source/mesh2d_source.h:133`, `engine/include/mforce/source/delay_line_source.h` (the counter removed), `tools/mforce_cli/main.cpp` (the printer removed), `tools/engine_tests/main.cpp`
- Delete: `tools/measure_25.py`

**Interfaces:**
- Produces: `enum class Fault`, `const char* fault_name(Fault)`, `unsigned char ValueSource::faults() const`, `void clear_faults()`, protected `void fault(Fault)`; `prepare()` throwing `std::invalid_argument` on `sampleRate <= 0`.

- [ ] **Step 1: Write the failing tests** — `tools/engine_tests/main.cpp`, after `run_tap_reading_tests`:

```cpp
static void run_no_throw_tests() {
    TestHost h;
    // A frequency driven to zero mid-note renders with the pin's minimum and
    // reports, instead of terminating the process from the audio thread.
    auto f = std::make_shared<ConstantSource>(440.0f);
    auto osc = std::make_shared<SineSource>();
    osc->set_param("frequency", f);
    osc->prepare(h.ctx); osc->start(48000);
    h.clock.tick(); osc->next();
    CHECK(osc->faults() == 0);
    f->set(0.0f);
    h.clock.tick();
    float v = 0.0f;
    bool threw = false;
    try { v = osc->next(); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK(std::isfinite(v));
    CHECK((osc->faults() & static_cast<unsigned char>(Fault::FrequencyClamped)) != 0);
    f->set(440.0f);
    h.clock.tick(); osc->next();                      // computes normally again
    osc->clear_faults();
    CHECK(osc->faults() == 0);
    // A table fill at a frequency above the rate fills one sample and reports.
    auto wt = std::make_shared<WavetableSource>();
    wt->set_param("frequency", std::make_shared<ConstantSource>(96000.0f));
    wt->set_param("inputSource", std::make_shared<ConstantSource>(0.5f));
    wt->prepare(h.ctx); wt->start(4800);
    h.clock.tick();
    threw = false;
    try { wt->next(); } catch (...) { threw = true; }
    CHECK(!threw);
    CHECK((wt->faults() & static_cast<unsigned char>(Fault::TableClamped)) != 0);
    // A node no host prepared: start() and next() report, nothing divides by zero.
    auto dl = std::make_shared<DelayLineSource>();
    dl->set_param("source", std::make_shared<ConstantSource>(1.0f));
    dl->start(4800);                                  // Debug: the assert fires — build the harness Release, or guard with NDEBUG
    CHECK((dl->faults() & static_cast<unsigned char>(Fault::NeverPrepared)) != 0);
    CHECK_NEAR(dl->next(), 0.0f, 1e-6f);              // served, not computed
    // prepare() with no rate is a host error and throws where throwing is allowed.
    threw = false;
    try { dl->prepare(RenderContext{0}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    CHECK(std::string(fault_name(Fault::FrequencyClamped)) == "frequency clamped");
}
```

The never-prepared block runs under `#ifdef NDEBUG` (the harness's Release build; the Debug assert is the Debug contract and stays). Register `run_no_throw_tests();` after `run_tap_reading_tests();`.

- [ ] **Step 2: Run to verify it fails** — build `engine_tests`: fails to compile (`faults`, `Fault`, `fault_name` undefined).
- [ ] **Step 3: The contract, part 1** — `dsp_value_source.h` as "The contract" shows for `Fault`, `fault_name`, `faults()`, `clear_faults()`, `fault()`, `faults_`, `operator=`, the zero-rate throw and the never-prepared guards (the cycle guard and the clockless fault are Task 2's; leave `report_cycle_once_` for now). `dsp_wave_source.h:28-29`: `float f = frequency.current(); if (f <= 0.0f) { f = frequency.min; fault(Fault::FrequencyClamped); }` — `frequency.min` is the pin's declared minimum (0.01). `wavetable_source.cpp:26-29`, the old condition kept, the throw replaced:

```cpp
  const float ratio = currFreq_ > 0.0f ? float(sample_rate()) / currFreq_ : 0.0f;
  int len = 1;
  if (ratio >= 1.0f) len = int(std::lround(ratio));
  else fault(Fault::TableClamped);
```

(`HybridKSSource::fill_table` already clamps to one; it gains the same fault on the same condition.) `mesh2d_source.h:133`: `sr_ = float(sample_rate());` — the fallback goes. Remove the Task 0 counter and printer; `git rm tools/measure_25.py`.
- [ ] **Step 4: Verify** — `engine_tests` passes; `git grep -n "throw" -- engine/include/mforce/core/dsp_wave_source.h engine/src/wavetable_source.cpp` lists nothing; `git grep -n "48000" -- engine/include/mforce/source/mesh2d_source.h` lists only the comment at :33; `python tools/null_gate_perform_source.py --jobs 3` → 78/78 (no stored patch reached either throw); `python tools/gates.py --fast` (CLI and UI rebuilt).
- [ ] **Step 5: Commit** — `git add -u engine tools/engine_tests/main.cpp tools/mforce_cli/main.cpp tools/measure_25.py`; `git commit -m "core: start() and next() never throw — a clamped value and a fault bit; prepare() throws on a zero rate; the measurement goes"`.

---

### Task 2: The fault bits reach the host; the cycle report is a bit

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (the cycle fault, the re-entry guards in `prepare`/`start`, the clockless fault; `<cstdio>` gone), `engine/include/mforce/render/instrument.h` (`FaultReport`, `Instrument::collect_faults`, `PitchedInstrument::collect_faults`, `DrumKit::collect_faults`), `tools/mforce_cli/main.cpp:80-137` (`run_patch`'s stats), `tools/mforce_ui/main.cpp` (Generate's status at the `generate_unified` return; `--gencheck` prints the block; the live pool polled in the frame loop beside the transport status draw at `:8817`), `tools/engine_tests/main.cpp`

**Interfaces:**
- Consumes: Task 1's `Fault`, `fault()`, `faults()`, `clear_faults()`.
- Produces: `struct FaultReport { std::string nodeId; const char* type; unsigned char faults; }`; `virtual void Instrument::collect_faults(std::vector<FaultReport>&)`.

- [ ] **Step 1: Write the failing tests**:

```cpp
static void run_fault_tests() {
    TestHost h;
    // A cycle with no tap: the previous value, a bit, no print, no overflow —
    // through next(), and through prepare() and start() wired before the host calls.
    auto loop = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                 std::make_shared<ConstantSource>(0.0f),
                                                 CombineOp::Sum, 0.0f);
    loop->prepare(h.ctx); loop->start(16);
    loop->set_param("source2", loop);
    h.clock.tick(); CHECK_NEAR(loop->next(), 1.0f, 1e-6f);
    CHECK((loop->faults() & static_cast<unsigned char>(Fault::CycleWithoutTap)) != 0);
    loop->clear_faults();
    loop->prepare(h.ctx);                              // re-entered through the cycle: returns, no overflow
    loop->start(16);
    CHECK((loop->faults() & static_cast<unsigned char>(Fault::CycleWithoutTap)) != 0);
    loop->clear_param("source2");
    // A clockless pull on a prepared node reports; the host is the only reader.
    auto bare = std::make_shared<Counter>();
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next();
    CHECK((bare->faults() & static_cast<unsigned char>(Fault::Clockless)) != 0);
    // The host collects by node id, once per id across the pool, and clears.
    const char* json = R"({"sampleRate":48000,"instrument":{"polyphony":2},
      "graph":{"output":"osc","nodes":[
        {"id":"osc","type":"SineSource","params":{"frequency":0.0,"amplitude":1.0}}]}})";
    auto ip = load_instrument_patch_json(json);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    pi->play_note({60.0f, 0.8f, 0.1f, 0.0f, false}, 0.0f);      // as the hold tests do
    auto b = render_all(ip, 4800);                               // the file's helper
    std::vector<FaultReport> faults;
    ip.instrument->collect_faults(faults);
    CHECK(faults.size() == 1);
    CHECK(faults[0].nodeId == "osc");
    CHECK((faults[0].faults & static_cast<unsigned char>(Fault::FrequencyClamped)) != 0);
    faults.clear();
    ip.instrument->collect_faults(faults);
    CHECK(faults.empty());                             // cleared as read
}
```

`play_note` and `render_all` are what the hold tests near line 855 use; a numeric `frequency` is a `ConstantSource` by the loader's `resolve_param`. Register after `run_no_throw_tests();`.

- [ ] **Step 2: Run to verify it fails** — fails to compile (`collect_faults`, `FaultReport`).
- [ ] **Step 3: The contract, part 2** — `dsp_value_source.h`: the cycle fault in `next()`, the guards in `prepare()`/`start()`, the clockless branch, `report_cycle_once_` / `cycleReported_` / `<cstdio>` deleted. `instrument.h`: `FaultReport`, the virtual, `PitchedInstrument::collect_faults` (for each voice in `voicePool`, for each `(id, node)` in `nodesById`: if `node->faults()`, OR into the entry for `id` (type from `type_name()`), then `node->clear_faults()`; entries in id order), `DrumKit::collect_faults` over its sources. `mforce_cli/main.cpp` `run_patch` (and the other render modes that build an `InstrumentPatch`: `--chords`, `--melody`, `--play`): after the stats, `collect_faults` and print the block exactly as "The contract" shows. `mforce_ui/main.cpp`: after `generate_unified` returns in Generate and in `--gencheck`, collect from the generate instrument and write `faults: osc (SineSource) frequency clamped; …` to the status line (gencheck prints it); the live pool: once a frame in the main loop, `collect_faults` on the live instrument and, when non-empty, set the status line once (sticky until the next load or Generate).
- [ ] **Step 4: Verify** — `engine_tests` passes; `git grep -n "fprintf\|cstdio" -- engine/include/mforce/core/dsp_value_source.h` lists nothing; `python tools/null_gate_perform_source.py --jobs 3` → 78/78; `python tools/gates.py --fast` (CLI and UI rebuilt); the CLI on a zero-frequency JSON (write `renders/scratch/ab25_tree/zero_freq.json` = the test's JSON) prints the `[faults]` block; on `patches/library/winds/oboe1.json` prints none; `mforce_ui --gencheck` on the same file prints it.
- [ ] **Step 5: Commit** — `git commit -m "core, render, hosts: fault bits reach the host — the cycle report is a bit, a clockless or never-prepared pull reports, the CLI and the UI say so after a render"`.

---

### Task 3: A rejected wire fails the load

**Files:**
- Modify: `engine/include/mforce/core/component.h:31-45`, the six overrides (`multi_source.h`, `additive/additive_source2.h:64-70`, `additive/formant.h`, `additive/full_additive_source.h`, `additive/partials.h`, `source/wavetable_source.h`), `engine/src/patch_loader.cpp:214-250` (`wire_params_generic` gains the id; false throws), `:365-420` (the unknown-key block throws), every `wire_params_generic(` call site (:581, :621, :665, :677, :689, :702, :740, :749, :792, :838, :850, :902 — the id is in scope at each), the three direct `set_param` calls that can fail (`:705` `cutoffFreq`, `:1051`, `:1058`, `:1121`): their result is checked and thrown on false with the same text; `tools/mforce_ui/main.cpp` (the editor's own `set_param` calls: results ignored — the editor wires from descriptors, so false cannot happen; a comment says so), `tools/engine_tests/main.cpp`

**Interfaces:**
- Consumes: nothing new.
- Produces: `bool Component::set_param(...)`, `bool add_param`, `bool clear_param`; the loader's three failure texts.

- [ ] **Step 1: Write the failing tests**:

```cpp
static void run_wire_rejection_tests() {
    auto osc = std::make_shared<SineSource>();
    CHECK(osc->set_param("frequency", std::make_shared<ConstantSource>(1.0f)));
    CHECK(!osc->set_param("freq", std::make_shared<ConstantSource>(1.0f)));         // no such pin
    auto add = std::make_shared<FullAdditiveSource>();                              // registry name "AdditiveSource"
    CHECK(!add->set_param("partials", std::make_shared<SineSource>()));              // wrong kind for the slot
    CHECK(add->set_param("partials", std::make_shared<FullPartials>(/* as the additive tests near line 3315 build one */)));
    auto bad_key = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"osc","type":"SineSource","params":{"frequncy":440.0}}]}})";
    std::string msg;
    try { load_instrument_patch_json(bad_key); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("node 'osc' (SineSource): unknown key 'frequncy'") != std::string::npos);
    auto bad_kind = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
        {"id":"s","type":"SineSource","params":{"frequency":440.0}},
        {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"partials":{"ref":"s"}}}]}})";
    msg.clear();
    try { load_instrument_patch_json(bad_kind); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("node 'add' (AdditiveSource): 'partials' does not accept a SineSource") != std::string::npos);
}
```

`AdditiveSource` is `FullAdditiveSource`'s registry name; its `partials` slot is wired through the descriptor loop (a Slot registers an input descriptor), unlike `AdditiveSource2`'s `partials`, which is a numeric array read by a hand-written branch. Register after `run_fault_tests();`.

- [ ] **Step 2: Run to verify it fails** — fails to compile (`set_param` returns `void`).
- [ ] **Step 3: The change** — `component.h` as "The contract"; the six overrides return `bool` (true when the override placed the part; false when the part is of the wrong kind — `additive_source2.h:67` `if (!p) return false;`; the base's result when the name is not the override's); the loader: `wire_params_generic(ValueSource& src, const std::string& id, const json& params, …)`; each `set_param`/`add_param` result checked and, on false, `throw std::runtime_error("node '" + id + "' (" + src.type_name() + "): '" + name + "' does not accept a " + source->type_name())`; the unknown-key block's `fprintf` becomes `throw std::runtime_error("node '" + id + "' (" + src.type_name() + "): unknown key '" + key + "'")` (the `warned` set and its comment go; the allowlist stays and its comment says it is the set of keys the hand-written branches consume). The call sites pass the id they already hold.
- [ ] **Step 4: Verify** — `engine_tests` passes; `git grep -n "value ignored" -- engine` lists nothing; `python tools/null_gate_perform_source.py --jobs 3` → 78/78 (Task 0 said no manifest patch has an unknown key; if one does, Task 0 step 5 repaired it); `python tools/gates.py --fast`; the UI opens `oboe1`, plays, Generates.
- [ ] **Step 5: The load A/B** — for each of the eight set files: `python tools/ab_render_set.py render renders/scratch/ab25_tree/<set>.txt renders/scratch/ab25_tree/<set> t3` (the build); `compare <set> old t3`: `DIFFER` must be 0 for every set (nothing valid moves); the patches "present in only one arm" (rendered by `old`, failed in `t3`) are the ones that stopped loading — every one is on Task 0's list (a) or is a kind mismatch named in the Ledger with its node, pin and reason (the class the old loader could not see); any other is a bug. Record the counts per set.
- [ ] **Step 6: Commit** — `git commit -m "core, loader: a rejected wire says so — set_param returns false, an unknown key or a wrong kind fails the load with the node, the pin and the reason"`.

---

### Task 4: The DelayLine reads its ring

**Files:**
- Modify: `engine/include/mforce/source/delay_line_source.h:37-43` (`on_prepare`: `buf_.assign(size_t(want), 0.0f)`), `:76-90` (`compute()`: `int i0 = int(rp); if (i0 >= ring_) i0 -= ring_;` the comment about the slot goes), `:135` (the member comment), `tools/null_gate_manifest.json`, `tools/engine_tests/main.cpp`

**Interfaces:**
- Consumes: Task 0's wrap list.
- Produces: the correct read; the refrozen manifest.

- [ ] **Step 1: Write the failing test**:

```cpp
static void run_delay_line_wrap_tests() {
    // A read position a hair under the write index rounds to exactly the
    // ring length in float; the read is slot 0, not a slot past the ring.
    RenderContext ctx{48000};
    auto dl = std::make_shared<DelayLineSource>();
    dl->set_param("frequency", std::make_shared<ConstantSource>(48000.0f / 15.000002f));   // len = 15.000002
    dl->set_param("source", std::make_shared<ConstantSource>(1.0f));
    dl->prepare(ctx); dl->start(4800);
    float out = 0.0f;
    for (int i = 0; i < 16; ++i) out = dl->next();   // the 16th pull: writeIdx_ 15, rp = -0.000002 + ring_ == float(ring_)
    CHECK_NEAR(out, 1.0f, 1e-3f);                     // slot 0 holds the first write (1.0); the old zero slot gave 0
}
```

If the constants do not hit the case on this platform's float, the test finds a `len` that does (`writeIdx_ - len` in `(-2^-20, 0)` with `ring_ = 2404`) and the Ledger records the values; the mechanism, not the constants, is the contract.

- [ ] **Step 2: Run to verify it fails** — `out == 0`.
- [ ] **Step 3: The change** — as Files says; `capacity()` unchanged (`ring_`).
- [ ] **Step 4: Verify** — `engine_tests` passes; `python tools/null_gate_perform_source.py --jobs 3`: the differing entries must be exactly the manifest members of Task 0's wrap list (`double_advance_ok` expected); then `--freeze`, rerun → 78/78. The tree A/B: render `t4` over the eight sets with this build, `compare <set> t3 t4`: `DIFFER` = the wrap list's members in that set, nothing else; a listed patch that does not differ is noted (the case fired but the slot's zero equalled slot 0's value — a zero line); `python tools/gates.py --fast`.
- [ ] **Step 5: Commit** — `git add -u engine tools/engine_tests/main.cpp tools/null_gate_manifest.json`; `git commit -m "nodes(delay): the read wraps to slot 0; the extra zero slot goes; the measured renders change"`.

---

### Task 5: Close the checkpoint

**Files:**
- Create: `docs/autonomy/refactor/reports/2026-10-20-checkpoint-2.5.md`
- Modify: `docs/autonomy/refactor/REVIEW.md`, `docs/autonomy/refactor/BACKLOG.md`, `docs/autonomy/STATUS.md`, `tools/structure/metrics.json`, `docs/architecture/part2-valuesource-contract.md` (section 6: "Decided …: landed as checkpoint 2.5", section 7 row 2.5's cell), this plan's Ledger

- [ ] **Step 1: Nothing old remains** — `git grep -n "throw" -- engine/include/mforce/core engine/include/mforce/source engine/include/mforce/filter engine/src/wavetable_source.cpp engine/src/hybrid_ks_source.cpp` lists only `prepare`'s and the registry's (load-time); `git grep -n "fprintf" -- engine/include/mforce/core` lists nothing; `git grep -nw "ignored" -- engine/src/patch_loader.cpp` lists only the paramMap and score notes that are not wires; `git grep -n "MFORCE_TRACE_WRAP\|delayLineWraps" -- engine tools` lists nothing.
- [ ] **Step 2: Every gate** — `python tools/gates.py` (all): everything passes except the two roundtrip gates on the six backlog-79 patches (`double_advance_ok`'s roundtrip diff may move with the wrap fix — note the set). `python tools/null_gate_wiring.py` 78/78.
- [ ] **Step 3: The tree A/B** — `compare <set> old t4` over the eight sets (and again after the fix wave if it touches engine code): `DIFFER` = the wrap list; "present in only one arm" = the unloadable list; the Appendix A table lists each set (in the set, rendered by both, differ, stopped loading).
- [ ] **Step 4: Timing** — `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre25.exe build/tools/mforce_cli/Release/mforce_cli.exe 7` on an idle machine; expected within 2 percent on every row (the clamp branch replaces the throw branch; the fault store sits on cold branches); a larger cost is recorded, not chased.
- [ ] **Step 5: Meter and delta** — `python tools/structure/check.py --snapshot`; `--delta <Task 0 sha>`.
- [ ] **Step 6: Independent review** — a fresh Fable reviewer on the checkpoint's commits with `docs/architecture/REVIEW_BRIEF.md`, the diff, `MODULES.md`, Part 2 section 6 as the signed design and this plan's Review Focus, told to refute, read-only; fix what is right in one wave; the same reviewer re-reviews. Verbatim into section 8.
- [ ] **Step 7: Architecture report R6** — the ten-section format: section 2 entries for `Fault` / `fault_name` / the fault byte (the design choice: an atomic byte per node, relaxed, set on the fault path only; alternatives: a plain byte — a data race with the UI's live poll — and a per-host counter, which loses the node), `prepare`'s throw and the two guards, the two clamps (the narrowest value that makes the old condition false, so no non-terminating render moves), `set_param`'s `bool` and the loader's three texts, `collect_faults` and the two hosts' reports, the DelayLine wrap; section 3 the interfaces (`set_param`/`add_param`/`clear_param` → `bool`, `wire_params_generic`'s id, `collect_faults`, `faults()`/`clear_faults()`); section 4 duplicates (the warning block and the failure are one block; `HybridKSSource`'s and `WavetableSource`'s length clamps are twins — Part 3's); section 5 the meter delta, the three lists against the A/B (nothing valid moved; what stopped loading, by set, each on list (a) or a named kind mismatch; the wrap list's renders), the timing table; section 6 the Ledger; section 7 the debt (`fill_table` still allocates inside `next()` — 2.6; mixer-mode graphs' faults are not collected — Part 4's mixer MODE; the UI's editor path still drops an unknown key on load and never writes it back — Part 4's codec; `Fault::Clockless` is set by every bare test node and read by nobody there; the Debug assert and the Release fault for a never-prepared node are two contracts for one error until Part 5 decides the host side); section 9 the reading guide; section 10 three lessons: (1) an exception on the audio thread calls `std::terminate` — why a bit replaces a throw, and the C# analogue (an unhandled exception on a worker thread), (2) a lock-free atomic as a one-byte mailbox: `memory_order_relaxed`, single writer, why no mutex and why not a plain byte (a data race is undefined behaviour in C++ even for a byte, unlike the "tearing" story a C# reader knows), (3) a `bool` return against an exception at a boundary: the node answers a question, the builder decides it is an error — and why the builder throws rather than returning a status up twenty call sites. REVIEW R6 awaiting with its verdict items: (0) the patches that stopped loading (by set; `sweep/` and `old/` left as they are, listed) accepted, or any to be repaired; (1) the wrap list's renders as frozen truth (`double_advance_ok` refrozen); (2) the fault reports' shape (the CLI block, the UI status line) — enough, or a count on the stamp too; (3) `prepare()` throwing on a zero rate as the host contract. BACKLOG row 1: 2.5 DONE, 2.6 next; STATUS handoff.
- [ ] **Step 8: Commit** — `git commit -m "docs: checkpoint 2.5 report (R6), REVIEW R6 awaiting, backlog, STATUS handoff, meter snapshot"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **A fault is a per-node atomic byte**, set with a relaxed `fetch_or` on the fault path only and read and cleared by the host after a render. Alternative, a plain byte: the UI's live poll reads it from another thread while the audio thread writes, a data race (undefined behaviour) rather than a benign tear. Alternative, a per-host counter: loses which node faulted, which is the whole report. Cost: one byte per node; nothing on the per-sample path.
2. **The clamp is the narrowest value that makes the old throw's condition false**: a non-positive frequency becomes the pin's declared minimum (the spec's "clamped to the node's declared range"); a table length below one becomes one (as `HybridKSSource` already does). Not the pin's maximum: a frequency above 20 kHz never threw, and clamping it would move renders the spec does not list. Every render that did not terminate is unchanged by construction.
3. **`prepare()` throws on a zero rate**, where the spec allows a throw, and Mesh2D's 48000 fallback goes: a zero rate is a host error, and one rule in the base is better than one fallback per node. No test prepares with a zero rate (grep).
4. **A never-prepared node faults and does not compute.** `start()` sets the bit and skips `on_start`; `next()` and `step()` serve `cur_`; the Debug assert stays as the Debug contract. The check sits on the clockless branch, so a prepared node in a host pays nothing. Alternative, let `on_start` run: the Release build divides by zero in DelayFilter, Limiter, Reverb, the Butterworth filters, Vibrato.
5. **A clockless pull sets `Fault::Clockless`** on the cold branch. A bare node in a test sets it and nobody reads it there; a host reads it after a render and reports the node that was prepared without its clock. Alternative, a Debug assert: every direct pull in `engine_tests` would need a ticking host (2.3 ruling 2).
6. **The tap-free cycle is a bit, and the guard covers the host calls.** `report_cycle_once_`, `cycleReported_` and `<cstdio>` go; `prepare()` and `start()` re-entered through a cycle return with the bit instead of overflowing. `computing_` after a throw is retired with the throws.
7. **`set_param` returns `bool`; the builder throws.** The node answers "did that wire land"; the loader, which holds the id, turns false — and an unknown key — into `std::runtime_error` with node, pin and reason. Alternative, a `set_pin` beside `set_param` returning a result struct: two doors for one act until Part 3's pins; the six overrides are touched either way. The UI's editor wires from descriptors and cannot produce false; its own JSON path (which drops unknown keys on load) is Part 4's codec.
8. **The unknown-key list is measured with the current loader**, whose warning is the authority (its `kStructural` allowlist is what the hand-written branches consume); `lint_patches.py` is the cross-check. Kind mismatches cannot be seen by the current loader: the load A/B finds them, and each is named in the Ledger.
9. **Tracked patches on the unknown-key list are repaired in Task 0** (the key was ignored, so removing it cannot move the render; the hash proves it); `sweep/` and `old/` are left as they are and stop loading at Task 3, listed. `old/` is never deleted (CLAUDE.md); a file that no longer loads is still the record of what was tried.
10. **The DelayLine wrap lands in its own task with its own measured list** (R3 item 0: "stays listed for 2.5"); the counter is committed in Task 0 and deleted in Task 1 (2.4's pattern); `double_advance_ok` is refrozen with any other listed manifest member.
11. **Reports, by host.** The CLI prints a `[faults]` block after the stats (one line per node id); the UI's Generate and `--gencheck` write the status line; the live pool is polled once a frame from the UI thread and the status line is set once per fault set. Mixer-mode graphs are not collected: mixer MODE is Part 4's (2.3 ruling 8).
12. **Landing order and the old arms.** Task 1 (no throw) and Task 2 (bits to the host) are byte-identical; Task 3 moves nothing valid and lists what stops loading; Task 4 is the one task whose renders change. 2.4's sealed `new` arms are 2.5's `old` (the engine is unchanged since `403269f`, verified by diff-stat); each task's arm is compared to the one before it.
13. **Timing is measured at the close against `cli_pre25.exe`**; the hot path trades a throw branch for a clamp branch and gains nothing else; a cost beyond noise is reported, not chased (R4 item 0's class).

(Execution rulings follow here.)

- **Task 0:** A/B base `2954f7d`; `cli_pre25.exe` = the build at the base (the 2.4 engine, diff-stat empty); the eight `old` arms are 2.4's sealed `new` arms with the retired pair's path rewritten to the 10-19 folder. The lint cross-check found what the loader cannot: `VarSource.value` in the 14 bwg_perc1/bwg_perc2 patches — VarSource is built by a hand-written branch that never reaches the unknown-key block inside `wire_params_generic`. Ruling: list (a) is the union of the loader's warnings and the lint's node-key findings, and Task 3 runs the unknown-key check once per node in `build_graph`, whichever path built it. The 14 (all `value: 0.0`, the branch's `val` default) were repaired byte-surgically and re-rendered: 14 of 14 identical to 2.4's arm. The lint's paramMap range notes and the retired `instrument.release` key are not node keys (Part 4's). The measurement: list (a) = 1 (`old/baselines_culled_2026-09-14/ks_morph_saw_test.json`); 157 non-loaders = 155 unknown node types (114 fhn) + 2 sweep manifests without a graph, none a render-path throw; the wrap list = 28 (both `double_advance` baselines at 1,470 reads, the 14 bwg, stk_bandedwg bowl_struck 5,852, the three stk_blowhole, five with 1–6 reads). Gates at step 0 not re-run (they ran at 403269f on the identical engine).
- **Task 1, the test TU:** `engine_tests` failed with C1128 once the fault header and `run_no_throw_tests` landed (the 6,000-line test file crossed MSVC's COFF section limit); `/bigobj` for MSVC in its CMakeLists — the compiler's own answer, no code change; Part 8 owns splitting the file.
- **Task 1, the first null gate was 72/78** (piano_bright/dark/default/seg, viola_default, perform/wiring_setting): `PitchedInstrument::prepare` never prepared the push-binding chains (the dynamicPins sources, stepped at a note's Setup and never pulled by the root), which computed anyway because a CurveNode needs no rate — until the never-prepared guard served the memo. Ruling: the voice prepares every chain with its context, beside the root, the tails and the perform source (a 2.2 contract hole the fault was built to find). All six sources are rate-free CurveNodes on the perform frequency with no other consumer, so preparing them moves nothing: 78/78.
- **Task 1, ruling 4 withdrawn for `next()` and `step()`:** 29 assertions in 13 pre-existing tests failed under "a never-prepared node serves `cur_`", every one a bare rate-free node pulled without prepare (a ConstantSource, a CurveNode, a PerformOut, a NameGate, a TapSource, a CombinedSource wired after the host calls) — the bare-node idiom 2.3's ruling 2 kept on purpose, and one `ensure_constant` leans on at load. Now `next()`/`step()` on a never-prepared node FAULT and compute as before; `start()` faults and skips `on_start` (the 2.2 §7 division sites). A rate-using node pulled unprepared can still misbehave in `compute()`, as today, and the fault names it when the host survives to read it; Part 3's nodes own their own guards (section 7). The test's never-prepared block changed with it.
- **Task 2, `collect_faults` lives on `MonoSource`** (mixer.h) with `FaultReport` and `fault_report_text`, not on `Instrument`: the CLI's `run_patch` renders every patch through `Patch::mixer`, whose channels hold `MonoSource`s, so the mixer walks its channels without a cast and the loader's mixer-mode adapters inherit the no-op (Part 4 gives them ids).
- **Task 2, the meter's known-answer test** pins the UI's `main()` at 1712 lines and the gencheck mode lives inside `main()`; its one-line faults print re-pinned it at 1713, as 2.3's ruling 21 did (the frozen fixture is Part 8's).
- **Task 2, `prepare()` does not clear the faults.** A scored patch is pre-rendered at load (the faults set then) and the mixer's `prepare()` re-prepared the instrument's nodes afterwards, which wiped the bits before the CLI could read them; the plan's contract had the clear and was wrong — a second prepare must change nothing a host can see. Faults persist until the host reads them.
- **Task 2, both loader paths fill `VoiceGraph::nodesById`.** The mixer path's voice loop left it empty ("Empty for mixer-path instruments"), so `collect_faults` had nothing to walk on the CLI's main path while `--gencheck` (the instrument path) reported; one moved map per voice, as the instrument path already paid.
- **Task 3, the allowlist is the union of the old warning's list and the lint's scrape.** The per-node check, now reaching the hand-built nodes, rejected keys their branches read (PerformNode's `field` first): twenty manifest patches stopped loading and `engine_tests` died on the throw. The loader's list is generated from `tools/lint_patches.py`'s own scraper (143 keys), global like the lint's ("a spurious entry can only hide a rejection, never invent one"); a structure-meter test (`test_loader_allowlist_covers_the_lint_scrape`) fails when the scrape gains a key the loader lacks. A per-type list is Part 4's codec. Then: engine_tests 6480, null gate 78/78, gates clean, `--gencheck` on oboe1 ok.
- **Task 3, the load A/B** (old vs t3 over the eight sets): DIFFER 0 in every set; one patch stopped loading in the sample — `sweep/stk_bandedwg_port/stk_bandedwg_bowl_struck` ("node 'Input' (VarSource): unknown key 'value'", the bwg class on a sweep patch the lint cross-check had not covered). The lint then ran over `sweep` and `old` too: four stk_bandedwg_port `_struck` patches in sweep, and 21 archive files in old with 44 legacy descriptor-path keys (Squeaker.json's AdditiveSource `amplVarPct` … among them) that the every-third sample never rendered. Every one stops loading and is listed (ruling 9); nothing tracked, queued or pending does.
- **Task 4:** RED = the new wrap assertion (0 vs 1.0). The fix in: the 2.2 fix-wave test `run_delay_line_zero_slot_tests` failed on its two zero-slot assertions — it pinned the workaround; restated as `run_delay_line_wrap_tests` (slot 0, the first write, on both laps) and the duplicate added at RED removed. Null gate: exactly the two listed baselines differed (`double_advance_bug`, `double_advance_ok`), refrozen, 78/78. A/B t3 vs t4: library 0, baselines 2, pending 1 (`Trombone_att1`), audition 14 (the bwg family), sweep 8 (`fbv_r090_q5`, stk_bandedwg `tbar_bowed`, the three stk_blowhole, trombone1 `loud`, trombone3 `ab_attempt2_ff` and `line_soft`), old 0, taps 7 (within pending and sweep), retired 0 — 25 distinct patches, every one on the wrap list; three listed did not move (`nlbore_sine_d005`, `nlbore_sine_neg040`: one or two reads of a slot 0 that held zero; `stk_bandedwg_bowl_struck`: fails to load since Task 3). The session was cut by the Fable quota before Task 4's commit; resumed 2026-10-26, the test restated, the gates re-run, committed.

- **Task 5 (the close), 2026-10-26:** Step 1 greps clean (the `throw` grep lists `prepare`'s and `envelope_json.h`'s three load-time throws; no `fprintf` in core; the loader's "ignored" hits are the paramMap and score notes and one stale-key comment; no trace of the wrap counter). Step 2: ctest, stamp, null gate 78/78, wiring null gate 78/78; the two roundtrip gates fail on the same six as at 2.4 (`double_advance_{bug,ok}`, `loop_tap_{counter,starved}`, `perform/wiring_{setting,smoke}`) — the wrap changed `double_advance_ok`'s render, not the set. Step 3 stands (no engine code since `fbfffe6`). Step 4: the first timing run was DISCARDED — taken while my own lint and CLI loads ran beside it, its base arm came out 1.4x slower than the clean run's on the same binary; the clean run is within four percent on the second-long rows (the new binary faster), oboe1 and E a few percent slower on sub-250 ms renders, recorded not chased (ruling 13); the D micro patch terminated the base (`WaveSource: non-positive frequency`, a dash in both arms of 2.4's table) and renders under 2.5 with no `[faults]` block, being mixer-mode — section 7's debt, live. Step 5: +327 lines (135 tests), two types, `dynamic_cast` +1 (a test's `PitchedInstrument*`), mutable +1 (`g_genFaults`), changelog −2, duplicates 157→161 = the loader voice-loop pair 55→62 (the `nodesById` fill), a nine-line tap-free-cycle fixture repeated at the top of `run_fault_tests` (:5959, from :2207; its eight-line sub-block counts again), and two blocks in `tools/test_figures/main.cpp`, a file the checkpoint did not touch (byte-identical) — the token detector's boundaries moved with the rest of the set, verified by scanning the `cb1b4ab` tree beside HEAD. Ruling: the Task 3 addendum's archive count was a misread of the lint's total (44 in 21 files, 37 of them paramMap range notes the loader never rejected): one archive file stops loading (`Squeaker.json`, confirmed with the CLI), and list (a)'s `ks_morph_saw_test` LOADS — its four keys are read by WavetableSource's hand-written branch (`patch_loader.cpp:829-851`) and entered the allowlist with the scrape; the old warning was a false positive there, and the measurement document's "stops loading after the cut" line for that file is superseded by the report. Report sections 1, 4, 5 and 6 (rulings 23-25) written accordingly; Appendix A was already right. R5's verdict recorded the same day (Matt: the forty renders are the truth, `previous()` and the positional paragraph left as they are).

- **Task 5, the fix wave (2026-10-26):** the refuter (section 8) found one Major — the Task 4 wrap wrapped the index and not the position, so `frac` was `ring_` on the wrap sample and the read extrapolated once per lap; the test could not see it because its source was a constant. Confirmed by reading and by RED: the test restated with the `Counter` source fails on the shipped wrap exactly as traced (`out[15]` 1107 against 1; the second lap 2213 against 1107) and passes with the position wrap. Rulings 26–33 in the report: the position wraps; `take_faults()` (exchange) in both collectors; `ComputingScope` in `prepare()`/`start()` (a throw out of `on_prepare` no longer wedges the node; pinned); every CLI render reports through one `report_faults(host)` template (run_melody's and run_compose's mixes, run_dun's two were silent) and the UI's chords render reports; the report sorted by node id (pinned with two nodes in reverse order); the three `cutoff` alias configurators throw on a failed wire and the loader rethrows a configurator's error with the node id and type; the null gate treats a `[faults]` line on stderr as a diff; `kStructural` → `kBranchKeys`; the comments, the dead include, the three test headers; the fixture collapsed into `tapless_cycle(h)`; a prepare-after-render test pins ruling 20. Not written (test debt, section 7): `StereoMixer`/`DrumKit` collectors, `HybridKSSource` `TableClamped`, the array `add_param` rejection. Gates after the wave: engine_tests ALL PASS (6486); ctest PASS (the meter's allowlist test on the new name), stamp PASS, roundtrip smoke the known wiring diff; null gate: exactly `double_advance_bug` and `double_advance_ok` differed, no `[faults]` on any manifest patch, refrozen (two manifest entries changed, verified by diff). A/B `t5` over the eight sets: against `t4` the same 25 differ (library 0, baselines 2, pending 1, audition 14, sweep 8, old 0, taps 7, retired 0), against `old` the same 25, nothing else; the three non-movers unchanged under both reads. Meter: +392 lines, duplicates 157→159 — the loader pair 55→62, run_dun's two render blocks (8 lines, one added line crossed the threshold), the two test_figures artifacts; the fixture and CLI pairs the first landing added are gone (re-verified against the `cb1b4ab` tree). Timing after the wave: three runs, none to the clean run's standard — two DISCARDED — the first with VS Code's C++ IntelliSense server re-indexing after the header edits and the stash/pop (49,700 CPU-seconds on its clock), the second under a transient load that left the base arm, the same binary, 1.7x slower than the clean run; a process sample showed the machine quiet before the third run, whose base arm still came out 1.1x the clean run's and whose render columns scatter ±11 percent both ways; section 5 keeps the clean run as the table and records the third run in prose. The wave's one change on the sample path is a float compare for an int compare in the DelayLine read.

- **Task 5, after the re-review (2026-10-26, commit 1481d15):** the refuter's second pass closed every issue but I9's declared test debt and I11 (Matt's), and raised five minor findings. Done in a second pass, not re-reviewed: N1 — `null_gate_perform_source.py --freeze` refuses a value that is not a 64-hex hash (REFUSED <patch>: <value>, exit 1) and the faults path reaps its WAV; N4 — the loader's configurator rethrow catches `std::exception` (the JSON reader's errors name their node); I8's two lowercase headers. N2 (the UI's three collect blocks) and N5 (`prepared()` true after a throwing `on_prepare`) listed in section 7; N3 (the id-order test pins the contract, not the sort) stated in section 8. Gates after the pass: ctest PASS, stamp PASS, roundtrip smoke the known wiring diff, null gate 78/78.

## Self-review notes

- **Spec coverage.** Part 2 §6 "Throws today": the two sites — Task 1. "The rule": `start` and `next` never throw — Task 1 (and Task 2's guards); clamp to the declared range and a fault flag — Task 1 (ruling 2 narrows "range" to the invalid side and says why); no I/O on a render path, a per-node bit set without locking, collected after a render by the control side (UI status line, CLI summary) — Task 2; `prepare` may throw — Task 1 (ruling 3 uses it). "A rejected wire says so": `set_pin` fails on an unknown name or a kind mismatch with the reason and the node's id — Task 3 (ruling 7: through `set_param`'s `bool` and the loader); the builder turns it into a load failure — Task 3; the lint tool pre-checks folders — Task 0 step 4. "Byte-identity": unchanged for every valid patch — every task's null gate and the Task 3/4 A/Bs; a zero-frequency patch renders with a fault — Task 1 and Task 2's collection test; a bad-wire patch fails to load with the reason, found by the null gate's own run — Task 3 step 5. §7 row 2.5 — Task 5 fills the cell. The routed items: DelayLine zero slot — Task 4; Mesh2D fallback — Task 1; never-prepared in Release — Task 1; clockless pull, `prepare`/`start` overflow, `computing_` after a throw, the cycle `fprintf` — Task 2.
- **Step scan.** Every code change names its file and lines or carries a code block; every test carries its assertions; the measurement, A/B and timing commands carry their expected outputs; the loader's three failure texts are exact. The one open constant (Task 4's `len`) is declared open with its rule.
- **Type consistency.** `Fault`, `fault_name`, `faults()`, `clear_faults()`, `fault()`, `faults_`, `FaultReport{nodeId, type, faults}`, `collect_faults(std::vector<FaultReport>&)`, `bool set_param/add_param/clear_param`, `wire_params_generic(src, id, params, valueNodes)`, `cli_pre25.exe`, `renders/scratch/ab25_tree/<set>/{old,t3,t4}` are used with those names throughout.
- **Review Focus.** 1 — Task 1's zero-frequency block; 2 — Task 1's never-prepared block; 3 — Task 2's cycle block (next, prepare, start); 4 — Task 3's four assertions; 5 — Task 4's test and A/B. The clockless note — Task 2's bare-node assertion.
- **Proportion.** Code appears for the contract, the loader's texts, the CLI's block and the four test sections; the rest is commands with expected outputs. The engine change is about a hundred lines across six headers and one source file; the plan is mostly the measurement and the proof.

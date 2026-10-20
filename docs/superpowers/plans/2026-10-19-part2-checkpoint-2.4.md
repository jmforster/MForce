# Part 2, checkpoint 2.4: taps reading B — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A tap is a one-sample delay wherever it sits: it reads the value its source held at the end of the previous tick, never a value the source computed earlier in the same tick. The renders that change are exactly the patches where a tap today reads a same-tick value, measured beforehand with the sealed binary; no library or baseline patch is among them.

**Architecture:** Every node keeps the value it held at the end of the previous tick (`prev_`) beside this tick's (`cur_`). `next()` and `step()` move `cur_` into `prev_` at the moment they claim the tick — before the sweep and the compute — so a tap read before, during or after its source's compute gets one answer. `ValueSource::previous()` is that read; `TapSource::compute()` uses it, while the tap's `current()` (the Setup-time live read between ticks) keeps reading the source's present value, which between ticks is the same number. No new verb, no pin change, no loader change, no UI change; the two-value memo lands first unused and byte-identical, and the tap's read switches in its own five-line commit. `DelayLineSource`'s compensation assumption — one sample from the closing tap — becomes true in every rooting; its comment changes, its code does not.

**Tech Stack:** C++20 (MSVC, CMake 3.20+), the existing `engine_tests` harness, `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/structure/check.py`, `python tools/measure_taps.py` (Task 0 only; deleted in Task 1, rebuilt from history by `git show <Task 0 sha>:tools/measure_taps.py`).

**Spec:** `docs/architecture/part2-valuesource-contract.md` section 2 "Taps" (readings A and B: "B, exact. Each node also keeps the value of the tick before, and a tap reads that. A tap is then always a one-sample delay"; "A and B agree everywhere a tap closes a cycle through its forward path. They differ only where a tap today reads a value its source computed earlier in the same tick") and section 7 (the 2.4 row: expected render changes = "only patches where a tap today reads a same-tick value, measured beforehand"); `docs/feedback_loop_design.md` section 2 ("a tap wire reads what its source computed last tick"; "Taps are exactly one sample"); `docs/architecture/target-architecture.md` section 2 principle 6 (structure and behaviour never change in the same commit — Task 1 is the structure, Task 2 the behaviour); `docs/architecture/measurements/2026-10-18-sharing-lag-tree.md` section "47 patches with same-tick tap reads" (the cross-check list); REVIEW.md R4 verdict (the `loop_root_sensitivity` pair is in `patches/old/baselines_retired_2026-10-19/` and stays in the A/B).

## Global Constraints

- No heap allocation, lock or throw in any `next()`, `step()`, `compute()` or `previous()` (CLAUDE.md non-negotiables). The checkpoint adds one `float` per node and one store per compute, nothing else.
- Explicit registries, no reflection (CLAUDE.md). Nothing here touches a registry.
- Byte-identity: `python tools/null_gate_perform_source.py --jobs 3` reports `78/78 manifest entries identical` after Task 1 AND after Task 2 — none of the measured patches is in `patches/library` or `patches/baselines`, so the manifest is never refrozen in this checkpoint. A manifest difference in any task is a bug until ruled on in the Ledger.
- The whole-tree A/B (Task 2 step 5, Task 3 step 3): every patch that differs is on Task 0's list and is explained; a listed patch that does not differ is noted with its reason (the list is an upper bound — 2.3's ruling 16).
- No new file over 600 lines, no new function over 80 (`tools/structure/limits.json`); `python tools/structure/check.py --delta <Task 0 sha>` is pasted into the report.
- No dated changelog, backlog id or REVIEW number in code comments.
- Commit after every task; `python tools/gates.py --fast` before every commit (its roundtrip smoke fails on `perform/wiring_setting.json` for the pre-existing backlog-79 reason; every other gate must pass).
- Work on main, no worktree (ruling 1 of checkpoint 2.1 stands). The in-tree build is `build/`; the UI exe may be locked by a running UI — rename-then-link (CLAUDE.md).
- The A/B base for every task is `renders/scratch/ab_tree/cli_pre24.exe` (Task 0 step 0: a copy of 2.3's `cli_final.exe`, the engine at `fdf7536`, which `git diff fdf7536 HEAD --stat -- engine tools/mforce_cli tools/engine_tests tools/mforce_ui` shows is HEAD's engine). The pre-2.4 arms of the six tree sets are 2.3's `renders/scratch/ab23_tree/<set>/final/hashes.txt`, re-used (ruling 8).

## Review Focus

The five input classes the spec implies but no existing test exercises, most likely to bite first. Each line names the task whose tests pin it.

1. **A tap on a source that already computed this tick** (the 47: a trombone or bore loop whose output is taken where the tap's source lies upstream of the tap's consumer, so the root pull computes the source first): today it reads this tick's value and is no delay at all; it must read the previous tick's. Task 2's `run_tap_reading_tests` pins it (the one assertion that fails before the task).
2. **A tap on a source mid-compute** (the cycle skeleton `out = 1 + tap(out)`, and every loop closed through a tap on the node whose forward path reaches the tap): the source has claimed the tick but not yet stored this tick's value. Reading A and reading B must agree here, or every loop patch moves. Task 2's clocked self-counter pins it; the null gate's `loop_tap_counter`, `loop_selfosc`, `loop_hysteresis_bow` prove it on stored patches (78/78).
3. **A tap on a node not pulled this tick** (a Multiplex solo input, a PhasedValueSource inactive stage, a tail swept after the tap): `previous()` is the node's last computed value, as `current()` was. Task 2 pins it (two ticks without a pull of the source).
4. **A tap inside a stepped subgraph** (a table fill's excitation closing a loop through a tap, or a tap in a push-binding chain evaluated with `step()` at a note's Setup): one step late, under `step()`'s stated limits. Task 2 pins the semantics; Task 0 step 3 confirms no stored patch does either (expected 0, listed if any).
5. **A Setup-time read through a tap** (a trigger chain or push binding whose value is read with `current()` between ticks): the live read must see the value the tap will deliver at the coming tick, which between ticks is the source's present value. Task 2 asserts `tap->current()` after the source computed; 2.3's "`TapSource::current()` reads live" test stays as it is.

Also noted, not a Review Focus line: a tap on a stateless view (`NameGate`, `PerformOut`) reads the view's own memo, which is what the view computed last tick, not the view's live forward; Task 0 step 3 confirms no stored patch taps either type (expected 0); ruling 2 records the semantics.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `engine/include/mforce/core/dsp_value_source.h` (modify) | `ValueSource` keeps `prev_` beside `cur_`; `next()` and `step()` move `cur_` to `prev_` when they claim the tick; `previous()` is the read; `operator=` copies both; `TapSource::compute()` reads `source->previous()`, `TapSource::current()` reads `source->current()`, one `guard_` for both. (Task 0 adds, and Task 1 removes, a measurement: `computed_this_tick()`, two static counters on `TapSource` and one count in its `compute()`.) |
| `engine/include/mforce/source/delay_line_source.h` (modify, comments only) | lines 21-28 and 69: the closing tap's sample is the rule, not a position. |
| `tools/mforce_cli/main.cpp` (Task 0 only; reverted in Task 1) | `main()` switches the tap trace on from `MFORCE_TRACE_TAPS` and prints `[taps] sameTickReads=N` on every exit path. |
| `tools/measure_taps.py` (new in Task 0; deleted in Task 1) | renders set files with the trace on, writes the measurement document. |
| `tools/engine_tests/main.cpp` (modify) | `run_previous_tests` (Task 1) and `run_tap_reading_tests` (Task 2), placed after `run_memo_tests` (they use its `TestHost` and `Counter`), registered after `run_memo_tests();` at the call list (line 5818). |
| `docs/architecture/measurements/2026-10-19-same-tick-taps.md` (new, Task 0) | the list of patches whose renders change, with counts, by tree; the comparison with the 10-18 list; the structural scans. |
| `docs/feedback_loop_design.md`, `docs/architecture/part2-valuesource-contract.md` (modify, Task 2) | the design records the delta (feedback_loop_design.md's header paragraph) and the decision (Part 2 section 2 "Taps", section 7 row 2.4). |
| `docs/autonomy/refactor/reports/2026-10-19-checkpoint-2.4.md` (new, Task 3), `REVIEW.md`, `BACKLOG.md`, `docs/autonomy/STATUS.md`, `tools/structure/metrics.json` (modify, Task 3) | the seal. |
| `renders/scratch/ab24_tree/*.txt` (gitignored, Task 0) | the eight set files: the six tree sets (baselines without the retired pair; old with the pair's new paths), `taps.txt` (the 47 in full), `retired.txt` (the pair). |

---

## The contract (what Task 1 writes; Task 2 switches the tap's read)

`engine/include/mforce/core/dsp_value_source.h`, the parts that change. Everything not shown is as it stands at `fdf7536`.

```cpp
struct ValueSource : Component {
  // ... prepare(), start(), on_prepare, on_start unchanged ...

  // This tick's value. Computed once per tick of the host's clock — every
  // wired Auto pin pulled in declaration order, then compute() — and served
  // to every later pull in the same tick, whoever asks and in whatever
  // order; a node pulled by several consumers advances once. The value it
  // replaces is kept for the tap edge (previous()). With no host clock
  // every pull computes. A node re-entered during its own compute (a cycle
  // with no tap, which the builders refuse) gets the previous value, and
  // the cycle is reported once.
  float next() {
    if (computing_) { report_cycle_once_(); return cur_; }
    if (ctx_.clock) {
      const Tick t = ctx_.clock->now;
      if (t == lastTick_) return cur_;
      lastTick_ = t;
    }
    prev_ = cur_;                   // the tick is claimed: what stood here is now the previous tick's
    computing_ = true;
    for (auto* s : auto_sources()) s->next();
    cur_ = compute();
    computing_ = false;
    return cur_;
  }
  // One sample now, whatever the tick, ... (comment as today) ...
  float step() {
    if (ctx_.clock) lastTick_ = ctx_.clock->now;
    prev_ = cur_;
    for (auto* s : auto_sources()) s->step();
    cur_ = compute();
    return cur_;
  }
  virtual float compute() = 0;
  virtual float current() const { return cur_; }

  // The value this node held at the end of the previous tick: cur_ until the
  // node claims this tick, the value cur_ replaced from then on — the same
  // number whether the asker comes before, during or after this node's
  // compute. What the tap edge reads (feedback_loop_design.md): a tap is one
  // sample late wherever it sits. With no host clock there is no "this
  // tick" and the read is cur_, as every pull computes. Not virtual: a
  // stateless view's live current() has no previous value but the one it
  // computed last tick.
  float previous() const {
    return ctx_.clock && lastTick_ == ctx_.clock->now ? prev_ : cur_;
  }

  // ... tracks_frequency_live, phase_delay_at, reseed unchanged ...

protected:
  // ... sample_rate, render_context, prepared unchanged ...

  // A node assigning itself (Envelope::replace_stages) assigns its own
  // state, both ticks' values among it; ... (rest of the comment as today) ...
  ValueSource& operator=(const ValueSource& o) {
    Component::operator=(o);
    cur_ = o.cur_;
    prev_ = o.prev_;
    return *this;
  }

  float cur_{0.0f};

private:
  // ... report_cycle_once_ unchanged ...
  RenderContext ctx_{};
  Tick lastTick_{-1};
  float prev_{0.0f};              // what cur_ held before this tick's compute
  bool computing_{false};
  bool cycleReported_{false};
};

// The tap edge (feedback_loop_design.md): a guarded read of another node's
// memo, never a pull. The source is advanced by its own consumers (or by the
// voice's tail list when only taps reach it), so a tap can close a feedback
// cycle that a pull could not. A tap is one sample late by rule: it reads
// the value its source held at the end of the previous tick, wherever the
// tap sits in the pull. Reads are armored, non-finite scrubbed to 0 and
// clamped to +-8, so a runaway loop saturates audibly instead of poisoning
// the graph.
struct TapSource final : ValueSource {
  std::shared_ptr<ValueSource> source;

  explicit TapSource(std::shared_ptr<ValueSource> src = nullptr)
    : source(std::move(src)) {}

  // No pins and no hooks: prepare and start reach nothing here.
  float compute() override { return guard_(source ? source->previous() : 0.0f); }   // Task 2; Task 1 leaves source->current() here
  // The live read: a Setup-time read between ticks sees the source's
  // present value — which, between ticks, is what the next tick's tap
  // delivers.
  float current() const override { return guard_(source ? source->current() : 0.0f); }

  const char* type_name() const override { return "Tap"; }

private:
  static float guard_(float v) {
    if (!std::isfinite(v)) return 0.0f;
    return v < -8.0f ? -8.0f : (v > 8.0f ? 8.0f : v);
  }
};
```

Why `prev_ = cur_` sits where it does: a tap reads its source in one of three positions — before the source claims the tick (`lastTick_ != now`: `previous()` returns `cur_`, which still holds last tick's value), while the source is mid-compute (its own forward path, the cycle skeleton: `lastTick_ == now`, `prev_` just took the old `cur_`, `cur_` not yet overwritten — `prev_` and `cur_` are the same number), or after the source stored this tick's value (`prev_` holds last tick's; `cur_` this tick's). Moving the value at the claim makes all three reads return last tick's value; moving it after the compute would hand the mid-compute reader a value two ticks old.

The Task 0 measurement, added to the same header and removed in Task 1:

```cpp
  // ValueSource, public, Task 0 only:
  bool computed_this_tick() const {
    return ctx_.clock && lastTick_ == ctx_.clock->now && !computing_;
  }
  // TapSource, Task 0 only:
  static inline bool      trace{false};
  static inline long long sameTickReads{0};
  float compute() override {
    if (trace && source && source->computed_this_tick()) ++sameTickReads;
    return read_();
  }
```

---

### Task 0: Measure the renders that will change; keep the base

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (the measurement, above), `tools/mforce_cli/main.cpp:1215-1248` (`main()`)
- Create: `tools/measure_taps.py`, `docs/architecture/measurements/2026-10-19-same-tick-taps.md`, `renders/scratch/ab24_tree/*.txt` (gitignored)

**Interfaces:**
- Produces: **the list** — every patch in the tree with `sameTickReads > 0`, by tree, with the count: the renders Task 2 may change. `renders/scratch/ab_tree/cli_pre24.exe`, the base of every A/B. The eight set files and their pre-2.4 arms under `renders/scratch/ab24_tree/<set>/old/hashes.txt`.

- [ ] **Step 0: Keep the pre-checkpoint binary.** `git diff fdf7536 HEAD --stat -- engine tools/mforce_cli tools/engine_tests tools/mforce_ui` prints nothing (HEAD's engine is the sealed 2.3 engine). `python tools/gates.py --fast` passes (known roundtrip failure only). `copy renders\scratch\ab_tree\cli_final.exe renders\scratch\ab_tree\cli_pre24.exe`. Record `git rev-parse HEAD` in the Ledger as the A/B base.
- [ ] **Step 1: The set files and the old arms.** `mkdir renders\scratch\ab24_tree`; copy `renders/scratch/ab23_tree/{library,baselines,audition,pending,sweep,old}.txt` across; delete the two `loop_root_sensitivity` lines from `baselines.txt` (52 remain) and append their new paths, `patches/old/baselines_retired_2026-10-19/loop_root_sensitivity_{dead,fires}.json`, to `old.txt` (275); write `taps.txt` = the 47 backticked paths of the 10-18 document's section "47 patches with same-tick tap reads", in order; write `retired.txt` = the two new paths. For each of the six tree sets, copy `renders/scratch/ab23_tree/<set>/final/hashes.txt` to `renders/scratch/ab24_tree/<set>/old/hashes.txt` (ruling 8: the same engine rendered them). Render the old arm fresh only where no arm exists: `python tools/ab_render_set.py render renders/scratch/ab24_tree/taps.txt renders/scratch/ab24_tree/taps old --cli renders/scratch/ab_tree/cli_pre24.exe` and the same for `retired.txt` → `retired`. Expected: `arm old rendered 47/47` and `2/2`.
- [ ] **Step 2: The counter and its driver.** `dsp_value_source.h`: `computed_this_tick()`, the two `static inline` members and the count in `TapSource::compute()` exactly as "The contract" shows. `tools/mforce_cli/main.cpp`, `main()`: first lines, before the `try`: `mforce::TapSource::trace = std::getenv("MFORCE_TRACE_TAPS") != nullptr;` and a local `struct TapTracePrint { ~TapTracePrint() { if (mforce::TapSource::trace) std::fprintf(stderr, "[taps] sameTickReads=%lld\n", mforce::TapSource::sameTickReads); } } tapTracePrint;` — a destructor prints on every return path, the `catch` included. Build the CLI. `tools/measure_taps.py`: `git show 587b539:tools/measure_sharing.py` is the model; narrowed to `python tools/measure_taps.py --set <file> [--set <file> ...] [--jobs 3] --out <doc>`: renders every patch of every set with `MFORCE_TRACE_TAPS=1`, parses `\[taps\] sameTickReads=(\d+)`, and writes the document with these sections: (a) "N patches with same-tick tap reads", grouped by set file, each `- \`path\`: count`; (b) "Against the 2026-10-18 list": the 10-18 patches not on this list, this list's patches not on the 10-18 list, and patches on both whose count changed — each line left for the controller to explain in prose below it; (c) "M patches that produced no trace line (they do not load)"; (d) an empty "Structural scans" heading for step 3. The driver drops the `--consumers` mode and the manifest default; a patch present in two set files is rendered once.
- [ ] **Step 3: Structural scans** (python one-offs over the JSON, results written under "Structural scans" in the document, each with the number and the paths if any): (a) every `{"tap": "<id>"}` whose target node's `type` is `NameGate` or whose id begins `__perf` — expected 0; (b) every `WavetableSource` / `HybridKSSource` whose `inputSource` is a `{"ref"}` and whose excitation subgraph (the ref's node and everything reachable through its params and inputs) contains a `{"tap": ...}` — expected 0 (the refutation of 2.3 scanned 62 such fills for sharing; this scans them for taps); (c) every tap wired on a pin of a node that is the head of a push-binding chain (the instrument block's push deliveries, loaded into `VoiceGraph::pushBindings`; find the loader's key by `grep -n pushBindings engine/src/patch_loader.cpp`) — expected 0; (d) for the patches on the list: which contain a `DelayLine` with `"compensate": true`, and which do not, by family (the report names the mechanism per family: one more sample of loop latency; with `compensate` on, the loop now lands on the length the setting computes, where it was one sample short).
- [ ] **Step 4: Measure.** `python tools/measure_taps.py --jobs 3 --set renders/scratch/ab24_tree/library.txt --set .../baselines.txt --set .../audition.txt --set .../pending.txt --set .../sweep.txt --set .../old.txt --set .../taps.txt --set .../retired.txt --out docs/architecture/measurements/2026-10-19-same-tick-taps.md`. Expected: library 0, baselines 0, retired 0, pending 1 (`brass/Trombone_att1`), audition 10 (`pierce1d_1` ×6, `trombone1` ×4), and every one of the 47 in `taps.txt` present with a count (the sampled `sweep` and `old` sets show their every-third subsets). Every line of section (b) gets its explanation in the document before step 5: a patch on the 10-18 list that no longer counts, or counts differently, is explained by what changed between `587b539` and `fdf7536` (the memo made the tap's source compute once per tick; the old trace counted reads against `advance()`'s bookkeeping), and a patch new to the list is explained the same way — or it is a defect in the counter, found before anything is built on the list.
- [ ] **Step 5: Commit** — `git add docs/architecture/measurements/2026-10-19-same-tick-taps.md tools/measure_taps.py engine/include/mforce/core/dsp_value_source.h tools/mforce_cli/main.cpp` ; `git commit -m "measure: the same-tick tap list for checkpoint 2.4 (the counter and its driver go with the next commit)"`.

---

### Task 1: The two-value memo, unused

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (`next()`, `step()`, `previous()`, `operator=`, `prev_`; the measurement removed), `tools/mforce_cli/main.cpp` (the trace switch and printer removed), `tools/engine_tests/main.cpp` (after `run_memo_tests`, ~line 2214; the call list at 5818)
- Delete: `tools/measure_taps.py`

**Interfaces:**
- Produces: `float ValueSource::previous() const` — the value the node held at the end of the previous tick (the contract's definition and its three positions).

- [ ] **Step 1: Write the failing test** — `tools/engine_tests/main.cpp`, after `run_memo_tests`:

```cpp
static void run_previous_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    c->prepare(h.ctx); c->start(16);
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // nothing computed yet: cur_
    h.clock.tick();
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // this tick not yet claimed: cur_, still last tick's
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 0.0f, 1e-6f);   // claimed and computed: what cur_ replaced
    CHECK_NEAR(c->current(), 1.0f, 1e-6f);
    h.clock.tick();
    CHECK_NEAR(c->previous(), 1.0f, 1e-6f);   // a new tick, not yet claimed: cur_
    CHECK_NEAR(c->next(), 2.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 1.0f, 1e-6f);
    h.clock.tick();                           // step() moves the value the same way
    CHECK_NEAR(c->step(), 3.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 2.0f, 1e-6f);
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->previous(), 3.0f, 1e-6f);   // before the latest step
    h.clock.tick(); h.clock.tick();           // not pulled for two ticks
    CHECK_NEAR(c->previous(), 4.0f, 1e-6f);   // its last value
    auto mid = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                std::make_shared<ConstantSource>(0.0f),
                                                CombineOp::Sum, 0.0f);
    struct Probe final : Generator {            // reads its target's previous() from inside the target's compute
        ValueSource* target{nullptr};
        float seen{-1.0f};
        float compute() override { seen = target->previous(); return 0.0f; }
        const char* type_name() const override { return "Probe"; }
    };
    auto probe = std::make_shared<Probe>(); probe->target = mid.get();
    mid->prepare(h.ctx); mid->start(16); probe->prepare(h.ctx); probe->start(16);
    mid->set_param("source2", probe);         // mid's sweep pulls the probe: a read mid-compute
    h.clock.tick();
    CHECK_NEAR(mid->next(), 1.0f, 1e-6f);
    CHECK_NEAR(probe->seen, 0.0f, 1e-6f);     // mid-compute: last tick's value (0), not two ticks old
    h.clock.tick();
    CHECK_NEAR(mid->next(), 1.0f, 1e-6f);
    CHECK_NEAR(probe->seen, 1.0f, 1e-6f);
    auto bare = std::make_shared<Counter>();  // no clock: every pull computes, previous() is the present value
    bare->prepare(RenderContext{48000}); bare->start(16);
    bare->next(); bare->next();
    CHECK_NEAR(bare->previous(), 2.0f, 1e-6f);
}
```

Register `run_previous_tests();` directly after `run_memo_tests();` in the call list.

- [ ] **Step 2: Run to verify it fails** — build `engine_tests`: fails to compile (`previous` is not a member).
- [ ] **Step 3: The contract** — `dsp_value_source.h` exactly as "The contract": `prev_`, the two moves, `previous()`, `operator=`; `TapSource::compute()` keeps reading `source->current()` for this task (split `read_()` into `guard_()` now, with `compute()` and `current()` both calling `guard_(source ? source->current() : 0.0f)`). Delete `computed_this_tick()`, the two statics, the count; delete the trace switch and `TapTracePrint` from `main()`; `git rm tools/measure_taps.py`.
- [ ] **Step 4: Verify** — `engine_tests` passes with `run_previous_tests`; `python tools/null_gate_perform_source.py --jobs 3` → `78/78` (`prev_` is written and never read: byte-identical by construction); `python tools/gates.py --fast`; `git grep -n "computed_this_tick\|MFORCE_TRACE_TAPS\|sameTickReads\|TapTracePrint" -- engine tools` lists nothing.
- [ ] **Step 5: Commit** — `git add -u engine tools/mforce_cli/main.cpp tools/engine_tests/main.cpp tools/measure_taps.py` ; `git commit -m "core: every node keeps the value it held at the end of the previous tick; the measurement counter goes"`.

---

### Task 2: The tap reads the previous tick; the measured renders change

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (`TapSource`'s comment and `compute()`), `engine/include/mforce/source/delay_line_source.h:21-28, 69` (comments), `tools/engine_tests/main.cpp` (after `run_previous_tests`; the call list), `docs/feedback_loop_design.md:1-12` (the header paragraph), `docs/architecture/part2-valuesource-contract.md` (section 2 "Taps", section 7 row 2.4)

**Interfaces:**
- Consumes: `ValueSource::previous()` (Task 1).
- Produces: the tap edge's rule — `TapSource::compute()` returns `guard_(source->previous())`; `TapSource::current()` returns `guard_(source->current())`.

- [ ] **Step 1: Write the failing test** — after `run_previous_tests`:

```cpp
static void run_tap_reading_tests() {
    TestHost h;
    auto c = std::make_shared<Counter>();
    auto tap = std::make_shared<TapSource>(c);
    c->prepare(h.ctx); c->start(16); tap->prepare(h.ctx); tap->start(16);
    h.clock.tick();
    CHECK_NEAR(c->next(), 1.0f, 1e-6f);       // the source computes first this tick
    CHECK_NEAR(tap->next(), 0.0f, 1e-6f);     // the tap is one sample late anyway (reading A gave 1)
    h.clock.tick();
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // the tap first: the source has not claimed this tick
    CHECK_NEAR(c->next(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 1.0f, 1e-6f);     // the tap's own memo: pulled again this tick, the same value
    h.clock.tick();
    CHECK_NEAR(c->next(), 3.0f, 1e-6f);
    CHECK_NEAR(tap->next(), 2.0f, 1e-6f);
    CHECK_NEAR(tap->current(), 3.0f, 1e-6f);  // the live read (a Setup between ticks) sees the present value
    h.clock.tick();                           // inside a fill: one step late
    CHECK_NEAR(c->step(), 4.0f, 1e-6f);
    CHECK_NEAR(c->step(), 5.0f, 1e-6f);
    CHECK_NEAR(tap->step(), 4.0f, 1e-6f);
    h.clock.tick(); h.clock.tick();           // a source not pulled this tick: its last value
    CHECK_NEAR(tap->next(), 5.0f, 1e-6f);
    // The cycle skeleton under a clock: out = 1 + tap(out) counts 1, 2, 3 — the
    // tap reads its source mid-compute, and readings A and B agree there.
    auto counter = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                                    std::make_shared<ConstantSource>(0.0f),
                                                    CombineOp::Sum, 0.0f);
    auto self = std::make_shared<TapSource>(counter);
    counter->prepare(h.ctx); counter->start(16);
    counter->set_param("source2", self);      // after the host calls, which walk the pins
    self->prepare(h.ctx); self->start(16);
    h.clock.tick(); CHECK_NEAR(counter->next(), 1.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(counter->next(), 2.0f, 1e-6f);
    h.clock.tick(); CHECK_NEAR(counter->next(), 3.0f, 1e-6f);
    counter->clear_param("source2");          // break the ownership cycle
    auto bare = std::make_shared<Counter>();  // no clock: positional, as every pull computes
    auto bareTap = std::make_shared<TapSource>(bare);
    bare->prepare(RenderContext{48000}); bare->start(16);
    bareTap->prepare(RenderContext{48000}); bareTap->start(16);
    bare->next();
    CHECK_NEAR(bareTap->next(), 1.0f, 1e-6f);
}
```

Register `run_tap_reading_tests();` after `run_previous_tests();`. The existing `run_tap_cycle_tests` (clockless) and the DelayLine loop tests stay as they are: their assertions hold under both readings.

- [ ] **Step 2: Run to verify it fails** — `engine_tests`: the second assertion fails (`tap->next()` reads 1, expects 0); nothing else.
- [ ] **Step 3: The rule** — `TapSource::compute()` returns `guard_(source ? source->previous() : 0.0f)`; the struct's comment as "The contract" shows (the "positional" sentence goes). `delay_line_source.h:21-28`: "the tap's positional z^-1" → "the closing tap's one sample"; `:69` → `comp = 1.0f;  // the closing tap: one sample, by rule`. `git grep -n positional -- engine` lists nothing.
- [ ] **Step 4: Verify** — `engine_tests` passes; `python tools/null_gate_perform_source.py --jobs 3` → `78/78` (no manifest patch is on the list; a difference here is a bug — stop and find it before step 5); `python tools/gates.py --fast`; the UI (rename-then-link): open `patches/audition/trombone1/trombone_attempt4.json` (on the list), play a key — it plays; open `patches/baselines/feedback/loop_hysteresis_bow.json`, play, key-up stops it.
- [ ] **Step 5: The whole-tree A/B** — for each of the eight set files: `python tools/ab_render_set.py render renders/scratch/ab24_tree/<set>.txt renders/scratch/ab24_tree/<set> new` (the build's CLI), then `compare renders/scratch/ab24_tree/<set> old new`. The rule: every `DIFFER` patch is on Task 0's list; a listed patch that does not differ is noted with its reason (its same-tick reads saw a source whose value had not changed between the two ticks — a constant, or a loop that never ignited); a differing patch NOT on the list is investigated before the commit (Review Focus 3 and 4 are the candidates; anything else is a bug). `baselines`' compare reports the two retired paths as present in one arm (expected; `retired` covers them). The counts, per set, go in the Ledger. Timing: `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre24.exe build/tools/mforce_cli/Release/mforce_cli.exe 7`; expected within 2 percent on every row (one store per node per sample); a larger cost is recorded for the report and not chased (R4 item 0 accepted this class of cost; a surprise here is reported, not experimented on).
- [ ] **Step 6: The documents** — `docs/feedback_loop_design.md`, the header paragraph "Execution deltas from this spec": one more sentence — "2026-10-19 (refactor checkpoint 2.4): the z⁻¹ is a rule, not a position — a tap reads the value its source held at the end of the previous tick (`ValueSource::previous()`) wherever it sits; §2's 'positional' paragraph describes the 2026-08-31..2026-10-19 behaviour." `docs/architecture/part2-valuesource-contract.md` section 2 "Taps": under "Order of work", one line — "Decided 2026-10-19: reading B landed as checkpoint 2.4 (`ValueSource::previous()`; the two-value memo), plan `docs/superpowers/plans/2026-10-19-part2-checkpoint-2.4.md`." Section 7 row 2.4, the "Expected render changes" cell: the measured count and where they live (none in library or baselines).
- [ ] **Step 7: Commit** — `git add -u engine tools/engine_tests/main.cpp docs/feedback_loop_design.md docs/architecture/part2-valuesource-contract.md` ; `git commit -m "core: the tap edge reads the previous tick — a tap is one sample late wherever it sits; the measured renders change"`.

---

### Task 3: Close the checkpoint

**Files:**
- Create: `docs/autonomy/refactor/reports/2026-10-19-checkpoint-2.4.md`
- Modify: `docs/autonomy/refactor/REVIEW.md`, `docs/autonomy/refactor/BACKLOG.md`, `docs/autonomy/STATUS.md`, `tools/structure/metrics.json`, this plan's Ledger

- [ ] **Step 1: Nothing old remains** — `git grep -n "positional\|computed_this_tick\|MFORCE_TRACE_TAPS" -- engine tools` lists nothing; `git grep -n "previous()" -- engine` lists the definition and the one reader (`TapSource::compute`).
- [ ] **Step 2: Every gate** — `python tools/gates.py` (all): everything passes except the two roundtrip gates on the six backlog-79 patches. `python tools/null_gate_wiring.py` 78/78.
- [ ] **Step 3: Whole-tree A/B** — Task 2 step 5's result stands unless the fix wave (step 5) touches engine code; then render `new` again over the eight sets and compare; the Appendix A table lists each set (patches in the set, rendered by both, differ) and names every differing patch by family.
- [ ] **Step 4: Meter and delta** — `python tools/structure/check.py --snapshot`; `python tools/structure/check.py --delta <Task 0 sha>`.
- [ ] **Step 5: Independent review** — a fresh Fable reviewer on the checkpoint's commits (Task 1 and Task 2; Task 0 for the measurement's trustworthiness) with `docs/architecture/REVIEW_BRIEF.md`, given the diff, `MODULES.md`, Part 2 section 2 "Taps" as the signed design and this plan's Review Focus as its focus, told to refute; fix what is right in a fix wave; the same reviewer re-reviews the fixes. Verbatim into section 8.
- [ ] **Step 6: Architecture report R5** — the ten-section format: section 2 entries for `ValueSource::prev_` / `previous()` (the design choice: two values per node, moved at the claim; the alternative: the tap holding its own one-sample state, and why not), `TapSource::compute` / `current` (one guard, two reads), and the measurement (counter, printer, driver — in history at the Task 0 sha); section 3 the one interface added (`previous()`) and `TapSource::read_` → `guard_`; section 4 none; section 5 the meter delta, the A/B by set and by family with the mechanism named (one more sample of loop latency where the tap read was same-tick; with `compensate` on, the loop lands on the length the setting computes), the list against the differ set (the upper bound explained), the timing table; section 6 the Ledger; section 7 the debt (`previous()` public on every node for one reader, the tap edge — Part 3's pins own a narrower door; `step()`'s limits apply to a tap inside a fill, BACKLOG row 9a; the clockless fallback reads A, 2.5's flag; a tap on a stateless view reads the view's memo, no stored patch does it); section 9 the reading guide (`dsp_value_source.h` `next()`/`step()`/`previous()` and `TapSource` whole; the two test sections; ten minutes); section 10 three lessons: (1) the unit delay as state — z⁻¹ in DSP notation is "the previous value", and a field named for it is the cheapest delay line there is; the C# analogue, a `Previous` property on a model updated in exactly one place; why the update sits at the claim (three read positions, one answer); (2) a read is not a pull — `previous()` is `const`, non-virtual and side-effect-free beside a virtual `current()` that stateless views override; when a method should be overridable (the view has a live answer) and when it must not be (the memo is the base's fact); the Java/C# analogue, a `final`/non-virtual method beside a virtual property; (3) measuring with code that must not survive — a `static inline` counter, an RAII printer at the top of `main()` so every return path prints, why neither may stay (the real-time rule, no file-level mutables), and the `git show <sha>:` recipe that makes the measurement reproducible without keeping it. REVIEW R5 awaiting with its verdict items: (0) the changed renders accepted as the frozen truth — all are in Matt's listening queues and sweeps (`audition/trombone1` ×4, `audition/pierce1d_1` ×6, `pending/brass/Trombone_att1`, the `sweep` brass and bore families, `old/nlbore_brass1`), none in library or baselines, so nothing is refrozen; re-listen any before accepting; (1) `previous()` as a public read on every node for the tap edge alone, or a narrower door now; (2) feedback_loop_design.md section 2's "positional" paragraph: left as the 08-31 design with the header delta (Task 2's choice), or rewritten. BACKLOG row 1: 2.4 DONE, 2.5 next; STATUS handoff.
- [ ] **Step 7: Commit** — `git commit -m "docs: checkpoint 2.4 report (R5), REVIEW R5 awaiting, backlog, STATUS handoff, meter snapshot"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **Two values per node, moved when the tick is claimed.** `prev_` takes `cur_` at the moment `next()` or `step()` claims the tick, before the sweep and the compute. Alternative, the tap keeps the delay itself (a value written at the end of each tick): a tap has no end-of-tick hook, and giving it one means a per-tick list of every tap kept by the loader — the kind of machinery checkpoint 2.3 deleted. Alternative, `prev_` written after the compute from a saved local: the same stores, but a reader that arrives mid-compute (every loop closed through a tap on the node whose forward path reaches the tap) would see a value two ticks old, and every loop patch would move. Cost: one `float` per node, one store per compute.
2. **`previous()` is a non-virtual read of the memo.** The stateless views (`NameGate`, `PerformOut`, `TapSource`) override `current()` to answer live because a Setup-time read between ticks must see the present; their previous value is what they computed last tick, which the sweep guarantees they did (a view reached only by taps is on the tail list). No stored patch taps a view (Task 0 step 3a).
3. **No clock: `previous()` is `cur_`.** Every pull computes and "this tick" has no meaning, so the clockless fallback keeps reading A; bare-node tests keep their values; a clockless pull inside a host is 2.5's fault flag, as before.
4. **`current()` on the tap stays the live read.** Between ticks the source's present value is the value it held at the end of the last tick, which is exactly what the tap delivers at the next tick; the trigger-chain and push-binding reads of 2.3 are unchanged.
5. **`step()` moves the value the same way.** A tap inside a stepped subgraph is one step late. Supported only as far as `step()` is (its two stated limits stand; R4 item 3, BACKLOG row 9a); no stored patch has a tap in a fill's excitation or in a push chain (Task 0 step 3b, 3c).
6. **Landing order.** The two-value memo lands first, written and never read — byte-identical, 78/78 (Task 1). The tap's read switches in its own commit (Task 2), the one commit whose renders change, five lines of engine code. Target-architecture principle 6 is kept at commit granularity.
7. **The list is measured again, with the sealed binary.** A counter in `TapSource::compute()` (a source that has claimed this tick and finished its compute — the one position where readings A and B differ), committed in Task 0 and deleted in Task 1, so the measurement is reproducible from history and nothing of it survives a seal. The 10-18 list of 47 (taken at `587b539` by the old trace, before the memo) is the cross-check, not the list; every difference between the two is explained in the measurement document. The A/B rule is 2.3's ruling 16: every differing patch is on the list and explained; the list is an upper bound (a same-tick read of an unchanged value moves nothing).
8. **The old arms are re-used.** The engine is unchanged since `fdf7536` (the diff-stat is empty), so 2.3's `final` arms are the pre-2.4 arms of the six tree sets; only `taps.txt` (the 47 in full, so no every-third sampling hides one) and `retired.txt` get a fresh old arm.
9. **No manifest refreeze.** None of the measured patches is in `patches/library` or `patches/baselines`; the null gate stays 78/78 after every task, and a manifest difference is a bug until explained.
10. **`DelayLineSource::compensate` keeps `comp = 1`.** The setting's assumption — one sample from the closing tap — was false under reading A for every patch on the list (the tap was no delay there) and is true in every rooting now; the comment changes, the code does not. The mechanism of the render change, per family: one more sample of loop latency; with `compensate` on, the loop now lands on the length the setting computes, where it was one sample short.
11. **The design documents record the delta, not a rewrite.** `feedback_loop_design.md` keeps section 2's "positional" paragraph as the 08-31 design and says in its header what changed and when; Part 2 section 2 records the decision. No dated changelog in code.
12. **The retired pair stays in the A/B** (`old.txt`, `retired.txt`): the Junction loop skeleton it carries remains rendered by both arms though it left the gate.

(Execution rulings follow here.)

- **Task 0:** A/B base `fc7ad11`; `cli_pre24.exe` = 2.3's `cli_final.exe` (the engine unchanged since `fdf7536`, diff-stat empty); the six tree sets' old arms are 2.3's `final` arms; `taps` (47) and `retired` (2) rendered fresh. The counter lists 45 patches (library 0, baselines 0, retired 0, pending 1, audition 10, sweep 28 of 419, old 6, taps 45 of 47); the same 157 non-loading patches as on 10-18. Against the 10-18 list: 45 entries at exactly half — the old trace counted the wrapper's read twice per sample (the sweep's advance and the owner's post-sweep `current()`), the counter counts `compute()` once per tick; two dropped (`stk_bowed_ext1/ext_tors_c10`, `stk_bowed_ext2/ext2_larson_btd`) because their bow chain's old advancer (`TorsFeed`, first consumer of `NewVel` in wiring order) sat inside the `TorsDelay` tail, so `StringLP`'s tap read `BridgeDelay` after the root pull had advanced it; under the memo the root pull computes the chain inside `BridgeDelay`'s compute, the mid-compute position where A and B agree. Confirmed by re-running `cli_pre23.exe` with the old trace (1,588,530 / 1,059,020, the 10-18 numbers). Scans: (a) 0 taps on a view; (b) 0 of 62 fills with a tap in the excitation; (c) 0 of 134 dynamicPins refs with a tap in the chain; (d) by family, every listed patch has a DelayLine. Gates at step 0 not re-run (they ran at `a3c7c2e` on the identical engine; every later commit ran them).
- **Task 1:** RED = engine_tests does not compile (`previous` not a member, five sites); GREEN = the contract as written; 6421 checks; null gate 78/78; the measurement deleted (`a8d5aa9` keeps it).
- **Task 2, the plan's `TapSource::current()` did not deliver reading B.** The first A/B arm over the 47 tap patches was byte-identical. A node reads its pins after its sweep through `current()` (2.1's rule; `CombinedSource`, `Biquad`, `CurveNode`), so the tap's live `current()` override — not its `compute()` — is what every consumer uses. Decided: `current()` is the memo once the tap has claimed this tick and the live source value before (a Setup-time read between ticks), through a new protected `ValueSource::claimed_tick()` that `previous()` also uses. Restated test-first: a `CombinedSource` summing `c + tap(c)` reads 1, 3, 5 (the live read gave 2, 4, 6); the post-pull `current()` reads are the memo; 2.3's `run_tap_reads_live_tests` keeps its never-pulled live reads and its last read (after a pull in the same tick) becomes the memo. Cost if wrong: a Setup-time reader of a tap pulled earlier in the same tick sees the memo; no stored patch has a tap on a Manual or trigger pin.
- **Task 2, 2.3's live-read test asserted reading A** at the tap-after-source position (`tap->next() == 2` after `c->next()` in the same tick); the plan wrongly said it held under both readings. One assertion moved to B.
- **Task 2, the A/B** (`cli_pre24` vs `557fc01`'s engine): taps 40 of 47, retired 0 of 2, library 0 of 80, baselines 0 of 52, pending 1 of 40, audition 10 of 708, sweep 24 of 419, old 5 of 157; 40 distinct patches tree-wide, every one on the list, none off it. Five listed patches did not move — the nonlinear-bore controls (`nlb_lip_lad000`, `nlbore_oboe_ctl`, three `onemass_lip1` `_st0`): their only same-tick tap feeds `NL_curve` with knots all 1.0, a flat curve whose output does not depend on its input. Null gate 78/78. Timing neutral (oboe1 1.01x, piano_default 0.99x, viola 0.99x; micro 0.98x–1.04x, D 0.95x at 24 ms = noise).
- **Task 3:** `git grep -w positional -- engine` is the honest check (the plain grep matches "Compositional" in `music/structure.h`); it lists nothing.
- **Task 3, the refutation (R5) and its fix wave — corrections to the rulings above.** (C1) Ruling 10's reason and Task 0 step 3(d)'s "one more sample of loop latency" were wrong: in the trombone, onemass and nlbore families the same-tick tap is `NL_curve ← tap(NL_in)` driving `NL_delay.ratio` (a control read; the closing taps on `Bore` sit mid-compute, where A and B agree, so the loop period is unchanged and `compensate` is irrelevant); in `pierce1d_1` it is `SignCurve ← tap(Delay_line)` driving `Damp_lpf.cutoffFreq` (`compensate` inert there: the loop walk finds no closing tap through input pins); only `stk_blowhole_port`'s `D1In ← tap(D2)` is a signal path. The code decision of ruling 10 stands. (I1) "Whole-tree" overstated: the A/B covers the 1,615-patch sample (sweep and old every third); on disk the class is 116 patches (101 `tap(NL_in)`, 6 pierce1d_1, 9 stk_blowhole; pierce1d_2's four tap mid-compute only), `audition/articulation1/trombone_tongue` among them (no score: the CLI A/B is vacuous for it). (I2) The counter sits at the tap's compute() position, not the delivery position; the A/B over the sample is the proof that no patch is off the list. (I3) Rulings 4 and 18's between-ticks claim holds for a never-pulled tap only: a tap pulled in the tick that just ended answers a Setup-time reader with its memo (one sample older than the coming delivery) and a Setup-time step() through it reads the same memo; inside a fill a tap stepped before its source reads two steps late. Stated in TapSource's comment and the report's §7, pinned as limits in `run_tap_reading_tests`; no engine behaviour changed. (M1) the two "positional" test comments reworded; (M2) `compensate` counts one tap on the cycle, said in its comment; (M3) the tail-list hole for a view on a trigger pin, in §7; (M5) every 10-20 date was wrong — today is 2026-10-19 — the plan, the measurement, the report and the retired folder renamed and every reference corrected; (M6) the scratch `dropped.txt` deleted. (M4) `operator=`'s `prev_` copy is inert in practice; noted.

## Self-review notes

- **Spec coverage.** Part 2 §2 "Taps": readings A and B defined — Task 1 (`previous()`), Task 2 (the tap reads it); "A and B agree everywhere a tap closes a cycle through its forward path" — Task 2's clocked self-counter and the null gate 78/78; "They differ only where a tap today reads a value its source computed earlier in the same tick ... measured before the choice is final" — Task 0; "B follows as its own checkpoint with the affected renders listed" — this plan, Task 2 step 5, the report's section 5. §7 row 2.4 — Task 0's list, Task 2's A/B. feedback_loop_design.md §2 "a tap wire reads what its source computed last tick" — made true by Task 2; "Taps are exactly one sample" — true in every rooting now. Target-architecture principle 6 — ruling 6. The R4 verdict's retired pair — ruling 12.
- **Step scan.** Every code change has a file and line or a code block; every test carries its assertions with the values worked out from the contract (the Task 2 test's expected values were traced position by position against `next()`/`step()`/`previous()` as written); the A/B, measurement and timing commands are given with their expected outputs; the scans of Task 0 step 3 each name the thing counted and the expected count.
- **Type consistency.** `ValueSource::prev_`, `previous()`, `TapSource::guard_`, `TapSource::trace` / `sameTickReads` / `computed_this_tick()` (Task 0 only), `TestHost`, `Counter`, `run_previous_tests`, `run_tap_reading_tests`, `cli_pre24.exe`, `renders/scratch/ab24_tree/<set>/{old,new}` are used with those names throughout.
- **Review Focus.** Items 1, 2, 3, 4 and 5 are pinned in Task 2's test (the first assertion, the clocked self-counter, the two unpulled ticks, the `step()` block, the `current()` assertion); items 4's and the view note's "no stored patch" claims are Task 0 step 3's scans.
- **Proportion.** Code appears for the contract, the measurement hook and the two test sections; the rest is commands with expected outputs. The engine change is about fifteen lines; the plan is mostly the measurement and the proof.

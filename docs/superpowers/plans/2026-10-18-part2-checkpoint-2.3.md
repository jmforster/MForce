# Part 2, checkpoint 2.3: the per-tick memo — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every node computes once per tick and serves that value to every later pull in the same tick, so sharing a source needs no machinery: `RefSource` wrapping, the usage counter, starved-ref promotion, the UI's mirror of the wrap rule and the sharing trace are deleted. Taps keep today's positional one-sample delay (reading A). The renders that change are exactly the measured wiring-order lag list and `double_advance_bug`.

**Architecture:** The host's clock joins the host's facts: `RenderContext` carries a pointer to a `SampleClock`, and every driver loop (a voice, a drum source, a mixer, a loader adapter, the UI's editor stream, a test host) owns one and ticks it once per sample before its pulls. `ValueSource::next()` compares the clock to the tick it last computed at and serves `cur_` when they match; `step()` advances one sample regardless, recursively, for the two table fills that draw many samples at a note's start. The tap edge becomes `TapSource`, the guarded read of its source's memo. The tap-only tail list stays as the loader derives it today, because a node behind a Manual pin is deliberately not pulled every tick and must not be swept.

**Tech Stack:** C++20 (MSVC, CMake 3.20+), the existing `engine_tests` harness, `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/measure_sharing.py` (Task 0 only; deleted in Task 1).

**Spec:** `docs/architecture/part2-valuesource-contract.md` section 2 (the memo: the rule, taps, reading A, the advance list, cycles, what changes in renders) and section 7 (the 2.3 row: expected render changes = the wiring-order lag patches measured beforehand, and `double_advance_bug`); `docs/architecture/target-architecture.md` section 2 principle 6 (structure and behaviour never change in the same checkpoint — this checkpoint's behaviour change is confined to the measured list, landed in its own task); `docs/architecture/measurements/2026-10-16-sharing-lag.md` (the manifest list, 13 patches); report R2 section 6 rulings 6 and 7 (the two tolerated byte-identity classes the memo retires); REVIEW.md R3 verdict (render cost carried here; the getters protected).

## Global Constraints

- No heap allocation, lock or throw in any `next()`, `step()` or `compute()` (CLAUDE.md non-negotiables). The memo adds one `Tick` and two `bool`s per node and nothing else; the clock is read, never allocated, on the sample path.
- Explicit registries, no reflection (CLAUDE.md). Tail-list membership is decided from the file's edges, as today.
- Byte-identity: after Tasks 1, 3 and 4 `python tools/null_gate_perform_source.py --jobs 3` reports `80/80 manifest entries identical`. Task 2 is the one task whose renders change: the differing manifest entries must be exactly the manifest members of Task 0's list, and only then is the manifest refrozen. Any other difference, in any task, is a bug until ruled on in the Ledger.
- No new file over 600 lines, no new function over 80 (`tools/structure/limits.json`); `python tools/structure/check.py --delta <Task 0 sha>` is pasted into the report.
- No dated changelog, backlog id or REVIEW number in code comments.
- Commit after every task; `python tools/gates.py --fast` before every commit (its roundtrip smoke fails on `perform/wiring_setting.json` for the pre-existing backlog-79 reason; every other gate must pass).
- Work on main, no worktree (ruling 1 of checkpoint 2.1 stands). The in-tree build is `build/`; the UI exe may be locked by a running UI — rename-then-link (CLAUDE.md).
- The A/B base for every task is `renders/scratch/ab_tree/cli_pre23.exe` (Task 0 step 0); the timing base for Task 5 is also `cli_1b4fe1b.exe`, the pre-2.1 CLI kept from report R2.

## Review Focus

The five input classes the spec implies but no existing test exercises, most likely to bite first. Each line names the task whose tests pin it.

1. **A node that pulls a source N times in one tick to draw N samples** (WavetableSource's and HybridKSSource's `fill_table` at a note's first sample): under a plain memo every draw would return the same value and the table would be flat. `step()` exists for exactly this; Task 1 pins it (`step()` advances on every call and is recursive; `next()` after it serves the last step).
2. **A source shared between a stepped subgraph and a per-tick consumer** (a noise node wired both to a Wavetable's excitation and to an audible path): the N steps at the note's start move the per-tick consumer's view N samples. No stored patch is known to do this; the whole-tree A/B (Task 2 and Task 6) finds any that does and it is listed; Task 1's test documents the semantics.
3. **A graph pulled by two driver loops** (a mixer-mode patch whose channel source and master gain share a node: the channel's adapter ticks its clock across the block, then the mixer's gain loop ticks its own): the shared node computes once per loop. Today the second holder reads a `RefSource` frozen at the first loop's last value; both are nonsense, mixer MODE goes in Part 4. Task 0 measures it (the trace counts those reads as lag reads); the Ledger records it as unsupported.
4. **A host that never set a clock** (a driver that prepares with `RenderContext{rate}` alone): every pull computes, which is today's semantics and what a bare node in a test wants, but in a host it means a shared node double-advances again with nothing said. Task 1's test documents the fallback; the Release-visible report of a clockless pull inside a host is checkpoint 2.5's fault flag (section 7 of the report).
5. **A cycle with no tap** (illegal; the UI refuses it, the loader never builds one, a hand-wired test can): the re-entered node must serve its last value and say so once, never recurse. Task 1 pins it.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `engine/include/mforce/core/render_context.h` (modify) | `Tick`, `SampleClock { Tick now; void tick(); }`, `RenderContext { int sampleRate; const SampleClock* clock; }`: the host's facts, now two. |
| `engine/include/mforce/core/dsp_value_source.h` (modify) | `ValueSource::next()` is the memo; `step()` the unconditional advance; `advance()` and `SharingTrace` gone; `TapSource` replaces `RefSource`; the pins' `next()` / `step()` bodies. |
| `engine/include/mforce/core/pins.h` (modify) | `Param` and `Input` declare `next()` and `step()` in place of `advance()`. |
| `engine/include/mforce/render/instrument.h` (modify) | `VoiceGraph` owns its `SampleClock` and completed `RenderContext`, and `tick()`s; `PitchedInstrument::prepare` completes each voice's context; `render_chunk` ticks the voice; `StreamingVoice` carries the clock and `tick()`s; per-note envelopes are prepared with the voice's context; `DrumKit`'s sources own clocks. |
| `engine/include/mforce/render/mixer.h`, `engine/src/mixer.cpp` (modify) | `StereoMixer` owns a clock for its gain, volume and pan pulls. |
| `engine/include/mforce/render/perform_source.h` (modify) | `tick()` pulls the bend with `next()`; the comment no longer explains `RefSource`. |
| `engine/src/patch_loader.cpp` (modify) | `resolve_param` resolves; it neither counts nor wraps. `promote_starved_refs` is gone. `collect_advance_ids` stays. The two adapters own clocks. `TapBind` holds a `TapSource`. |
| `engine/include/mforce/source/delay_line_source.h`, `engine/include/mforce/core/name_gate.h` (modify) | the loop walk recognises `TapSource`; comments no longer name `RefSource`. |
| `engine/include/mforce/source/shaper_source.h` (modify) | pins Auto again; `compute()` reads after the sweep. |
| the fourteen node files with Manual pulls (modify, mechanical) | `.advance()` → `.next()` (two sites → `.step()`): `formant.h`, `partials.h`, `combined_source.h`, `layered_red_noise_source.h`, `multiplex_source.h`, `phased_value_source.h`, `repeating_source.h`, `shaper_source.h`, `wave_evolution.h`, `biquad_source.h`, `vibrato.h`, `multi_source.h`, `additive_source2.cpp`, `full_additive_source.cpp`, `hybrid_ks_source.cpp` (step), `wavetable_source.cpp` (step, next). |
| `tools/mforce_cli/main.cpp`, `tools/mforce_cli/explore.cpp` (modify) | the trace switch and its exit print go; explore's loop ticks its voice. |
| `tools/mforce_ui/main.cpp` (modify) | one `g_editorClock` in `g_renderContext`, ticked once per sample by the three editor-stream loops; the live voices tick their own; `update_all_dsp` stops wrapping; `collect_envelopes` follows a `TapSource`. |
| `tools/engine_tests/main.cpp` (modify) | `run_memo_tests` (new); the sharing-trace test gone; `RefSource` tests become `TapSource` or memo tests; `->advance()` → `->next()`. |
| `tools/measure_sharing.py` (delete, Task 1) | its instrumentation goes with the memo; its two measurement documents stay. |
| `tools/ab_render_time.py` (modify, Task 5) | the patch list is the three library patches and the nine micro patches. |
| `docs/architecture/measurements/2026-10-18-sharing-lag-tree.md` (new, Task 0) | the whole-tree list of patches whose renders change. |

---

## The contract (what Task 1 writes; every later task codes against it)

```cpp
// render_context.h
using Tick = long long;

// The driver's sample counter. One per pull loop — a voice, a drum source, a
// mixer, an adapter, the UI's editor stream, a test host — ticked once per
// sample before that loop's pulls. Nodes read it through their context.
struct SampleClock {
  Tick now{0};
  void tick() { ++now; }
};

struct RenderContext {
  int sampleRate{0};                  // 0: no host has prepared this node yet
  const SampleClock* clock{nullptr};  // null: no host clock; every pull computes
};

// dsp_value_source.h
struct ValueSource : Component {
  // This tick's value. Computed once per tick — the sweep, then compute() —
  // and served to every later pull in the same tick, whoever asks and in
  // whatever order. A node re-entered during its own compute (a cycle with
  // no tap) gets the previous value and the cycle is reported once.
  float next() {
    if (computing_) { report_cycle_once_(); return cur_; }
    if (ctx_.clock) {
      const Tick t = ctx_.clock->now;
      if (t == lastTick_) return cur_;
      lastTick_ = t;
    }
    computing_ = true;
    for (auto* s : auto_sources()) if (*s) (*s)->next();
    cur_ = compute();
    computing_ = false;
    return cur_;
  }
  // One sample now, whatever the tick, through every Auto pin: for a node
  // that drives a source at its own rate (a table fill draws N samples at a
  // note's start). The memo holds the last stepped value.
  float step() {
    if (ctx_.clock) lastTick_ = ctx_.clock->now;
    for (auto* s : auto_sources()) if (*s) (*s)->step();
    cur_ = compute();
    return cur_;
  }
  virtual float compute() = 0;
  virtual float current() const { return cur_; }
  // prepare(ctx) and start(frames) as today, each also clearing computing_.
  ...
private:
  RenderContext ctx_{};
  Tick lastTick_{-1};
  bool computing_{false};
  bool cycleReported_{false};
};

// The tap edge: a guarded read of its source's memo, never a pull. Positional
// z^-1 (reading A): inside a cycle entered through its forward path it reads
// last tick's value. Non-finite to 0, clamped to ±8.
struct TapSource final : ValueSource {
  std::shared_ptr<ValueSource> source;
  explicit TapSource(std::shared_ptr<ValueSource> src = nullptr);
  float compute() override { return read_(); }
  float current() const override { return read_(); }   // a Setup-time read sees the live value
  const char* type_name() const override { return "Tap"; }
};

// pins.h / dsp_value_source.h
float Param::next();   // the source's value this tick, or the constant
float Param::step();   // one sample now, or the constant
float Input::next();   // ..., or 0 when unwired
float Input::step();

// instrument.h
struct VoiceGraph {
  std::shared_ptr<SampleClock> clock{std::make_shared<SampleClock>()};
  RenderContext ctx;                              // the host's facts completed with this voice's clock
  void tick() { clock->tick(); if (performSource) performSource->tick(); }
  ...
};
struct StreamingVoice {
  std::shared_ptr<SampleClock> clock;
  void tick() { clock->tick(); if (performSource) performSource->tick(); }   // once per sample, before source->next()
  ...
};
```

The per-sample driver loop, everywhere, is: `tick(); out = root->next(); for each tail: tail->next();`.

---

### Task 0: Measure the renders that will change; keep the base

Inline (controller). Nothing is cut until the list exists.

**Files:**
- Create: `docs/architecture/measurements/2026-10-18-sharing-lag-tree.md`
- Modify: `tools/measure_sharing.py` (a `--set <file>` option: the patch list from a set file instead of the manifest)

- [ ] **Step 0: Keep the pre-checkpoint binary.** `python tools/gates.py --fast` passes (known roundtrip failure only), `mforce_ui --stamp` clean; `copy build\tools\mforce_cli\Release\mforce_cli.exe renders\scratch\ab_tree\cli_pre23.exe`. Record `git rev-parse HEAD` in the Ledger as the A/B base.
- [ ] **Step 1: The untraced-consumer check.** `python tools/measure_sharing.py --consumers` prints exactly one line, `noise_sources.h:34` (`pink_.next()`: an owned member, not a shared pin). Any other line is a pull the trace cannot see and is read before the list is trusted.
- [ ] **Step 2: Build the whole-tree set.** The set files from the 2.2 close, `renders/scratch/ab22_tree/*.txt` (library, baselines, audition, pending in full; sweep and old every third), copied to `renders/scratch/ab23_tree/`; regenerate any that are missing with the recipe in report R3 Appendix A.
- [ ] **Step 3: Measure.** `python tools/measure_sharing.py --set <each set file> --out docs/architecture/measurements/2026-10-18-sharing-lag-tree.md`: for every patch, lag reads and same-tick taps. The document lists (a) every patch with lag reads > 0, grouped by tree, with the count — this is **the list**: the renders Task 2 changes; (b) every patch with same-tick taps > 0 — checkpoint 2.4's list, free here; (c) patches that produced no trace line (a load failure), named. The manifest's 13 from the 2026-10-16 document must all reappear with the same counts; a changed count is explained in the document before going on.
- [ ] **Step 4: Timing baseline.** Task 5's method, run now so the "before" is measured on the same day: `python tools/ab_render_time.py renders/scratch/ab_tree/cli_1b4fe1b.exe renders/scratch/ab_tree/cli_pre23.exe 7` after Task 5 step 1's patch-list fix (do that edit here; it is a tool, not engine code). Paste the table into the measurement document's last section.
- [ ] **Step 5: Commit** — `git add docs/architecture/measurements/2026-10-18-sharing-lag-tree.md tools/measure_sharing.py tools/ab_render_time.py` ; `git commit -m "measure: the whole-tree sharing-lag list for checkpoint 2.3; timing baseline"`.

---

### Task 1: The clock, the memo, `step()`; every driver ticks; the trace goes

Inline (controller). One buildable change: the compiler finds every `advance()` and every driver.

**Files:**
- Modify: `engine/include/mforce/core/render_context.h`, `engine/include/mforce/core/dsp_value_source.h`, `engine/include/mforce/core/pins.h:61,80`
- Modify (rename `.advance()` → `.next()`): `engine/include/mforce/source/additive/formant.h:57,186-187,268`, `engine/include/mforce/source/additive/partials.h:389-396`, `engine/include/mforce/source/combined_source.h:116-121`, `engine/include/mforce/source/layered_red_noise_source.h:78`, `engine/include/mforce/source/multiplex_source.h:107,110`, `engine/include/mforce/source/phased_value_source.h:121-133`, `engine/include/mforce/source/repeating_source.h:39`, `engine/include/mforce/source/shaper_source.h:125-133`, `engine/include/mforce/source/wave_evolution.h:386,389,478-479,592,595,1257-1258`, `engine/include/mforce/filter/biquad_source.h:52-53`, `engine/include/mforce/filter/vibrato.h:82`, `engine/include/mforce/core/multi_source.h:65`, `engine/src/additive_source2.cpp:152-153`, `engine/src/full_additive_source.cpp:57,59`, `engine/src/wavetable_source.cpp:123`, `engine/include/mforce/render/perform_source.h:75`
- Modify (`.advance()` → `.step()`): `engine/src/hybrid_ks_source.cpp:49`, `engine/src/wavetable_source.cpp:35`
- Modify (drivers): `engine/include/mforce/render/instrument.h` (VoiceGraph 155-175, prepare 191-197, StreamingVoice 268-282, prepare_voice_at 367-391, the bend at 342-346, make_glide 437-448, render_chunk 452-476, play_note's three paths, DrumKit 746-770), `engine/src/mixer.cpp:22-55` and `mixer.h`, `engine/src/patch_loader.cpp:57-75` (the adapters), `tools/mforce_cli/main.cpp:1218-1222`, `tools/mforce_cli/explore.cpp:362`, `tools/mforce_ui/main.cpp:235,3745-3755,3787-3824,3908-3922,3960-3987,4180-4190,4720-4730,4828-4835,13310-13316,13415-13430`
- Delete: `tools/measure_sharing.py`
- Test: `tools/engine_tests/main.cpp` (`run_sharing_trace_tests` 2160-2201 deleted; `run_memo_tests` new; the ten `->advance()` sites → `->next()`)

**Interfaces:**
- Produces: `Tick`, `SampleClock`, `RenderContext::clock`; `ValueSource::next()` (memo), `ValueSource::step()`; `Param::next/step`, `Input::next/step`; `VoiceGraph::clock`, `VoiceGraph::ctx`, `VoiceGraph::tick()`; `StreamingVoice::clock`, `StreamingVoice::tick()`; `StereoMixer::clock_` (private); the two adapters' clocks (private).
- Consumes: nothing new.

- [ ] **Step 1: Write the failing tests** (`tools/engine_tests/main.cpp`, replacing `run_sharing_trace_tests`):

```cpp
// The memo: one compute per tick however many pulls, in whatever order;
// step() advances now; no clock means every pull computes; a cycle with no
// tap is finite.
struct TestHost { SampleClock clock; RenderContext ctx{48000, &clock}; };
struct Counter final : Generator {
  int computes{0};
  float compute() override { return float(++computes); }
  const char* type_name() const override { return "Counter"; }
};

static void run_memo_tests() {
  TestHost h;
  auto c = std::make_shared<Counter>();
  auto sum = std::make_shared<CombinedSource>(c, c, CombineOp::Sum, 0.0f);   // one node on both pins, no wrapper
  sum->prepare(h.ctx); sum->start(16);
  h.clock.tick();
  CHECK_NEAR(sum->next(), 2.0f, 1e-6f);     // computed once, served once
  CHECK(c->computes == 1);
  CHECK_NEAR(c->next(), 1.0f, 1e-6f);       // a third pull this tick: the memo
  CHECK(c->computes == 1);
  h.clock.tick();
  CHECK_NEAR(sum->next(), 4.0f, 1e-6f);
  CHECK(c->computes == 2);
  h.clock.tick();                           // order independence: the leaf first
  CHECK_NEAR(c->next(), 3.0f, 1e-6f);
  CHECK_NEAR(sum->next(), 6.0f, 1e-6f);
  CHECK(c->computes == 3);
  h.clock.tick();                           // step(): one sample now, whatever the tick
  CHECK_NEAR(c->step(), 4.0f, 1e-6f);
  CHECK_NEAR(c->step(), 5.0f, 1e-6f);
  CHECK_NEAR(c->next(), 5.0f, 1e-6f);       // the memo holds the last step
  CHECK(c->computes == 5);
  h.clock.tick();                           // step() is recursive through Auto pins
  auto chain = std::make_shared<CombinedSource>(c, std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
  chain->prepare(h.ctx); chain->start(16);
  CHECK_NEAR(chain->step(), 6.0f, 1e-6f);
  CHECK_NEAR(chain->step(), 7.0f, 1e-6f);
  CHECK(c->computes == 7);
  CHECK_NEAR(sum->step(), 17.0f, 1e-6f);    // a node on two pins of a stepped parent steps twice (8 + 9): Review Focus 2
  CHECK(c->computes == 9);
  auto bare = std::make_shared<Counter>();  // no clock: every pull computes
  bare->prepare(RenderContext{48000}); bare->start(16);
  bare->next(); bare->next(); bare->next();
  CHECK(bare->computes == 3);
  auto loop = std::make_shared<CombinedSource>(std::make_shared<ConstantSource>(1.0f),
                                               std::make_shared<ConstantSource>(0.0f), CombineOp::Sum, 0.0f);
  loop->set_param("source2", loop);         // a cycle with no tap: illegal, must be finite
  loop->prepare(h.ctx); loop->start(16);
  h.clock.tick();
  CHECK_NEAR(loop->next(), 1.0f, 1e-6f);    // 1 + the previous value (0)
  h.clock.tick();
  CHECK_NEAR(loop->next(), 2.0f, 1e-6f);    // 1 + 1
  loop->clear_param("source2");             // break the ownership cycle
}
```

Also: `PinCounter` / `PrepCounter` / `OrderProbe` keep counting `on_start`; nothing in them pulls. The ten `->advance()` calls in the file become `->next()`.

- [ ] **Step 2: Run to verify it fails** — the build fails: `SampleClock`, `RenderContext::clock`, `step()` do not exist.
- [ ] **Step 3: The contract** — `render_context.h` and `dsp_value_source.h` exactly as "The contract": delete `SharingTrace` and `lastAdvancedSample_`; delete `advance()`; `next()` and `step()` as shown; `prepare()` and `start()` each set `computing_ = false`; `report_cycle_once_()` prints `[graph] cycle without a tap through <type_name()>: serving the previous value` to stderr once per node (`cycleReported_`). `RefSource::read()` loses its trace lines (the type itself goes in Task 3). The pin bodies: `Param::next/step`, `Input::next/step` (the `advance` bodies renamed; `step` calls `source->step()`). `pins.h`: the declarations.
- [ ] **Step 4: The nodes** — every site in Files: `.advance()` → `.next()`, except the two table fills → `.step()`. `perform_source.h:75`: `bend_->next()`. Build until green; `git grep -n "advance()" -- engine` must list nothing.
- [ ] **Step 5: The drivers** — `instrument.h`: `VoiceGraph` gains `clock`, `ctx`, `tick()`; `PitchedInstrument::prepare(c)`: `Instrument::prepare(c)`, then per voice `vg.ctx = c; vg.ctx.clock = vg.clock.get();` and the three prepares take `vg.ctx`; the bend (342-346) and `make_glide` (gains a `const VoiceGraph&` first parameter) prepare their envelope with `vg.ctx`; `render_chunk`: `vg.tick();` replaces the `SharingTrace` bump and the perform tick, `vg.source->next()`, tails `a->next()`; `prepare_voice_at` returns the clock too (`StreamingVoice` gains `clock` and `tick()`, declared as in the contract); `DrumKit::DrumSource` gains `SampleClock clock; RenderContext ctx;`, `DrumKit::prepare` completes each, `play_hit` ticks per sample. `mixer.h/.cpp`: `SampleClock clock_` and `RenderContext ctx_`; `prepare(ctx)` stores, completes with `&clock_`, prepares `gainL/gainR` and each channel's `volume/pan` with `ctx_`, each channel's `source` with the host's `ctx` (an adapter or instrument completes its own); the gain loop ticks `clock_` per iteration before the four pulls (`->next()`). `patch_loader.cpp` adapters: `SampleClock clock_; RenderContext ctx_;`, `prepare` completes and forwards, `render` ticks then `src->next()`. `mforce_cli/main.cpp`: the trace switch and print go. `explore.cpp:362`: `sv.tick();` before the pull (today it never ticked the perform clock; the bend was frozen there — note in the Ledger). UI: `static mforce::SampleClock g_editorClock; static mforce::RenderContext g_renderContext{AUDIO_SAMPLE_RATE, &g_editorClock};` (235; the assignment at 4077 keeps the pointer); the voice struct at 3745-3755 gains `std::shared_ptr<mforce::SampleClock> clock` copied from the `StreamingVoice` (3787-3824, 4720-4730, 4828-4835) and moved to `dyingSources`' sibling list at 4180-4190 — the clock dies with the graph; the callback loop 3908-3922: `voice.tick()` replaces the perform tick, `->next()` unchanged; the editor-stream pulls 3960-3987 are preceded, once per sample, by `g_editorClock.tick()`; the headless loops 13310-13316 (`sv.tick()`) and 13415-13430 (`g_editorClock.tick()` per sample) the same. Comments at 3747-3751 say "tick() once per sample before source->next()".
- [ ] **Step 6: Delete `tools/measure_sharing.py`.**
- [ ] **Step 7: Verify** — build all three targets; `engine_tests` passes with `run_memo_tests`; `python tools/null_gate_perform_source.py --jobs 3` → 80/80 (ruling 6: with the wrapping still in place a shared source has one puller, so the memo never serves a second consumer and every read is what it was); `python tools/gates.py --fast`; the UI opens oboe1, plays a key, Generates, and `--stamp` passes. `git grep -n "SharingTrace\|lastAdvancedSample_\|MFORCE_TRACE_SHARING" -- engine tools` lists nothing.
- [ ] **Step 8: Commit** — `git commit -m "core, render, hosts: the per-tick memo — next() computes once per tick; step() for a node that drives a source at its own rate; every driver owns a clock"`.

---

### Task 2: Delete the wrapping and the promotion; the renders on the list change

Inline (controller). The one task whose renders change.

**Files:**
- Modify: `engine/src/patch_loader.cpp:94-139` (`resolve_param`, `resolve_param_or`: the `usage` parameter and the wrapping branch go), `:150-181` and `:225-336` (both `promote_starved_refs` and their comment block go; the `collect_advance_ids` comment is rewritten), `:626-640` (the `usage` map and `resolveFn` capture), the 25 `&usage` / `usage` arguments, the six call sites `1161`, `1560`, `1751`, `1786-1802` (with the `coneRoots` collection), `1873`
- Modify: `tools/mforce_ui/main.cpp:1024-1086` (`update_all_dsp` keeps its first pass; the second pass and the `Consumer` map go; the comment says the memo serves every consumer)
- Modify: `engine/include/mforce/render/instrument.h:160-166,277-281` (comments: the tails' forward inputs are served by the memo, not a wrap)
- Modify: `tools/null_gate_manifest.json` (refrozen, step 4)

**Interfaces:**
- Produces: `resolve_param(const json&, const valueNodes&)`, `resolve_param_or(params, key, def, valueNodes)` — two and four parameters.
- Consumes: Task 1's memo.

- [ ] **Step 1: Write the failing test** — `tools/engine_tests/main.cpp`: a patch JSON string with one `Envelope` wired to two pins of two different nodes (both `{"ref":"env"}`), loaded with `load_instrument_patch_json`, one note played and rendered twice from two loads: bit-identical, and the envelope's value read through `pitched->voicePool[0].nodesById.at("env")->current()` after the render equals the value a lone copy of the same envelope reaches after the same number of samples (`start(n)`, `n` pulls under a `TestHost`) — the shared envelope advanced once per sample, not twice. Before this task the second pin holds a `RefSource`, so `nodesById.at(...)` finds the real node and the check already passes; after it the pin holds the node itself and the check proves no double advance. Keep the test anyway: it is the memo's loader-level guarantee.
- [ ] **Step 2: The loader** — as Files says. `collect_advance_ids`' comment: membership is decided from the file's edges because they are complete there (a built graph hides multi-input entries and setter-held envelopes); a tail's forward inputs, if the root pull reached them this tick, are served from the memo. Build; `git grep -n "RefSource" -- engine/src/patch_loader.cpp` lists only the tap branch and `TapBind` (Task 3's).
- [ ] **Step 3: The UI** — `update_all_dsp`: the first pass only. Build the UI.
- [ ] **Step 4: The expected differences** — `python tools/null_gate_perform_source.py --jobs 3`: the differing entries must be exactly the manifest members of Task 0's list (13 at the last measurement). Then, and only then, `python tools/null_gate_perform_source.py --freeze`; rerun → 80/80. For each refrozen entry, one line in the Ledger: patch, lag reads, what the render change is (a wrapped consumer now reads this sample instead of the previous one; `double_advance_bug` renders without the double advance it was written to show — it stays as the memo's regression baseline).
- [ ] **Step 5: The whole-tree A/B** — `python tools/ab_render_set.py render <set> renders/scratch/ab23_tree/<set> old --cli renders/scratch/ab_tree/cli_pre23.exe`, `... new` with the build, `compare`: the `differ` set over every set file must equal Task 0's list, patch for patch. A patch that differs and is not on the list, or is on the list and does not differ, is investigated before the commit (Review Focus 2 and 3 are the known candidates; a difference there is listed, with its mechanism named; anything else is a bug).
- [ ] **Step 6: Commit** — `git add -u engine tools/mforce_ui/main.cpp tools/null_gate_manifest.json tools/engine_tests/main.cpp` ; `git commit -m "loader, ui: sharing needs no machinery — the usage counter, RefSource wrapping, starved-ref promotion and the UI's wrap mirror are gone; the measured renders change"`.

---

### Task 3: `TapSource`

Sonnet implementer + Sonnet reviewer (mechanical, bounded, byte-identical), or inline.

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (`RefSource` → `TapSource` as in the contract; no `guard` flag), `engine/src/patch_loader.cpp:83-95,130-136` (`TapBind`, the tap branch, the comment), `engine/include/mforce/source/delay_line_source.h:99-119` (`walk_to_tap`: a `TapSource` whose source is `this` closes the loop; any other tap is walked through, as today), `engine/include/mforce/core/name_gate.h:17-19`, `engine/include/mforce/render/perform_source.h:111-117` (comments: the stateless views read live for Setup-time reads), `tools/mforce_ui/main.cpp:969-972,4738-4751` (the tap wire makes a `TapSource`; `collect_envelopes` follows it)
- Test: `tools/engine_tests/main.cpp:175-199` (`run_tap_guard_tests`: `TapSource` only; the "plain passes through" case goes), `:205-215`, `:330-365` (`RefSource(x, true)` → `TapSource(x)`), `:4249`, `:4305` (the two unguarded `RefSource` uses: wire the node directly; the test runs under a `TestHost` and ticks per pull)

**Interfaces:**
- Produces: `TapSource(std::shared_ptr<ValueSource> src = nullptr)`, `TapSource::source`.
- Consumes: Task 1's memo; Task 2's unwrapped wiring.

- [ ] **Step 1: Rename and narrow** — `TapSource` exactly as the contract; `read_()` is today's guarded `read()` without the trace. Build: every `RefSource` reference is an error this task owns. `git grep -n "RefSource" -- engine tools` lists nothing when green.
- [ ] **Step 2: The tests** — as Files says; `run_tap_cycle_tests` and the DelayLine loop tests pass unchanged in their assertions (the positional z^-1 is reading A).
- [ ] **Step 3: Verify** — `engine_tests`; `python tools/null_gate_perform_source.py --jobs 3` 80/80 on the refrozen manifest; the UI: a tap-closed loop patch (`patches/baselines/feedback/loop_hysteresis_bow.json`) plays and key-up stops it (`--gatecheck` on it lists its envelopes).
- [ ] **Step 4: Commit** — `git commit -m "core: the tap edge is TapSource; RefSource is gone"`.

---

### Task 4: ShaperSource's pins are Auto again

Sonnet implementer + Sonnet reviewer, with the A/B as judge.

**Files:**
- Modify: `engine/include/mforce/source/shaper_source.h` (the constructor's `Processor(Pull::Manual)` → `Processor()`; every `Pull::Manual` on its Params → default; `compute()` reads `input.current()`, `drive.current()`, ... after the base's sweep; the read-timing comment goes)
- Test: `tools/engine_tests/main.cpp` (the ShaperSource read-timing test from checkpoint 2.1 — find it with `grep -n "Shaper" tools/engine_tests/main.cpp` — becomes an `OrderProbe` declaration-order check like the other families', or goes if one exists)

- [ ] **Step 1: The change** — as Files says. Build.
- [ ] **Step 2: Verify** — `engine_tests`; null gate 80/80; the Shaper A/B: `grep -rl '"type": *"Shaper' patches > renders/scratch/ab23_tree/shaper.txt` (1,298 patches at planning time), render `old` with the Task 3 build (copy it to `renders/scratch/ab_tree/cli_t3.exe` before this task) and `new` with this build, `compare` → `0 differ`: under the memo a Manual read and a swept read see the same value, so the type's renders do not move again (its lag-read patches already changed in Task 2).
- [ ] **Step 3: Commit** — `git commit -m "nodes(shaper): pins Auto; the memo retires the read-timing distinction"`.

---

### Task 5: Render time — recovered, or the design question stated

Inline (controller). Report R2 item 0 was carried here by Matt's verdict: the per-sample path is rewritten in this checkpoint, so this is where the cost is recovered or the design revisited.

**Files:**
- Modify: `tools/ab_render_time.py` (`PATCHES` = `patches/library/winds/oboe1.json`, `patches/library/keys/acoustic_piano/piano_default.json`, `patches/library/strings/viola_default.json`, and the nine `renders/scratch/ab_tree/micro/*.json` — done in Task 0 step 4)

- [ ] **Step 1: Measure** — `python tools/ab_render_time.py renders/scratch/ab_tree/cli_1b4fe1b.exe build/tools/mforce_cli/Release/mforce_cli.exe 7` and the same against `cli_pre23.exe`. MIN over rounds per patch, as the tool reports.
- [ ] **Step 2: Decide by the rule** — oboe1, piano_default and viola_default each within 5 percent of the pre-2.1 binary: recovered; record and go to Task 6. Otherwise one experiment, `cli_exp1`: the sweep reads a flat array of raw `ValueSource*` refreshed in `prepare()` and `start()` (the two host calls that follow every wire, including a Setup push), in place of the pointer-to-`shared_ptr` indirection; A/B it (whole manifest 80/80) and time it. Keep it only if it is byte-identical and buys at least ten points on the forty-node chains; otherwise revert it.
- [ ] **Step 3: State the question if it remains** — the report's section 5 gives the three tables (pre-2.1 vs this build; pre-2.3 vs this build; the micro set) and, if the cost is not recovered, the design question in one paragraph for REVIEW R4 item 0: the generic per-node sweep against hand-written `next()` bodies, with the measured price per node per sample, and the two routes (accept it; or a `compute()`-pulls-its-pins form for the hot leaf types, decided per family in Part 3). No further experiments here.
- [ ] **Step 4: Commit** — only if the experiment is kept: `git commit -m "core: the sweep reads raw pointers refreshed at prepare and start"`.

---

### Task 6: Close the checkpoint

Inline (controller), with one Fable refuter.

**Files:**
- Modify: `tools/structure/metrics.json` (snapshot), `docs/autonomy/refactor/reports/2026-10-xx-checkpoint-2.3.md` (new), `docs/autonomy/refactor/REVIEW.md` (R4 awaiting), `docs/autonomy/refactor/BACKLOG.md`, `docs/autonomy/STATUS.md`, `docs/architecture/part2-valuesource-contract.md` (section 2's "advance list, generalised" paragraph gets a one-line note pointing at ruling 4; the 2.3 row's expected changes are the measured list)

- [ ] **Step 1: Confirm nothing old remains** — `git grep -n "RefSource\|SharingTrace\|promote_starved\|advance()" -- engine tools` lists nothing (the pins' `next()`/`step()` are the only pull verbs); `git grep -n "usage" -- engine/src/patch_loader.cpp` lists nothing about a counter.
- [ ] **Step 2: Every gate** — `python tools/gates.py` (all): everything passes except the known roundtrip set. `python tools/null_gate_wiring.py` 80/80 on the refrozen manifest.
- [ ] **Step 3: Whole-tree A/B** — against `cli_pre23.exe` with the final binary over every `ab23_tree` set: the `differ` set equals Task 0's list exactly; the Appendix A table lists each.
- [ ] **Step 4: Meter and delta** — `python tools/structure/check.py --snapshot`; `python tools/structure/check.py --delta <Task 0 sha>`.
- [ ] **Step 5: Independent review** — a fresh Fable reviewer on the checkpoint's commits with `docs/architecture/REVIEW_BRIEF.md`, given the diff, `MODULES.md` and Part 2 section 2, told to refute; fix what is right in a fix wave; the same reviewer re-reviews the fixes. Verbatim into section 8.
- [ ] **Step 6: Architecture report R4** — the ten-section format: entries for `SampleClock` / `RenderContext`, `ValueSource::next` / `step`, `TapSource`, the voice / drum / mixer / adapter clocks, the loader's `resolve_param` and `collect_advance_ids`, the UI's `update_all_dsp`; section 3 lists `advance()` → `next()` / `step()` on the node and the pins, `RefSource` → `TapSource`, `resolve_param`'s signature, `StreamingVoice::tick()`, `make_glide`'s parameter; section 4 the two deleted duplicates (the UI's mirror of the wrap rule; the promotion's second JSON walk); section 5 the delta, the A/B (the list, patch by patch, with each change named), the timing tables; section 6 the Ledger; section 7 the debt (a clockless pull inside a host is silent until 2.5's flag; `computing_` stays set after a throw until the next prepare or start, which 2.5's no-throw rule retires; mixer-mode cross-loop sharing unsupported; reading B is 2.4; `step()` does not guard re-entry); section 10 three lessons (a memo keyed on a clock versus a scheduler, and `Lazy<T>`/per-frame caches as the C# analogue; an observing pointer in a value-type context, its lifetime by ownership, and why not a reference member or a `shared_ptr`; two entry points on one Template Method, `next` and `step`, and what the second one is for). REVIEW R4 awaiting with its verdict items (0: the render-time ruling if Task 5 left the question; 1: the list of changed renders accepted as the new frozen truth; 2: `step()` as a permanent verb or a Part 3 item for the two table fills); BACKLOG 2.3 done, 2.4 next; STATUS handoff.
- [ ] **Step 7: Commit** — `git commit -m "docs: checkpoint 2.3 report (R4), REVIEW R4 awaiting, backlog, STATUS handoff, meter snapshot"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **The tick is the host's clock, reached through the context.** `RenderContext` carries `const SampleClock*`; every driver loop owns a `SampleClock` and ticks it once per sample before its pulls; `prepare(ctx)` is where a node learns it, exactly as it learns the rate. Alternative, the spec's sketch `next(Tick t)` threaded down the pull: every pull signature changes (`next`, both pin helpers, about sixty Manual sites), a pin would need an owner pointer to find the tick, and `compute()` would gain a parameter no node uses. Alternative, a thread-local tick bumped by the root pull: a tail pulled after the root would start a new tick and double-advance its forward inputs, and a test's top-level pull is indistinguishable from a driver's. JUCE's `AudioPlayHead`, reached through the processor's host, is the precedent for "the host's time is a host fact".
2. **No clock: every pull computes.** A bare node in a test, pulled in a loop, behaves as it always has; the hosts always complete the clock. The cost is that a driver written without a clock gets today's double-advance semantics silently; checkpoint 2.5's fault flag reports a pull on a clockless node from inside a host (section 7). Alternative, a Debug assert in `next()`: every one of the 197 direct pulls in `engine_tests` would need a ticking host first; rejected as churn for a test-only concern.
3. **Two verbs: `next()` and `step()`.** `next()` is this tick's value; `step()` is one sample now, recursively through Auto pins, for a node that drives a source at its own rate. Two sites need it (the WavetableSource and HybridKS table fills draw N samples in one tick). Without it every wavetable patch's table would be flat. The pins' `advance()` helpers become `next()` and `step()` so the vocabulary is the node's.
4. **The tail list stays as the loader derives it; the spec's "every node whose tick is not t" is rejected.** PhasedValueSource's inactive stages, RepeatingSource's input between repetitions, Multiplex's solo input, a trigger chain read at Setup: each is behind a Manual pin and deliberately not pulled every tick; sweeping them is a behaviour change. Membership from the file's edges is complete (a built graph hides multi-input entries and setter-held envelopes); under the memo a tail's forward inputs are served, not re-advanced, so the list needs no wrapping to be safe.
5. **Taps: `TapSource`, always guarded, reading A.** The guarded `RefSource` renamed and narrowed; `current()` stays live for Setup-time reads. The unguarded `RefSource`, the usage counter, starved-ref promotion (both JSON walks) and the UI's second pass are deleted. Reading B is checkpoint 2.4.
6. **Landing order for byte-identity.** The memo and the clocks land first with the wrapping still in place (Task 1): a shared source then has one puller, so the memo never serves a second consumer and no read moves — null gate 80/80. The wrapping is deleted in its own task (Task 2) and the measured renders change there and nowhere else. `TapSource` and ShaperSource follow, byte-identical on the refrozen manifest.
7. **The expected-diff list is measured over the whole tree, not the manifest.** The 2026-10-16 measurement covered the 80 manifest patches; audition, pending and sweep patches with lag reads (the `feedback_valve2` queue among them) change too and must be listed before the cut. The whole-tree A/B at Task 2 and at the close must differ on exactly the list.
8. **Each driver loop owns its clock; cross-loop sharing in mixer mode is unsupported.** A node shared between a channel's adapter and the mixer's gain loop computes once per loop (today the second holder reads a value frozen for the block). Measured by Task 0; mixer MODE goes in Part 4.
9. **A cycle with no tap is finite.** `computing_` set around the sweep and compute; a re-entered node serves its previous value and reports once to stderr. The flag is cleared by `prepare()` and `start()`, so a throw inside `compute()` (allowed until 2.5) cannot poison a node past the next note.
10. **Render time is measured against pre-2.1 and pre-2.3, with one bounded experiment.** The rule (within 5 percent of pre-2.1 on the three library patches) and the one experiment (raw-pointer sweep refreshed at prepare and start) are fixed in Task 5; beyond that the question goes to Matt with the numbers, not to a fourth build.
11. **ShaperSource's pins return to Auto.** The memo makes a Manual read and a swept read see the same value; 2.1's ruling 7 kept Manual only to preserve a lag read in 28 stored patches, which Task 2 changes on purpose. Ruling 6 of 2.1 (declaration order kept as advance order) needs no change: order no longer affects a shared source's value.
12. **Per-note envelopes (bend, glide) are prepared with the voice's completed context**, so they share the voice's clock like every other node of the voice.
13. **`current()` overrides stay on `NameGate`, `PerformOut` and `TapSource`.** They are stateless views; `fire_triggers` reads a trigger at note Setup, between ticks, and must see the live match, not the previous tick's memo.
14. **`explore.cpp`'s loop ticks its voice from now on.** It never ticked the perform clock, so a bend was frozen there; the tick is the same one-line contract every driver follows. Noted because it is a behaviour change on a tool path, not a render path.

(Execution rulings follow here.)

- **Task 0:** the tree measurement lists 342 patches with lag reads (the manifest's 13 among them), 47 with same-tick taps (checkpoint 2.4's list) and 157 that do not load (the same 157 that were absent from 2.2's A/B: 1,615 − 1,458). The untraced-consumer check printed nothing: `pink_.next()` is a dot call on an owned member, not a pin. The timing baseline (pre-2.1 vs pre-2.3) is in the measurement document.
- **Task 1, ruling 6 corrected:** Task 1 is byte-identical except for one class the trace could not see — a tap-only tail whose forward inputs walk back into the root pull's cone, so nodes already computed this sample were pulled a second time directly (no `RefSource`; the output taken mid-loop). The memo serves the second pull. `tools/predict_double_pull.py` finds the class from the file alone (tail list, first-consumer edges forward from each tail, intersected with the root cone): exactly one patch tree-wide, `loop_root_sensitivity_fires`, and the null gate differed on exactly that entry (79/80). It no longer fires (peak 0.146 → 0.00012) and now matches `loop_root_sensitivity_dead`: the "root sensitivity" the pair demonstrated was the double advance. Refrozen at Task 1; the pair's future is REVIEW R4's.
- **Task 1:** the two Setup-time push-binding chain pulls use `step()`, not `next()`: a Setup happens between ticks, and two Setups with no sample between them (two live notes in one callback interval) would otherwise hand the second note the first note's chain value.
- **Task 1:** the plan's cycle test wired the tap-free cycle before `prepare`, and `prepare` forwards through pins with no guard — stack overflow. The test wires it after the host calls; the base's defence stays on `next()` as the spec says. A hand-wired tap-free cycle still overflows in `prepare`/`start` (no file can express one: refs point backward, the UI refuses) — section 7, for 2.5's fault flags.
- **Task 1:** the plan's `step()` test expected 8 + 9 = 17 for a node on two pins of a stepped parent; the value is 18, because both pins read the node after the sweep (checkpoint 2.1's rule). The test now documents that rule.
- **Task 1:** the UI's voice struct gets a `tick()` of its own (clock, then perform), and its clock copy is reset at deactivation rather than queued with the dying sources: the pool's `VoiceGraph` owns the clock.
- **Task 2, the list is an upper bound:** the null gate differed on 2 of the 13 manifest entries the trace listed (`double_advance_bug`, `double_advance_ok`). The trace counted every wrapper read before the advancer, including a wrapper's sweep-time read whose owner then read its pins after the sweep through `current()` (2.1's rule), which `RefSource::current()` served live: that consumer already used this sample's value. The renders that change are what the gate and the tree A/B show; the rule becomes "every differing patch is on the lag list or the double-pull list, and is explained", not "equal, patch for patch".
- **Task 2, the tree A/B** (library, baselines, audition, pending, sweep against `cli_pre23.exe`): 109 patches differ of 1,301 rendered by both — library 0 of 80, pending 0 of 40, baselines 3 of 54, audition 74 of 708, sweep 32 of 419 — 108 on the lag list, 1 the double-pull patch, none unexplained. Every changed patch shares a stateful node (Biquad, VarSource, SVFSource, DelayLine, CombinedSource, BowTable, an Envelope) between two consumers; the wrapped one read it a sample late and reads this sample now. The Task 1 arm over the tracked sets differed on the double-pull patch alone.
- **Task 4:** every ShaperSource pin is Auto; with hysteresis off, `breakaway` and `capture` are now pulled every sample (the base sweeps every wired Auto pin) though `compute()` reads them only in hysteresis mode. Ruled to stand: 2.1's "pulled only in one mode" reason for a Manual pin is for pins whose pull is the mode's action (RepeatingSource consuming its input between repetitions); a threshold's modulator switched on mid-note should have been running since the note began, which Auto gives; no stored Shaper node (1,373 scanned) wires those pins with hysteresis off. A/B over 1,298 Shaper patches: 0 differ.
- **Task 5, ruling 10's outcome:** not recovered against pre-2.1 (oboe1 0.69x, piano_default 0.79x, viola 0.98x after Task 4; the checkpoint itself is neutral against pre-2.3 except oboe1 0.88x and the shared chain H 1.12x). The one experiment — the sweep over a compacted array of raw pointers to the wired Auto sources, refreshed by the pins' setters, prepare, start and assignment — is byte-identical (80/80) and buys 8 to 16 points wherever pins are wired (piano 1.10x, G 1.11x, I 1.16x, oboe1 1.08x) and nothing where none are (F 1.01x, H 0.96x). KEPT: the rule's "ten points on the chains" is met on the wired chains and the gain is where the rule did not look; the design is better for it (the pin's setter is the door). The 2.1 cost stands at oboe1 0.73x, piano 0.88x of pre-2.1 and goes to REVIEW R4 item 0 with the tables.
- **Task 6, the refutation's fix wave (one pass):** (I1) the sweep refresh in `ValueSource::operator=` ran before the derived pins were assigned (a base's assignment runs first), so a preset rebuild dropped a wired `minValue`/`maxValue` from the sweep until the next prepare or start — live in the editor. The base no longer refreshes; `Envelope::replace_stages` keeps its wires aside over the assignment and puts them back through the setter; a test pins it (a rebuilt envelope keeps pulling its range source). (I2) `step()` reaches Auto pins only and has no memo: a Manual pin or an owned sub-source inside a stepped subgraph serves the memo for the rest of the fill, and a node on two pins of the subgraph steps twice. No stored patch does either (62 ref-excited fills scanned, 0). Ruled: the limits are stated in `step()`'s comment and the report's section 7; no third verb and no pull-mode flag now; REVIEW R4 item 3 decides whether `step()` stays, and Part 3's node pass owns the structural fix (a pull mode the pins honour, or `compute()`-pulls-its-pins per family where a fill becomes a generator call). Also taken: `RenderContext::with_clock()` for the five drivers; `prepare()` resets the memo tick; the drum clock heap-owned like the voice's; the vestigial friend; the dead verb out of the test names (`advanceList` stays — feedback_loop_design.md's term, Part 5's rename); a test that `TapSource::current()` reads live. Left for later with a reason: the once-per-node `fprintf` on a tap-free cycle (2.5's flag; reachable through no buildable graph), the public pin `source` member (Part 3's pins; every writer goes through the setter today, by grep), shared `Slot` parts advancing twice through their own entry points (Part 3's `Slot<T>`).

## Self-review notes

- **Spec coverage.** Part 2 §2 "the rule": Task 1 (`next()` memo, base owns it, `compute()` is the node's work). "What it buys": one call per pin per sample (the pins' `next()`), sharing without machinery (Task 2), double advance impossible (Task 1's test and Task 2's), the cost (one compare; Task 5 measures). "Taps": Task 3 (`TapSource`, guard on the edge, reading A, B deferred). "The advance list, generalised": ruling 4 rejects the generalisation and says why; the list stays. "Cycles without a tap": Task 1 (`computing_`, report once). "What changes in renders": Task 0 measures, Task 2 lands, Task 6 proves the set equal. §7 row 2.3: the list plus `double_advance_bug`.
- **Step scan.** Every rename site has a file and line; every driver loop is named; the two `step()` sites are named; the tests carry their assertions; the A/B and timing commands are given with their expected outputs.
- **Type consistency.** `Tick`, `SampleClock::now/tick()`, `RenderContext::clock`, `ValueSource::next()/step()`, `Param::next()/step()`, `Input::next()/step()`, `TapSource::source`, `VoiceGraph::clock/ctx/tick()`, `StreamingVoice::clock/tick()`, `TestHost`, `Counter`, `resolve_param(val, valueNodes)` are used with those names throughout.
- **Review Focus.** Items 1, 4 and 5 are pinned in Task 1's tests; item 2 is documented by the same test and caught by the A/B; item 3 is measured in Task 0 and ruled on (ruling 8).
- **Proportion.** Code appears for the contract and the one test section; the node changes are a rename list; the compiler enforces the rest.

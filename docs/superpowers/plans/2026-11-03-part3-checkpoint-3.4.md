# Part 3, checkpoint 3.4: each evolution a `Component`; the holders and the string form gone — Implementation Plan

**Goal:** The thirteen wave evolutions (the twelve registered today and `TargetEvolution`) become `Component`s that implement the evolution interface directly, their settings and pins declared once as Part 2's members and read where they are used; the twelve `*EvolutionSource` wrappers and `IEvolutionHolder` are deleted; `TargetEvolution` is registered; the loader's string-typed evolution path is deleted, the two live patches that use it are converted, and a standalone converter remains for the eight archived ones; no render in `patches/library` + `patches/baselines` moves.

**Architecture:** `WaveEvolution` becomes the family's abstract base — `struct WaveEvolution : Component` — and owns the lifecycle its members share: `prepare(ctx, maxLen)` prepares every pin then calls the `on_prepare(ctx, maxLen)` hook, `start(frames)` starts every pin; both non-virtual, the template-method shape Part 2 gave `ValueSource`, one level down (Part 3 section 5: "Each evolution is a `Component` that implements the `WaveEvolution` interface directly ... The wavetable holds a `Slot<WaveEvolution>`"). The work verbs stay: `adjust(frequency)` once per note at the first sample (now `void`: every caller discards the float), `shape_excitation(values)` once per note after the fill (default no-op), `evolve(values, index)` once per sample. Each evolution declares its settings and pins as members (`Setting<float> muting{this, "muting", 0.0f, 0.0f, 1.0f}`), reads them live where the old wrapper's reference members were read, and seeds its `Randomizer` once at construction from the registered or given seed; no setting change rebuilds anything. The four evolutions with pins (Reed, Bowed, Brass, BitRotate) keep them Manual, declared in today's order and advanced inside `evolve` in today's order; the base prepares and starts them. `WavetableSource` holds `Slot<WaveEvolution> evolution`, reads it per sample through `evolution.get()`, and loses the cached pointer, `refresh_evolution`, the `on_slot_changed` override, `set_evolution` and `ownedEvolution_`. `TargetEvolution` is a registered part with `targetPartials` (an `Array`), `targetLength`, `morphRate`, `holdCycles` and `decayFactor`, its wave synthesised at prepare with the loader's float code moved verbatim. The loader's `WavetableSource` branch goes (the generic path builds it); a string in a pin position fails the load naming the node, the pin and, for `evolution`, the converter `tools/string_evolution_to_node.py`. Byte-identity is by construction — no sample arithmetic changes; what changes is which object holds the settings and who prepares the pins — and is proved at node level: the null gate after every task (twelve evolution patches and `delay_test` in the manifest), the A/B over the 132 against the pre-checkpoint binary, and a synthetic set for the forms the scope lacks (Averaging and Brass nodes; the string-form `target` and `pluck` rendered by the old binary before conversion and by the new one after).

**Tech Stack:** C++20 (MSVC, CMake 3.20+, `/fp:precise`), the `engine_tests` harness (`CHECK`, `PinCounter`, `OrderProbe`, `split_equivalent`, `allocations_in_one_note`), Python 3.11 (`tools/tests` under ctest `tool_tests`), `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/alloc_gate.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/structure/check.py`, `python tools/lint_patches.py`.

**Spec:** `docs/architecture/part3-nodes.md` section 5 (the holders: "Each evolution is a `Component` that implements the `WaveEvolution` interface directly. Its settings are declared once as Part 2's members and read where they are used; today they are baked in at construction and the wrapper rebuilds the object on every change. `IEvolutionHolder` and the cast are deleted. The wavetable holds a `Slot<WaveEvolution>`, type-checked at wiring. The string-typed loader path ... is deleted. `TargetEvolution` is registered like the others. Thirteen classes replace twenty-six.") and section 10 row 3.4 ("none"); `docs/architecture/target-architecture.md` section 5 ("The string-typed evolution (decided 2026-10-05) ... The string form goes. 70 wavetable nodes already use the node form; ten use the string form, of which two are live (the baseline `delay_test` and the pending patch `harsh_pluck`) and get converted." and "`TargetEvolution` is kept and becomes a node."); section 3.2 ("The thirteen copy-pasted evolution holders become one"). Matt, 2026-11-02: easily-tweaked decisions are the session's (CLAUDE.md "Refactor verdicts"); the oboe1 A/B at 0 moved is accepted standing. 3.1's report section 7 and ruling 8 named this checkpoint's job: "the holders are still wrappers ... 3.4 makes each evolution a `Component`, deletes the twelve wrappers and `IEvolutionHolder`, and merges the two prepares. The loader's string-typed evolution (`set_evolution`, `ownedEvolution_`) dies there too."

## Global Constraints

- **Byte-identity is total in scope.** `python tools/null_gate_perform_source.py --jobs 3` reports `78/78 manifest entries identical` after EVERY task — including after `patches/baselines/delay_test.json` is converted from the string form (its manifest hash is the proof of the conversion); the A/B at the close over `renders/scratch/ab31/library_baselines.txt` (132 patches) reports 0 DIFFER; the synthetic set `renders/scratch/ab34/synth.txt` reports 0 DIFFER after every task that touches one of its forms. No departure is expected; the one live-edit difference (ruling 3: a Pluck or Averaging setting edit no longer restarts the draw stream) is not a render and is named in the report's section 1.
- **No sample arithmetic changes.** Every `adjust`, `shape_excitation` and `evolve` body keeps its float operations in the same order; a wrapper's reference member (`const Setting<float>& muting` or a baked `muting_`) becomes a read of the evolution's own member, the same float. The one deleted computation, Pluck's `freqAdj_`, reaches no sample (ruling 2).
- **Measurement scope is `patches/library` + `patches/baselines`** (CLAUDE.md "Measurement scope") plus the synthetic set Task 0 writes under `renders/scratch/ab34/` (ruling 12) and the one pending patch the signed target names for conversion (ruling 8).
- **The eight string-form patches in `patches/old/baselines_culled_2026-09-14/` are not converted and not deleted** (CLAUDE.md: nothing in `patches/old/` is ever deleted); after Task 1 they fail to load with the message that names the converter. The report lists them.
- **No new file over 600 lines, no new function over 80** (`tools/structure/limits.json`). `wave_evolution.h` stays one file (1,281 lines today, over the baseline; it shrinks here and 3.6 splits it — ruling 14). `python tools/structure/check.py --delta <Task 0 sha>` is pasted into the report; `include_violations` and `unmapped_files` must not rise; `dynamic_cast` +0.
- **No dated changelog, backlog id, REVIEW, Part or checkpoint number in code comments.**
- **Commit after every task; `python tools/gates.py --fast` before every commit** (ctest, the UI stamp, the roundtrip smoke with its known wiring diff, the allocation gate). The UI is relinked before a stamp run (every task here touches a header the UI compiles).
- **Work on main, no worktree.** The in-tree build is `build/`.
- **The A/B base** is `renders/scratch/ab_tree/cli_pre34.exe`: Task 0 copies the current build's CLI (its engine is the tree at `b3c6160`, 3.3's seal; `dbbb822` changed docs only) before any code moves, after the stamp confirms the build is current, and renders the old arm into `renders/scratch/ab34/old` and `renders/scratch/ab34/synth_old` — before any patch is converted.
- **Edits go through script files**, never heredocs with backslashes (memory `feedback_bash_heredoc_limits`); the converter and the synthetic-patch writer are written with the Write tool.
- **Dates:** `date` first (the machine is HST); this plan is dated 2026-11-03.

## Review Focus

The six places a byte or a behaviour could move that this plan claims do not, each pinned by the task named.

1. **The draw stream of Pluck and Averaging.** The old `PluckEvolutionSource` rebuilt its evolution on every setting change with `seed_`, a member that never received the constructor's seed (always `0xDEADBEEF`); `AveragingEvolutionSource` rebuilt with the right seed. The new evolutions seed once at construction and never rebuild. Renders are identical in scope because: the one Pluck node in scope (`bend_mordent_test_pluck`) carries no `seed` key (constructed with `0xDEADBEEF` either way); the string form seeded `PluckEvolution` directly with `evolutionSeed` (42) and the converter writes that seed onto the node, where the new class seeds once from it; Averaging's rebuild used the right seed. Task 2's A/B on the synthetic `averaging_evolution.json` (a non-default seed AND non-default settings) and the null gate's `bend_mordent_test_pluck` and `delay_test` pin it. The live-edit difference is named in the report.
2. **Pin prepare and start order.** Reed's and Brass's hand-written `prepare`/`start` called `breath` before `reedStiffness`/`brassiness`; their declaration order (which the base now forwards in) is the reverse. Prepare is idempotent by contract (`ValueSource::prepare`: "Calling it twice with the same context changes nothing a render can see") and start on two distinct sources is order-free unless one is reachable from the other, in which case it runs twice in either order. Task 2's null gate (`reed_test`) and synthetic A/B (`brass_evolution`) pin it; the manual-pin test keeps `starts == 1` on both pins.
3. **The wavetable reads its slot per sample.** `evolution.get()` (`views.empty() ? nullptr : views.front()`) replaces a cached raw pointer: no arithmetic, two loads. The existing clear-then-no-more-pulls test stays green with no hook; Task 4's timing run reports the cost (expected noise, as `FullAdditiveSource` already reads its slot per sample).
4. **A string in a pin position now fails the load.** `skip_unresolvable` existed for the string-typed evolution alone (its own comment: "the three ks_morph_*_test patches"). Task 1 scans every patch in scope for a string value under a Param, Input or Slot key (expected: `delay_test` only, converted in the same task) and the null gate, roundtrip and allocation gates load all 132 afterwards; a test pins the message.
5. **`TargetEvolution`'s node form is the string form's arithmetic.** The target synthesis (sum of sines in float, `2.0f * dsp::kPi` — the same float as the loader's `2.0f * 3.14159265358979323846f`, pinned by 3.3's scaffold test — peak-normalised) moves verbatim from the loader into `on_prepare`; the resample and the morph in `evolve` are unchanged. Task 3's A/B: `renders/scratch/ab34/synth/target_evolution.json` written in the STRING form at Task 0 and rendered by the old binary, converted by the tool at Task 3 and rendered by the new one — 0 DIFFER. The empty-target guard (ruling 7) is exercised by a test, never by a render in scope.
6. **The tests bite.** Each task's close mutates once: Task 1 drops the string refusal (the refusal test fails); Task 2 removes the base's `start` loop (the manual-pin test's `starts == 1` fails) and inverts Averaging's `leading` read (the live-read test fails); Task 3 drops the peak normalisation (the target test fails). Restored, ALL PASS. The Ledger records each flip and its failure line.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `engine/include/mforce/source/wavetable/wave_evolution.h` (modify, Tasks 2, 3) | `WaveEvolution : Component` (the base: pin lifecycle, the three work verbs, the `on_prepare` hook); thirteen `final` evolutions deriving from it, each with its settings and pins as members; `IEvolutionHolder` and the twelve `*EvolutionSource` wrappers gone; `TargetEvolution` with `targetPartials`, `targetLength`, `morphRate`, `holdCycles`, `decayFactor`. |
| `engine/include/mforce/source/wavetable/wavetable_source.h`, `engine/src/wavetable_source.cpp` (modify, Tasks 1, 2) | `Slot<WaveEvolution> evolution{this, "evolution", "an evolution"}`; `on_prepare` calls `e->prepare(ctx, maxLen_)`, `on_start` calls `e->start(frames)`, the two `compute_*` call `adjust`/`shape_excitation`/`evolve` through `evolution.get()`; `set_evolution`, `ownedEvolution_`, `evolution_`, `refresh_evolution`, the `on_slot_changed` override gone. |
| `engine/src/patch_loader.cpp` (modify, Task 1) | the `WavetableSource` branch gone (the generic path builds it); `skip_unresolvable` gone — `refuse_string_pin(src, id, name, value)` throws the named message; `kBranchKeys` loses the keys the deleted branch alone read (ruling 10). |
| `engine/src/source_registrations.cpp` (modify, Tasks 2, 3) | the twelve registrations construct the evolutions themselves; `TargetEvolution` registered (Wavetable, Experimental). |
| `tools/string_evolution_to_node.py` (new, Task 1) | converts a patch's string-typed evolutions (`"pluck"`, `"averaging"`, `"target"`) into an evolution node wired into the wavetable's `evolution` slot; refuses `targetWave`; idempotent. |
| `tools/tests/test_string_evolution_to_node.py` (new, Task 1) | the converter's unit test (ctest `tool_tests` discovers it). |
| `patches/baselines/delay_test.json` (modify, Task 1) | the node form; its null-gate hash unchanged. |
| `patches/pending/harsh_pluck.json` (modify, Task 1) | the node form, under the target's 10-05 authorisation (ruling 8); the original copied to `renders/scratch/ab34/harsh_pluck_before.json`. |
| `tools/engine_tests/main.cpp` (modify, every task) | the holder tests become evolution tests; the string-form JSON tests become node-form tests plus the refusal test; the registry tables, `kPartTypes` and the counts gain `TargetEvolution`; the new tests named per task. |
| `renders/scratch/ab34/` (Task 0, gitignored) | `synth/*.json`, `synth.txt`, `old`, `synth_old`, per-task `synth_t<N>` arms, `new`, `synth_new`, the meter delta, the timing run, the refuter's files. |
| `docs/architecture/part3-nodes.md`, `target-architecture.md` (modify, Task 4) | section 5 gains the landed paragraph; section 8's stale "`EvolutionNode` template" wording corrected; section 10 row 3.4's result; the target's section 5 and 3.2 landed lines. |

---

### Task 0: Date, base, old arms, the synthetic set

**Files:**
- Create: `renders/scratch/ab34/write_synth.py`, `renders/scratch/ab34/synth/*.json`, `renders/scratch/ab34/synth.txt`
- Copy: `build/tools/mforce_cli/Release/mforce_cli.exe` → `renders/scratch/ab_tree/cli_pre34.exe`

**The synthetic set** (two notes each at 48 kHz, the shape of `renders/scratch/ab33/write_synth.py`; the two ab33 patches are listed by their existing paths):
- `renders/scratch/ab33/synth/brass_evolution.json` (BrassEvolution node, the breath wired) and `renders/scratch/ab33/synth/eks_stiffness.json` (EKS with stiffness 0.5, the 3.3 departure patch — its old arm is the post-3.3 binary's, so it is a plain member here).
- `averaging_evolution.json`: an `AveragingEvolution` node with `"seed": 77`, `sampleCount` 6, `speed` 1.7, `decayFactor` 0.995, `leading` true, `autoAdjust` true under a `WavetableSource` (Review Focus 1: a non-default seed with settings).
- `target_evolution.json`: a `WavetableSource` in the STRING form — `"evolution": "target"`, `morphRate` 0.0005, `holdCycles` 5, `decayFactor` 0.999, `targetPartials` the twenty of `ks_morph_saw_test`, `targetLength` 2048, `evolutionSeed` 42 — converted in place at Task 3.
- `pluck_string.json`: a `WavetableSource` in the STRING form, `"evolution": "pluck"`, `muting` 0.35, `evolutionSeed` 7 — converted in place at Task 1 (a second conversion proof beside `delay_test`, with a non-default muting and seed).
- `harsh_pluck_scored.json`: `patches/pending/harsh_pluck.json` with a two-note score added (its instrument block and paramMap kept) — converted in place at Task 1; `patches/pending/harsh_pluck.json` itself is also listed (whatever the CLI renders for an unscored instrument patch, both arms render the same thing; a FAILED on both arms is recorded, not a proof).

- [ ] **Step 0: Date, base** — `date`; `git rev-parse --short HEAD`; `git status -sb` (GOALS.md is Matt's uncommitted edit, untouched); `python tools/gates.py --fast` (the stamp says the build is current at `b3c6160`'s engine; rebuild and rerun if not); copy the CLI to `cli_pre34.exe`.
- [ ] **Step 1: The synthetic set** — write `write_synth.py` and run it; `python tools/ab_render_set.py render renders/scratch/ab34/synth.txt renders/scratch/ab34 synth_old --cli renders/scratch/ab_tree/cli_pre34.exe` — every patch renders (a synthetic patch that fails to render is fixed before the old arm is kept; `harsh_pluck.json` itself may FAIL on both arms and is then noted).
- [ ] **Step 2: The old arm** — `python tools/ab_render_set.py render renders/scratch/ab31/library_baselines.txt renders/scratch/ab34 old --cli renders/scratch/ab_tree/cli_pre34.exe` (132/132).
- [ ] **Step 3: The scrape baseline** — `python -c` over `tools/lint_patches.scrape_special_keys(False)` → `renders/scratch/ab34/scrape_before.txt`; `python tools/lint_patches.py patches/library patches/baselines` → `lint_before.txt` (ruling 10's baseline).
- [ ] **Step 4: Nothing to commit** (scratch only); record the base sha in the Ledger.

---

### Task 1: The string-typed evolution goes; the converter; two patches converted

**Files:**
- Modify: `engine/src/patch_loader.cpp` (the `WavetableSource` branch, `skip_unresolvable`, `kBranchKeys`), `engine/include/mforce/source/wavetable/wavetable_source.h` (`set_evolution`, `ownedEvolution_`; `refresh_evolution` reads the slot only), `engine/src/wavetable_source.cpp` (no change expected beyond the include of `<stdexcept>` if unused)
- Create: `tools/string_evolution_to_node.py`, `tools/tests/test_string_evolution_to_node.py`
- Modify: `patches/baselines/delay_test.json`, `patches/pending/harsh_pluck.json` (via the tool), `renders/scratch/ab34/synth/pluck_string.json`, `renders/scratch/ab34/synth/harsh_pluck_scored.json` (via the tool)
- Test: `tools/engine_tests/main.cpp` — `run_wavetable_evolution_tests` loses its `set_evolution` case (replaced by a Pluck holder in the slot until Task 2 makes it the evolution); the allocation tests' string-form `pluck` JSON becomes the node form, the string-form `target` JSON is removed (Task 3 restores it in the node form); new `run_string_pin_refusal_tests`.

**Interfaces:**
```cpp
// patch_loader.cpp: where skip_unresolvable was consulted, in both the input and the param loop.
static void refuse_string_pin(const Component& src, const std::string& id,
                              const char* pin, const std::string& value);
// throws: node '<id>' (<type>): '<pin>' is the string "<value>"; a pin takes a number or {"ref": id}
//   and, when pin == "evolution": " — the string-typed evolution is gone; python tools/string_evolution_to_node.py <patch> converts it to an evolution node"
```
```
tools/string_evolution_to_node.py <patch.json> [...]
  for each WavetableSource node whose "evolution" is a string:
    pluck     -> {"id": "<wt>_evolution", "type": "PluckEvolution",     "params": {"muting", "seed"}}
    averaging -> {..."AveragingEvolution", "params": {"sampleCount", "speed", "decayFactor", "leading", "autoAdjust", "seed"}}
    target    -> {..."TargetEvolution",    "params": {"morphRate", "holdCycles", "decayFactor", "targetPartials", "targetLength", "seed"}}
  the moved keys leave the wavetable (only those present move; the node form's defaults equal the string form's — ruling 7 pins TargetEvolution's);
  "seed" is evolutionSeed when present, else 42 (the string form's default); "evolutionSeed" leaves the wavetable;
  the new node is inserted immediately before the wavetable in "nodes"; the wavetable's "evolution" becomes {"ref": "<wt>_evolution"};
  "targetWave" is refused with a message (no patch in the tree carries it); a patch with nothing to convert is reported and not rewritten;
  the file is rewritten with indent 2 (formatting is not preserved).
```
**Byte-identity (Ledger table 1):** the string form built `PluckEvolution(muting, evolutionSeed)` owned by the wavetable and drove it with the same five calls the slot path drives a holder's evolution with; after conversion the registry builds `PluckEvolutionSource(seed)` whose evolution is `PluckEvolution(muting.value, seed)` and, when the loader sets `muting`, is rebuilt as `PluckEvolution(muting, 0xDEADBEEF)` — so until Task 2 the converted `delay_test` (seed 42) would render with the WRONG seed. Therefore Task 1 sets `seed_` from the constructor in `PluckEvolutionSource` (a one-line fix of the latent bug, deleted with the wrapper in Task 2) so the rebuilt evolution carries the node's seed; the null gate's `delay_test` hash proves the chain. `TargetEvolution` has no node until Task 3: `target_evolution.json` stays in the string form (unrenderable by the new binary) until then, and the set is compared without it at Task 1 and 2.

- [ ] **Step 1: The scan** — a script lists every patch under `patches/library` + `patches/baselines` with a string value under a key that is a Param, Input or Slot descriptor of its node type (`--dump-descriptors` gives the types; `renders/scratch/ab33/descriptors.json` is the last dump); expected: `delay_test.json` (`evolution`) only. Any other hit is a form this plan did not foresee: stop and rule before deleting.
- [ ] **Step 2: The converter and its test (RED → GREEN in Python)** — `tools/tests/test_string_evolution_to_node.py`: the three forms converted from small in-memory patches (node inserted before the wavetable, keys moved, seed defaulted to 42, idempotent on a second run, `targetWave` refused, a patch without the form untouched); `python -m unittest discover -s tools/tests -p "test_*.py"`.
- [ ] **Step 3: The engine test (RED)** — `run_string_pin_refusal_tests`: a graph JSON with `"evolution": "pluck"` on a wavetable fails the load with a message containing `'evolution' is the string "pluck"` and `string_evolution_to_node.py`; one with `"frequency": "440"` fails naming `frequency`; the allocation test's pluck JSON converted to the node form, the target JSON removed. RED: the string loads silently (the first case renders without an evolution).
- [ ] **Step 4: The loader** — delete the `WavetableSource` branch (the generic path's factory constructs `WavetableSource(seed.value_or(0xC0FFEEu))` as the branch did); delete `skip_unresolvable` and its comment; add `refuse_string_pin` and call it in both loops; `WavetableSource::set_evolution`, `ownedEvolution_` gone, `refresh_evolution()` reads the slot (`evolution_ = holder ? holder->get_evolution() : nullptr` — Task 2 removes the rest); `PluckEvolutionSource::seed_` set from the constructor.
- [ ] **Step 5: `kBranchKeys`** — rerun the scrape → `scrape_after.txt`; diff: the keys that left are removed from `kBranchKeys` (expected among them `evolutionSeed`, `muting`, `targetWave`, `targetPartials`, `targetLength`, `morphRate`; a key still scraped elsewhere stays; `evolution` leaves the list and stays known on the wavetable through its descriptor); `ctest -R structure_meter_tests`; `python tools/lint_patches.py patches/library patches/baselines` reports no new finding against `lint_before.txt`.
- [ ] **Step 6: The conversions** — `python tools/string_evolution_to_node.py patches/baselines/delay_test.json renders/scratch/ab34/synth/pluck_string.json renders/scratch/ab34/synth/harsh_pluck_scored.json`; copy `patches/pending/harsh_pluck.json` to `renders/scratch/ab34/harsh_pluck_before.json`, then convert it; read each result.
- [ ] **Step 7: GREEN, mutation, gates** — build; `engine_tests` ALL PASS; mutation: drop the `refuse_string_pin` call in the input loop → the refusal test fails naming the line, restore; null gate `78/78` (`delay_test` identical after conversion); `ab_render_set.py render renders/scratch/ab34/synth.txt renders/scratch/ab34 synth_t1` and `compare … synth_old synth_t1` → 0 DIFFER over the members that render (target_evolution expected FAILED on the new arm until Task 3 — listed, not counted); the tree scan for the string form: the eight `patches/old/baselines_culled_2026-09-14/` files and nothing else; `gates.py --fast`.
- [ ] **Step 8: Commit** — `git add -u` plus the two new tool files; `git commit -m "patch: the string-typed evolution is gone; a string in a pin position fails the load naming the converter; delay_test converted; tools/string_evolution_to_node.py"`.

---

### Task 2: `WaveEvolution` is the base; the twelve wrappers and `IEvolutionHolder` go

**Files:**
- Modify: `engine/include/mforce/source/wavetable/wave_evolution.h`, `engine/include/mforce/source/wavetable/wavetable_source.h`, `engine/src/wavetable_source.cpp`, `engine/src/source_registrations.cpp`, `engine/include/mforce/core/pins.h` (the `Slot` comment: "an evolution"), `tools/engine_tests/main.cpp`
- Test: `run_wavetable_evolution_tests` (rewritten), `run_evolution_shape_tests`, `run_evolution_setting_tests`, `run_evolution_manual_pin_tests`, `run_evolution_lifecycle_tests` (new), `run_evolution_live_read_tests` (new)

**Interfaces:**
```cpp
namespace mforce {
// The base of the wave evolutions: a part the wavetable holds in its
// `evolution` slot and drives — prepare once per build, start once per note,
// adjust at a note's first sample, shape_excitation after the fill, evolve
// once per sample. An evolution's pins are Manual (it advances them inside
// evolve, in its own order) and are prepared and started here, once for
// every member of the family.
struct WaveEvolution : Component {
  void prepare(const RenderContext& ctx, int maxLen) {
    for (auto* pin : pins()) pin->prepare(ctx);
    on_prepare(ctx, maxLen);
  }
  void start(int frames) { for (auto* pin : pins()) pin->start(frames); }
  virtual void adjust(float frequency) = 0;                          // once per note, at the first sample
  virtual void shape_excitation(std::vector<float>& /*values*/) {}   // once per note, after the fill
  virtual void evolve(std::vector<float>& values, int index) = 0;    // once per sample
protected:
  // maxLen: the longest table the owner will ever fill (its floor
  // frequency's length). Capture the rate and reserve every per-note or
  // per-table buffer to maxLen here, so nothing after prepare allocates.
  virtual void on_prepare(const RenderContext& /*ctx*/, int /*maxLen*/) {}
};

struct PluckEvolution final : WaveEvolution {
  Setting<float> muting{this, "muting", 0.0f, 0.0f, 1.0f};
  explicit PluckEvolution(uint32_t seed = 0xDEAD'BEEFu) : rng_(seed) {}
  const char* type_name() const override { return "PluckEvolution"; }
  void adjust(float frequency) override;      // muting.value where muting_ was; freqAdj_ gone
  void evolve(std::vector<float>& values, int index) override;   // unchanged
private:
  void average(std::vector<float>& values, int index);           // unchanged
  float sampleCount_{2.0f}, speed_{1.0f};
  Randomizer rng_;
};
// AveragingEvolution: five settings; adjust/evolve/average read sampleCount.value, speed.value,
//   decayFactor.value, leading.value, autoAdjust.value where base*_ / speed_ / leading_ / autoAdjust_ were.
// EKSEvolution: five settings, its own members; rng_(seed).
// ReedEvolution: Param reedStiffness, Input breath, Setting tubeLoss, loopFilter — declared in the
//   wrapper's order; evolve advances reedStiffness then breath as today.
// BowedStringEvolution: Input bow, Param frictionGain, four settings; evolve advances bow then frictionGain.
// BrassEvolution: Param brassiness, Input breath, four settings; evolve advances brassiness then breath.
// ReactionDiffusion, SortErosion, CellularAutomaton, HistogramEqualize, BezierPull: their settings;
//   the four on_setting_changed clamps kept verbatim (ruling 4).
// BitRotateEvolution: Param shiftBits, Param stepEvery; evolve advances stepEvery then shiftBits as today.
// Every evolution's prepare(ctx, maxLen) override becomes on_prepare(ctx, maxLen); the bodies are unchanged.
}
```
```cpp
// wavetable_source.h
Slot<WaveEvolution> evolution{this, "evolution", "an evolution"};
// on_prepare: ... maxLen_ computed; if (auto* e = evolution.get()) e->prepare(ctx, maxLen_);
// on_start:   ... if (auto* e = evolution.get()) e->start(frames);
// compute_raw / compute_interpolated: auto* e = evolution.get(); if (e) { e->adjust(currFreq_); e->shape_excitation(values_); } ... if (e) e->evolve(values_, idx);
```
**Comparison (Ledger table 2):** per evolution, the old wrapper's member → the evolution's own member, and the old two-object lifecycle → the base's. The owner's call order per note and per sample is unchanged: today `holder->prepare(ctx)` (pins), `refresh`, `maxLen_`, `evolution_->prepare(ctx, maxLen_)` (buffers) → now `maxLen_`, `e->prepare(ctx, maxLen_)` = pins then `on_prepare` (buffers); today `holder->start(frames)` (pins) → `e->start(frames)` (pins). Within `prepare`, the pins' order is declaration order (Review Focus 2). The settings' values at `adjust` time are the same floats: the loader sets every setting before the first prepare, and the old wrapper's rebuilt evolution carried the current values.

- [ ] **Step 1: The tests (RED)** — rename the three holder tests to evolution tests over the evolution types (`PluckEvolution pluck;` where `PluckEvolutionSource pluck;` was; `avg.get_evolution()` gone; the CA case calls `ca.shape_excitation`/`ca.evolve` directly); `run_wavetable_evolution_tests`: a `ReedEvolution` in the slot (`w.get_part("evolution") == reed`, `w.evolution.get() == reed.get()`), the clear case, the refused-signal case, and a `PluckEvolution` in the slot rendering a non-zero peak; `run_evolution_lifecycle_tests`: a `PinCounter` on Reed's `breath` and Brass's `brassiness` — after `w.prepare(ctx)` `rate == 48000` and `ctxPrepares == 1`, after `w.start(4800)` `starts == 1`; a second `w.prepare(ctx)` leaves `ctxPrepares == 2` and the render unchanged (idempotence, Review Focus 2); `run_evolution_live_read_tests`: an `AveragingEvolution` with `sampleCount` 2, `speed` 1, `decayFactor` 0.5 on the table {1, 2, 3, 4}: `leading` false → `evolve(t, 0)` gives 1.25, a fresh one with `leading` true gives 0.75 (the settings are read where they are used, after construction); a `PluckEvolution` seeded 7 with `muting` set to 0.5 after construction renders under a wavetable bit-identical to one with `muting` set before `prepare` (the stream does not depend on when the setting was set); the type-name audit: `SourceRegistry::create("PluckEvolution")` is a `WaveEvolution` and not a `ValueSource`; the split tests and wired tests say `PluckEvolution` where they said `PluckEvolutionSource`. RED: the wrapper types are gone or `WaveEvolution` has no `pins()`.
- [ ] **Step 2: The header** — `WaveEvolution : Component` with the lifecycle above; each evolution absorbs its wrapper's members, `type_name`, constructor (seed only) and `on_setting_changed`; `adjust` returns `void` (Pluck's `freqAdj_` and its two lines deleted); `prepare(ctx, maxLen)` overrides become `on_prepare`; `IEvolutionHolder` and the twelve wrappers deleted; `TargetEvolution` takes the base too but keeps its constructor arguments until Task 3 (unregistered, unreferenced).
- [ ] **Step 3: The owner and the registry** — `wavetable_source.h`/`.cpp` as the Interfaces block; `source_registrations.cpp`'s twelve factories construct the evolutions (`std::make_shared<PluckEvolution>(seed.value_or(0xDEAD'BEEFu))`; the six algorithmic ones take no seed); `pins.h`'s Slot comment says "an evolution".
- [ ] **Step 4: GREEN, mutation, gates** — build everything (the UI compiles the header); `engine_tests` ALL PASS; mutation A: the base's `start` loop removed → the manual-pin test (`a->starts == 1`) and the lifecycle test fail, restore; mutation B: Averaging's `average` reads `!leading.value` → the live-read test fails, restore; null gate `78/78` (`bend_mordent_test_pluck`, `hi_hat1`, `reed_test`, `bow_evolution_discovery`, the six `algev_test_*`, `delay_test`); `synth_t2`: 0 DIFFER over `brass_evolution`, `eks_stiffness`, `averaging_evolution`, `pluck_string`, `harsh_pluck_scored`; `gates.py --fast`.
- [ ] **Step 5: Commit** — `git commit -m "nodes: each wave evolution is a Component; WaveEvolution is the family's base and prepares the pins; the twelve holders and IEvolutionHolder are gone"`.

---

### Task 3: `TargetEvolution` registered

**Files:**
- Modify: `engine/include/mforce/source/wavetable/wave_evolution.h` (TargetEvolution), `engine/src/source_registrations.cpp`, `tools/engine_tests/main.cpp` (`kPartTypes`, `kFamilyTable`, `kMenuTable`, the counts 85/63/22 → 86/63/23, `run_target_evolution_tests` new, the node-form target allocation test)
- Modify (via the tool): `renders/scratch/ab34/synth/target_evolution.json`

**Interfaces:**
```cpp
// Morphing Karplus-Strong: the table averages toward a target waveform
// instead of toward silence. The target is one period synthesised at
// prepare from harmonic amplitudes, peak-normalised, resampled to each
// note's table length; holdCycles of plain averaging precede the morph.
struct TargetEvolution final : WaveEvolution {
  Array          targetPartials{this, "targetPartials", nullptr, 0.0f, -1.0f, 1.0f};   // harmonic amplitudes, fundamental first
  Setting<int>   targetLength  {this, "targetLength",   1024, 16, 8192};              // samples in the synthesised period
  Setting<float> morphRate     {this, "morphRate",      0.001f, 0.0001f, 0.1f};
  Setting<int>   holdCycles    {this, "holdCycles",     3, 0, 100};
  Setting<float> decayFactor   {this, "decayFactor",    0.998f, 0.9f, 1.0f};
  TargetEvolution() { targetPartials.values = {1.0f}; }                                // a sine until told otherwise
  const char* type_name() const override { return "TargetEvolution"; }
  void adjust(float) override;                          // the four per-note counters reset, as today
  void evolve(std::vector<float>& values, int index) override;   // as today; an empty target keeps the hold-phase formula for the whole note
protected:
  void on_prepare(const RenderContext&, int maxLen) override;    // synthesise target_ from the partials (the loader's loop, verbatim, 2.0f * dsp::kPi); resampledTarget_.reserve(maxLen)
private:
  std::vector<float> target_, resampledTarget_;
  float morphAmount_{0.0f}; int sampleCount_{0}, cycleCount_{0}, tableLen_{0};
};
```
Registration: `reg.register_type("TargetEvolution", {.family = Family::Wavetable, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<TargetEvolution>(); });` after `BitRotateEvolution` (the menu lists it last among the Experimental wavetable parts).

**Comparison (Ledger table 3):** the loader's `targetPartials` branch computed `target[i] += partials[h] * std::sin(pos * TAU * float(h + 1))` over `tLen` with `TAU = 2.0f * 3.14159265358979323846f`, then divided by the peak; `on_prepare` runs the same loop over `targetPartials.values` and `targetLength.value` with `2.0f * dsp::kPi` (the same float). The `evolve` body is unchanged: `morphRate.value`, `holdCycles.value`, `decayFactor.value` are the constants the constructor baked. The unused `Randomizer rng_` and the `seed` argument go (no draw anywhere in the class); the converter still writes `"seed"` onto the node for the record and the loader hands it to a factory that ignores it. `targetWave` is not carried (0 uses in the tree). An empty `targetPartials` or a zero-length target left the string form dividing by `target_.size() == 0` on the first evolve; the node form keeps the hold-phase formula instead (a render path cannot divide by zero) — no patch in scope reaches it.

- [ ] **Step 1: The tests (RED)** — `run_target_evolution_tests`: the shape (`setting_names == "targetLength,morphRate,holdCycles,decayFactor,"`, one array `targetPartials` with one row of 1.0); after `prepare` with partials {1} and length 8 the target is a sine peak-normalised to 1 (sample 2 is 1.0, sample 6 is −1.0, sample 0 and 4 within 1e-6 of 0); with partials {1, 0.5} the peak is 1 and the shape differs; a wavetable with it renders finite and non-zero for 4,800 samples and, after `holdCycles` of pure KS, moves toward the target (the correlation with the resampled target rises between cycle 1 and cycle 40); an empty `targetPartials` renders finite; the registry creates it as a part (`!ValueSource`), on the Experimental shelf of Wavetable, label "Target Evolution"; `kPartTypes` has 23 entries, `kFamilyTable` and `kMenuTable` the new row, `main == 63 && experimental == 23`, `walked == 86`; the allocation test's node-form target JSON (`{"id":"ev","type":"TargetEvolution","params":{"morphRate":0.0005,"holdCycles":5,"decayFactor":0.999,"targetPartials":[1.0,0.5,0.33,0.25],"targetLength":2048}}`) allocates nothing in a note. RED: `TargetEvolution` has no settings / is not registered.
- [ ] **Step 2: The class and the registration** — as the Interfaces block.
- [ ] **Step 3: The conversion** — `python tools/string_evolution_to_node.py renders/scratch/ab34/synth/target_evolution.json`; read it.
- [ ] **Step 4: GREEN, mutation, gates** — build; `engine_tests` ALL PASS; mutation: the peak division removed → the sine test fails, restore; null gate `78/78`; `synth_t3`: 0 DIFFER over all seven members including `target_evolution` (Review Focus 5); `gates.py --fast`.
- [ ] **Step 5: Commit** — `git commit -m "nodes: TargetEvolution is a registered part (Wavetable, Experimental); its target is synthesised at prepare from targetPartials"`.

---

### Task 4: Close the checkpoint

- [ ] **Step 1: Every gate** — `python tools/gates.py` (all): ctest (engine_tests, test_figures, the meter and its tests, `tool_tests` with the converter's test), the stamp, the roundtrip smoke (the known wiring diff), the allocation gate, null gate `78/78`, the whole-corpus roundtrip (the backlog-79 six — run directly to list them; `delay_test`'s converted form must not join them), wiring null gate `78/78`.
- [ ] **Step 2: The A/B** — `ab_render_set.py render renders/scratch/ab31/library_baselines.txt renders/scratch/ab34 new` then `compare renders/scratch/ab34 old new` → `0 DIFFER` of 132 (`delay_test` compared across its conversion); `synth_new` vs `synth_old` → 0 DIFFER of 7 (`harsh_pluck.json` itself: identical, or FAILED on both arms and said so).
- [ ] **Step 3: Timing** — `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre34.exe build/tools/mforce_cli/Release/mforce_cli.exe` with nothing else of this session's running; the per-sample arithmetic is unchanged and the wavetable's slot read is two loads, so the expectation is noise; if the base arm's walls are not within 5 percent of 3.3's clean run, say so and run once more.
- [ ] **Step 4: The meter** — `python tools/structure/check.py --delta <Task 0 sha>`; paste it (`wave_evolution.h` and `patch_loader.cpp` shrink; `duplicate_blocks` expected to fall; `include_violations` +0; `unmapped_files` 0; `dynamic_cast` +0); `--snapshot`; `--modules-md`.
- [ ] **Step 5: The refuter, the wave, the re-review** — the Fable refuter (read-only, extra-high, the ten-item brief with checkpoint-specific questions: the Ledger's three comparison tables against the deleted code; the six Review Focus items; whether any evolution reads a setting at a different moment than its wrapper's rebuild implied; whether the base's pin loop reaches a pin the hand code did not; whether `TargetEvolution`'s synthesis is the loader's loop to the operation; whether the string refusal can be reached by any in-scope patch; the converter's output against the loader's defaults; the tests' bite), a fix wave with tests first (Majors and the cheap, clearly-right Minors), the same refuter's re-review by message, a re-check of the wave's follow-up; the A/B new arm re-rendered after the wave.
- [ ] **Step 6: The report and the docs** — `docs/autonomy/refactor/reports/2026-11-03-checkpoint-3.4.md` in the ten-section format (section 1 names the one live-edit difference — Pluck/Averaging settings no longer restart the draw stream — the eight archived patches that stop loading, the pending conversion, and `targetWave` not carried; section 2 the thirteen classes and the base, `IEvolutionHolder` and the twelve wrappers removed, the converter; section 3 the interface: `adjust` void, `prepare(ctx, maxLen)`/`start` on the base, `Slot<WaveEvolution>`; section 4 twelve wrapper shells and four pin lifecycles collapsed; section 5 the gates, the A/B, the synthetic A/B, the timing, the meter; section 6 the rulings and the three tables; section 7 the debt: `wave_evolution.h` still one file (3.6), the I-prefix inconsistency of the three part interfaces, the per-sample slot read, the clamps as hooks, the old/ eight; section 8 the refutation; section 10 lessons — an abstract base that is also the interface versus a pure interface beside it (C# abstract class vs interface), why `adjust` returning an unused float was a lie the compiler could not catch, a member that filed itself versus a reference to someone else's member, the vexing idempotence question of prepare); Part 3 section 5 gains the landed paragraph, section 8's "`EvolutionNode` template" wording is corrected to the landed shape, section 10 row 3.4 its result; the target's section 5 (the string form, `TargetEvolution`) and 3.2 gain landed lines; REVIEW R11 — under the 11-02 rule nothing to decide, listing the pending conversion and the eight archived patches so Matt sees them; CLEANUP rows as the Ledger named them; BACKLOG row 2 (3.4 DONE, 3.5 next); STATUS; the plan's Ledger.
- [ ] **Step 7: Commit** — `git commit -m "docs: checkpoint 3.4 report (R11), Part 3 section 5 landed paragraph and row 3.4, the target's landed lines, backlog, STATUS handoff, the plan's Ledger, CLEANUP rows, meter snapshot, MODULES.md regenerated"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **`WaveEvolution` is the family's abstract base, not a pure interface beside `Component`.** It derives from `Component` and owns the pin lifecycle once — `prepare(ctx, maxLen)` and `start(frames)` forward to every pin, non-virtual, with `on_prepare` the hook — the template-method shape Part 2 gave `ValueSource`. The alternative, the 3.1 sibling pattern (`struct PluckEvolution final : Component, WaveEvolution` with `WaveEvolution` pure), keeps four hand copies of the pin lifecycle (Reed, Bowed, Brass, BitRotate) and the forgotten-override class the 3.1 refuter named (I7: a holder that forgets its `prepare` never learns the rate, no compile error). `Slot<WaveEvolution>`'s `dynamic_cast` works on either. `IPartials` and `IFormant` keep their shape; whether they take the same base is a CLEANUP row, asked when 3.8 opens `Partials`.
2. **`adjust(frequency)` returns `void`.** Every caller discards the float (`compute_raw`, `compute_interpolated`); Pluck's `freqAdj_` was the pitch correction the tuning allpass replaced (EKS's comment says so) and reaches no sample. Deleting a dead computation is not a render change.
3. **Settings are read live; the draw stream is seeded once.** The old Pluck wrapper rebuilt its evolution on every setting change with a constant seed (`seed_` never received the constructor's), the Averaging wrapper with the right one; both restarted the stream on a live edit. The evolutions seed at construction from the registered or given seed and no setting change touches the rng. In scope every render is identical (Review Focus 1); in the editor a muting or speed edit mid-stream no longer restarts the draws — named in section 1, not asked (the 11-02 rule: understood, not far-reaching, reversible by one `reanchor()` in a hook if ever wanted).
4. **The four `on_setting_changed` clamps stay verbatim** (RD `subSteps ≥ 1`, Sort `swapsPerSample ≥ 1`, CA `rule` in 0..255 and `stepEvery ≥ 1`, HE `bins` in 4..256). `Setting::clamp()` would add upper bounds where the hooks enforced lower ones only (a `subSteps` of 20 loads today) — not byte-identical for an out-of-range patch. Whether every setting should clamp to its declared range is Part 4's loader question.
5. **Pins stay Manual and are advanced inside `evolve` in today's order**; the base prepares and starts them in declaration order (Review Focus 2). Declaration order is kept as the wrappers had it, so descriptor order (the editor's pin order, the loader's wiring order) is unchanged.
6. **The wavetable reads its slot per sample.** `evolution.get()` is the evolution itself now (no second hop through a holder), so the cached `evolution_`, `refresh_evolution()`, the `on_slot_changed` override, `set_evolution` and `ownedEvolution_` go; the stale-pointer class the 3.1 hook closed cannot arise. `Component::on_slot_changed` stays (AdditiveSource2 overrides it until 3.7). Two loads per sample; Task 4 measures.
7. **`TargetEvolution`'s shape**: `targetPartials` (Array, default one row of 1.0 — a sine), `targetLength` (int, structural by type, 16..8192, default 1024 = the loader's), `morphRate` (0.0001..0.1, default 0.001), `holdCycles` (0..100, default 3), `decayFactor` (0.9..1.0, default 0.998) — the string form's words and defaults, so the converter moves keys and the archived patches' vocabulary survives; the wave is synthesised in `on_prepare` (a structural change is applied by the next prepare; the editor re-prepares at every Play); `targetWave` is not carried (0 uses in the tree; an explicit period could come back as a second array when someone wants it); the unused rng goes; an empty target keeps the hold-phase formula (a render path cannot divide by zero). Shelf Experimental, label derived ("Target Evolution"); the ranges are a session call (CLEANUP).
8. **The string path dies first, with its converter; the two live patches are converted in place.** `tools/string_evolution_to_node.py` is the paramMap precedent (a standalone tool the refusal names; the eight archived patches convert on demand if ever revived). `patches/baselines/delay_test.json` is tracked and in the null-gate manifest, whose unchanged hash is the proof. `patches/pending/harsh_pluck.json` is Matt's sandbox, which runs never write into — except that the signed target architecture (section 5, decided with Matt 2026-10-05) names it as one of the two live patches that "get converted", the same one-time authorisation the paramMap conversion of the nine pending patches had. It is converted under that line, the original is kept at `renders/scratch/ab34/harsh_pluck_before.json`, and REVIEW R11 names it so Matt sees it.
9. **`skip_unresolvable` goes with the string form.** Its comment names the evolution string as its reason; after Task 1 a string under a Param, Input or Slot key fails the load naming the node, the pin and the value (2.5's "a rejected wire says so"), with the converter named for `evolution`. Task 1's scan confirms no other string-in-pin form is alive in scope before the deletion.
10. **`kBranchKeys` loses exactly the keys the deleted branch alone read**, decided by diffing the lint's scrape before and after (the meter's known-answer test guards the other direction). `evolution` leaves the list and stays known on the wavetable through its descriptor, so an `evolution` key on any other type is now refused as the unknown key it is.
11. **`WavetableSource` leaves the loader's hand-written branch.** The branch constructed it exactly as its registry factory does and did nothing else but the string form; the generic path builds it from Task 1.
12. **The synthetic set**: the two ab33 members (Brass, EKS) by path; new `averaging_evolution` (node form, seed 77, every setting non-default), `pluck_string` and `target_evolution` (string forms converted in place by the tool at Tasks 1 and 3), `harsh_pluck_scored` (the pending patch with a score) and `harsh_pluck.json` itself. The old arm is rendered before any conversion; the compare is by path, so a converted file is compared across its conversion.
13. **Order of tasks**: the string deletion first (Task 1: independent of the object model, its proof is one manifest hash), the merge (Task 2: the twelve at once — a holder cannot sit in a `Slot<WaveEvolution>`, so the slot type and the registrations change together), `TargetEvolution` (Task 3: a new registered part with its own tests and counts), the close. Task 1 carries a one-line transitional fix (`PluckEvolutionSource::seed_` from the constructor) so the converted `delay_test` renders with its seed between Tasks 1 and 2; Task 2 deletes the wrapper.
14. **`wave_evolution.h` stays one file.** The checkpoint is the object model; 3.6 is one type per file. The file shrinks by the twelve wrapper shells; Part 3 section 8's "the `EvolutionNode` template" is the rejected draft's wording (section 5 says why) and Task 4 corrects it to "the `WaveEvolution` base and one file per evolution".
15. **The interface keeps the name `WaveEvolution`** (the signed section 5 uses it; the C# was `IWaveEvolution`). The three part interfaces then carry two conventions (`IPartials`, `IFormant`, `WaveEvolution`) — the 3.1 refuter's I-prefix note; one CLEANUP row, decided when 3.8 touches `IPartials`.
16. **REVIEW R11 under the 11-02 rule.** Expected to carry no decision: every call here is exact by construction or the kind the rule says the session decides (rulings 3, 7, 8, 9). The entry names the pending conversion and the eight archived patches that stop loading, so the deletion's consequences are visible without being asked.

(Execution rulings, by task.)

## Self-review notes

- **Spec coverage.** Section 5's six sentences — "a `Component` that implements the `WaveEvolution` interface directly" (ruling 1, Task 2); "settings declared once as Part 2's members and read where they are used" (Task 2's Interfaces block; ruling 3); "`IEvolutionHolder` and the cast are deleted" (Task 2); "`Slot<WaveEvolution>`, type-checked at wiring" (Task 2; the door is 3.1's); "the string-typed loader path ... is deleted" (Task 1; ruling 8 the two conversions, ruling 9 the refusal); "`TargetEvolution` is registered like the others" (Task 3; ruling 7 its shape); "Thirteen classes replace twenty-six" (Task 2 deletes twelve wrappers and `IEvolutionHolder`, Task 3 registers the thirteenth); row 3.4 "none" — the Global Constraints. The target's section 5: the two live patches (ruling 8), the converter (the paramMap precedent), `TargetEvolution` kept.
- **Step scan.** Every code step names the file, the signature or the rule; every test names its subject and its expected value; every verification step has its command and expected output; every mutation names the flip. Open by design: the exact text of the refusal message beyond its three named parts, the converter's node id scheme (`<wt>_evolution`), and which of `kBranchKeys`' entries leave (decided by the scrape diff, Task 1 step 5, recorded in the Ledger).
- **Type consistency.** `WaveEvolution`, `PluckEvolution` … `BitRotateEvolution`, `TargetEvolution`, `Slot<WaveEvolution>`, `on_prepare(ctx, maxLen)`, `refuse_string_pin`, `tools/string_evolution_to_node.py`, `cli_pre34.exe`, `renders/scratch/ab34/{old,new,synth_old,synth_t<N>,synth_new}` are used with those names throughout.
- **Review Focus.** 1 — Task 0's `averaging_evolution`, Task 1's conversions, Task 2 step 4; 2 — Task 2 step 1 (lifecycle test) and step 4; 3 — Task 2 and Task 4 step 3; 4 — Task 1 steps 1, 3, 7; 5 — Task 0 (`target_evolution` string form), Task 3 steps 3–4; 6 — every task's mutation step.
- **Proportion.** Code appears for the base and one evolution (the shape every other follows), the owner's three lines, `TargetEvolution`'s declaration and the converter's contract; the rest is comparison prose, commands and rulings. The engine change is about 350 lines of wrapper out, 40 lines of base and `TargetEvolution` in, 45 lines of loader out; one pin kind's hint text changes; no descriptor on any existing type changes name, order or default.

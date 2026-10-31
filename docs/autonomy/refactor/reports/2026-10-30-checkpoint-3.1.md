# Checkpoint 3.1 — `Component` root; the parts leave `ValueSource`

Part 3 (`docs/architecture/part3-nodes.md`), checkpoint 3.1 of 3.8. Commits `ba097f6`..`f1f8244` on main (plan `docs/superpowers/plans/2026-10-30-part3-checkpoint-3.1.md`, committed as `98bf883`; execution ledger in the plan's Ledger). Report R8. Written 2026-10-30 at extra-high effort, inline execution with a Fable refuter (Matt's choice, 2026-10-30).

## 1. Summary

Twenty-two registered types that never produced a signal — the four `Partials` types and `ExpandRule`, the five formant and spectrum types, the twelve evolution holders — derived from `ValueSource` so that the graph and the editor could hold them, and faked the signal contract with a `compute()` returning 0 and `on_prepare`/`on_start` forwarders. They now derive from `Component` and their interface (`IPartials`, `IFormant`, `IEvolutionHolder`) and nothing else. The registry creates `Component`s; the loader's graph, the voice and the editor's node handle hold `Component`s; every consumer holds its part in a `Slot<I>` and drives it through the interface — prepare once per build, start once per note, next once per sample — exactly as the additive source already drove its partials. Nothing in the graph walks a part.

One door wires every pin kind. `Component::set_param(name, std::shared_ptr<Component>)` lets the pin decide: a `Param` or `Input` takes a signal and refuses a part; a `Slot<T>` takes a `T` and refuses the rest; `add_param` appends on a multi slot, `clear_param` empties one; `get_param` serves the signal pins, `get_part` the slots. The type-check at wiring, which Part 2 put "in one place", is that door — paid at load or at an editor change, never on a render path. The five runtime `dynamic_cast`s the consumers kept (`FullAdditiveSource::set_param` ×2, `FormantSequence` ×2, `WavetableSource::set_param`), the loader's `add_formant` and `CompositePartials`' own are gone with it.

`Component` gained a third hook beside `on_setting_changed` and `on_array_changed`: `on_slot_changed(name)`, called by the door after a slot is set, added to or cleared. It exists because the wavetable keeps a raw pointer to the evolution its holder carries and read it per sample; resolving that pointer only at prepare, as the plan said, left a stale pointer between an editor rewire and the next Play — a test caught it (section 8 did not have to). AdditiveSource2 copies its partials' arrays on the same hook.

**Behaviour.** No render changed: the null gate is 78/78 after every one of the six commits, the A/B over the 132 patches of `patches/library` and `patches/baselines` against the pre-checkpoint binary is 0 DIFFER, the allocation gate is clean. One behaviour changed by design, for malformed wires only: a signal into a slot (a `ConstantSource` into `evolution`, a `SineSource` into `partials`) was accepted and inert, and a part named where a signal is expected (`{"ref":"parts"}` into `frequency`, a part as `graph.output` or a channel's `source`) was accepted and read 0 for ever — both now fail the load naming the node, the pin and the reason. No patch in scope carries either (every one of the 494 wires into a part in library and baselines lands in a slot-shaped pin; no part is an output or a value anywhere). REVIEW R8 carries it.

**Tooling.** `tools/alloc_gate.py` splits the allocation gate's 132 patches over three processes (50 s against 78 single-threaded), so the gate is in `gates.py`'s fast list and ran before every commit of this checkpoint. The two minors the 2.6 review left: `BowedStringEvolution`'s period floor is `WaveSource::kMinTableHz`, not a second literal; `SegmentSource::update_segments` no longer reserves what `on_prepare` reserved.

## 2. Types and files

30 files, +1036/−641 lines (`git diff --stat 98bf883 f1f8244`; the six task commits, the review wave and two test commits).

| Area | What changed |
|---|---|
| `core/component.h`, `core/pins.h`, `core/dsp_value_source.h` | The door (`set_param`, `add_param`, `clear_param` over `std::shared_ptr<Component>`; `get_part`; `on_slot_changed`), defined where `ValueSource` is complete. `SlotBase` gains `add`, `clear`, `first` as virtuals; `Slot<T>` gains `add` (appends on a multi slot) and `get()` (the single slot's part as a `T`); `set` replaces. `ensure_constant` takes a `Component&` (it reads only the pin registry). |
| `core/source_registry.h`, `src/source_registry.cpp`, `src/source_registrations.cpp` | `SourceFactory` and `create` return `std::shared_ptr<Component>`; the fifteen configurators take a `Component&`; `FixedSpectrum` gains a configurator that keeps a spectrum without `gains` a load error. The eighty-odd factory lambdas are untouched: a lambda returning `shared_ptr<SineSource>` converts. |
| `src/patch_loader.cpp` | `GraphResult::nodes` is one map of `Component` (the `valueNodes` and `formantNodes` pair is gone); `resolve_param` hands the door any node, `resolve_signal`/`as_signal` give a `ValueSource` or fail naming the id, the type and "a part, not a signal"; `reject_wire` and `reject_unknown_keys` take `Component`s and the former says the kind; `add_mono` wraps signals only (a part gets no mono adapter, so a part as output or channel source meets the existing "must be a mono-renderable source" error); the three hand-written formant branches (FormantSpectrum, FixedSpectrum, FormantSequence) fall to the generic path; `adopt_nodes` fills the voice's table and its signal list; `tracks_frequency_live` is asked of signals only; a tap's target, a chain's root, a dynamic pin's source, a subgraph's root and a voice's output resolve as signals. |
| `render/instrument.h` | `PushBinding::consumer` and `VoiceGraph::nodesById` hold `Component`s; `VoiceGraph::signals` (`{id, ValueSource*}`, id order, built at load) is what `collect_faults`, `capture_begin` and `deliver_continuation`'s reseed walk. |
| `source/multiplex_source.h`, `core/multi_source.h` | The clone maps hold `Component`s (`NodeMap`); `MultiSource::set_param` takes what the door takes and casts its list entries once. |
| `source/additive/formant.h` | `IFormant::{prepare, start, next, contains, get_gain}`; the five types derive from `Component, IFormant`; `FormantSpectrum::formants` and `FormantSequence::spectra` are multi `Slot<IFormant>` pins (the hand-written `input_descriptors`, `set_param`, `add_param`, `clear_param` of FormantSequence gone). |
| `source/additive/partials.h` | `IPartials::{prepare, start, next, …}`; `Partials`, `CompositePartials`, `ExpandRuleNode` derive from `Component`; `CompositePartials::partials` is a multi `Slot<IPartials>` (its `Entry` list and three overrides gone). |
| `source/additive/full_additive_source.h`/`.cpp`, `additive_source2.h` | `Slot<IFormant> formant`, `Slot<IPartials> partials`, the `set_param` override and the two cached interface pointers gone; `Slot<Partials> partials` on AdditiveSource2 with `on_slot_changed` copying the arrays. |
| `source/wave_evolution.h`, `source/wavetable_source.h`/`.cpp` | `IEvolutionHolder::{prepare, start}` with empty defaults; the twelve holders derive from `Component, IEvolutionHolder`; `Slot<IEvolutionHolder> evolution`, the owner's `refresh_evolution()` on the hook, at prepare and in `set_evolution`. |
| `tools/mforce_ui/main.cpp` | `GraphNode::dspSource` is a `Component`; `GraphNode::signal()` for the output, the listen tap, the stream channels, the capture list and `prepare_graph` (which prepares signals only: a part is prepared by its owner); `wire_pin` takes a `Component`; the FormantSpectrum table fills its slot; no face pin for that slot. |
| `tools/mforce_cli/main.cpp` | `run_dump_descriptors` holds a `Component`. |
| `tools/engine_tests/main.cpp` | `run_wiring_door_tests`, `run_formant_part_tests`, `run_partials_part_tests`, `run_component_root_tests` (the audit, `kPartTypes`); the sweeps skip and count the parts; the family tests wire parts through the door. |
| `tools/alloc_gate.py`, `tools/gates.py`, `tools/structure/modules.json` | The gate wrapper; the allocation gate in `FAST`. |

Twenty fake `compute()` bodies, their forwarders, nine runtime casts and three loader branches left; one hook, one wrapper and four test functions arrived.

## 3. Interfaces changed

| Interface | Before | After |
|---|---|---|
| `Component::set_param` | `(std::string_view, std::shared_ptr<ValueSource>)`, Param/Input only; slots by per-node override | `(std::string_view, std::shared_ptr<Component>)`: Param/Input take a signal, a Slot its `T`, else refused |
| `Component::add_param` / `clear_param` | `set_param` / `set_param(name, nullptr)` | a multi slot appends / a slot empties; both call `on_slot_changed` |
| `Component::get_part` | — | the part a slot holds (a multi slot's first) |
| `Component::on_slot_changed(std::string_view)` | — | hook after a slot changes |
| `SlotBase` | `descriptor`, `set` | + `add`, `clear`, `first` (virtual) |
| `Slot<T>` | `set` (appends on multi) | `set` replaces, `add` appends on multi, `get()` the single part as `T*`, `first()` |
| `SourceFactory`, `SourceRegistry::create` | `std::shared_ptr<ValueSource>` | `std::shared_ptr<Component>` |
| `JsonConfigurator` | `(ValueSource&, …)` | `(Component&, …)` |
| `ensure_constant` | `(ValueSource&, …)` ×2 | `(Component&, …)` ×2 |
| `IFormant` | `fmt_prepare`, `fmt_start`, `fmt_next` | `prepare`, `start`, `next` |
| `IPartials` | `partials_prepare`, `partials_start`, `partials_next` | `prepare`, `start`, `next` |
| `IEvolutionHolder` | `get_evolution` | + `prepare`, `start` (empty defaults) |
| `MultiplexSource::ValueNodeMap` | map of `ValueSource` | `NodeMap`, map of `Component` |
| `VoiceGraph::nodesById` | map of `ValueSource` | map of `Component`; + `signals` |
| `PushBinding::consumer` | `ValueSource` | `Component` |
| `GraphNode::dspSource` (UI) | `ValueSource` | `Component`; + `signal()` |
| `FormantSpectrum::formants`, `FormantSequence::spectra`, `CompositePartials::partials` | `std::vector<std::shared_ptr<I>>` / `Entry` list | multi `Slot<I>` |
| `FullAdditiveSource::formant/partials`, `WavetableSource::evolution` | `Input` + a cached interface pointer | `Slot<I>` |
| `AdditiveSource2::partials` | `Input` + override | `Slot<Partials>` + `on_slot_changed` |

The twenty-two parts lose `next()`, `current()`, `prepare()`, `start()`, `step()`, `previous()`, the fault register and `reseed()` — the signal contract, which they never honoured.

## 4. Duplicates collapsed

- The type-check at wiring, written five times in consumers (two casts in `FullAdditiveSource::set_param`, two in `FormantSequence`, one in `WavetableSource::set_param`) and once each in `CompositePartials::set_param` and the loader's `add_formant`: once, in the door and `Slot<T>::add`.
- Three hand-written loader branches (FormantSpectrum, FixedSpectrum, FormantSequence) that did what the generic path does: gone; the generic path builds them.
- Twenty `compute() { return 0.0f; }` (or `{ x_next(); return 0.0f; }`) and their `on_prepare`/`on_start` forwarders: gone with the base.
- Three hand-written `input_descriptors()` overrides describing a list the node held by hand (FormantSequence, CompositePartials, and the one `MultiSource` keeps — see section 7): two gone; the slot registers its own descriptor.
- The two `GraphResult` maps (`valueNodes`, `formantNodes`) kept in step by hand: one map.
- Bowed's period floor literal beside `kMinTableHz`; Segment's `reserve` beside `on_prepare`'s.

New duplication, acknowledged: the three interfaces each carry a `prepare`/`start`/`next` trio (section 7); `GraphNode::signal()` is called where the UI needs a signal (eight sites) rather than held.

## 5. Metrics

**Gates at the seal** (`python tools/gates.py`, every gate):

| Gate | Result |
|---|---|
| ctest (engine_tests, test_figures, structure meter) | PASS — `engine_tests` ALL PASS, 6659 checks (6559 at the plan: +100; four new test functions, the sweeps' part set) |
| `mforce_ui --stamp` | PASS |
| roundtrip smoke (one patch per shape) | the known wiring diff (backlog 79), 5/6 clean — pre-existing |
| allocation gate (`tools/alloc_gate.py --jobs 3`, library + baselines) | PASS, 132 patches, 0 allocate, 46–48 s; in `FAST` since Task 0 and run before every commit |
| null gate (`null_gate_perform_source.py --jobs 3`) | 78/78 manifest entries identical — after every one of the six commits |
| roundtrip (UI save/load, library + baselines) | the backlog-79 six (`double_advance_bug`, `double_advance_ok`, `loop_tap_counter`, `loop_tap_starved`, `wiring_setting`, `wiring_smoke`) — the same six 2.6's report names; 0 id changes over 132 |
| wiring null gate (paramMap conversion) | 78/78 |

The structure meter's known-answer test (`draw_properties_panel` at 1035 lines) held through every UI edit.

**The A/B** over every patch under `patches/library` and `patches/baselines` (`renders/scratch/ab31/library_baselines.txt`, 132 patches; the old arm rendered at Task 0 by `renders/scratch/ab_tree/cli_pre31.exe`, the build at `6007b29`; the new arm by the seal's build):

```
compared 132 patches present in both arms
  identical : 132
  DIFFER    : 0
```

Expected total (Part 3 section 10 row 3.1: "none") and found total. Appendix A lists the set.

**Timing.** `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre31.exe build/tools/mforce_cli/Release/mforce_cli.exe` (the minimum wall of alternating rounds; "speedup" = base / new), three runs, none on a quiet machine: the first while the refuter was reading the tree, the second and third with nothing of this session's running but with the base arm's own walls 25 to 35 percent above the first run's (oboe1 803, 1000, 1095 ms for the same binary; 2.6's clean run had 925) — the machine was busy with something not mine, and the spread between runs of one binary exceeds every row's delta.

| patch | run 1 base / new (ms) | run 2 | run 3 | render columns, run 1 / 2 / 3 |
|---|---|---|---|---|
| oboe1 | 803.5 / 790.8 (1.02x) | 1000.5 / 944.7 (1.06x) | 1095.4 / 1045.4 (1.05x) | 19.2→20.2, 22.1→21.7, 22.7→27.9 |
| piano_default | 990.0 / 977.6 (1.01x) | 1163.0 / 1161.8 (1.00x) | 1140.4 / 1378.7 (0.83x) | 37.8→40.0, 45.6→45.7, 42.4→48.2 |
| viola_default | 1184.8 / 1188.8 (1.00x) | 1276.4 / 1306.0 (0.98x) | 1720.7 / 1383.5 (1.24x) | 6.0→6.1, 5.8→6.0, 11.1→6.7 |
| A sine | 431.3 / 430.6 (1.00x) | 478.4 / 474.2 (1.01x) | 561.2 / 560.0 (1.00x) | 126.4→126.2, 149.1→143.1, 163.4→184.8 |
| B sine+env | 443.9 / 439.0 (1.01x) | 527.8 / 509.5 (1.04x) | 649.6 / 562.5 (1.15x) | 145.7→137.9, 175.0→178.4, 223.6→189.1 |
| C filter x8 | 714.4 / 711.6 (1.00x) | 865.3 / 857.3 (1.01x) | 1064.0 / 917.2 (1.16x) | 412.7→412.7, 531.2→516.5, 641.5→500.2 |
| D 8 sines | 1148.1 / 1149.8 (1.00x) | 1546.5 / 1506.6 (1.03x) | 1845.5 / 1710.0 (1.08x) | 843.7→852.3, 1204.6→1167.0, 1423.6→1308.1 |
| E shared ref | 485.7 / 470.9 (1.03x) | 559.7 / 564.0 (0.99x) | 653.3 / 653.3 (1.00x) | 180.0→167.1, 218.5→218.9, 256.7→240.3 |
| F 40 sines | 2909.5 / 3081.7 (0.94x) | 3974.8 / 4109.4 (0.97x) | 4586.5 / 4590.9 (1.00x) | 2701.9→2871.5, 3740.4→3875.7, 4175.0→4178.1 |
| G 40 own env | 3800.7 / 3870.8 (0.98x) | 5188.1 / 5334.5 (0.97x) | 6417.0 / 4890.5 (1.31x) | 3591.1→3663.9, 4950.7→5095.5, 6008.9→4662.9 |
| H 40 shared | 2843.3 / 2874.8 (0.99x) | 3740.5 / 4218.7 (0.89x) | 3979.3 / 3720.8 (1.07x) | 2635.0→2664.6, 3504.8→3982.7, 3756.7→3430.2 |
| I 40 envs | 1573.3 / 1638.5 (0.96x) | 2128.5 / 2119.2 (1.00x) | 2037.5 / 1976.3 (1.03x) | 1366.0→1431.0, 1891.6→1864.7, 1759.0→1753.0 |

The measurement is inconclusive and says so: rows swing from 0.83x to 1.31x between runs of binaries that differ only in control-side code. What stands is the mechanism, which the refuter traced independently (section 8, brief item 8): no render path gained a virtual call, a cast, an allocation or a lock; `FullAdditiveSource::wave()` reads its two parts through `Slot::get()` (one vector load each) where it read two cached `shared_ptr`s; `WavetableSource` reads the same raw pointer it read before; the holders' `next()` was never called per sample. The rows the per-sample work could touch (F, G, H — forty sines and envelopes with no parts at all) show 0.94x, 0.97x and 1.00x across the three runs for F, with a base-arm spread of 58 percent. REVIEW R8 item 7 offers the choice: a rerun on a quiet machine before Part 3 continues, or the mechanism argument accepted for a checkpoint that changed no per-sample path. The three runs are kept as `renders/scratch/timing31_run{1,2,3}.txt`.

**The meter** (`python tools/structure/check.py --delta ba097f6`, the tree after Task 0 against the seal `f1f8244`):

| Measure | Before | After | Change |
|---|---|---|---|
| files | 170 | 171 | +1 |
| lines | 60,500 | 60,727 | +227 |
| files_over_limit | 20 | 20 | +0 |
| functions_over_limit | 99 | 99 | +0 |
| file_level_mutable_vars | 120 | 120 | +0 |
| const_cast | 10 | 10 | +0 |
| dynamic_cast | 58 | 60 | +2 |
| changelog_comments | 250 | 250 | +0 |
| duplicate_blocks | 158 | 153 | -5 |
| include_violations | 13 | 13 | +0 |
| unmapped_files | 0 | 0 | +0 |

The engine shrank where the parts live — `formant.h` −46, `partials.h` −43, `full_additive_source.h` −26, `wave_evolution.h` −16, `additive_source2.h` −5 — and grew where the door lives: `component.h` +31, `engine/src/component.cpp` 33 (new; the door's body, the one file added), `pins.h` +9, `descriptors.h` +3, `instrument.h` +5, `patch_loader.cpp` +7, `source_registrations.cpp` +9 (two configurators). The tests grew +247 (seven functions, the audit's set, the review wave's cases). The two `dynamic_cast`s the meter counts net: nine runtime casts in consumers and the loader left; the door's two, `Slot<T>::set`'s and `add`'s one each, `MultiSource`'s one and the loader's five (`as_signal`, `reject_wire`'s kind, `add_mono`, `tracks_frequency_live`, `adopt_nodes`) arrived — every one at load or at an editor change. Five duplicate blocks gone (the fake lifecycles, the formant branches); no function over the limit (the review wave's gap test was split in two at `f1f8244` when it reached 82 lines). The one new type is the tests' `Plug`.

## 6. Decisions and rejected alternatives

The fourteen planning rulings (plan Ledger 1–14) and the execution rulings, in the order made. Each says what it costs if wrong.

**Planning.**

1. **One wiring door for every pin kind** (`Component::set_param(name, std::shared_ptr<Component>)`, the pin deciding). Alternative: a second door the caller chooses by kind — every caller (the loader's generic wiring, the UI's `wire_pin`, Multiplex's clone fan, forty test sites) would need to know a pin's kind before wiring it, which only the pin knows. Both casts live in the door and run at load or at an editor change. Cost if wrong: an implicit upcast at the call — none at run time.
2. **`get_param` keeps serving signals; `get_part` serves slots.** Widening `get_param` would break every caller reading `->current()` on the result for three test sites. Cost: a second getter.
3. **The verbs lose their prefixes.** Their own comment said the prefixes avoided a collision with `ValueSource`'s verbs; a part has none. `IEvolutionHolder` gains `prepare`/`start` beside `WaveEvolution::prepare(ctx, maxLen)`; 3.4 merges the two objects and the two prepares.
4. **Pull all the way down** (Matt, 2026-10-30): the owner calls into its part; the part prepares, starts and pulls its own pins (every pin on every part is `Pull::Manual`); a slot forwards nothing, like a setting. The UI's `prepare_graph` stops preparing parts directly (it prepared them twice: once itself, once through the owner — idempotent by contract, so no render moves).
5. **One node table and one signal list on the voice.** `nodesById` holds every `Component` by id; `signals` is built once at load for the three walks that need signals, one of which (`deliver_continuation`'s reseed) runs on the audio thread. Cost: one vector of `{id, pointer}` per voice.
6. **`resolve_param` returns a `Component`; `resolve_signal` a `ValueSource` or throws.** A part where a constructor, a tap, a chain or a voice's output needs a signal fails with "'<id>' is a <type>, a part, not a signal"; in mixer mode a part gets no mono adapter, so a part as the graph's output meets "graph.output must be a StereoMixer or a mono-renderable source" and a part as a channel's source "No mono source for: <id>" — three messages, one test each (the refuter's I11; the plan named only the first of the two mixer-mode ones).
7. **Two malformed wires change behaviour, by design** — the signal-into-slot and part-into-signal cases, now load failures with the reason; 2.5's "a rejected wire says so" for the two kinds; no patch in scope carries either. REVIEW R8 item.
8. **The holders stay wrappers in 3.1**; 3.4 makes each evolution a `Component` itself. Folding 3.4 in would add twelve families' settings moving from constructor arguments to live members — a byte question per family — on top of the root change.
9. **The three formant branches collapse into the generic path.** `FormantSequence`'s undocumented `"formants"` key (read beside the descriptor's `"spectra"`) is dropped: no patch in scope uses it; the `kBranchKeys` allowlist keeps `"formants"` for `FormantSpectrum`, so a `FormantSequence` carrying it would load without its spectra — the one key whose meaning narrows.
10. **AdditiveSource2 gets `Slot<Partials>`, the concrete type** — it reads the arrays, not the interface, and 3.7 deletes it. (The second sentence, "apply_partials moves to the top of on_prepare", was overruled in execution: ruling 25.)
11. **`ExpandRule` stays a loader-cast part**: `ExpandRuleNode` is a `Component`; the loader's `expandRule` branch casts on the map as it did; the `to_struct()` snapshot at load is unchanged (its "at every start" comment is not what the code does — Part 4 owns the loader's shape and that fix).
12. **The A/B set is library + baselines** (Matt, 2026-10-30): 132 patches, the old arm rendered at Task 0 from the build at `6007b29`.
13. **The allocation gate joins `--fast` through a Python wrapper**, not a `--jobs` inside the executable: the slicing and fan-out are what `null_gate_perform_source.py` already does; the executable stays the single-process tool.
14. **Order of tasks**: tooling first, the widening with nothing leaving (one commit, since the UI must build for the gates), then the families smallest consumer surface first, then the close.

**Execution** (the ledger's `Ruling:` lines, dated by commit).

15. *(Task 0, `ba097f6`)* **The gate moves to `FAST` through the wrapper**: 49.9 s alone at three jobs, 46 s inside the gates, under the plan's minute (R7 item 0 named both outcomes). Cost if wrong: 46 s on every commit's gates.
16. *(Task 1, `96d4905`)* **The plan's Task 1 assertions could not hold yet.** "A part into a Param is refused" and "a part as a signal fails the load" need a part that has left `ValueSource`; in Task 1 none had. The door's refusal was pinned with a bare `Component` (a test-local `Plug`), the slot getters waited for Task 3, the loader case moved to Task 2 with a `Formant` as the first part that had left. Cost: none; each line came alive in the task that made it true.
17. *(Task 1)* **`get_param("evolution") == reed` stays `get_param` until Task 4** makes the pin a slot (the plan's step 8 said `get_part` a task early).
18. *(Task 1)* **Multiplex's `ValueNodeMap` is `NodeMap`**; the three test sites that spelled the alias follow.
19. *(Task 2, `f6e5243`)* **A slot's `set` replaces and `add` appends on a multi slot** (`SlotBase::add`; `Component::add_param` routes a slot to it). The generic wiring's clear-then-add and the UI's first-pass clear keep their meaning, and the tests' "set replaces, add appends" stands. Cost: none; both callers clear first anyway.
20. *(Task 2)* **A `FixedSpectrum` without `gains` stays a load error**, through a configurator: the deleted branch threw; the generic array pass would have accepted an empty spectrum silently. Cost: three lines.
21. *(Task 2)* **The UI builds no face pin for `FormantSpectrum`'s `formants` descriptor** — its inline table is that pin; the face and the roundtrip stay as they were. Cost: one line in `build_pins`.
22. *(Task 2)* **The registry sweep's oracle is the named set `kPartTypes`**, grown by each family task and the audit's constant at the close (the plan had the empty-set check until Task 5).
23. *(Task 2)* **A null part into a slot clears it**, as a null into a Param restores the constant; the old additive override ignored a null formant. The test asserts the clear.
24. *(Task 3, `08a8433`)* **The `ExpandRuleNode` test drives `to_struct()`** in place of a lifecycle the node no longer has; nothing pulls its pins either way.
25. *(Task 3)* **AdditiveSource2 keeps copying the Partials' arrays at wire time**, not at prepare as plan ruling 10 said: the one baseline on the type copies exactly when it did, and a copy at prepare would read arrays a Partials' pending rebuild may have changed — a byte question a type slated for deletion does not deserve. First as an override after the base door, then (Task 4) on `on_slot_changed`. Cost: one hook override on a type 3.7 deletes.
26. *(Task 3)* **The partials verbs were renamed by a word-boundary regex over `partials.h`** (the three definitions and every mention); no free functions existed, so the plan's open question was moot.
27. *(Task 4, `722f38c`)* **`Component::on_slot_changed(name)`**, the third hook beside `on_setting_changed` and `on_array_changed`, called by the door after a slot is set, added to or cleared. The plan's "evolution_ resolved in on_prepare" alone left the wavetable evolving through a stale pointer after a clear or rewire until the next prepare — the test at the old `evolution.source` check caught it, and a live rewire in the editor (under the audio mutex, with the stream running) would have read a part the slot no longer held, dangling if the slot had been its last owner. `WavetableSource` refreshes its pointer on the hook, at prepare and in `set_evolution`; AdditiveSource2 copies on it and loses its override. Cost: one virtual call at wire time.
28. *(Task 4)* **`IEvolutionHolder::prepare`/`start` have empty default bodies** (a holder without pins has nothing to do) rather than pure virtuals; eight of twelve holders would otherwise carry empty overrides.
29. *(Task 5)* **The audit's count is twenty-two, not the plan's twenty-four.** The registered parts are the four Partials types (`CompositePartials` among them), `ExpandRule`, the five formant types and the twelve holders: 4 + 1 + 5 + 12 = 22; the plan counted `CompositePartials` twice and an abstract `Partials` base that is not registered. `kPartTypes` is the one constant, the sweep's and the audit's oracle.
30. *(Task 5)* **The report is written after the refutation**, as 2.6's was, so section 8 is complete at the seal.

## 7. Debt knowingly left

- **The holders are still wrappers**, `Component, IEvolutionHolder` over a `WaveEvolution` member rebuilt on every setting change; `IEvolutionHolder::prepare(ctx)` (the holder's pins) sits beside `WaveEvolution::prepare(ctx, maxLen)` (the evolution's buffers). Checkpoint 3.4 makes each evolution a `Component`, deletes the twelve wrappers and `IEvolutionHolder`, and merges the two prepares. The loader's string-typed evolution (`set_evolution`, `ownedEvolution_`) dies there too.
- **`AdditiveSource2`** and its `Slot<Partials>` over the concrete type, copying arrays at wire time: deleted in 3.7 with its one baseline patch migrated.
- **The three interfaces each carry a `prepare`/`start`/`next` trio.** A `Part` base interface would hold the trio once and let a `Slot<Part>` drive any part; whether the three interfaces share a root is a 3.4/3.8 design question once the holders are gone and `Partials` is split.
- **The UI's part strips capture nothing** where they captured a flat zero line before (a part has no signal to capture); the Partials and Formant strips draw their own spectra and are unaffected. Part 7.
- **The UI reads a node as a signal through `GraphNode::signal()`**, a `dynamic_cast` at each of eight control-side sites, rather than holding a typed view; `prepare_graph` now prepares signals only. Part 7 rebuilds the node model.
- **The UI skips a tap wire from a part silently** (`update_node_dsp_unlocked`: only a signal has a previous sample to tap); the editor should refuse the wire with a message. Part 7.
- **The loader's `expandRule` branch** casts the `Component` map to `ExpandRuleNode` and snapshots `to_struct()` at load; a wired envelope into an `ExpandRule` pin is frozen at load (pre-existing), and the node's comment says "at every start". Part 4.
- **`kBranchKeys`** still allowlists `"formants"`, `"spectra"` and `"gains"`, which are descriptors now; the allowlist could shrink. Part 4.
- **`MultiSource::source`** is still a hand-written multi input over a list of signals with its own `set_param`/`clear_param` overrides and `input_descriptors()`; a multi `Input` (or `Slots<ValueSource>`) would retire the last hand-written list. Part 3, a later checkpoint.
- **`Slot<T>::parts` and `views` are public** members the owners iterate; an accessor would hide the pair. Part 3's encapsulation pass.
- **`VoiceGraph` holds two structures** (`nodesById`, `signals`) that must agree; `adopt_nodes` is the one writer. Part 5 may fold them into the voice's own type.
- **`capture_begin` looks ids up linearly** in `signals` (control side, per capture start, twenty-odd nodes).
- **The editor's rewire pass fires `on_slot_changed` twice per rewire** (clear, then refill; two pointer swaps, control side, under the audio mutex) — harmless today; an owner with an expensive hook would feel it. Part 7.
- **The loader's allowlist is global**: `"formants"` stays in it because the `FormantSequence` configurator reads it, so a stray `"formants"` on any other node still passes `reject_unknown_keys`. Part 4's allowlist, pre-existing.
- **The reseed test registers a type into the registry singleton** for the rest of the test run (section 8, N2); an `unregister` for tests was judged the worse smell.
- **`FormantSequence`'s `"formants"` key** is no longer read (ruling 9); a `FormantSequence` in `patches/old/` carrying it loads without its spectra.
- Carried unchanged from 2.6's section 7: the offline hosts' per-note buffers (`play_note`, `play_hit`, `StereoMixer::render`), the live legato `Setup`'s glide allocation, onsets and hold absent from the live path, the pull step in five places, the two `fill_table` twins, the probe's `thread_local`s in the meter, the `gap` key accepted on a RepeatingSource baseline.

## 8. Independent review

The refuter (Fable, extra-high effort, read-only: it built, rendered and ran nothing of the project) received the plan, the ledger, the spec sections, the ten-item brief with checkpoint-specific questions and the review package `98bf883..fe9f3d5`; its report follows verbatim, then what was done about each finding, then its re-review of the wave.

Range `98bf883..fe9f3d5` (six commits), repository `C:\@dev\repos\mforce`, branch `main`, HEAD `fe9f3d5`. I read the brief, the plan (`docs/superpowers/plans/2026-10-30-part3-checkpoint-3.1.md`: Spec, Global Constraints, the five Review Focus items, the fourteen Ledger rulings), the execution ledger (`.superpowers/sdd/2026-10-30-part3-checkpoint-3.1/progress.md`, every `Ruling:`), the Task 1 and Task 4 briefs, Part 3 section 2 and section 10, Part 2 section 3, `CLAUDE.md`, `tools/structure/modules.json` (there is no `MODULES.md`), then the review package in full (4,997 lines) and, at HEAD, every file it names that I cite below, traced line by line: the door and `Slot<T>` (`component.h`, `pins.h`, `dsp_value_source.h`), the loader (`patch_loader.cpp`), the voice (`instrument.h`), the three part families, the wavetable, the UI's link, rewire, lock and strip paths, the tests, the gate wrapper. I ran nothing of the project's: no build, render, gate or structure meter. I ran `git log`/`git show`/`git ls-files`/`git check-ignore`, ripgrep over the tree, and one read-only Python scan of my own (`scan_parts.py`, 3,138 graph patches under `patches/`, and the 132 in `patches/library` + `patches/baselines` separately) for the key whose meaning the plan narrowed and for parts in signal positions. Line numbers below are HEAD's.

Headline: the engine side of the checkpoint is sound — the door is one door, the voice's audio-thread walk casts nothing, the parts carry nothing of `ValueSource`, the twenty-two is the registry's own count, and the tests bite where they exist. Three findings grade Major: (I1) in the UI, unlinking a holder from `WavetableSource.evolution` no longer removes the evolution, a regression against `98bf883` that the roundtrip gate cannot see; (I2) the dropped `FormantSequence` `"formants"` alias is a silent downgrade on 34 archived patches because the loader's allowlist still carries the key; (I3) the new allocation-gate wrapper reports PASS when a child process crashes. The rest is Minor: duplicated part-type knowledge in the UI, refusal messages that never say what a slot expects, three interfaces with two conventions, stale comments.

---

### The brief, item by item

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | Separation of concerns | PASS with a flag | The door routes by pin kind and does nothing else (`dsp_value_source.h:276-298`); `as_signal` (`patch_loader.cpp:148-156`) casts-or-throws, `resolve_signal` (`:158-165`) names the site and delegates; `adopt_nodes` (`:1328-1336`) moves the map and derives the list — one job, the only writer of `signals`; the hook (`component.h:91`) is a notification. `Slot<T>::set` (`component.h:173-178`) is cast + clear + `add`, and `add` casts again — two jobs' worth of cast for one wire (purposeful: a refused `set` must keep the slot, tests `main.cpp:3466-3467`; writable once). Flag: the wavetable's `evolution_` invariant has two maintainers, the hook (`wavetable_source.h:39`) and `on_prepare` (`wavetable_source.cpp:16`) — the second is belt-and-braces, not a second job, but it is where the next reader will wonder which one is load-bearing. |
| 2 | Single responsibility (names promising one thing, doing two) | PASS with a flag | `add_param` on a single slot *replaces* (`pins.h:169`, `component.h:167`) — "add" that does "set". `get_part` (`component.h:43-47`) promises "the part" and returns a list's first on a multi slot. `component.h:48` says "every other pin replaces" for `add_param`; `MultiSource::set_param("source", …)` *appends* (`multi_source.h:36-42`), so the sentence is false for one pin kind (pre-existing behaviour, new comment). `Slot<T>::set` on a multi slot replaces the whole list — consistent with the test's "set replaces, add appends" (`main.cpp:3492-3493`), and the right semantics for the loader's single-object case. |
| 3 | Dependency direction | PASS with a flag | One include added: `wave_evolution.h:3` → `core/dsp_wave_source.h` (source → core, allowed by `modules.json`). No `core` file includes `source`. `component.h:36` declares a virtual whose body lives in `dsp_value_source.h:276` (the comment says why); the root header is therefore not usable alone — a TU that includes only `component.h` and odr-uses `Component` gets an unresolved `set_param`. Today only `dsp_value_source.h:10` and `engine_tests/main.cpp:1947` include it directly (grep), and the tests TU also includes `dsp_value_source.h`, so it links; the hazard is latent. Tests reach `vg.signals`, `vg.nodesById`, `seq.spectra.views`, `spec.formants.views` — all public members, no friend access; see item 5. |
| 4 | Duplication | FAIL (Minor) | (a) the double `dynamic_cast` in `Slot<T>::set`/`add` (`component.h:165, 175`); (b) three lifecycle trios with no shared base and two conventions: `IFormant` (`formant.h:17-24`) and `IPartials` (`partials.h:79-88`) pure, `IEvolutionHolder` (`wave_evolution.h:640-645`) default-empty — so `FixedSpectrum` carries three empty overrides (`formant.h:136-138`) while eight holders carry none; (c) the UI keeps the part-type knowledge in three hand lists beside the registry and the tests' `kPartTypes`: `pin_type_compat_error`'s `is_iformant`/`is_ipartials` (`mforce_ui/main.cpp:889-920`), `is_partials_type` (`:3462`, lacks `CompositePartials`), `is_formant_type` (`:3465`, lacks `FixedSpectrum`) — four copies of "which types are parts", mutually inconsistent (pre-existing, untouched by the diff, but the diff's `kPartTypes` is the fourth); (d) the UI's `signal()` is a `dynamic_cast` and every caller evaluates it twice (`:4176-4177`, `:4188-4189`, `:4921`, `:4938`); (e) `BandSpectrum`'s configurator sets `gains` that the generic array pass (`patch_loader.cpp:384`) already set (`source_registrations.cpp:98-104`, signature touched by the diff). |
| 5 | Encapsulation | PASS with a flag | `Slot<T>::parts` and `views` are public parallel vectors (`pins.h:178-179`); a push to one without the other leaves `views` dangling. Nothing outside the owners and the tests touches `.views` (grep: `formant.h`, `partials.h`, tests only) and nothing touches `.parts`; the hazard is permitted, not exercised. `VoiceGraph::signals` holds raw `ValueSource*` (`instrument.h:186-187`) into `nodesById`'s pointees; valid because `nodesById` is a member of the same struct, `adopt_nodes` is the sole writer of both (`patch_loader.cpp:1328-1336`, grep: no other assignment anywhere), and moving the map does not move the pointees. `GraphNode::signal()` hands out a raw pointer from a `shared_ptr` exactly as the old `dspSource.get()` did. |
| 6 | Ownership | PASS with a flag (traced) | The slot owns the part (`parts`); the owner reads it through `views`. `WavetableSource::evolution_` (`wavetable_source.h:57`) is refreshed by the hook (`:39` → `:48-51`), by `on_prepare` (`wavetable_source.cpp:16`) and by `set_evolution` (`:32`). Can the part die while the pointer is read? In the UI every rewire runs under `g_audioMutex` (`update_all_dsp` `:1041-1049`, `update_node_dsp` `:1000-1002`) and the audio callback holds that mutex for its whole body (`:3855`), so the window between `parts.clear()` and `refresh_evolution()` is never observed; the holder is additionally kept alive by its own `GraphNode::dspSource`, node deletion is UI-thread and destruction is deferred (`voice_gc` `:4160-4165`). In the engine the voice owns every node (`nodesById`) for the voice's life. Not pinned: a holder wired *during* a live stream gets `evolution_` pointing at an evolution whose `WaveEvolution::prepare(ctx, maxLen)` has never run — same as the old override (`98bf883` `wavetable_source.h` set `evolution_` at wire time too), not a regression. `CompositePartials::partials.views` vs `parts`: written only by `Slot::add`/`clear`, read only through `views` (`partials.h:1104-1146`). |
| 7 | Abstraction fit | PASS with a flag | Part 3 §2: "The registry creates Components" (`source_registry.h:35-37`, `.cpp:17`), "The graph holds Components" (`GraphResult::nodes` `patch_loader.cpp:231`, `VoiceGraph::nodesById` `instrument.h:183`, `GraphNode::dspSource` `main.cpp:263`), "A wire into a Slot is type-checked against the interface" (`component.h:165`), "The parts lose their fake next, current, prepare and start" (every `compute()`/`on_prepare`/`on_start` forwarder deleted; the audit `main.cpp:6566-6582` pins that no part is a `ValueSource`), "ValueSource keeps exactly the signal contract" (nothing added to `ValueSource`). Part 2 §3: "type-checked when wired" — yes; "fails with the reason ('partials expects a Partials node')" — half: `reject_wire` (`patch_loader.cpp:239-246`) names node, pin and the offered type, never what the pin expects; `Slot::hint` (`pins.h:180`) exists and every slot passes `nullptr`. `on_slot_changed`: the right hook — it is the third of a pattern already there (`component.h:89-91`) and the alternative, no cached pointer and `evolution.get()->get_evolution()` per sample, is a virtual call per sample; the plan's "resolved in on_prepare" alone was shown unsafe by a test (ledger Task 4). `IEvolutionHolder` default bodies: honest for the eight pinless holders; not honest as an interface convention next to its two pure siblings, and silently tolerant of a future holder that forgets an override (its envelope never learns the rate, no compile error). `Slot<Partials>` (concrete) on `AdditiveSource2`: defensible — `apply_partials` (`additive_source2.h:73-85`) reads `get_mult1()` and friends, the `Partials` API, not `IPartials`; 3.7 deletes the type. `get_part` returning a multi slot's first: callers are tests only (grep); as a public API it is ambiguous. |
| 8 | Real-time rule | PASS (traced) | `instrument.h` contains no `dynamic_cast` (grep: 0). `deliver_continuation` reseeds `vg.signals` (`:544`); `collect_faults` (`:240-247`) and `capture_begin` (`:299-300`) read the same list, control side. `FullAdditiveSource::wave()` calls `Slot::get()` twice per sample (`full_additive_source.cpp:48, 56`) = `views.empty() ? nullptr : views.front()` (`pins.h:188`), no cast, no allocation, no lock — one load more than the old `shared_ptr::get()`, no virtual call added. `WavetableSource` reads `evolution_` as before (`wavetable_source.cpp:59, 80, 114, 138`). `FormantSpectrum::next` and `CompositePartials::next` iterate raw `T*` (`formant.h:107`, `partials.h:1112`) where they iterated `shared_ptr` before. The hook runs only from the door (load, UI edit under the mutex). `Slot::add/set` allocate (`push_back`) — load/edit time. `apply_partials` allocates at wire time (pre-existing). The UI's `signal()` `dynamic_cast` runs on the UI thread only (`find_output_source` `:4171`, `prepare_graph` `:4202`, the capture list `:4380`, `play_continuous` `:4886-4938`, `transport_generate` `:8348/8370` per click). |
| 9 | Names and comments | FAIL (Minor) | Stale at HEAD: `wave_evolution.h:634` "WavetableSource dynamic_casts its "evolution" input to this." — false twice (the door casts; the pin is a slot), left standing directly above the new paragraph; `wave_evolution.h:649, 673` "evolution input"; `wavetable_source.h:28` "to the "evolution" input"; `multiplex_source.h:29, 61` "valueNodes map", member `instanceValueNodes_` (`:181`) holds `Component`s; `engine_tests/main.cpp:2971-2972` "The evolution input holds the node and keeps a typed view"; `:3137-3138` "and the holder's own next() advances nothing" (a holder has no `next`); `:4853-4857` `kSilentUntilWired` lists `PluckEvolution`, `AveragingEvolution` "parts read by WavetableSource" — dead entries, parts are skipped at `:4843` before this set is consulted; `mforce_ui/main.cpp:12263` "fmt_prepare/fmt_next must have been called". Misleading: `component.h:48` "every other pin replaces" (see item 2). No checkpoint, backlog or REVIEW number in any added comment (grep over the diff's `+` lines: none; the two hits are JSON test strings). |
| 10 | Tests | PASS with a flag | They bite where they exist — mutation traced: drop the hook call from `set_param`'s slot branch → `main.cpp:3478` (`a2.partialCount == 12`) fails; drop it from `clear_param` → `:2990` (`breath->nexts == 10` after clear) fails, the ledger's own RED; drop `Slot<T>::set`'s pre-check → `:3466-3467` ("refused, the slot keeps its part") fail; drop `", a part, not a signal"` → `:6493` fails; move any registered type across the line → the audit `:6566-6582` fails by name. `kPartTypes` (`:4824-4830`) is exactly the registry's 22 part names (I listed all 85 `register_type` names; the plan's "twenty-four" double-counted `CompositePartials` and its own arithmetic was 23). Untested, each confirmed by grep or trace: (a) **no `deliver_continuation`/legato test exists at all** (grep: 0) — Review Focus 5's audio-thread walk is pinned only by the list's contents (`:6499`); (b) **nested prepare**: delete the loop in `FormantSpectrum::prepare` (`formant.h:98-100`) or `FormantSequence::prepare` (`:214-216`) and every test passes — the split tests wire `ConstantSource`s only (`make_split_formant` `:5189-5194`), whose `current()` needs no prepare; (c) `on_slot_changed` on `add_param`/`clear_param` of a multi slot — no owner with a multi slot overrides the hook, so removing the call from `add_param` (`component.h:53`) fails nothing; (d) `add_param` on a *single* slot (`component.h:167`'s branch) — untested; (e) a part as `graph.output` or a channel's `source` — untested, and the plan's claim about the message is wrong: the output path says "graph.output must be a StereoMixer or a mono-renderable source" (`patch_loader.cpp:1646`), the channel path says "No mono source for: <id>" (`:1689`); the instrument path says "'<id>' is a <type>, a part, not a signal" (`:148-156`); (f) a signal into `evolution` at the *loader* level — door-level only (`:3000`); the one loader-level slot refusal is `partials` (`:6072`, pre-existing); (g) the UI paths — nothing; (h) Review Focus 1's example (`{"ref":"parts"}` into `frequency`) was replaced by a `Formant` into `frequency` (`:6485-6493`), same class. |

---

### Review Focus, item by item

| # | Focus | Pinned where | Traced | Not pinned |
|---|---|---|---|---|
| 1 | A part where a signal is expected | Door: `main.cpp:6472` (`Plug` into `frequency` refused). Loader: `:6485-6493` — a `Formant` ref into `AdditiveSource.frequency` fails naming `add`, `frequency`, "not a signal" (`reject_wire` `patch_loader.cpp:239-246`). | Yes: `wire_params_generic` `:330-344` → `set_param` false → `reject_wire`; `isSignal` cast at `:243` appends the kind. `vg.source = as_signal(...)` `:~1545` and `:~1760` throws for a part as output; `add_mono` `:469-477` skips a part so the mixer path falls to `:1646`/`:1689`. | A part as `graph.output` (instrument or mixer mode) and as a channel's `source` — no test; the plan names the wrong message for the channel case. A `FullPartials` (not a `Formant`) into a signal pin — same path, not the plan's literal example. |
| 2 | A signal into a slot | Door: `:6473` (`SineSource` into `partials`), `:6474` (`Plug` into `partials`), `:3000` (`ConstantSource` into `evolution`), `:6522` (`SineSource` into `formants`), `:6555` (into `CompositePartials.partials`), `:3494` (into `spectra`). Loader: `:6064-6072` (pre-existing) — `SineSource` into `partials` fails "does not accept a SineSource". Scan: 0 such wires in scope and 0 in the whole tree. | Yes: `Slot<T>::set`/`add` cast fails → false → `reject_wire`. | The loader message for `evolution`, `formant`, `formants`, `spectra` — door-level only. The message never says what the slot expects (I4). |
| 3 | A part edited in the UI, then Play | Engine analogue: `:6531-6541` — `fm.gain` set on a loaded instrument, `prepare`, render differs. The 2.6 Partials shaping test stands (`run_structural_setting_tests`). UI `prepare_graph` prepares signals only (`main.cpp:4199-4208`); the owner prepares its parts (`full_additive_source.cpp:12-13`, `wavetable_source.cpp:15`). | Yes for the engine path. UI path: traced by reading, not by a test — `prepare_graph` → `s->prepare` on each signal → `FullAdditiveSource::on_prepare` → `partials.get()->prepare(ctx)`. | The UI path itself (no harness). And the UI's *unlink* path, which this focus implies and which regressed — I1. |
| 4 | A multi slot cleared and refilled | `:6512-6518` (`add`, `add`, `clear`, `add`, then `contains` answers for exactly the refilled set); `:6523-6525` (`set` replaces on a multi slot). Loader: `wire_params_generic` `:330-337` does `clear_param` then `add_param` per element; the null gate's 57 `FormantSpectrum` wires exercise it (ledger Task 2). UI: `rebuild_formant_spectrum` `main.cpp:527-539` does the same through the door. | Yes. | `on_slot_changed` firing on `add`/`clear` of a multi slot (no owner observes it today). |
| 5 | A live legato note-on on a voice with parts | `:6495-6500` — a loaded additive patch's `signals` holds `add` only and `nodesById` holds both. The walk: `instrument.h:544`, no cast, raw pointers from a list built once (`patch_loader.cpp:1328-1336`). | Yes, by reading: the loop is `for (auto& s : vg.signals) s.node->reseed();`. | No test calls `deliver_continuation` at all (grep: 0). No test of `signals` for a Multiplex voice (the Multiplex itself is a signal and lands in the list; its clones are inside it, as before — no regression, no pin). No test that `collect_faults` or `capture_begin` still find a signal through the new list. |

---

### Rulings weighed

Plan rulings 1-14, then the execution rulings by task.

| Ruling | Verdict | Evidence |
|---|---|---|
| P1 One wiring door, both casts inside it | **Stands** | `dsp_value_source.h:276-298` routes by `find<Param>`/`find<Input>`/`find<SlotBase>`; the signal cast at `:279, 286`, the interface cast in `Slot<T>::add` `component.h:165`. Every caller — loader `:330-344`, UI `wire_pin` `main.cpp:482-485`, tests — wires without knowing the pin kind. The cast runs at load or at a UI edit (under `g_audioMutex`); no render path reaches the door. The one cost the ruling did not name: the UI discards the door's answer (`wire_pin` ignores the bool) — the door refuses, the editor does not say so (I1, I5). |
| P2 `get_param` serves signals, `get_part` slots | **Stands, overstated** | `component.h:38-47`. Correct typing. "For three test sites" — at HEAD `get_part` has *only* test callers (grep); the loader's `build_bindings` `patch_loader.cpp:~1106` calls `get_param` on a part's pin and gets the Param's signal, as intended. |
| P3 Prefixes dropped; `IEvolutionHolder` gains `prepare/start` | **Stands** | `formant.h:19-21`, `partials.h:81-83`, `wave_evolution.h:642-643`. The regex rename reached every mention (grep for the old verbs: only `imgui` and one UI comment, I6). The holder's `prepare(ctx)` beside `WaveEvolution::prepare(ctx, maxLen)` is the two-prepare debt the ruling names for 3.4. |
| P4 Pull all the way down; the UI stops preparing parts | **Stands** | `instrument.h:210-215` prepares `source` and `advanceList` only; owners prepare parts (`full_additive_source.cpp:12-13`, `wavetable_source.cpp:15`, `formant.h:98-100`, `:214-216`, `partials.h:1104`); `prepare_graph` `main.cpp:4199-4208` skips parts. Null gate 78/78 is the proof the double prepare was idempotent. |
| P5 `signals` as a second structure beside `nodesById` | **Stands, with a flag** | `instrument.h:183-187`; one writer (`adopt_nodes`), three readers, no cast on the audio thread (`:544`). Alternative weighed correctly. Flag: raw public pointers whose validity is an unstated invariant on `nodesById`; and the sort at `patch_loader.cpp:1335` is unnecessary work (`collect_faults` sorts its output `:248-249`; reseed and capture are order-free) — harmless, deterministic. |
| P6 `resolve_param` → Component; `resolve_signal` throws | **Stands** | `patch_loader.cpp:117-165`; constructor sites, taps (`:~510`), the Multiplex root (`:~1030`), the voice output, the advance list, the dynamic-pin chain (`:~1313`) all go through `as_signal`. Message "'<id>' is a <type>, a part, not a signal" — `where` is `"param"` for a number/tap (`:161-162`), which can never be a part; fine. |
| P7 Two malformed wires now fail loudly, by design | **Stands in scope; overstated as a tree-wide statement** | Scan: 0 of either shape in the 132; 0 signal-into-slot anywhere; 0 part-into-signal-pin anywhere *except* the 101 `FormantSequence.formants` refs under `patches/old/` that ruling 9 narrows — and those fail nothing (I2). |
| P8 Holders stay wrappers | **Stands** | `wave_evolution.h:640-1290`; twelve `Component, IEvolutionHolder`; settings as today. |
| P9 Formant branches collapse; the `FormantSequence` `"formants"` alias dropped | **Wrong in one clause (Major, I2)** | The collapse is right: the generic path builds all three (`patch_loader.cpp:~470-495`), `FixedSpectrum`'s missing-gains error survives via a configurator (`source_registrations.cpp:483-487`), an unresolved or wrong-kind formant ref is loud (`resolve_param` throws / `reject_wire`). The alias: the ruling predicted "a FormantSequence carrying it would load without its spectra" and accepted that — but `kBranchKeys` still lists `"formants"` (`:271`) and `"spectra"` (`:284`), both now descriptors, so the allowlist entry exists only to let that silent load happen. My scan: **34 `FormantSequence` nodes carry `"formants"` and no `"spectra"`** (all under `patches/old/vowels/` and `patches/old/baselines_culled_2026-09-14/`; `patches/old/` is gitignored at `.gitignore:104`, 67 files tracked from before). "No patch in scope" is true; "the one key whose meaning narrows" understates a silent wrong render on every archived vowel patch, against the loader's own stated rule (`:258-262`: "must not render silently downgraded") and 2.5's. |
| P10 `AdditiveSource2` gets `Slot<Partials>`; copy at prepare | **Stands on the slot; superseded on the timing (see T3/T4)** | `additive_source2.h:34`. The copy moved to the hook, not prepare — the execution rulings below. |
| P11 `ExpandRule` stays a loader-cast part | **Stands** | `patch_loader.cpp:~440-446` casts on the `Component` map. The pre-existing silence (an unresolved `expandRule.ref` or a non-`ExpandRule` target is ignored, no throw) is Part 4's by the ruling. |
| P12 A/B set = library + baselines, 132 | **Stands** | Set file and scope as stated; I did not re-run the A/B (read-only) and take the ledger's 0 DIFFER as reported, not verified. |
| P13 Allocation gate joins `--fast` through a wrapper | **Stands on placement; the wrapper has a defect (I3)** | `gates.py:27-28`; the ledger's 46-50 s is under the minute. The wrapper's exit-code merge (`alloc_gate.py:86-90`) treats a crashed child as a pass. |
| P14 Task order | **Stands** | Six commits in the stated order; every task landed with the null gate per the ledger. |
| T0 Gate → FAST under the minute; Bowed's floor; Segment's reserve | **Stands** | `WaveSource::kMinTableHz == 20.0f` (`dsp_wave_source.h:24`), byte-identical by construction. `SegmentSource::on_prepare` reserves `(values.values.size()/2 + 1) * 2` (`segment_source.h:69-70`); the removed `on_start` reserve was `(pairCount + 1) * 2` over the same vector with `pairCount = shape.size()/2`, `shape = values.values` (`:124-137`) — the same bound, the same vector; the ledger's reading is right. |
| T1a Task 1's refusal assertions cannot hold while parts are still `ValueSource`s; a test-local `Plug` stands in | **Stands** | `main.cpp:6464-6467`; the family tasks made each line true (`:6474`, `:6522`, `:6555`). |
| T1b `get_part("evolution")` waits for Task 4 | **Stands** | `:2981` at HEAD. |
| T1c `ValueNodeMap` → `NodeMap` | **Stands, incomplete** | `multiplex_source.h:32`; the comments at `:29, :61` and the member `instanceValueNodes_` `:181` still say "valueNodes" (I6). |
| T1 finding: six roundtrip diffs are the backlog-79 six | **Not verified** | I ran nothing; the claim is consistent with the diff (the UI save path is untouched). |
| T2a Set replaces, add appends on a multi slot | **Stands** | `component.h:173-178` / `:164-171`; tests `:3488-3497`, `:6523-6525`. Both the loader (`clear_param` first) and the UI (first-pass clear) are indifferent, as the ruling says. |
| T2b `FixedSpectrum` without gains stays a load error, through a configurator | **Stands** | `source_registrations.cpp:483-487`; the generic path wraps it with the node id (`patch_loader.cpp:~486-489`). Scan: 0 `FixedSpectrum` nodes anywhere in `patches/`, so the branch's only user is the test suite. |
| T2c No UI face pin for `FormantSpectrum.formants` | **Stands, as a hack** | `main.cpp:626` — a type-name string compare in `build_pins`, the UI's inline table being the pin. Correct for the roundtrip; one more hand-written exception in the UI (3.2's "families declared at registration" is where it belongs). |
| T2d The sweep's oracle is the named set, grown per task | **Stands** | `kPartTypes` `:4824-4830`, two users (`:4864`, `:6566-6582`). |
| T2e A null part clears a slot | **Stands** | `dsp_value_source.h:292`, `component.h:51`; test `:3468-3469`. Consistent with a null into a Param restoring the constant. |
| T3a `AdditiveSource2` copies at wire time (against P10) | **Stands, for the stated reason only** | The reason given is byte-identity for the one baseline, and that holds (null gate). The type is broken either way: `apply_partials` (`additive_source2.h:73-85`) reads `mult1_` raw via `partial_count()` (`partials.h:416-418`) and `get_mult1()` without forcing `update_arrays()`, so a freshly configured `Partials` is copied before its arrays exist unless something called `get_array` first — the test works around it at `:3475`. Pre-existing; 3.7 deletes the type; declined below. |
| T3b `ExpandRuleNode` test drives `to_struct()` | **Stands** | `:3327`, `:3434-3437`. |
| T3c Verbs renamed by regex; no free functions existed | **Stands** | grep confirms. |
| T4a `on_slot_changed` added mid-task after a stale-pointer failure | **Stands — the right fix, with a flag** | The failure (`:2990`) is a real one the plan's design would have shipped: a part cleared after prepare, read through a dangling pointer until the next prepare. The hook closes it at the door, under the UI's mutex. Flag: the `component.h:84-88` comment claims a part "wired or cleared after prepare is never read through a stale handle"; a part wired after prepare is read through a *fresh* handle to an *unprepared* evolution (pre-existing; I6 wording). The mid-task scope change was recorded, costed, and is one virtual call at wire time — proportionate. |
| T4b `IEvolutionHolder::prepare/start` default bodies | **Stands, flagged** | Saves eight empty overrides; inconsistent with the two pure siblings; tolerant of a forgotten override (I7). 3.4 deletes the interface, so the cost of either choice is small. |
| Audit's 22 against the plan's 24 | **Executor right** | Registry: 5 partials-family names (`FullPartials`, `SequencePartials`, `ExplicitPartials`, `CompositePartials`, `ExpandRule`), 5 formant names, 12 `*Evolution` names = 22; `Partials` (the base) is not registered. The plan's goal line counts `CompositePartials` twice ("the four Partials types … and CompositePartials") and its own sum is 23, not 24. |

---

### Issues

#### I1 — Major — UI: unlinking a holder from `WavetableSource.evolution` no longer removes the evolution (regression)

**Effect.** In the editor, delete the wire from a `PluckEvolution` (or any holder) to a `WavetableSource`, press Play: the table still evolves — the pluck is still there. The link is gone from `s_links` (so Save writes a patch without it) while the engine object still holds the holder; editor and sound disagree until the node is recreated or the patch reloaded.

**Trace.** `IsLinkDestroyed` → `update_all_dsp()` (`mforce_ui/main.cpp:14388-14402`) → `update_node_dsp_unlocked` first pass (`:953-960`): for a non-multi input pin, `wire_pin(pin.name, pin.constantSrc)`; `Pin`'s constructor always makes a `constantSrc` (`:112`), inputOnly pins included. For `evolution` this is `set_param("evolution", ConstantSource)` → the door's slot branch (`dsp_value_source.h:291-296`) → `Slot<IEvolutionHolder>::set` → `dynamic_cast<IEvolutionHolder*>(ConstantSource*)` is null → `false`, the slot untouched (`component.h:174-178`) → `wire_pin` discards the bool (`:482-485`). The second pass re-adds only pins that still have links. Net: the holder stays.

**At `98bf883`.** `evolution` was an `Input` (`wavetable_source.h:33` then); `WaveSource::set_param` accepted the constant into the Input and the override set `evolution_ = holder ? … : nullptr` — null for a constant. Unlink removed the evolution. The checkpoint changed the pin's kind and, with it, the UI's unlink semantics for this pin; neither the plan nor the ledger looked at the UI's disconnect path ("the UI changed only as far as the roundtrip gate needs" — the roundtrip never unlinks).

**Pre-existing twins.** `AdditiveSource.formant`/`.partials` and `AdditiveSource2.partials` had the same defect before (their old `set_param` overrides also refused a constant, so unlink never cleared them). Not a regression there, same fix.

**Smallest fix.** In the first pass, clear every input pin before wiring its constant back: `node.dspSource->clear_param(pin.name);` ahead of the `wire_pin` (two lines at `:956-958`). For a Param/Input, `clear_param` restores the pin's own constant and `wire_pin` then installs the UI's; for a slot, `clear_param` empties it and the constant is refused. Alternative: `wire_pin` falls back to `clear_param` when `set_param` returns false for a `ConstantSource`.

**Test that would have caught it.** None in the suite can (no UI harness). The engine contract is right and already pinned (`:3000`, `:3466-3467`). This belongs in REVIEW as a UI item with a manual check: wire a holder, play, unlink, play.

#### I2 — Major — Loader: the `FormantSequence` `"formants"` alias is dropped silently; 34 archived patches now render without their spectra

**Effect.** Open any of `patches/old/vowels/*.json` (32) or `patches/old/baselines_culled_2026-09-14/fadd_vowel_morph_test.json` (plus one more in the scan's 34): the `FormantSequence` loads with an empty `spectra`, `contains()` is false and `get_gain()` 0 (`formant.h:235-250`), the vowel morph is gone, and nothing is printed. Example: `patches/old/vowels/EEOO_w250.json` — node `fseq` (`FormantSequence`) has keys `['blend', 'formants']`, no `spectra`.

**Trace.** The deleted branch read `"formants"` for a `FormantSequence` (`98bf883` `patch_loader.cpp` around the old `:755-768`). At HEAD the generic path wires `spectra` only (`FormantSequence::spectra` `formant.h:208`); `reject_unknown_keys` (`patch_loader.cpp:258-305`) would have refused `"formants"` — except `kBranchKeys` still lists it (`:271`), kept "for `FormantSpectrum`", whose `formants` is now a descriptor (`formant.h:93`) and needs no allowlist entry. `"spectra"` (`:284`) is likewise now a descriptor. Both entries are dead for their stated purpose and alive only to hide this case — the loader's own comment at `:260-262` admits an allowlist entry "can only hide a rejection".

**Scope.** Out of the measurement scope by Matt's rule (`patches/old/` is gitignored, `.gitignore:104`), so the plan was not obliged to repair them. It was obliged, by its own standard (P7, 2.5's "a rejected wire says so", the loader comment) to refuse loudly rather than downgrade silently. Matt's "no patch back-compat" preference argues for a loud refusal, not an alias.

**Smallest fix.** A `FormantSequence` configurator (three lines, the `FixedSpectrum` pattern at `source_registrations.cpp:483-487`): `if (p.contains("formants")) throw std::runtime_error("FormantSequence takes 'spectra', not 'formants'");` — and remove `"formants"` and `"spectra"` from `kBranchKeys` (`:271, :284`), since both are descriptors now (check the lint's scrape still agrees). **Test:** `load_fails_naming(seqWithFormants, "spectra")` in `run_formant_part_tests`.

#### I3 — Major — `tools/alloc_gate.py`: a crashed child is a PASS

**Effect.** The allocation gate now runs in `--fast` before every commit (`gates.py:27-28`). If one of the three `alloc_gate.exe` slices crashes (an access violation, an uncaught exception — exit codes like `0xC0000005`/`0xC0000409`), the wrapper prints a summary over the surviving slices and exits 0; `gates.py` sees PASS. The patches in the crashed slice were never checked.

**Trace.** `alloc_gate.py:76` collects every child's code; `:86-90` returns 1 if any child returned 1, else 2 if any returned 2, else 0 — any other code falls through to 0. The merged `total` (`:80`) is never compared to `len(patches)`; a missing summary line is silent.

**Smallest fix.** After the loop: `if any(c not in (0, 1, 2) for c in exits): return 3` and `if total != len(patches): print(..., file=sys.stderr); return 3` (four lines). **Test that would have caught it:** none exists for the wrapper; a one-line check in Task 0 step 2 that `total == 132` would have been the start. A unit test with a fake `EXE` that exits 139 is cheap.

#### I4 — Minor — A refused slot wire never says what the slot expects

Part 2 §3 promised "partials expects a Partials node". `reject_wire` (`patch_loader.cpp:239-246`) says "'evolution' does not accept a Constant" — node, pin, offered type; not the expected interface. `Slot::hint` (`pins.h:180`) is `nullptr` on all nine slots (`formant.h:93, 208`, `partials.h:1097`, `full_additive_source.h:29-30`, `additive_source2.h:34`, `wavetable_source.h:23`). **Fix:** give each slot a hint ("an IFormant", "a Partials", "an evolution holder"), and have `reject_wire` append it when the pin is a slot (it has the `Component&`; a `const SlotBase* slot_named(name)` on `Component`, or reuse `input_descriptors()`'s hint). **Test:** extend `:6072`'s assertion with the expectation text.

#### I5 — Minor — The UI's part-type knowledge is three hand lists, inconsistent, and `evolution` is not among them

`pin_type_compat_error` (`main.cpp:889-920`) validates `formant`/`formants`/`spectra`/`partials` links by type-name lists and knows nothing of `WavetableSource.evolution`: a `SineSource` can be linked into `evolution`, the door refuses, `wire_pin` discards the answer, the link is drawn and inert — as silent as before (the Input accepted it and `evolution_` was null), so not a regression, but the plan's "a rejected wire says so" is loader-only. `is_partials_type` (`:3462`) omits `CompositePartials`; `is_formant_type` (`:3465`) omits `FixedSpectrum`. **Fix (one wave):** `pin_type_compat_error` asks the door — create a scratch target through the registry, `set_param(pin, scratchSource)`, read the bool — and delete the hand lists; the strips' two predicates can read `SourceCategory`/the registry once 3.2 declares families. At minimum, `wire_pin` returns the bool and the link handler undoes a refused link with a status message.

#### I6 — Minor — Stale and misleading comments

`wave_evolution.h:634` (false: "WavetableSource dynamic_casts its "evolution" input"), `:649`, `:673`; `wavetable_source.h:28`; `multiplex_source.h:29, 61` and member `instanceValueNodes_` `:181`; `engine_tests/main.cpp:2971-2972`, `:3137-3138`, `:4855` (dead `kSilentUntilWired` entries); `mforce_ui/main.cpp:12263`; `component.h:48` ("every other pin replaces" — `MultiSource::source` appends); `component.h:84-88` ("never read through a stale handle" — a fresh handle to an unprepared evolution is still possible during a stream, pre-existing). All one-line edits.

#### I7 — Minor — Three interfaces, two conventions

`IFormant` and `IPartials` are pure; `IEvolutionHolder::prepare/start` have empty bodies (`wave_evolution.h:642-643`). `FixedSpectrum` carries three empty overrides (`formant.h:136-138`) to satisfy the first convention while eight holders carry none under the second. A future holder with pins that forgets `prepare` compiles and never prepares them. **Fix:** pick one — pure everywhere (eight one-line overrides) or a shared `IPart { prepare; start; }` base the three derive from, which also removes duplication (b). 3.4 deletes `IEvolutionHolder`, so this can wait for that design.

#### I8 — Minor — `Slot<T>::set` casts twice; `add_param` on a single slot replaces

`component.h:164-178`: `set` casts, then `add` casts again. Write `set` as cast → clear → push (no second cast), or have `add` take the already-cast `T*`. `add` on a single slot replacing (`:167`) is documented (`pins.h:169`) but "add" is the wrong verb for it; the door's `add_param` could refuse a single slot (false) or the name could be `wire_param`. Not a defect; a name.

#### I9 — Minor — `component.h` is not self-sufficient

`set_param` declared at `component.h:36`, defined in `dsp_value_source.h:276`. The root header depends on a sibling for its own vtable. Pre-existing coupling (`get_param` already returned `shared_ptr<ValueSource>`), now with an out-of-line body. **Fix:** none cheap today; a `component.cpp` in `core` (the registry already has `source_registry.cpp` there) would hold it honestly — or accept and document that `dsp_value_source.h` is the include.

#### I10 — Minor — Sweep hygiene

`run_split_equivalence_tests` creates every type twice (`:4843`, `:4844`); the dead `kSilentUntilWired` entries (`:4855`); `run_component_root_tests`'s `seen > 22` (`:6581`) is weak — `seen == reg.registered_types().size()` says what it means.

#### I11 — Minor (flag) — The plan misnames the channel-source failure

Plan Review Focus 1 and ruling 6: "a part as output or channel source fails with the existing 'must be a mono-renderable source' message". The output path says that (`patch_loader.cpp:1646`); the channel path says "No mono source for: <id>" (`:1689`); the instrument path says "'<id>' is a <type>, a part, not a signal" (`:148-156`). None of the three is asserted with a part. One test with three patches would pin them.

#### I12 — Minor — `get_part` as public API

`component.h:43-47`: test-only callers; on a multi slot returns the first. Either `first_part(name)` or `nullptr` for a multi slot plus a `parts(name)` span. Cosmetic until a non-test caller appears.

---

### What I would fix in one wave

1. **I1** — the UI's first-pass clear (`main.cpp:956-958`): `clear_param` every input pin before wiring its constant back; manual check wire/play/unlink/play on a wavetable and an additive; REVIEW item.
2. **I2** — `FormantSequence` configurator throwing on `"formants"`; drop `"formants"`/`"spectra"` from `kBranchKeys`; test `load_fails_naming(…, "spectra")`.
3. **I3** — `alloc_gate.py`: unknown child exit → 3; `total != len(patches)` → 3; a fake-exe unit test.
4. **I4 + I5 together** — slot hints, `reject_wire` naming the expectation, `wire_pin` returning the bool and the link handler reporting a refusal (the UI asking the door is the real fix; the hint lands first).
5. **I11** — one test, three patches: a part as instrument output, as mixer output, as a channel source.
6. **Nested prepare and `deliver_continuation`** (item 10 b, a) — a `PinCounter` on a `Formant` inside a `FormantSpectrum` inside a `FormantSequence` on a `FullAdditiveSource`: `rate == 48000`, `starts == 1` after the owner's prepare/start; one legato test on the `additive` patch asserting the reseed reached `add` (a seeded node whose first draw after `deliver_continuation` equals its first draw after a fresh `start`).
7. **I6, I7, I8, I10** — comments, the convention, the double cast, the sweep — cosmetic, one commit.

### Declined to judge

- **`AdditiveSource2`'s copy semantics beyond byte-identity** (T3a): the type copies dirty arrays at wire time whether the copy happens at the hook or at prepare, and the loader's `set_default_partials` after `wire_params_generic` (`patch_loader.cpp:~545-548`) overrides the copy for `partialMode == "default"` anyway. Pre-existing; 3.7 deletes the type and migrates its one baseline. Not this checkpoint's.
- **A holder wired during a live stream points `evolution_` at an unprepared evolution** (item 6): identical at `98bf883`; a stream-rewire design question for the UI, not a regression.
- **The holders as wrappers, the two prepares, the string-typed `set_evolution`**: 3.4's, by the spec's table and ruling 8.
- **`expandRule`'s silent ignore of an unresolved or wrong-kind ref** (`patch_loader.cpp:~440-446`): Part 4's by ruling 11; pre-existing.
- **The A/B (0 DIFFER), the timing columns, the null gate counts, the roundtrip's six known diffs, the allocation gate's wall time**: reported in the ledger, not in the package I was given, and I ran nothing; I take them as the executor's claims. R8's coordinator should reproduce the A/B compare line at least.
- **Whether the 34 archived vowel patches load today for other reasons**: I cannot run them; what I can say is that the loader accepts `"formants"` on a `FormantSequence` by construction (`kBranchKeys:271`) and wires nothing for it.
- **`MultiSource::set_param` appending**: pre-existing semantics the diff retyped without changing; the comment it contradicts is new (I6).
- **The structure meter's numbers** (`check.py --delta`, function/file limits): not run; the two new functions I measured by eye (`run_wiring_door_tests` 32 lines, `run_formant_part_tests` 37, `adopt_nodes` 9) are well under 80.

### What was done about each

The wave, one pass, each fix with its test written first (commit `b1ae7a4`):

- **I1 (Major) — fixed.** The editor's rewire pass clears every input pin before wiring its constant back (`tools/mforce_ui/main.cpp`, `update_node_dsp_unlocked`'s first pass), so unlinking a holder from a wavetable, or a formant or partials from an additive source, empties the slot — the constant the pass wires next is refused by a slot, as the door says it should be. No engine test can reach the editor; the door's semantics stay pinned (a null clears, a signal is refused); REVIEW R8 carries the manual try: wire a holder, play, unlink, play. The refuter's "pre-existing twins" (the additive slots never cleared on unlink before either) are closed by the same line.
- **I2 (Major) — fixed.** A `FormantSequence` configurator refuses the `"formants"` key by name ("takes its spectra under 'spectra', not 'formants'"); `"spectra"` left the loader's allowlist, being a descriptor now (`"formants"` stays: the configurator reads it, and the lint's scrape test requires every read key). Test: a sequence keyed `"formants"` fails the load naming `spectra` — RED, then GREEN. The 34 archived vowel patches now fail loudly instead of rendering without their spectra; none is in scope, and a loud failure is what Matt's no-back-compat rule asks for.
- **I3 (Major) — fixed.** `tools/alloc_gate.py`'s merge is a function, `merge(results, expected)`: a child that exits with anything but 0, 1 or 2 fails the gate (3) with a line naming the crash; a summary count short of the patches handed out fails the same way. Five unit tests in `tools/tests/test_alloc_gate.py` (clean, an allocation, a load failure, a crashed child, a short count), registered in ctest as `tool_tests` and in the module map.
- **I4 — fixed.** Every slot declares what it takes (`"an IFormant part"`, `"an IPartials part"`, `"a Partials part"`, `"an evolution holder"`; the two multi slots say which kind), `InputDescriptor::slot` marks a slot's descriptor, and `reject_wire` appends the expectation: `'partials' does not accept a SineSource; 'partials' takes an IPartials part`. Test: the message names it — RED, then GREEN.
- **I6 — fixed.** The stale comments: the `IEvolutionHolder` header, the two holders' "evolution input", the wavetable's, Multiplex's two "valueNodes" comments and its member (`instanceNodes_`), the three test comments, the editor's formant strip, `component.h`'s "every other pin replaces" (now: a single slot, a Param or an Input replaces; a hand-written list may append) and the hook's "never read through a stale handle" (now: a view into a part the slot no longer holds is never read).
- **I8 — fixed.** `Slot<T>::set` casts once and pushes.
- **I9 — fixed.** The door's body lives in `engine/src/component.cpp` (the core module, the engine's sources); `component.h` says so.
- **I10 — fixed.** The sweep's dead `kSilentUntilWired` entries are gone; the audit checks `seen == registered_types().size()`.
- **I11 — pinned.** One test loads a part in each output position: the voice's output ("a part, not a signal"), a mixer-mode output ("mono-renderable"), a channel's source ("No mono source") — the last two through temp files, the standalone loader taking files only. Section 6 ruling 6 above is corrected to name all three.
- **The two coverage gaps (brief item 10 a, b) — tests added.** A `Formant` three parts deep (`FormantSequence` → `FormantSpectrum` → `Formant`, a pin counter on its frequency) is prepared, started and advanced through a `FullAdditiveSource`; a test-registered `ReseedProbe` type asserts that `continue_voice_live` reseeds it exactly once — the live note-on's walk reaches the voice's signals.
- **I5 — not done, by ruling (Part 7).** The editor's three hand lists of part types and `wire_pin` discarding the door's answer are the node model's, which Part 7 rebuilds; the editor asking the door is the real fix. Cost if wrong: a silently inert link in the editor, as today. Listed in section 7.
- **I7 — not done, by ruling (3.4).** Two interface conventions (`IFormant`/`IPartials` pure, `IEvolutionHolder` default-empty): 3.4 deletes `IEvolutionHolder`, and whether the three share a `Part` root is that design's question (section 7).
- **I12 — deferred.** `get_part` on a multi slot returns the first; test-only callers today.

Verified before the commit: `engine_tests` ALL PASS (6673 checks), ctest 5/5 (`tool_tests` new), null gate 78/78, the roundtrip's backlog-79 six, the allocation gate clean, the fast gates.

### Re-review

The same refuter, by message, on `fe9f3d5..b1ae7a4`; verbatim.

One commit, `b1ae7a4`, 21 files, +279/−95. I read `git show b1ae7a4 --stat` and `git diff fe9f3d5 b1ae7a4` in full, then traced every closure at HEAD (`b1ae7a4`): the UI's rewire pass and its lock, the door's new home, `Slot<T>::set`, the descriptor flag and `reject_wire`, the `FormantSequence` configurator and the allowlist, the wrapper's `merge` and its unit test, the five new engine tests and `main()`'s order, the lint scrape and the structure test that bind the allowlist, and a sweep for every stale term I had listed. I built, ran and rendered nothing; the working tree shows only the two pre-existing entries (`GOALS.md` modified, the checkpoint report untracked). Line numbers are HEAD's.

Headline: all three Majors are closed by code I traced, not by assertion. I1's closure is by reading (no editor harness exists, as before); I2 and I3 are closed with tests that bite. Nine of twelve issues are closed, one partly, three deferred by ruling with reasons I accept for two of them. The wave introduces no Major. Two new Minors: the new test block depends on a directory that only a *later* test creates (a fresh clone fails it) and leaves two files behind; the reseed test registers a type into the global registry for the rest of the run. One three-line fix (N1) and the checkpoint is closeable.

---

#### Issues, closed or not

| Issue | Status | The line that closes it, and what I traced |
|---|---|---|
| **I1** (Major) UI: unlinking a holder left the slot full | **Closed** (by reading; untestable in the suite) | `tools/mforce_ui/main.cpp:953-959`: the first pass now runs `clear_param(pin.name)` on every input pin, then `wire_pin(constantSrc)` on the single ones. Traced end states against `fe9f3d5`: a Param — `Param::set(nullptr)` (`pins.h:75`) then `set(constantSrc)`, identical to before; an Input — `pins.h:96`, identical; a multi pin — `clear_param`, identical; a slot — cleared through the door (`component.cpp:25`), then the constant is refused (`component.h:176-177`) and the slot stays empty, which is what the old `Input` going null did. `node.inputs` only ever holds `UiPinKind::Input` (enum `:86`; every `emplace_back` passes it), so the new `continue` at `:957` is inert. The `FormantSpectrum`'s `formants` pin is not in `inputs` (`:626`), so its rows are untouched. Lock: still `update_all_dsp` under `g_audioMutex` (`:1041-1049`) against the callback at `:3855`. The REVIEW manual try (wire, play, unlink, play) is the only pin; the engine semantics stay pinned at `engine_tests/main.cpp:3000, 3466-3467`. |
| **I2** (Major) `FormantSequence` `"formants"` loaded silently empty | **Closed** | `source_registrations.cpp:489-497`: a configurator throws "FormantSequence takes its spectra under 'spectra', not 'formants'" when `p.contains("formants")`; the generic path wraps it with the node id. `"spectra"` left `kBranchKeys` (`patch_loader.cpp:287`); `"formants"` stays (`:274`) and the stated reason holds — `tools/lint_patches.py:56-57` scrapes `source_registrations.cpp` for `p.contains("…")`, and `tools/structure/tests/test_structure.py:129-140` requires the allowlist to cover the scrape, so the key must stay once the configurator reads it. Test `main.cpp:6604-6610` (`load_fails_naming(seqAlias, "spectra")`): with the configurator deleted the patch loads (the key is allowlisted) and the CHECK fails — RED as claimed. The 34 archived vowel patches now fail by name. Nit: the needle `"spectra"` is weaker than `"not 'formants'"`; no other failure on that patch can contain it, so it holds. |
| **I3** (Major) wrapper passed on a crashed child | **Closed** | `tools/alloc_gate.py:50-77` `merge(results, expected)`: any exit not in {0,1,2} → 3 with a "crashed" line (`:69-71`); `total != expected` → 3 (`:72-74`); `main` folds through it (`:105-112`). `tools/tests/test_alloc_gate.py` has five cases including a Windows crash code (−1073741819) and a short count; registered as ctest `tool_tests` (`CMakeLists.txt:70-72`); `tools/tests/` mapped in `modules.json`. `--list` still passes the count check because the executable prints its summary unconditionally (`tools/alloc_gate/main.cpp:232`). Flag, not an issue: a crash alongside an allocation reports 3 only, the ALLOC lines still print. |
| **I4** (Minor) refusal never said what a slot takes | **Closed** | Hints on all six slot declarations: `additive_source2.h:34`, `formant.h:93, 208`, `full_additive_source.h:29-30`, `partials.h:1097`, `wavetable_source.h:23`. `InputDescriptor::slot` (`descriptors.h:40`), set by `Slot::descriptor()` (`pins.h:189`), default false from `Input::descriptor()` (`:98`). `reject_wire` appends "; '<pin>' takes <hint>" (`patch_loader.cpp:243-248`). Test `main.cpp:6598-6603` ("takes an IPartials part"); with the `takes` lines deleted it fails. Side effect, neutral: the hint doubles as the UI's advisory tag and now renders in the Properties panel's input-only loop (`main.cpp:9376-9384`) as grey "(an IFormant part)" beside the pin — arguably what a user wants there. |
| **I5** (Minor) the editor's own part-type lists; `wire_pin` discards the door's answer | **Not closed, by ruling** (Part 7 debt) | `pin_type_compat_error` (`main.cpp:889-920`) still knows nothing of `evolution`; `wire_pin` (`:482-485`) still discards the bool. Pre-existing; a refused link is still drawn silently. I accept the deferral as scope, not as a judgment that it is fine — it belongs on REVIEW's list. |
| **I6** (Minor) stale comments | **Closed** | Sweep at HEAD for `evolution" input`, `evolution input`, `valueNodes`, `instanceValueNodes_`, `fmt_prepare`, `fmt_next`, `dynamic_casts its`, `every other pin replaces`, `never read through a stale`: zero hits outside `third_party`. `wave_evolution.h:632-636` one header; `:646`, `:670` "slot"; `wavetable_source.h:28`; `multiplex_source.h:29, 61` and `instanceNodes_` (`:127-181`); `component.h:48-49` now true for `MultiSource` ("a node that keeps a hand-written list may append in its override"); `:87-88` now says only what the hook guarantees; `engine_tests/main.cpp:2971-2972`, `:3138-3140`; `mforce_ui/main.cpp:12263-12264`. |
| **I7** (Minor) three interfaces, two conventions | **Not closed, by ruling** (3.4 deletes `IEvolutionHolder`) | `wave_evolution.h:639-640` default bodies stand beside pure `IFormant`/`IPartials`. Agreed: the right time is 3.4's design, not a patch now. |
| **I8** (Minor) `Slot<T>::set` cast twice | **Closed** | `component.h:175-182`: one cast, clear, push; `add` at `:165-172`. The two push lines are now the duplicated pair instead of the cast — the cheaper duplication. |
| **I9** (Minor) root header not self-sufficient | **Closed** | `engine/src/component.cpp:1-33` holds the door; `engine/CMakeLists.txt:13`; `modules.json` core paths include it (core may include only core; it includes `dsp_value_source.h`, core). `dsp_value_source.h` no longer defines it; `component.h:35` names the new home. |
| **I10** (Minor) sweep hygiene | **Closed, two of three** | Dead `kSilentUntilWired` entries gone (`main.cpp:4852-4855`, three entries remain, all signals); `seen == int(reg.registered_types().size())` (`:6579`). The double `reg.create(n)` per type (`:4843-4844`) remains — cosmetic. |
| **I11** (Minor) the plan misnamed the channel-source failure; none of the three positions tested | **Closed in code; the doc correction is outside the range** | `main.cpp:6612-6633`: voice output → "not a signal" (`as_signal`), mixer-mode output → "mono-renderable" (`patch_loader.cpp:1646`), channel source → "No mono source" (`:1689`), the two file-only paths through `load_patch_file` on temp files. Each message is the path's own; each CHECK fails if its path's throw is removed. The ruling-6 correction lives in the untracked report (`docs/autonomy/refactor/reports/2026-10-30-checkpoint-3.1.md`), not in `b1ae7a4`; I could not verify it from the commit. See N1 for how the temp files are written. |
| **I12** (Minor) `get_part` on a multi slot | **Not closed, by ruling** (test-only callers) | `component.h:43-47` unchanged; grep: still test-only. Agreed. |
| Coverage gap (a): no `deliver_continuation` test | **Closed** | `main.cpp:6652-6670`: a registered `ReseedProbe` (`:6584-6589`) in a voice beside a part; `prepare_voice_at` then `continue_voice_live` → `probe->reseeds == 1` (`:6668`). Pins reach and count (the fresh path does not reseed; `== 1` would catch a double). The no-cast property stays a grep fact (`instrument.h`: none). See N2. |
| Coverage gap (b): nested prepare untested | **Closed** | `main.cpp:6634-6651`: `FullAdditiveSource` → `FormantSequence` → `FormantSpectrum` → `Formant` with a `PinCounter` on `frequency`; `rate == 48000 && starts == 1 && nexts == 3` (`:6650`). Mutation traced: delete the loop in `FormantSpectrum::prepare` (`formant.h:98-100`) → `rate` stays 0; in `FormantSequence::start` (`:219-221`) → `starts == 0`; in either `next` → `nexts == 0`. Each pin a different forward. Bites. |

---

#### New findings

##### N1 — Minor — `run_review_gap_tests` depends on `renders/scratch/` existing and leaves two files behind

`main.cpp:6615-6633` writes `renders/scratch/_part_as_output.json` and `renders/scratch/_part_as_channel.json` with a bare `std::ofstream` and never removes them. `renders/` is gitignored in its entirety; the only test that creates the directory is `run_passage_melody_tests` (`:1320-1321`, `std::filesystem::create_directories("renders/scratch")`, with `remove` at `:1360`), which `main()` calls at position 69 — `run_review_gap_tests` runs at position 13 (`:6685`). On a fresh clone the `ofstream` silently fails, `load_patch_file` throws a different message ("empty input" or a missing-file error), `load_fails_naming`'s needle misses, and two CHECKs fail. On Matt's machine `renders/scratch/` exists, hence ALL PASS (6673). The public-release goal makes "the suite passes on a fresh clone" a requirement, and the sibling test shows the house pattern. **Fix (three lines):** `std::filesystem::create_directories("renders/scratch")` before the writes and `std::filesystem::remove(path)` after each, or write to `std::filesystem::temp_directory_path()`. **Test that would have caught it:** a run from a clean checkout; cheap to do once before the checkpoint closes.

##### N2 — Minor — the reseed test registers a type into the global registry for the rest of the run

`main.cpp:6655-6657` adds `ReseedProbe` to the `SourceRegistry` singleton (guarded by `has`). Every later sweep over `registered_types()` sees it: `run_split_equivalence_tests` (called from `:5909`, after position 13) now splits a `ReseedProbe` too — equal arms, nonzero, a Generator — so it passes; `run_component_root_tests` ran earlier (position 12), so its `seen ==` was evaluated pre-pollution and would pass either way (a signal, not in `kPartTypes`). No failure today; an order-dependent global mutation in a test that other tests read, and `engine_tests` is the only binary that sees it. **Fix:** build the voice without the registry — `load_instrument_patch_json` needs a registered type, so either a `SourceRegistry::unregister` for tests (one method, called at the block's end) or an existing seeded signal (`WhiteNoiseSource`) whose first draw after `continue_voice_live` equals a fresh voice's first draw after `start`, which pins the reseed without a probe type. Cosmetic until a sweep asserts an exact registry count.

##### N3 — flag, not an issue — every `update_all_dsp` now clears and refills every slot of every node

`main.cpp:957-959` plus the second pass: on every link destroy anywhere in the graph, each slot is cleared (hook fires: `WavetableSource::refresh_evolution` swaps to `ownedEvolution_`/null) and refilled (hook fires again: back to the holder). Under `g_audioMutex`, control side, two pointer swaps; `AdditiveSource2::apply_partials` runs once (on the refill), as it did before. Harmless; noted because the hook's call count per rewire doubled and a future owner with an expensive hook would feel it.

##### N4 — flag, not an issue — `"formants"` remains a global allowlist key

Kept for a sound reason (I2), but the allowlist is global (`patch_loader.cpp:264-305`, its own comment at `:260-262`): a stray `"formants"` on an `AdditiveSource` (whose pin is `formant`) still passes `reject_unknown_keys`. Pre-existing weakness of the design, Part 4's by the plan; the wave did not widen it.

##### N5 — nit — the alias test's needle

`main.cpp:6610` matches `"spectra"`; `"not 'formants'"` would name the configurator's refusal and nothing else. Holds today.

---

#### Standing

The checkpoint's engine side is in the state the plan promised and I could not refute it: one door (`component.cpp:9-32`) type-checks every wire at load or at an editor change; the voice's audio-thread walk reads a list built once (`instrument.h:544`) and `instrument.h` carries no cast; the twenty-two parts implement their interfaces and nothing of `ValueSource` (`main.cpp:6566-6580`); nested parts are prepared, started and advanced through their owners (`:6634-6651`); the live note-on's reseed reaches a signal (`:6652-6670`); a malformed wire fails the load naming node, pin, the offered type, the kind and now what the slot takes (`patch_loader.cpp:239-248`); the `FormantSequence` alias fails by name instead of loading hollow (`source_registrations.cpp:489-497`); the wrapper fails on a crash or a short count and has a unit test. The UI's unlink regression is closed by a change I traced to the same end state the old `Input` produced, under the same mutex; it stays unpinned by any test because no editor harness exists — REVIEW's manual try is the pin, and it should be done before the checkpoint closes.

Open: I5 (the editor draws a refused link silently; pre-existing; Part 7), I7 (3.4), I12 (cosmetic), the sweep's double create (cosmetic), N1 and N2 (new, both Minor, both in one test function). N1 is the only one I would hold the close for: three lines, and without them a fresh clone fails the suite. N2 can ride with it or wait.

Byte-identity, the A/B, the null gate, the roundtrip's six and the gate times are the executor's claims as before; nothing in this wave touches a render path (the door moved TUs, `Slot::set` lost a cast, the UI's first pass changed on the control side), so I expect them to hold and have no evidence either way.

### After the re-review

- **N1 — fixed** (`993ae4b`): the output-position tests make `renders/scratch/` before writing their two patches and remove each file after the load; a fresh clone passes. The refuter's nit N5 rides with it: the alias test's needle is `not 'formants'`, the configurator's own words.
- **N2 — not done, by ruling.** The reseed test registers a `ReseedProbe` into the registry singleton for the rest of the run. Removing it afterwards needs an `unregister` on the production registry for the sake of one test, a worse smell than one extra Generator in the test binary's registry; the sweeps that run after it are self-consistent (the audit compares against `registered_types()` itself, the split compares the probe's two arms and finds them equal) and no sweep asserts a fixed count. Cost if wrong: an order-dependent extra type in the test binary's sweeps, nowhere else.
- **N3, N4 — flags, listed** in section 7: the editor's rewire pass now fires the slot hook twice per rewire (control side, two pointer swaps); `"formants"` is a global allowlist key (Part 4's allowlist is global by design).
- **I5, I7, I12 — stand as ruled** (Part 7, 3.4, cosmetic); I5 is REVIEW R8 item 6.

### Re-check

The same refuter, on `b1ae7a4..993ae4b`; verbatim.

Read-only: `git diff b1ae7a4 993ae4b` (one file, `tools/engine_tests/main.cpp`, +8/−3), traced at HEAD `993ae4b`; ran nothing; the working tree still shows only the two pre-existing entries.

**N1 — closed.** `file_fails_naming` (`main.cpp:6617-6629`) now calls `std::filesystem::create_directories("renders/scratch")` at `:6618` before the `ofstream`, so a fresh clone gets the folder the way the sibling test at `:1321` makes it (idempotent when it exists). `std::filesystem::remove(path)` at `:6627` sits after the try/catch and runs on both the throw path and the no-throw path, so neither `_part_as_*.json` survives whichever way the load goes; the needle verdict is carried in `named` and returned as before. `<filesystem>` was already included at `:1227`. The order dependence on `run_passage_melody_tests` is gone.

**N5 — closed.** `:6610` now matches `not 'formants'`, which only the `FormantSequence` configurator's message (`source_registrations.cpp:496`) contains.

**Introduced:** nothing. The lambda still writes relative to the working directory, which the suite already assumes (`:1320`); the `remove` is the throwing overload, consistent with `:1360`; no render path, no engine source touched.

**N2 by ruling:** accepted. I had graded it cosmetic; an `unregister` on the production registry for one test is indeed the worse smell, the later sweeps are self-consistent, and none asserts a fixed count.

**Closeable:** yes. Every Major is closed by code I traced; every new finding is closed or ruled; what remains outside the code is REVIEW's manual try for I1 (wire a holder, play, unlink, play), which should be done at the close, and I5 on REVIEW's list as Part 7 debt. Byte-identity, the A/B and the gate times remain the executor's claims, which nothing in these two commits can have moved.

The seal is `f1f8244`, one test-only commit after the re-check: the review wave's gap test, at 82 lines, split in two (loader cases; lifecycle cases) to stay under the function limit — nothing the re-check read changed meaning.

## 9. Reading guide

Ten minutes: section 1 (what moved and the one behaviour change), section 5 (the gates, the A/B, the timing, the meter), section 6 rulings 27, 25, 19 and 29 (the four that departed from the plan or corrected it: the hook, AdditiveSource2's copy, set/add on a multi slot, the count), section 7 (the debt, mostly 3.4's and Part 7's), section 8 (the refutation and what was done about it), section 10 (the lessons). The plan's Ledger carries the execution rulings in full.

## 10. C++ lessons

### Lesson 1: an interface is a class with nothing in it

`struct IFormant { virtual ~IFormant() = default; virtual void prepare(const RenderContext&) = 0; … };` is C++'s interface: a class whose every method is pure virtual (`= 0`) and that holds no data. `struct Formant final : Component, IFormant` inherits from two bases — the registry root, which has data (the pin table), and the interface, which has none — and that is exactly what a C# `class Formant : Component, IFormant` says. C++ allows inheriting from several classes with data too (and the diamond that comes with it); this codebase uses the C#/Java discipline by convention: one base with state, any number of pure interfaces. Before this checkpoint every part inherited `ValueSource`'s state (two sample values, a tick, a fault byte, a render context) and its contract (`next`, `prepare`, `start`) that it never honoured — `compute()` returned 0 so that the graph could hold it. The fix was not to make the fake honest but to remove the base that demanded it.

Why `virtual ~IFormant() = default;` on an interface: a `std::shared_ptr<IFormant>` deleting through the interface pointer must reach the derived destructor; without a virtual destructor the derived part's members would leak. C# has no equivalent because the garbage collector knows the object's real type; in C++ the destructor is just another virtual call, and an interface must declare it so.

### Lesson 2: `dynamic_cast`, and where a design lets it live

`dynamic_cast<T*>(p)` asks at run time "is the object behind `p` really a `T`?" and answers null if not; `std::dynamic_pointer_cast<T>(sp)` is the same question for a `shared_ptr`, returning a `shared_ptr<T>` that shares ownership or an empty one. It needs run-time type information, costs a walk of the class hierarchy, and is the C++ spelling of C#'s `as` — and like `as`, a design that needs it in many places is a design that lost track of its types. Part 2 asked for it to live in one place, "the wire's type-check". Before this checkpoint it lived in seven: each consumer cast its incoming `ValueSource` to the interface it wanted, and failed silently when the cast returned null. Now the door does the cast once for every pin kind (`Component::set_param` in `dsp_value_source.h`, `Slot<T>::add` in `component.h`), at load or at an editor change, and the loader turns a null into a load failure that names the node, the pin and the kind. The consumers hold `Slot<IFormant>` and read `formant.get()`, a typed pointer that needed no cast — the compiler checked it at wiring, which is the whole point of the template: `Slot<IPartials>` and `Slot<IFormant>` are two different types.

The voice's `signals` list is the other half of the lesson. Three host walks need every signal node of a voice (faults, capture, the live note-on's reseed), and the voice now holds `Component`s. Casting each node in each walk would put a `dynamic_cast` per node per note-on on the audio thread. The list is built once at load — where the cast is already paid for every node — and the walks read typed pointers.

### Lesson 3: a `shared_ptr<Derived>` converts to a `shared_ptr<Base>` by itself

Eighty-odd registry factories are lambdas like `[](auto) { return std::make_shared<SineSource>(); }` stored in a `std::function<std::shared_ptr<Component>(std::optional<uint32_t>)>`. The lambda returns a `shared_ptr<SineSource>`; the function type wants a `shared_ptr<Component>`. None of them needed an edit, because `std::shared_ptr` has a converting constructor from a `shared_ptr` of a derived type — the same implicit upcast a raw pointer has — and `std::function` accepts any callable whose return converts. The same conversion is why every existing `set_param(name, someValueSource)` call compiled unchanged when the parameter became `std::shared_ptr<Component>`: the argument upcasts at the call. The downcast never happens by itself; that is what `dynamic_pointer_cast` is for (lesson 2). C#'s reference conversions work the same way, which is why this one is the lesson that feels like home.

### Lesson 4 (short): a hook is the observer you already have

`on_setting_changed(name)`, `on_array_changed(name)` and now `on_slot_changed(name)` are empty virtuals on `Component` that the base calls after it changes a pin by name. A node that derives state from a pin — the wavetable's pointer to the evolution its holder carries, AdditiveSource2's copied arrays — overrides the hook and refreshes. This is the Observer pattern with one observer, the node itself, and no subscription machinery: the base is the subject, the virtual call is the notification, the name is the event. It is also why resolving a cached view "at prepare" is not enough for a graph an editor edits live: prepare is the host's moment, not the pin's, and the window between them is where a stale pointer lives. The lesson for a C# reader is that a C++ class with virtual hooks is an event source with the events fixed at design time, which is the right trade when the events are three and known.

## Appendix A: the A/B set

The 132 patches under `patches/library` and `patches/baselines` (`renders/scratch/ab31/library_baselines.txt`), rendered by both arms and compared by hash; every one identical.

| Folder | Patches |
|---|---|
| `patches/baselines` | 34 |
| `patches/baselines/feedback` | 7 |
| `patches/baselines/perform` | 2 |
| `patches/baselines/prof_additive` | 9 |
| `patches/library/bells` | 3 |
| `patches/library/effects` | 2 |
| `patches/library/keys` | 8 |
| `patches/library/percussion` | 2 |
| `patches/library/strings` | 2 |
| `patches/library/voice` | 58 |
| `patches/library/winds` | 5 |

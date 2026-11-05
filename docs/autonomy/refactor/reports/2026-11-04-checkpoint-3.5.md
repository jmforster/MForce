# Checkpoint 3.5 — the envelope presets become one type; the `KSPianoString` alias goes

Part 3 (`docs/architecture/part3-nodes.md`), checkpoint 3.5 of 3.8. Commits `750f70e`..`a2ff727` on main (`6a825a4` the task seal, `b1d0222` the review wave, `a2ff727` the re-review's two flags) (plan `docs/superpowers/plans/2026-11-03-part3-checkpoint-3.5.md`, committed as `3a33ebd`; execution ledger in the plan's Ledger). Report R12. Written 2026-11-04 at extra-high effort, inline execution with a Fable refuter.

## 1. Summary

Part 3 section 6 said the six envelope preset classes could go because "`Envelope` already builds its stages from a `preset` setting". The plan found the premise wrong in the letter: `Envelope` has no such setting. What it has is a *load-time form* — `envelope_from_preset_json`, the one dispatch both loaders share, reads `"preset": "ar" | "adsr"` and the knob keys beside it and returns a plain `Envelope` whose stage list is the truth — and that is how the 1,125 nodes the section counts already load (935 in the stages form, 167 `adsr`, 23 `ar`). The six classes (`AREnvelope`, `ASEnvelope`, `ASREnvelope`, `ADSEnvelope`, `ADREnvelope`, `ADSREnvelope`) were the opposite shape: the layout's knobs as `Setting`s, and a `rebuild()` that laid the stages out again on every change, seventy lines each. The intent of the section stands; the collapse went in the direction the code already leaned, and Matt's own note in `envelope.h` states ("presets are UI conveniences"): **the stage list is the one truth of an `Envelope`; a preset is a way of writing one.**

Now four static factories join `make_ar`, `make_adsr` and `make_adsr_abs` on `Envelope` — `make_as(attack, level, reverse)`, `make_asr(attack, level, release)`, `make_ads(attack, decay, level)`, `make_adr(attack, decay, decayLevel)` — each the class's `rebuild()` body verbatim. `envelope_json.h` is a table of six preset builders (`kEnvelopePresets`: name, builder) over those factories; each builder reads the class's keys with the class's defaults, lays the stages out through the factory, then shapes each curved stage from its `<stage>Curve` (0..3, refused outside) and `<stage>Power` keys, which the classes carried and the old `ar`/`adsr` forms did not. `envelope_preset_keys()` is the one list of keys the form reads, and the editor's key filter consults it where a hand-copied list stood. The loader's Envelope branch reads `seed` in both forms, where it read it in the stages block alone (a rewritten node under a `MultiplexSource` would otherwise have lost the injected seed). The six classes, their header, their six registrations and the `KSPianoString` registration are gone; `source_registry` gained `retired_type_hint`, which both loaders' unknown-type messages consult, so a patch that still says `ASEnvelope` or `KSPianoString` fails naming `tools/migrate_deleted_types.py`. Vibrato's two internal ramps are `make_as` calls. The registry's derived-label suffix list lost `Envelope` (no name ends in it but `Envelope`).

On disk, one tool with a `RULES` table rewrote 497 nodes in 489 files across `audition/`, `baselines/`, `library/`, `pending/`, `sweep/` and `scratch/` — `{"type": "ASEnvelope", …}` → `{"type": "Envelope", "params": {"preset": "as", …same keys…}}`, keys in order with `preset` first, and `KSPianoString` → `KSString` — eleven of them tracked (the seven preset files and four pianos in scope, all in the null gate's manifest). `patches/old/` is untouched: its 54 preset-class and 5 alias nodes (59, in 41 files) now fail to load naming the tool. The rewrite is the same one the alternative design would have needed, so if Matt wants the preset knobs back as settings on `Envelope` that is an additive step later, never a second migration (section 6, ruling 1).

**Behaviour.** No render changed: the null gate is 78/78 after every task and across the rewrite (the eleven in-scope files' hashes are the proof of the rewrite itself); the A/B over the 132 patches of `patches/library` and `patches/baselines` — the old arm rendered by the pre-checkpoint binary from the *unrewritten* files before anything moved, the new arm by the final binary from the rewritten ones — is 132 identical, 0 DIFFER; a synthetic set of eight written in the CLASS form with every knob off its default (the two forms with no user on disk among them, a class under a multiplex with accuracies below 1, a bare `adsr` for the written default) is 8/8 identical same-binary (class form against the tool's rewrite under one engine) and 8/8 cross-binary (class form under the old engine against the rewrite under the new). The equivalence of the six layouts was proved while both existed: Task 1's test built each class through `set_setting` and the form from the same keys over a knob grid and compared the eight stage fields per stage, then Task 3 deleted the class half and left literal stage tables.

What a person sees changed is in the editor only, three things: the six types are gone from the Envelopes menu (the registry generates it) and a **Preset** combo with **Apply** sits above every Envelope's stage table, laying the stages out as the chosen preset with its defaults through the engine's dispatch — the replacement for the six entries, no knobs; the preset-only Duration/Shape knob layout of 2026-08-29 went with the types it drew, so the 174 rewritten nodes show the stage table, whose columns hold the same numbers; and `sustainLevel` on a two- or three-stage shape reads the four-slot view, 0, where the deleted AS/ASR/ADS classes reported the level they stored (the knob was already inert on such a shape). REVIEW R12 carries the first two for Matt's eyes with the note that the knob groups can return as settings on `Envelope` without touching the files again.

## 2. Types and files

| | Before (`9f0140c`) | After (`a2ff727`) |
|---|---|---|
| Envelope types registered | 7 (`Envelope` + six preset classes) | 1 |
| Physical names registered | 7 (`KSPianoString` an alias of `KSString`) | 6 |
| Registrations | 86 (63 Main, 23 Experimental) | 79 (57, 22) |
| `envelope.h` | 568 lines; `SustainDefault`, a protected ctor, `is_common_knob_`, a virtual `sustain_level()` | 586; four more factories; `sustain_level()` a private function; `replace_stages` kept for one caller (the editor's combo) |
| `envelope_presets.h` | 244 lines, six `final` classes, `kRampCurveLabels` | deleted |
| `envelope_json.h` | 58 lines, an if-chain over two names | 147; `envelope_form::{curve, shape, ar, as, asr, ads, adr, adsr}`, `EnvelopePreset`, `kEnvelopePresets[6]`, `envelope_from_preset_json` a lookup, `envelope_preset_keys()` |
| `vibrato.h` | two `ASEnvelope`s built and `set_setting("attack")` | two `make_as(attack.value)` calls |
| `source_registrations.cpp` | 649 | 601 (−48: six registrations, the alias, the include) |
| `source_registry.{h,cpp}` | `derived_label` strips `Source`, `Filter`, `Node`, `Envelope` | strips three; `retired_type_hint(name)` |
| `patch_loader.cpp` | the Envelope branch reads `seed` in its stages block; `kBranchKeys` 130 | reads it for both forms; 134 (`decayCurve`, `decayLevel`, `decayPower`, `reverse`); the unknown-type throw appends the hint |
| `tools/mforce_ui/main.cpp` | 14,512; `kEnvelopeKeys` (13 names); the preset-only Duration/Shape layout (88 lines) | 14,479; the filter on `envelope_preset_keys()`; `apply_envelope_preset` with the Preset combo and the headless `--apply-preset`; the status line appends the hint |
| `tools/migrate_deleted_types.py` | — | 165 lines, `RULES` table (seven rows, two with `dead` keys), `--check`, indent-keeping; 15 unit tests under `tool_tests` |
| `tools/engine_tests/main.cpp` | 7,823 lines, 7,552 checks | 7,946, 7,460 checks (the equivalence grid's class half gone with the classes; the factory, form, loader and retired-type tests in) |
| Patches | 174 preset-class nodes and 116 alias nodes in the four active folders; 169 + 18 more in `sweep/`, 12 + 8 in `scratch/` (497 in 489 files) | 0 outside `old/` (54 + 5 there, in 41 files); 11 tracked files changed |

## 3. Interfaces changed

- **`Envelope`** (public): `make_as`, `make_asr`, `make_ads`, `make_adr` beside the three factories that existed. Gone: `SustainDefault` and the protected constructor that took it (a preset's own sustain default — the form's defaults carry that now), the protected `is_common_knob_`, the virtual `sustain_level()` (one override, gone with the classes; now a private function behind `get_setting("sustainLevel")`). `replace_stages` is unchanged in shape and has one caller, the editor's combo; its comment says so.
- **The preset form** (`envelope_json.h`): six names where there were two; the four new take exactly the deleted class's keys with the class's defaults (`as`: `attack` 0.2, `attackCurve` 0, `attackPower` 0, `reverse` false, `sustainLevel` 1.0; `asr`: + `release` 0.1, `releaseCurve` 3, `releasePower` 0; `ads`: `decay` 0.1, `decayCurve` 0, `decayPower` 0, `sustainLevel` 0.0; `adr`: `decayLevel` 0.7, no sustain); `ar` and `adsr` keep their reads and defaults and gain their curve and power keys (so a curve key on an old form node, none on disk, now applies where it was ignored by the lint and refused by nothing). A curve outside 0..3 is refused by name. `EnvelopePreset {name, build}`, `kEnvelopePresets[]` in menu order (`ar`, `as`, `asr`, `ads`, `adr`, `adsr`), `envelope_preset_keys()`.
- **`SourceRegistry`**: `retired_type_hint(std::string_view) -> const char*` — the seven names this engine once registered and the tool that rewrites a patch still using one; `nullptr` otherwise. `derived_label` no longer strips `Envelope`.
- **The loader**: "Unknown node type: ASEnvelope; python tools/migrate_deleted_types.py <patch> rewrites a type this engine no longer has"; a name the engine never had gets today's message. The Envelope branch's `seed` read covers both forms.
- **The editor**: the Envelope key filter is the engine's list plus `stages`; every Envelope node is the stage-table node; the Preset combo; the unknown-types status line appends the hint once.
- **The tool**: `python tools/migrate_deleted_types.py <patch-or-dir> [...]` rewrites in place; `--check` exits 1 while a rewrite is pending; a `dynamicPins` entry naming a layout knob on a preset-class node is refused by name (there were none).

## 4. Duplicates collapsed

- Six `rebuild()` bodies (and the two factories two of them wrapped) → four factories plus the two that existed: one statement of each layout.
- Six registrations of the same five lines → none; one `Envelope` registration.
- Six `on_setting_changed` overrides clamping curves 0..3 → one `curve()` refusal in the dispatch.
- The editor's `kEnvelopeKeys` (a hand copy of the form's keys, the drift class the shared dispatch was created against) → `envelope_preset_keys()`.
- The editor's preset-only Duration/Shape layout (88 lines of grouping over the classes' descriptors) → nothing; the stage table was already drawn for every Envelope.
- `derived_label`'s `Envelope` suffix rule had six users; `Envelope` itself must not be stripped, so the rule and its guard go together.

The meter counts the result: `duplicate_blocks` 151 → 145, `changelog_comments` 248 → 242, thirteen types gone and two added (`EnvelopePreset`; the tests' `StageRow`).

## 5. Metrics

**Gates at the seal** (`python tools/gates.py --fast` after each task, the slow set at the close at `6a825a4`, the fast set again after the wave at `b1d0222`):

| Gate | Result |
|---|---|
| ctest (engine_tests, test_figures, structure meter, tool_tests) | PASS; `engine_tests` ALL PASS (7457 checks); the tool's 14 unit tests under `tool_tests`; the meter's known-answer test re-pinned twice (the editor's properties panel 1,035 → 930 → 967 lines) |
| `mforce_ui --stamp` | PASS (stale: no) |
| roundtrip smoke (one patch per shape) | FAIL: the known wiring diff (5/6 shapes clean), as at 2.4 through 3.4 |
| allocation gate (library + baselines, `--jobs 3`) | PASS (46–52 s; the eleven rewritten files among the 132) |
| null gate (patches) | PASS: 78/78 manifest entries identical — after Task 1 (nothing on disk moved), after Task 3's rewrite, and again after the indent-keeping re-run |
| roundtrip (UI save/load, whole corpus, 132 patches) | at `6a825a4`: FAIL on the backlog-79 six (`double_advance_bug`, `double_advance_ok`, `feedback/loop_tap_counter`, `feedback/loop_tap_starved`, `perform/wiring_setting`, `perform/wiring_smoke`), 0 id changes, 27 id-only (non-instrument) — the same six as at 3.2 through 3.4, pre-existing, Part 4's codec (`renders/scratch/ab35/roundtrip_t5.txt`); after the wave (review F5): PASS — `0 NEW render diffs, 6 known diffs`, exit 0, so a seventh now fails the run (`roundtrip_wave.txt`) |
| lint (library + baselines) | the eight pre-existing voice-patch paramMap curve-endpoint notes; nothing new; `--selftest` now passes both cases (ruling 17) |

**A/B** (`renders/scratch/ab35`): old arm from `renders/scratch/ab_tree/cli_pre35.exe` (the engine at `0d1e826`, rendered at Task 0 from the unrewritten files), new arm from the build at `6a825a4` over the rewritten files: `identical : 132`, `DIFFER : 0` (`render_new.txt`). Synthetic set (`synth.txt`, eight class-form patches; `synth_form/` the tool's rewrite): same-binary `synth_t2_class` vs `synth_t2_form` 8/8 identical under the Task 1 engine; cross-binary `synth_old` (old engine, class form) vs `synth_new` (new engine, form) 8/8 identical. The eight: `as_class` (reverse, Expo attack, level 0.6, both accuracies below 1, seed 77), `asr_class`, `ads_class`, `adr_class` (the two forms with no user on disk), `ar_class`, `adsr_class` (every curve and power off its default), `adsr_class_bare` (no `release`: the tool writes 0.1), `as_in_multiplex` (three clones, accuracies 0.8/0.9: the injected seed through the form path).

**Timing** (`python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre35.exe build/tools/mforce_cli/Release/mforce_cli.exe`, one run on a quiet machine — no per-sample path changed; the layouts are construction-time):

Run 1 (`renders/scratch/ab35/timing35_run1.txt`) ran while the refuter was building and rendering on the same machine: the base arm's own walls sit 5–8 percent above 3.4's clean run (oboe1 860 ms against 797), and the rows spread 0.95x to 1.07x with both tails on the micro patches (D 8 sines 1.07x, I 40 envs 0.95x) — contention, since the two arms alternate round by round and nothing per-sample changed in the Envelope (`sustain_level()` lost its `virtual`, but it is called by `get_setting` alone, never by `compute()`; the class's members and `compute()` are untouched).

```
patch             base wall   new wall   speedup  base rndr   new rndr
----------------------------------------------------------------------
oboe1                 860.4      828.0     1.04x       21.6       19.8
piano_default         997.7     1016.8     0.98x       42.5       39.0
viola_default        1218.9     1237.8     0.98x        6.4        6.1
A sine                454.6      450.2     1.01x      134.4      133.7
B sine+env            489.6      467.7     1.05x      161.5      158.6
C filter x8           775.8      763.0     1.02x      453.4      452.6
D 8 sines            1287.1     1202.7     1.07x      971.4      892.8
E shared ref          517.1      492.2     1.05x      188.4      182.3
F 40 sines           3260.1     3097.5     1.05x     3044.5     2880.3
G 40 own env         4055.6     4238.6     0.96x     3840.3     4021.4
H 40 shared          3041.3     3047.1     1.00x     2822.0     2824.3
I 40 envs            1638.9     1720.4     0.95x     1411.8     1504.4
```

Run 2 (`timing35_run2.txt`), after the refuter had finished and with nothing else of the session's running: eleven rows 0.99x to 1.04x; one row, H (40 sines sharing one envelope), 0.89x — new 3,459 ms against base 3,083 — which no code path explains (G, forty sines with their own envelopes, and I, forty envelopes, are 1.00x and 0.99x; a slower `Envelope` would show there first, and the shared-reference path did not change). Both arms' base walls sit above run 1's, so the machine was not quieter than during run 1; the row is contention on one round and is reported as measured.

```
patch             base wall   new wall   speedup  base rndr   new rndr
----------------------------------------------------------------------
oboe1                 899.5      874.2     1.03x       21.9       20.1
piano_default        1022.3     1032.1     0.99x       42.0       38.9
viola_default        1230.2     1212.2     1.01x        6.6        6.0
A sine                443.9      438.9     1.01x      128.6      131.1
B sine+env            475.3      473.2     1.00x      151.6      158.2
C filter x8           753.4      758.9     0.99x      439.7      448.0
D 8 sines            1250.5     1242.9     1.01x      921.8      913.9
E shared ref          507.9      490.5     1.04x      188.1      192.1
F 40 sines           3355.4     3405.1     0.99x     3130.9     3188.2
G 40 own env         4217.5     4213.4     1.00x     3997.0     3998.5
H 40 shared          3083.1     3459.1     0.89x     2867.5     3238.3
I 40 envs            1882.8     1909.8     0.99x     1657.6     1692.7
```

Run 3 (`timing35_run3.txt`), the same conditions: rows 0.92x to 1.05x, and the rows that were low in run 2 are high here and the other way round (H 0.89x → 1.05x, G 1.00x → 0.92x, D 1.01x → 0.95x). Over the three runs no row is consistently on one side, the base arm's own walls move by 5–10 percent between runs, and the one code change on an envelope's object (`sustain_level()` no longer virtual) is never on the per-sample path. The spread is the machine's, reported as measured.

```
patch             base wall   new wall   speedup  base rndr   new rndr
----------------------------------------------------------------------
oboe1                 866.5      857.6     1.01x       21.8       20.0
piano_default        1037.5     1009.0     1.03x       42.2       39.4
viola_default        1228.7     1228.9     1.00x        6.9        6.0
A sine                447.7      430.9     1.04x      129.3      125.8
B sine+env            478.1      467.2     1.02x      163.6      160.6
C filter x8           782.3      799.6     0.98x      467.2      470.4
D 8 sines            1267.9     1335.2     0.95x      953.8      998.4
E shared ref          513.9      507.7     1.01x      194.6      192.6
F 40 sines           3255.7     3383.1     0.96x     3014.1     3166.2
G 40 own env         4169.2     4535.3     0.92x     3947.1     4310.9
H 40 shared          3387.4     3237.2     1.05x     3120.0     3021.4
I 40 envs            1823.1     1761.9     1.03x     1605.8     1542.1
```

**Structure meter** (`python tools/structure/check.py --delta 9f0140c`; at `6a825a4` in `renders/scratch/ab35/meter35_delta.txt`, after the wave at `b1d0222` in `meter35_delta_final.txt`; the snapshot `tools/structure/metrics.json` is the latter):

| Measure | Before | At `6a825a4` | After the wave | Change |
|---|---|---|---|---|
| files | 180 | 179 | 179 | −1 |
| lines | 61,474 | 61,357 | 61,394 | −80 |
| files_over_limit | 20 | 20 | 20 | +0 |
| functions_over_limit | 98 | 98 | 98 | +0 |
| file_level_mutable_vars | 120 | 120 | 120 | +0 |
| const_cast | 10 | 10 | 10 | +0 |
| dynamic_cast | 60 | 62 | 63 | +3 |
| changelog_comments | 248 | 242 | 242 | −6 |
| duplicate_blocks | 151 | 145 | 146 | −5 |
| include_violations | 14 | 14 | 14 | +0 |
| unmapped_files | 0 | 0 | 0 | +0 |

The three `dynamic_cast`s: two in the test file (`dynamic_cast<PitchedInstrument*>` in two loader-level helpers) and one in the editor (the headless `--apply-preset` path's node lookup); the engine's count is 15 → 15. The wave's 37 lines are the headless block, the shared function and the tests; `duplicate_blocks` gives one back for the two headless blocks' shared scaffolding (`--rename`'s and `--apply-preset`'s). Files: `envelope_presets.h` removed; `envelope_json.h` 58 → 147, `envelope.h` 568 → 586, `source_registry.cpp` 123 → 131, `source_registry.h` 115 → 120, `patch_loader.cpp` 1,743 → 1,747, `source_registrations.cpp` 649 → 601, `vibrato.h` 126 → 124, `tools/engine_tests/main.cpp` 7,823 → 7,946, `tools/mforce_ui/main.cpp` 14,512 → 14,479. No new file over 600 lines, no new function over 80 (the longest new one, `run_envelope_form_tests`, is 56).

**Tests**: 7,552 → 7,656 at Task 1 (the equivalence grid, the factory tables, the loader-level pair) → 7,457 at Task 3 (the grid's class half gone with the classes; the six forms' literal tables, the retired-type messages, the registry tables at 79 rows in) → 7,460 after the wave (the view the editor re-pulls). Mutations, each restored to ALL PASS: Task 1 — `make_asr`'s attack clamp 1.0 → 0.5 fails 14 checks (the ASR equivalence rows and the factory table); its release ramp Sine → Linear fails the factory table alone (the form shapes the stage from the key's default, so a factory's own ramp type is only reachable by a direct caller — Vibrato is one; ruling 18). Task 2 — the tool's `adsr` default dropped fails its unit test. Task 3 — an `ASEnvelope` registration resurrected fails six checks (the family table, `walked == 79`, the envelopes set, the menu, the message test); the hint pointed at `nullptr` fails four. The tool: 14 unit tests (each rule, the `adsr` default, key order, idempotence, nested node lists, the refusal, byte-identical no-op, indent 1 and 4 kept, check mode, the tree walk with a broken file reported not fatal).

## 6. Decisions and rejected alternatives

Rulings 1–20 are in the plan's Ledger with their reasons; the ones that shape the code:

1. **Presets are factories and a load form, not node state** (ruling 1). The alternative — `Setting<int> preset` plus the eleven layout knobs as settings on every `Envelope`, inert when the shape is authored — would leave 935 stages-form nodes with eleven dead knobs each and need a per-preset visibility mechanism to hide them (a new mechanism for one family). The on-disk rewrite is identical under both designs, so the alternative stays open as an additive step. Cost if wrong: the settings and a rebuild added to `Envelope`, about the size of one deleted class. REVIEW R12.
2. **Part 3 sections 1 and 6 corrected, not rewritten** (ruling 2): "already has a `preset` setting" → "already loads a `preset` form"; the landed paragraph names the correction.
3. **Form defaults are the classes' own, with one asymmetry left where it was** (ruling 3): the `adsr` form's `release` defaults to 0.0 (167 nodes on disk, every one carrying the key) and the class's was 0.1; the tool writes 0.1 on a class node lacking the key (all nine on disk carried it; the synthetic `adsr_class_bare` proves the written default). Moving the form's default would change no active render but `old/` and `sweep/` were not surveyed for a bare case. CLEANUP 23.

| Layout | Class default → form default | Stage fields (percent, min, max; start → end, type, power) |
|---|---|---|
| `ar` | attack 0.2 (both); clamps 0..1 (both) | `{attack, 0, 1; 0 → 1, Linear, 0}`, `{0, 0, 0; 1 → 0, Sine, 0}` |
| `as` | attack 0.2, level 1.0, reverse false | `{attack, 0, 1; startV → endV, Linear, 0}`, `{0; endV → endV, Linear}` — reversed: level → 0, held at 0 |
| `asr` | + release 0.1, level 1.0 | `{attack, 0, 1; 0 → level}`, `{0; level → level}`, `{release, 0, 0; level → 0, Sine}` |
| `ads` | decay 0.1, level 0.0 | `{attack, 0, 1; 0 → 1}`, `{decay, 0, 0.5; 1 → level}`, `{0; level → level}` |
| `adr` | decay 0.1, decayLevel 0.7 | `{attack, 0, 1; 0 → 1}`, `{decay, 0, 0.5; 1 → decayLevel}`, `{0, 0, 0; decayLevel → 0, Sine}` (the release is the expand) |
| `adsr` | release **0.1 → 0.0** (the tool writes 0.1); the rest equal | `make_adsr`'s four, `adsrLayout_` set |

4. **Curves in the preset form are ints 0..3** (ruling 4), what the class settings were and what 113 nodes carry; the stages form keeps its strings. CLEANUP 25.
5. **The layouts live on `Envelope`, the dispatch is a table, the key list is the engine's** (ruling 5). Vibrato needs a layout without JSON; the editor needs the key list.
6. **`seed` in both forms** (ruling 6): the class path took a seed through the registry; the form path read it only in the stages block. No render in scope depends on it (the one multiplex in scope, `MPXTest3`, has an `ADSREnvelope1` with no accuracy keys); the synthetic `as_in_multiplex` pins it across binaries.
7. **The rewrite runs over every `patches/` folder but `old/`** (ruling 7): Part 3 counted and authorised the four active folders (`pending/` included — the 176 and 116 are their counts, and its originals are kept at `renders/scratch/ab35/pending_before/`); `sweep/` and `scratch/` are gitignored working material the same command keeps loadable; `old/` is the archive, left, the loader naming the tool.
8. **The loader names the tool for the seven retired names** (ruling 8), through one function in the registry that the editor's status line consults too (the plan had a loader-local list; one list for two loaders is the point). CLEANUP 27 for the duplication with the tool's `RULES`.
9. **The editor** (ruling 9): every Envelope is the stage-table node; the Preset combo with Apply (`replace_stages` under the audio mutex, then `apply_config()` so the cached knobs land on the new layout, `changed` flowing into the existing dirty flag) replaces the six create-menu entries with no new capability. The editor's new-Envelope default (`make_adsr(0.05, 0.1, 0.7, 0.2)`) and the registry's `make_ar(0.2)` differ today and are left (CLEANUP 26).
10. **`derived_label` loses the `Envelope` suffix** (ruling 10) — no name but `Envelope` ends in it, and that one must not be stripped to nothing.
11. **The generator scripts** (rulings 11, 20): ten emit `KSString`; two AR ramp drivers emit the form, and their `attackMin`/`attackMax` (keys the class never had, dead through the global allowlist) now apply as the scripts meant. Nothing in the active tree is generated by them.
12. **The tool keeps the file's indent step** (ruling 19), after a one-space baseline came back as a 584-line diff; inline objects still expand (`reed_test.json`, 51/19 lines). The gitignored `audition/`, `sweep/`, `scratch/` files keep the first run's indent 2 (whitespace only; git holds no original).
13. **The lint's selftest** (ruling 17) was passing nothing: its bad key `frequency` is in the global scrape. Fixed in Task 1's commit; what it exposes — a key any branch reads is accepted on every node, by the lint and by the loader — is Part 4's.

Decided under the 2026-11-02 rule and named in REVIEW R12 rather than asked: the pending-folder rewrite; the 59 archived nodes in `old/` that stop loading; the editor's knob groups gone for 174 nodes; `sustainLevel`'s reading on a two- or three-stage shape; the `adsr` release asymmetry; the seven-name hint list.

## 7. Debt knowingly left

- `sustainLevel` reports the four-slot view (0) on an `as`/`asr`/`ads` shape where the class reported its stored level; the knob was already inert there. A per-shape sustain slot would read truthfully (CLEANUP 22).
- The `adsr` form's `release` default 0.0 against the class's 0.1 (CLEANUP 23); the six `algev_test_*` `ar`-form nodes carrying `release`/`releaseMax` that no form reads (CLEANUP 24); ints against strings for curves across the two forms (CLEANUP 25); the editor's new-Envelope default against the registry's (CLEANUP 26); `retired_type_hint`'s seven names and the tool's `RULES` as one list in two languages (CLEANUP 27); the test file's nineteen `dynamic_cast<PitchedInstrument*>`, two new (CLEANUP 28); the loader's stages branch reading `stage_accuracy`/`ramp_accuracy` itself before the generic settings pass reads them again (CLEANUP 29).
- `replace_stages` has one caller (the editor's `apply_envelope_preset`, reached by the combo and by the headless `--apply-preset`); if Part 7 gives the combo another way to set a layout, the function goes.
- The preset form refuses a curve outside 0..3 where the deleted classes clamped it (review F6; ruling 4): the one place the form is stricter than the class it replaces; no node on disk has such a value.
- **Coverage, corrected by the re-review (N1):** the three in-scope `as` nodes (`Morph_env` in `bassoon1`, `oboe2`, `feedback/loop_hysteresis_bow`) are dead in the render — nothing reads them as a signal — so library and baselines pin `as`, 114 of the 174 rewritten nodes, only as "it loads". The in-scope rewrites a render pins are `asr`, `adsr` (twice) and `ar`; `as` is pinned for its layout by the synthetic sets (cross-binary: `as_class`, `as_in_multiplex`, the refuter's `as_level_gt1`, `as_rev_multiplex`), by the factory and form tests, and by the equivalence test at `750f70e` while the class existed. CLEANUP 30 names the dead node.
- `--apply-preset` is a headless path the gates do not yet run; `rt_smoke.py` could call it on one rewritten node (Part 8's gate work). The editor's `dynamic_cast` count is 22 (one more, that path's node lookup).
- `patches/old/`: 54 preset-class nodes and 5 alias nodes in 41 files fail to load by name; `python tools/migrate_deleted_types.py patches/old` would rewrite them (not run: the archive is Matt's). `sweep/` and `scratch/` were rewritten (gitignored; whitespace reformatted to indent 2 on the first run).
- The editor's Apply button was not clicked in a running editor (no headless path to it, no desktop control in this session); the roundtrip of four rewritten patches through the editor's load and save renders identically and the path the button calls is pinned by engine tests.
- The lint and the loader accept any key some branch reads on every node (the global allowlist); the selftest now uses a typo key. Part 4.
- The tests' `dynamic_cast<PitchedInstrument*>` count and the equivalence proof living in history (`750f70e`) rather than in the tree: the form tests hold literal tables; a reader who wants the class-against-form proof checks out that commit.
- Carried unchanged from 3.4's section 7: the three part interfaces' two conventions (3.8), `TargetEvolution`'s ranges, the four evolutions' bounds hooks, the wavetable's two first-sample blocks (3.6), the live-edit draw stream; and from earlier: `Theme`'s literals (Part 7), the `.cpp` files outside family folders (3.6), the `music/pitch_bend.h` edge (Part 5), `kFamilyOrder`, `Shelf` in the engine registry, `types_in` per frame, the Part-number comments, `denormals.h`'s encoding, `MultiSource`'s hand-written list, `Slot`'s public vectors, `nodesById` + `signals`.

## 8. Independent review

The refuter (a Fable 5.1 agent at extra-high effort, brief at `renders/scratch/ab35/review/brief_r12.md`) reviewed `750f70e`..`6a825a4` against the plan's nine review-focus items, with its own A/B of the eleven rewritten scope files plus three controls, eight synthetics of its own, five `patches/old/` copies through the tool, and three mutations in a worktree. Its report is reproduced from `renders/scratch/ab35/review/review_r12.md`, then what the wave did about each finding, then the re-review.

### The review, verbatim

Reviewer: independent refuter (Fable 5.1). Date: 2026-11-04 (HST, `date`). Tree: `6a825a4` on main, base `9f0140c`.
Everything below is a number I produced or read from a file named here. Nothing tracked was edited; the
mutation work ran in a worktree under `renders/scratch/ab35/review/wt_mut` (removed at the end). The docs
modifications `git status` shows in the main checkout (`part3-nodes.md`, `STATUS.md`, `BACKLOG.md`, `CLEANUP.md`,
`REVIEW.md`, the report under `reports/`) are the coordinator's Task 5 edits, not mine.

**Working-tree note (18:11 HST):** `git status` also shows `M tools/migrate_deleted_types.py` in the main
checkout, mtime `2026-11-04 18:10:52` — after every measurement below (the old/ A/B and the isolation ran before
18:03, `date` in the transcript) and after my worktree was removed. I did not write it; its diff adds a `dead`
key set to `Preset` that drops `attackMin`/`attackMax` on an AREnvelope and the clamps/`timeMode` on an
ADSREnvelope with a per-node note — i.e. F3's fix, uncommitted and unreviewed. Every F3 number here is against
the committed tool at `6a825a4`. The 14 tool tests I ran were also against the committed tool; whether they pass
against the working-tree version is not tested (the `Rename.apply`/`Preset.apply` signatures now take `notes`,
so `rewrite()` must have changed with them).

### 1. Verdict

**Byte-identity claim: UPHELD.** What I ran:

- `python tools/null_gate_perform_source.py --jobs 3` → `78/78 manifest entries identical` (`nullgate_r12.txt`).
- My own A/B, not the stored arms: all eleven rewritten in-scope files plus three unrewritten controls, the
  `git show 9f0140c:<patch>` copy rendered by `renders/scratch/ab_tree/cli_pre35.exe` against the tree's file
  rendered by `build/tools/mforce_cli/Release/mforce_cli.exe` → **14/14 IDENTICAL** (`ab_r12.txt`). My old-arm
  hashes match `old/hashes.txt` entry for entry where they overlap (e.g. FormantSequence1 `824c5f4a…`, MPXTest3
  `c0e4e6b2…`, BaselineSIN `83d832ba…`).
- `diff old/hashes.txt new/hashes.txt` → identical, 132 lines each.
- The checkpoint's eight class-form synthetics re-rendered cross-binary by me (`synth/` + old CLI vs
  `synth_form/` + new CLI) → **8/8 IDENTICAL**; eight synthetics of my own with knob values the checkpoint's set
  lacks (a level above 1, a decay that hits the 0.5 s clamp, release 0 → expand, decay 0 + decayLevel 1, sustain
  1.3 with release 0 on the four-slot shape, attack 0, a reversed shaped AS at accuracies 0.6/0.7 under a 3-clone
  multiplex, a Vibrato at attack 0.9) → **8/8 IDENTICAL** cross-binary (`synth_r12.txt`).
- Five `patches/old/` copies through the tool (old CLI on the original vs new CLI on the rewritten copy): 4
  IDENTICAL (`String1` ADSR×2, `Additive2` AS×2, `bow_family/bowspeed015` ASR, `ks_piano/v3_full` KSPianoString),
  **1 DIFFER** (`expand_sweep2/sweep_slow_ramp`, AREnvelope carrying `attackMin`/`attackMax` 4.0) — isolated to
  those two keys, Finding 3. Every unrewritten original fails under the new CLI with the tool's name.
- `engine_tests.exe` → `ALL PASS (7457 checks)`; `python -m unittest -b tools.tests.test_migrate_deleted_types` →
  `Ran 14 tests … OK`; `python tools/lint_patches.py --selftest` → bad 1/1, good 0/0 OK;
  `python tools/structure/check.py --delta 9f0140c` → lines −117, `dynamic_cast` +2 (both in
  `tools/engine_tests/main.cpp`, diff lines 334 and 353), everything else +0 — matches `meter35_delta.txt`.
- `python tools/test_stable_roundtrip.py patches/baselines patches/library --jobs 3` → `132 patches: 0 id
  changes, 6 NEW render diffs, 0 known diffs` — the same six as `roundtrip_t5.txt` (`roundtrip_r12.txt`).
- `mforce_ui.exe --stamp` → `exe built 11-04 17:35, commit 6a825a4, stale: no` (the roundtrip drove a current editor).

The eleven tracked rewrites equal the tool's output exactly: `json.load(tree file) == migrate.rewrite(json of
git show 9f0140c:file)` for all 11 (no hand edits rode along).

### 2. Findings

#### F1 — Major — the editor's Preset Apply writes the stale cached `sustainLevel` over the fresh ADSR's four sustain slots
`tools/mforce_ui/main.cpp:9958-9965` (Apply), `:505-509` (`apply_config`), `:1863-1864` and `:10303-10304` (the cache synced from `get_setting`).

What is wrong: after `env->replace_stages(…)` the button calls `node->apply_config()`, which pushes EVERY cached
setting back through `set_setting`, `sustainLevel` included. The cache for `sustainLevel` is synced from
`get_setting("sustainLevel")` at load (`:1864`) and after array edits (`:10304`), and `Envelope::get_setting`
reports the four-slot view — **0.0 for any shape that is not the four-slot ADSR** (`envelope.h`,
`sustain_level()`). So on every loaded `as`/`asr`/`ads`/`adr`/`ar` node — which is all 174 rewritten nodes and
every stages-form node that is not four-slot — Apply ADSR runs `make_adsr(0.2, 0.1, 0.7, 0.0)` and then
`set_setting("sustainLevel", 0.0)`, which `has_adsr_shape_()` accepts and which rewrites decay-end, expand
start/end and release-start to 0.0. The result is 0→1, 1→0, hold 0, 0→0: an AR with a dead tail, not the ADSR
the tooltip promises ("this preset with its defaults"). The plan (Task 4 step 1) said the button "re-pulls the
cached settings"; the code pushes them.

Concrete scenario: open `patches/library/winds/bassoon1.json`, select `Morph_env` (preset `as`), choose ADSR,
Apply → the stage table shows the expand row at 0.0/0.0 and the release row starting at 0.0. (Not clicked — no
headless path; the engine side of the sequence is pinned below.)

Engine-side proof of the arithmetic, run in the worktree as an added CHECK that performs exactly the editor's
sequence (`cached = get_setting("sustainLevel")` on a two-stage form, `replace_stages(adsr)`,
`set_setting("sustainLevel", cached)`, expect slot 2 start = 0.7):
```
  editor-apply probe: cached sustain 0.000; adsr expand slot after the push 0.000
FAIL ...\wt_mut\tools\engine_tests\main.cpp:6946  e->stage(2).ramp.startVal == 0.7f
```
(`wt_tests_a.txt`; the probe's source is in `mutate_r12.py`, "probe".) The cached value a two-stage node
reports is 0.000 and the push leaves the fresh ADSR's expand slot at 0.000.

Smallest fix: inside the Apply block, after `replace_stages`, re-pull instead of push —
`for (auto& [desc, val] : node->settingValues) val = env->get_setting(desc.name);` — or at least skip
`sustainLevel` in that one `apply_config` call. Test that would catch it: the probe above as a permanent engine
test is only half (it pins `replace_stages` + a push); the editor sequence itself needs a headless
`--apply-preset <patch> <node> <preset> <out>` beside `--roundtrip`, asserting the saved stages of an `as` node
after ADSR carry 0.7 in slots 1–3.

#### F2 — Major — the same `apply_config()` runs outside `g_audioMutex`
`tools/mforce_ui/main.cpp:9964` against `:9489-9493`.

What is wrong: the lock block closes after `replace_stages`; `node->apply_config()` then calls
`set_setting("sustainLevel", …)` unlocked, and `Envelope::on_setting_changed` writes `stages_[1].ramp.endVal`,
`stages_[2].ramp.startVal/endVal`, `stages_[3].ramp.startVal` while the audio thread's `next_raw_()` reads
`stages_[currStage_].ramp` on the same object. The generic settings row at `:9489-9493` takes the lock for the
identical `set_setting` call ("Under the audio lock: set_setting can rebuild internal arrays … while a stream tap
is mid-next()"), so this path is the odd one out. Consequence is four torn float stores during a playing note,
not a crash or an allocation. Smallest fix: move `node->apply_config()` (or the re-pull from F1) inside the
`lock_guard` block. Test: none at the engine level; the stamp/roundtrip cannot see it. A TSan build of the
editor would; absent that, a code-structure check that every `set_setting` call site in `main.cpp` sits in a
`g_audioMutex` scope.

#### F3 — Minor — the tool carries keys the class never read into a form that reads them; a render moves when it is run on 7 `patches/old/` files
`tools/migrate_deleted_types.py:43-57` (`Preset.apply`), rows `AREnvelope` and `ADSREnvelope` of `RULES` (`:64-72`).

What is wrong: `Preset.apply` copies every param key. `AREnvelope` had no `attackMin`/`attackMax` setting (dead
keys, let through by the global allowlist), but the `ar` form reads both into `make_ar`. `ADSREnvelope` likewise
had no clamp or `timeMode` settings; the `adsr` form reads all seven. The plan's "same rewrite, byte-identical
by construction" holds only for keys the class read. On disk today: 7 files, all under `patches/old/`
(`expand_sweep2/sweep_slow_ramp`, `expand_sweep4/inflate_r3`, `formantseq_sweep/{sweep_lohi_ramp,
vowel_glide_ramp}`, `formantseq_sweep2/{vowel_glide_flat, vowel_glide_ramp_w5, vowel_glide_ramp_w9}`), each an
AREnvelope with `attack 1.0, attackMin 4.0, attackMax 4.0` (ruling 20's generator output); no ADSREnvelope
anywhere carries a clamp or `timeMode` key (my scan over all 3,145 files, `scan_r12.txt`). Nothing in the six
rewritten folders carries the pattern, so no render moved in this checkpoint.

Measured (`ab_r12.txt`): `sweep_slow_ramp.json` original under `cli_pre35.exe` → `4eaac29c6e932eef…`;
tool-rewritten copy under the new CLI → `7e95ef7d890091a0…` (DIFFER); the same copy with the two keys removed
under the new CLI → `4eaac29c6e932eef…` (IDENTICAL). The class clamped a 1.0-fraction attack to [0, 1 s]; the
form pins it to 4 s. The loader's error message sends Matt to exactly this tool when he opens one of these.

Smallest fix: `Preset` takes a `drop` set of the keys the class had no setting for (`ar`: `attackMin`,
`attackMax`; `adsr`: the six clamps and `timeMode`) and removes them, printing the node and keys so the file's
author sees it — or refuses by name, as it does for `dynamicPins`. Ruling 20 argues the form's reading is "what
the scripts meant"; that is a choice for the generator scripts, not for a tool whose contract is render identity.
Test: an AREnvelope with `attackMin`/`attackMax` 4.0 rewrites to `{"preset": "ar", "attack": 1.0}` (or raises);
an ADSREnvelope with `timeMode: seconds` likewise.

#### F4 — Minor — the tool's refusal test pins one knob name of eleven
`tools/tests/test_migrate_deleted_types.py:88-96`.

Mutation (worktree): `LAYOUT_KNOBS` without `reverse` and `decayLevel` → `Ran 14 tests … OK`. The test asserts
the refusal for `attack` only; the other ten names in `LAYOUT_KNOBS` are unpinned. Smallest fix: loop the
`assertRaises` over `mig.LAYOUT_KNOBS` (and assert the set has eleven names). Test: that loop.

#### F5 — Minor — `tools/test_stable_roundtrip.py` reports the six backlog-79 diffs as "NEW" on every run and exits 1
`tools/test_stable_roundtrip.py:30-32` (`KNOWN_DIFFS = {"patches/baselines/FormantSequence1.json"}`).

Pre-existing (the Ledger says the six have stood since 3.2), not 3.5's doing, but the gate the plan leans on for
claim 8 is permanently red: the file's own contract ("a NEW entry here is a regression and fails the run")
fires on six patches that are accepted by eye, FormantSequence1 is listed yet reports `0 known diffs`, and a
seventh diff is caught only by someone counting to seven. Smallest fix: move the six into `KNOWN_DIFFS` with
the backlog-79 note, drop FormantSequence1 if it no longer diffs, so the run is green and a seventh fails.
Test: the run itself exiting 0.

#### F6 — Flag — a curve outside 0..3 now fails the load where the class clamped it
`engine/include/mforce/source/envelope/envelope_json.h:24-30` against the deleted classes' `on_setting_changed`
(`std::clamp(attackCurve.value, 0, 3)` before every rebuild). Ruling 4 chose the refusal; no node on disk has a
curve outside 0..3 or a non-integer curve (`scan_r12.txt`: `curve values outside 0..3 or non-numeric: []`), so
nothing moves. Recorded because it is the one place the form is stricter than the class it replaces.

#### F7 — Flag — `presetIdx` is a function-level `static`, shared by every Envelope node
`tools/mforce_ui/main.cpp:9940`. Selecting ADSR on one node shows ADSR as the combo's current value on every
other node's panel. Cosmetic; per-node state (or no persisted selection) if it bothers anyone.

### 3. The nine review-focus items

| # | Claim | What I checked | Verdict |
|---|---|---|---|
| 1 | The six layouts equal the six classes | Field-by-field read of `git show 9f0140c:…/envelope_presets.h` against `make_as/asr/ads/adr` (+`make_ar/make_adsr`) and the six builders: stage initialisers, clamps (attack 0..1, decay 0..0.5, release/expand 0,0), curve defaults (release Sine, the rest Linear), power default 0, shaped stage indices (ar 0,1; as 0; asr 0,2; ads 0,1; adr 0,1,2; adsr 0,1,3), ADR's release at percent 0, AS reverse (`startV/endV` swap), ADS/ADR decay clamp 0.5 — all agree. `set_from_float` does not clamp (`pins.h:131`), so the class and the form both take an out-of-range level raw (my `as_level_gt1`, `adsr_sustain_gt1_release0` identical cross-binary). The `adsr` seconds branch gains shaping; on disk only 3 `scratch/tweaked_loop*` adsr-form nodes carry curve keys, all at defaults. 16 synthetic cross-binary renders identical; 14 scope renders identical. | Upheld |
| 2 | `adsr` `release` 0.0 vs 0.1 | The nine on-disk ADSREnvelope nodes: MPXTest3 and reed_test from `git show 9f0140c` carry `release`; the other seven in `renders/scratch/ab35/pending_before/` (bowed_cello ×2, buzzy_bow ×2, slow_attack_horn ×2, tripower) carry `release`. 9/9. The tool's default and its test read; `adsr_class_bare` identical cross-binary. | Upheld |
| 3 | Settings after construction | Old path: `wire_params_generic` in descriptor order, each class knob → `rebuild()` from all stored knobs (pure), `is_common_knob_` = the three accuracies/timeScale only, so `sustainLevel` rebuilt AS/ASR/ADS/ADSR and was a base no-op on AR/ADR (`|| name == "sustainLevel"`). New path: the dispatch reads the same keys, then `set_setting` of the four base knobs — `sustainLevel` a no-op on 2/3 stages, an idempotent rewrite on 4. No engine reader of `get_setting("sustainLevel")` outside the editor (grep); no `dynamicPins` on any preset node (my scan agrees with `scan_t0.txt`). | Upheld (render); the editor-visible 0.0 view is what F1 trips over |
| 4 | `seed` in both forms | Old: `reg.create(type, seed)` → `set_seed` at construction, later `replace_stages` copies `seed_`; new: `set_seed` after the dispatch, before `wire_params_generic`; `on_start`/`reseed()` read `seed_` later, so order is immaterial. Both truncate the injected `int64` to its low 32 bits (`get<int>` vs `get<int64_t>`). `as_in_multiplex` and my `as_rev_multiplex` (accuracies 0.6/0.7, reversed, Expo) identical cross-binary; `run_envelope_form_loader_tests` pins `get_seed() == 77` on the form. | Upheld |
| 5 | Vibrato's ramps | `make_as(attack.value)` = `{0→1, Linear, 0, attack, 0, 1}, {1→1, Linear, 0, 0, 0, 0}` = `ASEnvelope` at its defaults (sustain 1.0, reverse false, curve 0, power 0) after `set_setting("attack")`. Vibrato's `attack` is its own Setting 0..1. Seven scope files carry a Vibrato (cello_full_range, viola_default, bassoon1, oboe1, oboe2, bow_evolution_discovery, loop_hysteresis_bow); oboe1/viola_default/bassoon1/oboe2/both baselines in my 14; `vibrato_attack_hi` (attack 0.9) identical cross-binary. | Upheld |
| 6 | The alias | Both registrations were `std::make_shared<KSString>()` with no seed and no configurator (`register_configurator` absent from `source_registrations.cpp`); the four pianos identical in my A/B; `old/ks_piano/v3_full` identical through the tool. No remaining code switch on the string outside comments and `retired_type_hint`. | Upheld |
| 7 | `derived_label`'s suffix list | `register_type("…Envelope")` matches only `"Envelope"` (79 registrations); `derived_label("Envelope") == "Envelope"` is a CHECK (`main.cpp:7745`); `kFamilyTable` row `{"Envelope", Family::Envelope, "Envelope", Shelf::Main}` (`:7668`); `walked == 79`, 57/22 pass. | Upheld |
| 8 | The editor | Roundtrip reproduced (0 id changes, the six; F5 on the gate's shape). Key filter: `envelope_preset_keys()` ∪ `stages` replaces the 13-key `kEnvelopeKeys`; the eight new names were carried as `jsonExtras` before and are now dropped on save — correct for the stages form. The combo/Apply read: F1 (wrong layout) and F2 (unlocked push). Not exercised in the running editor. | Not upheld for Apply (F1, F2); upheld for load/save |
| 9 | The tests bite | Three mutations of my own in the worktree. (a) `make_adr`'s release ramp Sine → Linear (`envelope.h`): `1/7458 FAILED` — `main.cpp:6876 stages_are(*Envelope::make_adr(0.1f, 0.2f, 0.55f), …)`, the factory table only (the form test re-shapes stage 2 from `releaseCurve`'s default, as ruling 18 says). (b) the `as` builder's `sustainLevel` default 1.0 → 0.7 (`envelope_json.h`): `1/7457 FAILED` — `main.cpp:6894 stages_are(*envelope_from_preset_json(J{{"preset", "as"}}), …)`; no render anywhere would see it (291 `as`-form nodes outside `old/`, 0 without `sustainLevel`), so the form tests are that default's only pin. (c) `LAYOUT_KNOBS` minus `reverse`, `decayLevel` (the tool): `Ran 14 tests … OK` — did not bite, F4. The Ledger's own four mutations were not re-run. | Upheld for the engine tests (both bit, one check each, where predicted); the tool's refusal test does not (F4) |

### 4. Coverage

In scope (library + baselines), the rewritten nodes exercise: `as` at `attack 0.2, attackCurve 0, attackPower 0,
reverse false, sustainLevel 1.0, accuracies 1.0` (bassoon1, oboe2, loop_hysteresis_bow — three identical
`Morph_env`s); `asr` at `attack 0.0, release 0.02, sustainLevel 1.0` (bow_evolution_discovery); `adsr` with all
four knobs and `release` present (MPXTest3 under a 3-clone multiplex with the injected seed reaching accuracies
1.0, reed_test); `ar` at `attack 0.7` (FormantSequence1, peak 0.34 — a live render); the alias on four pianos (two
with bow keys); Vibrato in seven files at their authored attacks. The 22 `ar`-form and 84 `adsr`-form baseline
nodes pin the two pre-existing forms with the new `shape()` calls at their defaults.

Only the synthetic sets exercise: `ads` and `adr` (no node of either has ever existed on disk, `old/` included);
any non-default curve or power; `reverse true`; a stored level ≠ 1.0 on AS/ASR/ADS; accuracies < 1 on a preset
node; the injected seed reaching an accuracy < 1; the bare-`adsr` release default; the clamp-biting decay;
release 0 and attack 0 layouts; sustain > 1.

Nothing exercises: the Apply button (F1/F2); a preset-class node under the editor's save after Apply; the
`adsr` seconds branch with a non-default curve (no node); the tool on `old/` beyond my five copies (F3 on seven).

### 5. Commands and outputs

All from `C:\@dev\repos\mforce`; files named are under `renders/scratch/ab35/review/` unless pathed.

```
cmake --build build --config Release --target engine_tests mforce_cli          -> exit=0 (build_r12.txt)
build/tools/engine_tests/Release/engine_tests.exe                                -> ALL PASS (7457 checks) (engine_tests_r12.txt)
python -m unittest -b tools.tests.test_migrate_deleted_types                    -> Ran 14 tests in 0.069s  OK
python tools/null_gate_perform_source.py --jobs 3                               -> 78/78 manifest entries identical (nullgate_r12.txt)
python tools/lint_patches.py --selftest                                         -> selftest bad expected 1 finding(s), got 1  OK / selftest good expected 0, got 0  OK
python tools/structure/check.py --delta 9f0140c                                 -> files -1, lines -117, dynamic_cast 60 -> 62 (+2), every other row +0 (matches meter35_delta.txt)
python tools/test_stable_roundtrip.py patches/baselines patches/library --jobs 3 -> 132 patches: 0 id changes, 6 NEW render diffs, 0 known diffs, 27 id-only (non-instrument), 0 skipped; exit=1 (roundtrip_r12.txt)
build/tools/mforce_ui/Release/mforce_ui.exe --stamp                             -> exe built : 11-04 17:35 / commit : 6a825a4 / stale : no
diff renders/scratch/ab35/old/hashes.txt renders/scratch/ab35/new/hashes.txt   -> (no output) 132 lines each
git -C . log --oneline 9f0140c..HEAD                                            -> 6a825a4 594df28 b800bec 7b326a2 750f70e 3a33ebd
```

`python renders/scratch/ab35/review/ab_r12.py` (ab_r12.txt) — scope, `git show 9f0140c:<patch>` + cli_pre35.exe vs tree + new CLI:
```
FormantSequence1 rewritten IDENTICAL 824c5f4a49f02385 | MPXTest3 rewritten IDENTICAL c0e4e6b2910a9576 | bow_evolution_discovery rewritten IDENTICAL c5bf0585ffb48d55
feedback/loop_hysteresis_bow rewritten IDENTICAL db1bac655270d5d3 | reed_test rewritten IDENTICAL 266243bf463ec22f
piano_bright IDENTICAL ac453b85766b837d | piano_dark IDENTICAL b468270941c03ed9 | piano_default IDENTICAL decb02c5d007e979 | piano_seg IDENTICAL 2df338dd32ce9bb6
bassoon1 IDENTICAL 3bfab91032eaff4a | oboe2 IDENTICAL 3f9cf513ad3c2b90 | oboe1 (unchanged) IDENTICAL 73a304f4eb99c1f9 | viola_default (unchanged) IDENTICAL a42669dcfdc14965 | BaselineSIN (unchanged) IDENTICAL 83d832ba95e393f5
old/ copies through the tool: sweep_slow_ramp DIFFER old 4eaac29c6e932eef new 7e95ef7d890091a0 | String1 IDENTICAL | Additive2 IDENTICAL | bowspeed015 IDENTICAL | ks_piano/v3_full IDENTICAL
every unrewritten original under the new CLI: rc=1 ERROR: Unknown node type: <name>; python tools/migrate_deleted_types.py <patch> rewrites a type this engine no longer has
isolation: sweep_slow_ramp copy with attackMin/attackMax removed, new CLI -> 4eaac29c6e932eef (= the original under the old CLI)
```

`python renders/scratch/ab35/review/synth_r12.py` (synth_r12.txt) — class form + cli_pre35.exe vs tool-rewritten copy + new CLI:
```
as_level_gt1 IDENTICAL | ads_decay_clamp IDENTICAL | asr_release0 IDENTICAL | adr_level1_decay0 IDENTICAL
adsr_sustain_gt1_release0 IDENTICAL | ar_attack0 IDENTICAL | as_rev_multiplex IDENTICAL | vibrato_attack_hi IDENTICAL
the checkpoint's eight (synth/ vs synth_form/): adr ads adsr adsr_bare ar as as_in_multiplex asr -> 8/8 IDENTICAL
```

`python renders/scratch/ab35/review/scan_r12.py` (scan_r12.txt), every `patches/` folder, 3,145 files parsed:
```
remaining preset-class nodes: old/ only — ADSREnvelope 16, AREnvelope 7, ASEnvelope 29, ASREnvelope 2 (KSPianoString 5 per the tool's --check)
ar/adsr FORM nodes carrying curve/power keys: 3 (scratch/tweaked_loop{,2,3}.json, all at the defaults 0/0.0/0/0.0/3/0.0)
curve values outside 0..3 or non-numeric: []   fractional curve values: {}   reverse non-bool: []
ADSREnvelope class nodes with clamp/timeMode keys: []   AREnvelope class nodes with clamp keys: 7 (all old/, attack 1.0 attackMin 4.0 attackMax 4.0)
Envelope preset-form nodes with seed: 0   preset-form nodes with non-default accuracy/timeScale: 0
```

Tracked rewrites vs the tool: 11/11 `EQUAL` (`json.load(tree) == migrate.rewrite(json(git show 9f0140c:file))`).
Claim 2 originals: MPXTest3 and reed_test at 9f0140c carry `release`; `pending_before/` bowed_cello ×2, buzzy_bow ×2,
slow_attack_horn ×2, tripower carry `release` — 9/9. Generator scripts: the 12 edited `tools/gen_*.py` parse (`ast.parse`); not executed.

Mutations (worktree `wt_mut` at 6a825a4, own `build_mut/`, configured with `-DRTMIDI_TARGETNAME_UNINSTALL=rt_uninstall`-style rename; removed afterwards):
```
(a) make_adr release Sine->Linear + the Apply probe   -> 2/7458 FAILED: main.cpp:6876 (factory ADR row); main.cpp:6946 e->stage(2).ramp.startVal == 0.7f  [probe: cached sustain 0.000; expand slot 0.000]  (wt_tests_a.txt)
(b) as builder sustainLevel default 1.0 -> 0.7        -> 1/7457 FAILED: main.cpp:6894 (form `as` default row)  (wt_tests_b.txt)
(c) LAYOUT_KNOBS minus reverse, decayLevel (tool)     -> Ran 14 tests  OK  (did not bite)
```

Not tested: the Apply button in the running editor; the Ledger's four mutations; `python tools/gates.py --fast`;
`tools/ab_render_time.py` (read `timing35_run1.txt`: 0.95–1.07× across the twelve rows, no per-sample path changed);
`tools/alloc_gate.py`; the generator scripts' execution.

## What was done about each finding (the wave, tests first)

| Finding | Severity | Done | Pinned by |
|---|---|---|---|
| F1 — Apply pushed the cached `sustainLevel` (0 on a two- or three-stage node) over the fresh ADSR's four slots | Major | **Fixed.** The button's body is one function, `apply_envelope_preset(node, env, name)`: `replace_stages` then a *re-pull* of every cached setting from the new layout (`val = env.get_setting(desc.name)`); no `apply_config`, no push on the path. | A headless twin, `mforce_ui --apply-preset <patch> <node> <preset> <out>`, beside `--roundtrip` and `--rename`, calling the same function: `bassoon1.json Morph_env adsr` saves four stages `{0.2: 0→1 Linear}, {0.1: 1→0.7 Linear}, {0: 0.7→0.7}, {0: 0.7→0 Sine}`; `reed_test.json breathEnv asr` saves three `{0.2: 0→1}, {0: 1→1}, {0.1: 1→0 Sine}`; `adsx` fails "Unknown envelope preset: adsx" (`renders/scratch/ab35/review/wave_apply_*.json`). Engine side, `run_envelope_rebuild_pin_tests` pins the view the re-pull reads: `get_setting("sustainLevel")` is 0 after `replace_stages(make_as)` and 0.7 after `replace_stages(make_adsr(…0.7…))`, slot 2 at 0.7. |
| F2 — that push ran outside `g_audioMutex` | Major | **Fixed** by the same function: the lock is held across `replace_stages` and the re-pull (reads); nothing on the path calls `set_setting`. | Reading; the stamp and the headless twin exercise the function. |
| F3 — the tool carried keys a class never read into a form that reads them (seven `old/` files would move) | Minor | **Fixed.** `Preset` takes `dead`: `ar` drops `attackMin`/`attackMax`, `adsr` drops the six clamps and `timeMode`; each drop is printed by node and key. The review's distinction is accepted and recorded as ruling 22: the generator scripts emit the form on purpose (ruling 20 stands for them), the tool's contract is render identity. The six rewritten folders carried no such key (the refuter's scan of 3,145 files; `--check` still 0 pending), so nothing on disk changes. | Unit tests: an `AREnvelope` with `attackMin`/`attackMax` 4.0 rewrites to `{"preset": "ar", "attack": 1.0}` with the note; an `ADSREnvelope` with `decayMax` and `timeMode` loses both; an `ASREnvelope`'s `release` stays (15 tests). |
| F4 — the refusal test pinned `attack` alone | Minor | **Fixed.** The test loops `assertRaises` over all eleven `LAYOUT_KNOBS` and asserts the set's size; `sustainLevel`, `stage_accuracy`, `timeScale` pass through. | The loop (the refuter's own mutation, `reverse` and `decayLevel` removed, now fails). |
| F5 — the whole-corpus roundtrip gate permanently red: the backlog-79 six reported as NEW, `FormantSequence1` listed but not diffing | Minor, pre-existing | **Fixed.** `KNOWN_DIFFS` is the six with the backlog-79 note; `FormantSequence1` removed (it renders identically now). The run exits 0 with "6 known diffs"; a seventh fails it. | The run itself (section 5). |
| F6 — a curve outside 0..3 fails the load where the classes clamped | Flag | **Stands** (ruling 4; no node on disk has one, the refuter's scan). Named in section 7 as the one place the form is stricter than the class it replaces. | — |
| F7 — `presetIdx` a function-level `static` shared by every node | Flag | **Fixed.** The combo's choice is per node (`presetByNode`, keyed by the node id, default `adsr`). | Reading. |

One cost of the wave: the headless path's `dynamic_cast<Envelope*>` is one more in the editor (21 → 22; the meter's checkpoint delta +3, all in tests and the editor, the engine 15 → 15). Not taken from the review: adding `--apply-preset` to `rt_smoke.py` (Part 8's gate work); a TSan build of the editor.

### The re-review, verbatim

Reviewer: the same refuter. Date: 2026-11-04 HST. Tree: `b1d0222` on main (`git log`), six files changed against
`6a825a4`: `tools/mforce_ui/main.cpp`, `tools/migrate_deleted_types.py`, `tools/tests/test_migrate_deleted_types.py`,
`tools/test_stable_roundtrip.py`, `tools/engine_tests/main.cpp`, `tools/structure/tests/test_structure.py` — no
`engine/` file (`git diff --stat 6a825a4 b1d0222`). Nothing tracked edited by me; the one mutation ran in a worktree
`wt_rr` (removed). `mforce_ui.exe --stamp`: `exe built 11-04 18:13, commit b1d0222, stale: no`, so the headless runs
below drove the committed editor.

### 1. Verdict

**Closeable.** F1–F5 and F7 are fixed as described and I exercised the fixes myself; F6 stands by ruling. Nothing the
wave touched can move a render in scope: the engine is untouched (`mforce_cli.exe` was not even relinked by the
rebuild), the null gate is 78/78 at `b1d0222`, the tool's `--check` over the six rewritten folders is `0 node(s)
pending`, and the editor's save path is render-identical to the tree for the four live rewritten in-scope nodes
(roundtrip hash == tree hash, below). New findings are Flags only; the one worth reading is N1, a correction to the
coverage picture (mine included): the three in-scope `as` nodes are dead in the render.

### 2. The seven findings

| # | Was | Wave | What I ran / read | Verdict |
|---|---|---|---|---|
| F1 | Major — Apply pushed the cached `sustainLevel` (0 on a non-four-slot node) over the fresh ADSR | `apply_envelope_preset(node, env, name)`: `replace_stages` then `val = env.get_setting(desc.name)` for every cached setting; no `apply_config`, no `set_setting` (`main.cpp:969-980`); the button calls it (`:9973`); `--apply-preset <patch> <label> <preset> <out>` calls the same function (`:12941-12962`); `run_envelope_rebuild_pin_tests` pins the view (`get_setting` 0 after `make_as`, 0.7 after `make_adsr(…0.7…)`, slot 2 = 0.7). | Headless, nodes of my choosing (`apply_rr.txt`): a stages-form node (`piano_default` `Hammer_click_env` → `asr`: 3 stages `0.2:0→1 L`, `0:1→1`, `0.1:1→0 Sine`); an adsr-form node (`reed_test` `breathEnv` → `ar`: 2 stages; → `adsr`: the form's defaults 0.2/0.1/**0.7**/0.0 with slots 1–3 at 0.7, not the node's 0.9 — "defaults, not the node's values", as the tooltip says); an `as` node (`bassoon1` `Morph_env` → `ads`, → `adr`); `bow_evolution_discovery` `ASREnvelope` → `ads`, `MPXTest3` `ADSREnvelope1` → `ar`, `FormantSequence1` `AREnvelope1` → `adsr`. Every saved file loads and renders in the CLI; in every case the nodes other than the target are identical to a plain `--roundtrip` of the same patch (JSON compare). `engine_tests` ALL PASS (7460). | Fixed |
| F2 | Major — that push ran outside `g_audioMutex` | The lock is held across `replace_stages` and the re-pull; the path has no `set_setting`. | Read (`main.cpp:975-979`). Not exercisable headlessly beyond the function running; no TSan build. | Fixed (by reading) |
| F3 | Minor — the tool carried keys the class never read (`attackMin`/`attackMax` on AR; clamps + `timeMode` on ADSR) | `Preset(dead=…)`: `ar` drops the two, `adsr` drops the seven, each printed by node and key (`migrate_deleted_types.py:66-71, 76-82`); `rewrite(doc, notes)`; 15 tests. | Fresh copy of `old/expand_sweep2/sweep_slow_ramp.json` through the tool: note printed (`node 'menv' (AREnvelope): attackMax, attackMin dropped …`), `menv` → `{"preset": "ar", "attack": 1.0}`, render under the new CLI `4eaac29c6e932eef` = the original under `cli_pre35.exe`. `--check` over `audition baselines library pending sweep scratch` → `0 node(s) pending`, exit 0; over `old/` → 59 in 41. Mutation (worktree): `ar`'s `dead` loses `attackMax` → `FAILED (failures=1)` in `test_keys_the_class_never_read_are_dropped_with_a_note` (`{'attack': 1.0, 'attackMax': 4.0, 'preset': 'ar'} != {'attack': 1.0, 'preset': 'ar'}`). | Fixed; bites |
| F4 | Minor — the refusal test pinned `attack` alone | `test_a_dynamic_pin_on_any_layout_knob_is_refused_by_name`: `assertEqual(len(LAYOUT_KNOBS), 11)` then `assertRaises` per knob; three non-layout names pass through. | Read; `Ran 15 tests … OK`. My earlier mutation (two names removed) would now fail the size assert; not re-run. | Fixed (by reading) |
| F5 | Minor — the roundtrip gate permanently red, `FormantSequence1` listed but not diffing | `KNOWN_DIFFS` = the backlog-79 six; label "known diff (backlog 79)". | My run at `b1d0222`: `python tools/test_stable_roundtrip.py patches/baselines patches/library --jobs 3` → six lines `known diff (backlog 79) …`, then `132 patches: 0 id changes, 0 NEW render diffs, 6 known diffs, 27 id-only (non-instrument), 0 skipped`, exit 0 (`roundtrip_rr.txt`). | Fixed; the gate is green and a seventh diff fails it |
| F6 | Flag — a curve outside 0..3 refused where the classes clamped | Stands under ruling 4; named in the report's section 7. | — | Stands, agreed |
| F7 | Flag — `presetIdx` shared by every node | `presetByNode` (`unordered_map<int,int>` keyed by `node->id`, default adsr) (`main.cpp:9953-9954`). | Read. Not clicked. | Fixed (by reading) |

Also reproduced: `python -m unittest -b tools.tests.test_migrate_deleted_types` → `Ran 15 tests … OK`;
`python tools/lint_patches.py --selftest` → OK/OK; `python tools/structure/check.py --delta 9f0140c` → lines −80,
`dynamic_cast` 60 → 63 (+3), `duplicate_blocks` −5, every other row as `meter35_delta_final.txt`;
`python tools/null_gate_perform_source.py --jobs 3` → `78/78 manifest entries identical` (`nullgate_rr.txt`).
Refusals of the headless path: a Vibrato (`Vib`) → `rc=1 apply-preset: no Envelope node 'Vib'`; `NoSuchNode` → the
same message; `adsx` → `rc=1 apply-preset failed: Unknown envelope preset: adsx`; `AS` → the same (N2).

### 3. New findings (none above Flag)

#### N1 — Flag (coverage correction) — the three in-scope `as` nodes are dead in the render
`patches/library/winds/bassoon1.json`, `patches/library/winds/oboe2.json`,
`patches/baselines/feedback/loop_hysteresis_bow.json`, node `Morph_env` in each. Nothing references the node as a
signal (a JSON walk finds `"Morph_env"` only as its own `id`, in the UI group "Drive"'s member list and in the
positions map); the CLI hash of bassoon1 is `3bfab91032eaff4a` with `Morph_env` as `as` (tree), as `ads`
(`asform_to_ads.json`) and as `adr` (`asform_to_adr.json`). So library + baselines pin the `as` form only as "it
loads", not by bytes; the live in-scope class→form pins are `asr` (bow_evolution_discovery, hash moves
`c5bf0585…` → `c31af161…` when its layout changes), `adsr` (MPXTest3 `c0e4e6b2…` → `debae305…`; reed_test
`266243bf…` → `2cc2cc2a…`) and `ar` (FormantSequence1 `824c5f4a…` → `061b515c…`). The `as` layout — the 114-node
majority of the rewrite — is pinned by renders only in the synthetic sets (`as_class`, `as_in_multiplex`, my
`as_level_gt1`, `as_rev_multiplex`, all identical cross-binary) and by the form/factory tests. My R12 coverage
section said the three `Morph_env`s exercise `as` in scope; they do not. Nothing to fix in code; the checkpoint
report's coverage sentence should say "loads" for `as`, and `Morph_env` is a candidate for the CLEANUP list
(a dead node in two library patches and a baseline).

#### N2 — Flag — `--apply-preset` takes the engine's lower-case names while the combo shows upper-case labels
`tools/mforce_ui/main.cpp:12956` passes `argv[4]` straight to `envelope_from_preset_json`; `AS` is refused
("Unknown envelope preset: AS") although the UI shows `AS`. Lower-casing `argv[4]` (or matching the label) would
make the twin accept what the user sees. Taste.

#### N3 — Flag — the tool's drop note is not ASCII
`tools/migrate_deleted_types.py:78`: the note's em dash printed as `�` in my Git Bash run (Python's stdout under
the console code page, a UTF-8 terminal). Every other line the tool prints is ASCII. An ASCII dash avoids it.

#### N4 — Flag — `renders/scratch/ab35/roundtrip_wave.txt` carries the pre-wave label
Its six lines read `known diff (backlog 3n)`; the committed script prints `known diff (backlog 79)`. The evidence
file was produced before the label edit in the same commit (counts and exit are the current script's). My run is
the current output (F5 row).

Not tested: a mutation of `apply_envelope_preset` itself (the editor would have to be built in a worktree: ~140 MB
of vendored imgui/glfw/rt* to copy plus a UI link); the Apply button under a running audio stream; duplicate node
labels against the headless lookup's last-match-wins loop (`main.cpp:12952`).

### 4. Files

`apply_rr.py` / `apply_rr.txt` and `apply_rr/` (the saved JSONs, their roundtrip twins and WAVs), `engine_tests_rr.txt`,
`nullgate_rr.txt`, `roundtrip_rr.txt`, `build_rr.txt`, `oldcopies/sweep_slow_ramp_rr.json`,
`wav/old_sweep_slow_ramp_rr_new.wav` — all under `renders/scratch/ab35/review/`.

## After the re-review

The re-review found the seven findings closed and nothing introduced, and raised four Flags: N1 (a coverage correction), N2 and N3 (two one-line fixes, taken), N4 (a stale label in a scratch log). What was done:

| Flag | Done |
|---|---|
| N1 — the three in-scope `as` nodes (`Morph_env` in `bassoon1`, `oboe2`, `feedback/loop_hysteresis_bow`) are dead in the render: nothing reads them as a signal, and `bassoon1`'s hash is the same whether `Morph_env` is `as`, `ads` or `adr` | **Accepted; the coverage claim is corrected here and in section 7.** The in-scope class→form rewrites that a render pins are `asr` (`bow_evolution_discovery`), `adsr` (`MPXTest3`, `reed_test`) and `ar` (`FormantSequence1`); `as` — 114 of the 174 nodes — is pinned in scope only as "it loads", and for its layout by the synthetic sets (cross-binary, `as_class` and `as_in_multiplex`; the refuter's `as_level_gt1` and `as_rev_multiplex`) and the factory and form tests. The equivalence test at `750f70e` covered it while the class existed. `Morph_env` is a dead node in two library patches and a baseline: CLEANUP 30. |
| N2 — `--apply-preset` refused an upper-case name ("AS") where the combo shows upper-case labels | **Fixed:** the headless path lower-cases the name; `bassoon1 Morph_env AS` applies. |
| N3 — the tool's drop note carried an em dash that a non-UTF-8 console printed as "�" | **Fixed:** the note is ASCII; the unit test asserts it. |
| N4 — `renders/scratch/ab35/roundtrip_wave.txt` shows the pre-wave label "backlog 3n" | A scratch log written before the label edit in the same commit; the re-review's own run shows "backlog 79". Left as is. |

The two fixes are the commit after the wave (with the meter's known-answer pin for `main`); no engine file changed, so the null gate and the A/B stand.

## 9. Reading guide

Ten minutes: section 1 (what moved and the one correction to Part 3), the table in section 6 item 3 (the six layouts and the one default that differs), section 5's gates table and A/B paragraph. Twenty more: the plan's Ledger rulings 1, 17, 18 and 19 (the design call, the selftest that passed nothing, the mutation the form masked, the indent), section 8's findings and the wave. The code, in reading order: `envelope.h` (the seven factories together), `envelope_json.h` (the table and the six builders, 147 lines), `patch_loader.cpp`'s Envelope branch, `source_registry.cpp`'s `retired_type_hint`, `tools/migrate_deleted_types.py`, and in the tests `run_envelope_factory_tests`, `run_envelope_form_tests`, `run_envelope_form_loader_tests`, `run_retired_type_message_tests`.

## 10. C++ lessons

### Lesson 1: a table of function pointers as a dispatch

`kEnvelopePresets` is an array of `{const char* name; std::shared_ptr<Envelope> (*build)(const nlohmann::json&);}` — the second member is a *pointer to a function* taking a JSON and returning an envelope, and `&envelope_form::as` is such a pointer. `envelope_from_preset_json` walks the array and calls `e.build(p)`. The alternatives were the if-chain that stood (fine for two names, a ladder for six) and a virtual method on a `Preset` class hierarchy (six classes again, which is what we were deleting). A function pointer carries no state and costs one indirect call at load time; `std::function` would also hold lambdas with captures, at the price of a heap allocation and type erasure we do not need. The C# analogue is a `Dictionary<string, Func<JsonElement, Envelope>>` of static methods — the same shape, with the delegate doing what the pointer does. Why a table and not a `switch` on the name: the table is data the editor can read too (the combo lists `kEnvelopePresets[i].name`), so the six names exist in one place.

### Lesson 2: static factories versus subclasses to name a layout

`ADSREnvelope : Envelope` and `Envelope::make_adsr(...)` both answer "give me an ADSR". A subclass earns its place when the *behaviour* differs — a different `compute()`, a different lifecycle — and the base's virtual calls are what select it at run time. Here nothing behaved differently: every preset class rendered with the base's `compute()` over a stage list; what differed was *data*, the list. A static factory is a named constructor: `Envelope::make_as(0.3f)` reads as "an AS envelope", returns the base type, and leaves nothing for a `dynamic_cast` or a `type_name()` to distinguish. The rule of thumb Matt asked for in the primer: inheritance when the verb changes, a factory (or a table) when only the noun's initial state does. C#: `public static Envelope CreateAdsr(...)` on the class, versus `class AdsrEnvelope : Envelope` — the same choice, and C# programmers reach for the static method more readily because there is no header to put a subclass in.

### Lesson 3: `inline constexpr` at namespace scope

`inline constexpr EnvelopePreset kEnvelopePresets[] = {...}` lives in a header included by three translation units (the loader, the editor, the tests). Before C++17 a namespace-scope array in a header either had to be `static` (one private copy per translation unit, three arrays) or declared `extern` and defined in one `.cpp`. `inline` on a variable (C++17) means what it means on a function: every translation unit sees the same definition and the linker keeps one. `constexpr` says the table is a compile-time constant — the function pointers are constant expressions because the functions are `inline` and their addresses are known. C# has nothing to say here: a `static readonly` field is one object by construction; C++ makes you ask where the bytes live because a header is text pasted into each file that includes it.

### Lesson 4: the most vexing parse, met in a test

`std::vector<float> out(size_t(n));` compiled as a *function declaration* — `out` is a function taking a `size_t` named `n` and returning a vector — and the next line's `out.data()` failed with "left of '.data' must have class/struct/union". Any declaration that can be read as a function prototype is one; `size_t(n)` is a parameter named `n` in redundant parentheses. The fixes: `std::vector<float> out(static_cast<size_t>(n));` (a cast expression cannot be a parameter declaration), or brace initialisation `std::vector<float> out{...}` — with care, since braces on a vector mean an initialiser list of elements, so `out{n}` is a one-element vector. C# has no such ambiguity (`new List<float>(n)`); C++ inherits it from C's declaration syntax. The take-away is the error message: when "must have class type" appears on a variable you just declared, read the declaration as a prototype.

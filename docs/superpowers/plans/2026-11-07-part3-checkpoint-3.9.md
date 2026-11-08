# Part 3, checkpoint 3.9: the formant family — Implementation Plan

**Goal:** the formant half of CLEANUP 17, as Part 3 section 11 decided it with Matt on 2026-11-07: `Formant` is the abstract base with one verb `gain(freq)`; `FormantPeaks` holds its peaks as rows and replaces `FormantSpectrum` together with the per-peak `Formant` node; `FormantBands` is `BandSpectrum` renamed; `FixedSpectrum` and `IFormant` and `contains()` are gone; `FormantSequence` keeps its name and takes any `Formant`; the lifecycle the two part bases share lives once, in `Part`; every patch in the tree outside `old/` is rewritten by a tool; the editor loses its node-to-row bridge; no render in `patches/library` + `patches/baselines` moves.

**Architecture:** Part 3 section 11 (the design, decided) and section 2 (the parts). Two things the survey adds. First, `PartialSet` (3.8) and the new `Formant` base would each own the same pin lifecycle — `prepare` and `start` over every pin, `next` over the Param pins listed at prepare, then the hooks — so that lifecycle moves up once into **`Part : Component`** (`core/part.h`) and both derive from it; writing it twice is the copy the 11-02 "never a third copy" rule forbids before it exists (ruling 1). `WaveEvolution` keeps its own shape for now (its prepare carries the owner's table length; CLEANUP). Second, **dropping `contains()` is byte-identical only if each new `gain` returns 0 exactly where the old `contains` was false**, which three of the four implementers already do and one does not: `BandSpectrum::get_gain` for a frequency inside one bin *below* `startFreq` truncates a negative position to bin 0 and interpolates with a negative fraction, returning a non-zero gain where `contains` said no; `FormantBands::gain` therefore folds the old predicate in first (ruling 4). With that, the consumer's `contains ? floor + weight × gain : floor` becomes `floor + weight × gain`, identical to the bit except when `formantFloor` is a negative zero and the partial is out of band (the factor's zero changes sign) — unreachable from any patch in scope, whose floors are constants parsed as +0; listed as the one departure by construction (ruling 5). **`FormantPeaks`** carries four `Array` pins (`frequency`, `gain`, `width`, `power`, one group, as `ExplicitPartials` groups its four) and caches each row's band edges at prepare and on an array change, so `gain(freq)` is `FormantSpectrum`'s max over the rows of `Formant::get_gain`'s formula, verbatim, with no per-sample pin read (ruling 3). The survey: in scope 68 `FormantSpectrum` nodes in 60 files hold 335 `Formant` children, every child a `Formant` with constant params and no second referrer; `FormantSequence` (3 nodes) holds only `FormantSpectrum`s; `AdditiveSource.formant` points at a `FormantSpectrum` (57), a `BandSpectrum` (3) or a `FormantSequence` (3), never a bare `Formant`; `FixedSpectrum` is in no patch anywhere; outside scope and `old/` there are 10 more `FormantSpectrum` files (44 peaks) and one more `FormantSequence`. The editor's bridge is about 250 lines of `tools/mforce_ui/main.cpp` (a `FormantRow` struct and `formantRows` per node; the load-time consumption of sole-referrer `Formant` children into rows; `rebuild_formant_spectrum` building child DSP from rows; the save-time re-emission; the row-table UI; the paste path; the "not convertible" rule) plus a hand mirror of the peak formula in the formant strip; all of it goes, and the generic grouped-array table and `gain(f)` sampling take over.

**Execution (the 11-06 rule):** Fable wrote this plan and executes the design-bearing tasks — `Part` (Task 1), `FormantPeaks` with its twin (Task 2), the base, the consumer and the two rebased leaves (Task 4) — and refutes at the close (the editor and the tool are where the gates are blind, and the Fable window reset on 11-07); Opus 5.5 executors take the synthetic set (Task 0), the tool and the tree rewrite (Task 3), the editor (Task 5) and the close's measurements (Task 6), each verified by the coordinator (null gate rerun, body-diff where text moved, one mutation of each twin) before its commit. Written 2026-11-07 at xhigh (Matt: "Plan at xhigh").

**Tech Stack:** C++20 (MSVC, CMake 3.20+, `/fp:precise`), `engine_tests` (`same_bits`, `CHECK`, `PinCounter`, `load_fails_naming`, `render_one_live_note`, `alloc_probe`), `python tools/gates.py`, `null_gate_perform_source.py`, `ab_render_set.py`, `ab_render_time.py`, `alloc_gate.py`, `structure/check.py`, `test_stable_roundtrip.py`, `migrate_deleted_types.py`, `git mv`.

**Spec:** `docs/architecture/part3-nodes.md` section 11 (quoted in the Goal), section 2 (the parts), section 10 row 3.9; CLEANUP 17 (closed by this checkpoint), CLEANUP 36 (the editor's hand lists shrink here, not vanish), 3.1 review item 4(e) (`BandSpectrum`'s configurator sets `gains` the generic array pass already sets — it goes); 3.4 ruling 1 and 3.8 ruling 1 (the part-base shape); 3.5's migration tool and tree-wide rewrite (the pattern for Task 3); 3.8 ruling 8 (a render that moves by deletion is REVIEW's, none here). Matt, 2026-11-02: easily-tweaked decisions are the session's.

## Global Constraints

- **Byte-identity is total in scope**: `python tools/null_gate_perform_source.py --jobs 3` → 78/78 after EVERY task (the manifest's files are rewritten in place at Task 3 and their hashes must not move); the A/B at the close over the 132 (`renders/scratch/ab38/library_baselines.txt` plus `patches/baselines/additive_bright_attack_test.json`) against the pre-3.9 renders of the UNREWRITTEN files → 0 DIFFER. The one listed departure is ruling 5's negative-zero factor, proved unreachable in scope by the twin's own scan.
- **Amendment at Task 0 (ruling 12, Matt's decision "2")**: EIGHT in-scope renders MOVE by the fold — the library vowels whose first peak's `frequency` the instrument paramMap drove through a curve (`sing_alto_A`, `sing_alto_U`, `sing_bass_U`, `sing_soprano_A`, `sing_soprano_E`, `sing_tenor_O`, `sing_tenor_U`, `speech_w_AW`). They are not failures but the checkpoint's listed departures: the null gate manifest takes their new hashes in the Task 3 commit (78/78 against the updated manifest from then on), the close A/B expects exactly those eight DIFFER and nothing else, the WAV pairs (pre-3.9 CLI over the unrewritten file; the close build over the rewritten file) sit under `renders/scratch/ab39/review/`, and REVIEW R15 lists them as renders for Matt's ears.
- **Every moved expression keeps its operations and order**: the peak formula, the max, the band interpolation, the crossfade; each new `gain` is 0 exactly where the old `contains` was false; a twin per implementer proves it, and the reviewer mutates each.
- **The real-time rule**: `FormantPeaks` caches its band edges at `prepare` and on `on_array_changed` (control side) and allocates nothing in `gain`; `python tools/alloc_gate.py --jobs 3 patches/library patches/baselines` → 0 allocate after every task.
- **One type per file, the file named after the type**; `include_violations` +0; `unmapped_files` 0; `dynamic_cast` +0 in the engine (the editor's one cast for the strip stays, retyped).
- **No dated changelog, backlog id, REVIEW, Part or checkpoint number in code comments, file names or folder names.**
- **The tree rewrite is 3.5's shape**: the tool runs over every patch folder except `old/`; `patches/pending/` originals are kept under `renders/scratch/ab39/pending_before/`; `old/` is left to fail by name with the hint; the tool is idempotent and `--check` is green afterwards.
- **Commit after every task; `gates --fast` before every commit**; the UI relinked before a stamp run.
- **Work on main, no worktree.** Edits through script files (CRLF/LF mix); a failed build's stale binary is not a pass.
- **Dates:** `date` first; this plan is dated 2026-11-07.

## Review Focus

The refuter's oracle. Each row names the task that pins it.

1. **`Part` is the lifecycle `PartialSet` had, moved, not rewritten** (Task 1): `prepare`/`start` over `pins()`, the Param list filled at prepare into the fixed array, `next` over the list; `PartialSet` keeps only its verbs; the lifecycle test still passes unchanged; a `PinCounter` test on a `Formant` leaf shows the same counts (Task 4).
2. **`FormantPeaks` is `FormantSpectrum` over `Formant`s, bit for bit** (Task 2): the twin retypes `Formant::get_gain` (band edges from `frequency ± width/2`, the two power ramps) and `FormantSpectrum::get_gain` (max over containing children, 0 if none) and compares `same_bits` against `FormantPeaks::gain` over rows × frequencies including both band edges, a gain-0 row, overlapping rows and a single row; the reviewer names a mutation the twin would miss and performs one (an edge comparison made inclusive; the max made a sum; a row's power applied to the wrong ramp).
3. **Every new `gain` is 0 where the old `contains` was false** (Tasks 2 and 4): `FormantBands` for a frequency in the sub-bin below `startFreq`, at `startFreq`, at the span's end and one bin beyond; `FormantSequence` where only one neighbour contains; `FormantPeaks` outside every row. The twins compare `old.contains(f) ? old.get_gain(f) : 0` against `new.gain(f)`.
4. **The consumer's factor** (Task 4): `floor + weight × gain` against `contains ? floor + weight × gain : floor` over floor ∈ {+0, −0, 0.2, 1}, weight ∈ {0, 0.5, 1}, gain ∈ {0, 0.3, 1}: identical except the (−0, out-of-band) rows, which differ in the zero's sign only; the twin counts them and the Ledger records the count; the survey found every in-scope `formantFloor` a constant (58 at 0.0, 3 at 1.0, none wired); the reviewer confirms, and says whether an `Envelope` could emit −0 if one were wired.
5. **The tool** (Task 3): the fold rewrites each `FormantSpectrum` and its children into one `FormantPeaks` whose four arrays are the children's constants in `formants` order; a child referenced twice, a child with a wired pin, a non-`Formant` child, a `Formant` referenced by anything but a `FormantSpectrum`, and a `FixedSpectrum` are each refused by name; `BandSpectrum` is renamed; a second run changes nothing; the tree's 69 + 3 + 4 files are rewritten and `--check` is green; `old/` fails by name with the hint.
6. **The editor** (Task 5): every `FormantRow`/`formantRows` site, the consumption, the re-emission, the row-table UI, the paste path and the strip's mirror are gone (`grep -c FormantRow` 0); a vowel loads into one `FormantPeaks` node whose four arrays show in the generic grouped table; editing a row's frequency changes the strip; `test_stable_roundtrip.py` over the 60 vowel files → 0 NEW render diffs; the stamp current.
7. **The record and the clock** (Task 6): the meter (`files` +0 net, `include_violations` +0, `dynamic_cast` +0 in the engine, the UI's `main.cpp` shorter by the bridge); timing over the vowels (`FormantPeaks` reads no pin per sample where `Formant::next` re-read four per peak; a speed-up or flat is expected, a slow-down is a finding); the Ledger's notes against the diff.
8. **The ten-item brief** (`docs/architecture/REVIEW_BRIEF.md`) over the whole diff, with line citations.

---

## File structure

| Old | New |
|---|---|
| the lifecycle inside `additive/partial_set.h` (`prepare`, `start`, `next`, `params_`, `paramCount_`, `kMaxParams`, the three hooks) | `core/part.h`: `struct Part : Component` — the owner-driven part's lifecycle, moved verbatim: `void prepare(const RenderContext&)`, `void start(int)`, `void next()`, the protected hooks `on_prepare`/`on_start`/`on_next`, the private Param list. `PartialSet : Part` keeps `partial_count`, `get_partial_value`, `sum_partials`. `WaveEvolution` unchanged (CLEANUP: its `prepare(ctx, maxLen)` and `start` could sit on `Part` with the length passed another way) |
| `additive/formant.h` (`IFormant` pure; `Formant` the peak) | `additive/formant.h`: `struct Formant : Part` — abstract; one pure verb `virtual float gain(float freq) const = 0`; the file comment says what a formant is (section 11's first paragraph). The peak type is deleted (its formula lives in `FormantPeaks`) |
| `additive/formant_spectrum.h` (`FormantSpectrum`, a multi-slot of `IFormant`) + the peak nodes | `additive/formant_peaks.h`: `struct FormantPeaks final : Formant` — `Array frequency{this, "frequency", "peaks", 1000.0f, 1.0f, 20000.0f}`, `Array gain{this, "gain", "peaks", 1.0f, 0.0f, 10.0f}`, `Array width{this, "width", "peaks", 500.0f, 1.0f, 10000.0f}`, `Array power{this, "power", "peaks", 2.0f, 0.01f, 10.0f}` (the peak's pin ranges, one group `"peaks"`); `on_prepare` and `on_array_changed` call `cache_edges()` (sizes `lo_`, `hi_` to the row count, `lo = f − w·0.5`, `hi = f + w·0.5`, the peak's `next()` arithmetic); `gain(freq)`: `float g = 0; for each row i: if (freq > lo_[i] && freq < hi_[i]) g = std::max(g, peak_gain(i, freq)); return g;` with `peak_gain` the deleted `Formant::get_gain` body verbatim (`freq < f ? pow((freq − lo)/(f − lo), p) × gain : pow((hi − freq)/(hi − f), p) × gain`). Ragged arrays (unequal lengths) fail the load naming the node (ruling 6); the editor keeps them equal as it does for `ExplicitPartials`. `formant_spectrum.h` deleted |
| `additive/band_spectrum.h` (`BandSpectrum`) | `git mv` → `additive/formant_bands.h`: `struct FormantBands final : Formant` — the two Manual Params and the `gains` array as they are; `gain(freq)` = the old predicate first (`if (!(freq >= sf && freq <= sf + fi × (n − 1))) return 0.0f;`) then `get_gain`'s body verbatim; the base advances `startFreq`, `freqIncrement` (declaration order, as `next()` did); the registry configurator that set `gains` goes (the generic array pass sets it: 3.1 review item 4(e)) |
| `additive/fixed_spectrum.h` (`FixedSpectrum`) | deleted (no patch anywhere; its registration and configurator; its smoke and part tests) |
| `additive/formant_sequence.h` (`FormantSequence : Component, IFormant`) | `struct FormantSequence final : Formant` — `Slot<Formant> spectra{this, "spectra", "a Formant part", true}`; `on_prepare`/`on_start`/`on_next` forward to the children (the base advances `blend` first, as `next()` did, then `on_next` advances the children and sets `fPtr_`/`fWeight_`); `gain(freq)` = `g0 × (1 − w) + g1 × w` over the children's `gain` (which are 0 where their `contains` was false); the registry configurator refusing the old `formants` key stays |
| `additive/additive_source.h` / `.cpp` | `Slot<Formant> formant{this, "formant", "a Formant part"}`; `wave()`: `Formant* fmt = formant.get(); if (fmt) { fmt->next(); … }` as today; `partials.h`'s body: `float fmtFactor = 1.0f; if (formant) fmtFactor = fmtFloor + fmtWt * formant->gain(pfreq);` (ruling 5); the pins `formant`, `formantWeight`, `formantFloor` keep their names |
| `additive/partial_set.h`, `partials.h`, `composite_partials.h` | `partial_set.h`: `IFormant*` → `Formant*` in the three verbs; `PartialSet : Part`; the lifecycle gone from it. `partials.h`: the body's factor (above); `IFormant*` → `Formant*`. `composite_partials.h`: the signature |
| `src/source/source_registrations.cpp` | `Formant`, `FormantSpectrum`, `FixedSpectrum` unregistered; `BandSpectrum` → `FormantBands`; `FormantPeaks` registered (Family Additive, the derived label "Formant Peaks"); 77 → 75 registrations |
| `src/source_registry.cpp`, `retired_type_hint` | `"Formant"` and `"FormantSpectrum"` → "deleted; the peaks are rows of a FormantPeaks (tools/migrate_deleted_types.py rewrites a FormantSpectrum and its Formants)"; `"FixedSpectrum"` → "deleted; author a FormantBands or FormantPeaks"; `"BandSpectrum"` → "renamed FormantBands (the tool rewrites it)" |
| `tools/migrate_deleted_types.py` | a file-level pass `fold_formant_spectra(doc)` before the per-node rules: for each `FormantSpectrum` with a ref-array `formants`, every ref must name a `Formant` node whose params are all numbers (no `{"ref"}`), referenced by nothing else in the document; the node becomes `{"id", "type": "FormantPeaks", "params": {"frequency": [...], "gain": [...], "width": [...], "power": [...]}}` in `formants` order (a missing key takes the peak's default: 1000 / 1 / 500 / 2), the children are removed from the node list; anything else is refused by name. Per-node rules: `BandSpectrum` → `Rename("FormantBands")`; `FixedSpectrum` → `Refuse(…)`; a `Formant` the fold did not consume → `Refuse("a peak outside a FormantSpectrum: author a one-row FormantPeaks")`; a `FormantSpectrum` the fold refused → its reason. Docstring rows; unit tests (`tools/tests/test_migrate_deleted_types.py`): the fold on a three-peak vowel (arrays, order, children gone, `formant` ref unchanged), a twice-referenced child refused, a wired child refused, a ragged default filled, idempotence, `--check` |
| `tools/mforce_ui/main.cpp` | the bridge deleted: `struct FormantRow`, `formantRows`, the default rows at build (lines ~425–427), `rebuild_formant_spectrum`, the `"formants"` face-pin skip, the load-time ref count and consumption (~1562–1700), the save-time re-emission (two sites), the row-table UI (~9846–9910), the paste path (~10252–10326), the "owned Formants are not convertible" rule; the strip (`~12039–12146`) casts `dynamic_cast<Formant*>` and samples `gain(f)` for every kind (the `formant_gain_at` mirror and the row-tuple overlay go; the snapshot overlay stays only if the generic array snapshot already serves `Array` pins — else it goes, an editor-visible change the report names); `is_formant_type` and `pin_type_compat_error`'s list → `FormantPeaks`, `FormantBands`, `FormantSequence` |
| `tools/engine_tests/main.cpp` | the smoke and split tests on the deleted types go (`make_split_formant` becomes a one-row `FormantPeaks` maker so the formant split cases stay); `run_additive_formant_pin_tests` and `run_formant_part_tests` rewritten for the three leaves; `kPartTypes` 23 → 21 (`Formant`, `FormantSpectrum`, `FixedSpectrum`, `BandSpectrum` out; `FormantPeaks`, `FormantBands` in); the registry counts 77 → 75; the `kFamilyTable` rows; four twins (Review Focus 2–4) and the `Part` lifecycle test on a leaf |
| Patches (Task 3) | 69 files rewritten by the fold (60 in scope), 3 renamed `FormantBands`, the 4 `FormantSequence` files follow their children; `pending/` originals kept; `old/` untouched |

---

### Task 0: Date, base, the old arm, the synthetic set

- [ ] `date`; sweep stale `mforce_ui_*.exe`; build; `gates --fast`. Copy the CLI → `renders/scratch/ab_tree/cli_pre39.exe`. The old arm is ab38's new arm (no engine file changed since `268fb0a`): `renders/scratch/ab38/new/hashes.txt` → `renders/scratch/ab39/old/hashes.txt` plus the replacement baseline's line (sha `7ceff853…`); the list `renders/scratch/ab39/library_baselines.txt` = ab38's 131 + `patches/baselines/additive_bright_attack_test.json` = 132. Record the sha.
- [ ] **Opus 5.5:** `renders/scratch/ab39/synth/make_synth.py` writes, in today's forms, two notes each: `bands_edges` (a `BandSpectrum` with `startFreq` 300, `freqIncrement` 150, eight gains, under a `FullPartials` whose partials fall below `startFreq`, inside the sub-bin below it, at the span's end and beyond); `bands_moving` (`startFreq` wired to an AR envelope); `peaks_overlap` (a `FormantSpectrum` of three overlapping `Formant`s, one with gain 0, powers 0.5 / 2 / 6); `peaks_one` (one peak); `sequence_mixed` (a `FormantSequence` over a `FormantSpectrum` and a `BandSpectrum`, `blend` wired — the generality); `floor_weight` (a vowel with `formantFloor` 0.2, `formantWeight` 0.8); `floor_env` (`formantFloor` wired to an envelope). Rendered with `cli_pre39.exe` → `synth/old/hashes.txt`. After Task 3 the tool rewrites the synthetics too; the new arm renders the rewritten files.
- [ ] The scan: every `formantFloor` in scope (constant or wired), for ruling 5.

### Task 1: `Part` (Fable)

- [ ] **RED first:** a `Part` lifecycle test on a tiny test-local part with two Params and a Setting: prepared/started once each, advanced once per `next`, in declaration order; fails to compile until `core/part.h` exists.
- [ ] `core/part.h` with the lifecycle moved verbatim from `partial_set.h`; `PartialSet : Part` keeps the three verbs; the module map needs nothing (`core/` is a prefix).
- [ ] Build; ALL PASS (the 3.8 lifecycle test unchanged); null gate 78/78; synthetics 0 DIFFER; `gates --fast`. Commit: "core: Part, the owner-driven part's lifecycle, once; PartialSet keeps its verbs".

### Task 2: `FormantPeaks` beside the old types (Fable)

- [ ] **RED first:** `run_formant_peaks_twin_tests` — `Formant::get_gain` and `FormantSpectrum::get_gain` retyped as lambdas over rows, against `FormantPeaks::gain`, `same_bits` over rows × 2,000 frequencies (log-spaced 20 Hz–20 kHz plus every row's exact band edges), the row sets of Review Focus 2; fails to compile until the type exists.
- [ ] `additive/formant_peaks.h` as the table gives (still `: Component, IFormant` in this task, with `contains` = "any row contains", so it can stand in the old slot); registered; the load refuses ragged arrays by name (a test); the editor shows its four arrays in the generic grouped table with no code (verify by loading a hand-rewritten vowel — `renders/scratch/ab39/synth/vowel_peaks.json`, `sing_alto_A` folded by hand — and rendering it identical to `sing_alto_A`).
- [ ] Build; ALL PASS; mutate (an edge inclusive; the max a sum) → the twin fails; restore. Null gate 78/78; synthetics 0 DIFFER; `alloc_gate` 0; `gates --fast`. Commit: "additive: FormantPeaks, the peaks as rows; a FormantSpectrum over Formants bit for bit".

### Task 3: The tool and the tree (Opus 5.5)

- [ ] The fold pass, the three rules, the docstring, the unit tests (the table); `python tools/migrate_deleted_types.py patches/library patches/baselines patches/audition patches/pending patches/sweep patches/scratch` after copying `patches/pending/` to `renders/scratch/ab39/pending_before/`; `--check` over the same → exit 0; `--check patches/old` → the refusals, by name.
- [ ] Null gate 78/78 over the rewritten manifest files (the engine still has both forms; the hashes must not move); `python tools/ab_render_set.py render renders/scratch/ab39/library_baselines.txt renders/scratch/ab39 new` → compare with `old` → 0 DIFFER over 132 (old files, old CLI; rewritten files, the Task 2 build); the synthetics rewritten and compared likewise; `python tools/lint_patches.py` clean; `git add -u patches` (the rewritten tracked files; untracked folders stay untracked). Commit: "patches: every FormantSpectrum and its Formants folded into a FormantPeaks, BandSpectrum renamed FormantBands, by the tool".

### Task 4: `Formant` the base; the leaves rebased; the deletions (Fable)

- [ ] **RED first:** `run_formant_bands_twin_tests` (the predicate-folded `gain` against `contains ? get_gain : 0`, the frequencies of Review Focus 3, `startFreq`/`freqIncrement` moved between samples), `run_formant_sequence_twin_tests` (the crossfade against the old form over two children of different kinds and blends 0, 0.37, 0.5, 1), `run_formant_factor_twin_tests` (Review Focus 4, counting the −0 rows), and a `Part` lifecycle test on `FormantBands` (`PinCounter`s on its two pins: 1/1/n in declaration order); they name `Formant` as a base, so they fail to compile until it is one.
- [ ] `formant.h` → the base alone; the peak `Formant` class, `FormantSpectrum`, `FixedSpectrum`, `IFormant` and `contains` deleted; `git mv band_spectrum.h formant_bands.h` with the class renamed and the predicate folded; `FormantSequence : Formant`; `FormantPeaks : Formant` (its `contains` gone); the slot types; the consumer's factor; registrations (75), `retired_type_hint`; the tests (the table); `kPartTypes` 21.
- [ ] Build; ALL PASS; mutate each new twin (the fold's `>=` made `>`; the crossfade's weights swapped; the factor's `+` made the old conditional) → each fails; restore. Null gate 78/78; synthetics 0 DIFFER; `alloc_gate` 0; `gates --fast`; `grep -rn "IFormant\|FormantSpectrum\|FixedSpectrum\|BandSpectrum" engine tools --include=*.h --include=*.cpp` → only the hints and the tool. Commit: "additive: Formant is the abstract base with one verb; FormantBands and FormantSequence rebased; the peak node, FormantSpectrum, FixedSpectrum, IFormant and contains() gone".

### Task 5: The editor (Opus 5.5)

- [ ] The bridge deleted and the strip retyped as the table gives; the hand lists; build (the UI relinked); `python tools/test_stable_roundtrip.py` over the 60 vowel files → 0 NEW render diffs; a headless check that `sing_alto_A` loads to one `FormantPeaks` node with four arrays of five (`--dump` or the roundtrip's own report); the stamp current; `grep -c "FormantRow\|formantRows\|formant_gain_at\|rebuild_formant_spectrum" tools/mforce_ui/main.cpp` → 0.
- [ ] `gates --fast`. Commit: "editor: the FormantSpectrum node-to-row bridge is gone; FormantPeaks shows its rows in the grouped table and the strip samples gain(f)".

### Task 6: Close

- [ ] **Opus 5.5:** the A/B over the 132 against `old` → 0 DIFFER; the synthetics; `alloc_gate` 0; `structure/check.py --delta <Task 0 sha>` then `--snapshot`; timing (`ab_render_time.py`, three rounds, plus the paired script over the vowels `sing_alto_A`, `sing_bass_A`, `speech_m_AH`, `words/liar1`, `FormantSequence1`, `fadd_band_test`, `viola_default`).
- [ ] **Fable refuter** (Agent, read-only; brief at `renders/scratch/ab39/review/brief_r15.md`: the eight Review Focus items, the table, the Ledger; the editor and the tool are the blind surfaces); the fix wave; the same refuter's re-review by SendMessage.
- [ ] Docs: report `docs/autonomy/refactor/reports/<date>-checkpoint-3.9.md` (ten sections; section 1 — nothing audible; the editor's one visible change (a vowel is one node; the formant strip's overlay if it went); section 10's lessons: pure virtual versus abstract base, the third time, and why `Part` exists now; "equivalent" predicates and byte-identity (the sub-bin case); negative zero again, as a departure listed rather than hidden; a file-level migration pass versus a per-node rule); Part 3 section 11's landed paragraph and row 3.9; REVIEW R15 (nothing to decide unless the strip overlay went — then one editor-visible item under the 11-02 rule, named); BACKLOG row 2 (3.9 DONE; Part 4 next); CLEANUP 17 closed, 36 updated, a row for `WaveEvolution` onto `Part`; STATUS; the Ledger. Commit.

---

## Ledger

Rulings made in planning; execution notes are appended per task.

1. **`Part : Component` carries the owner-driven lifecycle once.** `PartialSet` has it since 3.8; a `Formant` base needs the same three verbs, the same Param list, the same hooks. Two copies would be the copy the 11-02 rule forbids and the forgotten-override class 3.4 named, one level up. The name: Part 3 section 2 calls these types "the parts" (a `Partials` or an evolution "is a part that plugs into a slot, not a signal"); the collision with "Part 3" is in prose only. Alternative `DrivenPart`; rejected as saying what every part is. `WaveEvolution` stays as it is: its `prepare(ctx, maxLen)` carries the owner's table length and its per-sample verb is `evolve`, not `next`; folding it onto `Part` is a CLEANUP row, not this checkpoint's.
2. **`Formant` is the base's name** (Matt, 2026-11-07): the peak type that held it is deleted, so there is no collision; the plurals `FormantPeaks` and `FormantBands` are Matt's; `FormantSequence` keeps its name (no "Morph").
3. **The peaks are rows, with their band edges cached.** `Formant::next()` re-read four pins into a centre, two edges and a power every sample for every peak; the rows are constants, so the edges are computed at `prepare` and whenever an array changes (control side, under the editor's lock) and `gain` reads them. Same arithmetic (`f − w × 0.5`, `f + w × 0.5`), same comparisons (`>`, `<`), same two ramps, same `max`. The per-peak pins go: no patch anywhere wires one (379 peaks, 0 wired).
4. **Each new `gain` returns 0 exactly where the old `contains` was false**, by folding the predicate in where the old `get_gain` did not already imply it. The one implementer where it did not: `BandSpectrum::get_gain` below `startFreq` within one bin (`int(pos)` truncates −0.5 to 0, the interpolation runs with a negative fraction). Without the fold, dropping `contains` would move every render whose partials fall just below a band spectrum's start — `fadd_band_test`, the one in-scope band spectrum, starts at 100 Hz, and whether a partial of its notes falls inside the bin below that is the Task 0 scan's to say; the twin covers the case regardless, and without the fold it would be a correctness trap for any patch that does. The twin's frequency grid includes that sub-bin.
5. **The consumer's factor is `floor + weight × gain` unconditionally**, and the one way it differs from `contains ? floor + weight × gain : floor` is the sign of a zero: when `formantFloor` is −0 and the partial is out of band, `−0 + weight × 0` is `+0` where the old branch gave `−0`. Every `formantFloor` in scope is a constant — 58 at 0.0 (parsed +0) and 3 at 1.0 — and none is wired (the 2026-11-07 survey; Task 0 re-checks); the twin counts the differing rows and the Ledger records the count as the checkpoint's one departure by construction (3.3's EKS precedent), with the Task 0 scan as the proof it is unreachable in scope.
6. **Ragged peak arrays fail the load by name.** `FormantPeaks` with `frequency`, `gain`, `width`, `power` of different lengths is an authoring error, not a silent truncation; the editor keeps the four equal (`ExplicitPartials`' table already does this for its four).
7. **`BandSpectrum`'s configurator goes with the rename.** It set `gains` from the JSON that the generic array pass already set (3.1 review item 4(e)); the same values twice. `FormantSequence`'s configurator refusing the old `formants` key stays: a patch still saying so must not load in silence.
8. **The migration is a file-level fold, not a per-node rule**, because a `FormantSpectrum`'s rewrite consumes other nodes. The pass runs before the per-node rules so a consumed `Formant` is never seen by them; a `Formant` the fold did not consume (referenced by anything else, wired, or standing alone) is refused by name, since no in-scope patch has one and the right rewrite for a stray peak is a human's. 3.5's conventions: indent and key order kept, `pending/` originals kept, `old/` left to fail by name.
9. **Byte-identity is proved on both sides of the rewrite**: the null gate over the rewritten manifest files (the engine still holding both forms at Task 3, the hashes unmoved), and the close A/B with the old arm rendered from the unrewritten files by the pre-3.9 CLI (ab38's new arm, reused: no engine change since `268fb0a`) against the rewritten files on the close build — 3.5's shape.
10. **The editor loses the bridge; what the generic table gives is what the editor shows.** The row table was hand-written for `FormantSpectrum`; `FormantPeaks`' four `Array` pins fall into the grouped-array table `ExplicitPartials` already uses. If the formant strip's start/end snapshot overlay depended on the row tuples and the generic array snapshot does not serve it, the overlay goes and the report names it as the one editor-visible change (reversible; the 11-02 rule; Part 7 decides whether a part has an overlay at all).
11. **Execution per the 11-06 rule**: Fable on Tasks 1, 2 and 4 and the refuter; Opus 5.5 on Tasks 0 (the set), 3, 5 and 6's runs; the coordinator reruns the null gate and mutates each twin before every commit. The Fable window reset on 2026-11-07 (5 percent at planning).
12. **The driven peak (Matt, 2026-11-07, at Task 0: "2").** The 11-07 survey counted a peak's pin as wired only when its JSON value was a `{"ref"}`; eight in-scope library vowels and two `patches/sweep/vowel_pitch_control/` files drive the first peak's `frequency` through the instrument paramMap (`{"target": "f1.frequency", "curve": [[c, c] … [1100, 1100]]}`, which `build_bindings` wires live to a `CurveNode` over the voice's `freqOut`: F1 follows f0 above the curve's knee). Three ways were put to Matt — a slot on `FormantPeaks` for a peak that must stay a node, with the four-pin peak surviving under a new name; rows only, the eight vowels losing the live F1; stop and redesign — and Matt chose rows only: the decided design stands unchanged, no new peak type and no slot. The fold writes the curve's floor (the first knot's output, which in every one of the ten files is also the peak's own constant: 800, 325, 350, 800, 350, 400, 350, 804, 350, 350 Hz) as the row's frequency and removes the paramMap entry that targeted the peak, so the file loads; the tool reports every such rewrite by file and node ("paramMap target folded to a constant") and refuses a file whose first knot's output differs from the peak's constant (that shape is a human's); the two sweep files are rewritten the same way, originals kept. The eight renders move and are REVIEW R15's (ruling 12's amendment in Global Constraints). The capability lost from the library — F1 following f0 through a paramMap curve — goes to the report's section 7 and to IDEAS: spectrum-wide offset/scale pins on `FormantPeaks` (Part 3 §11's later addition) would restore pitch-following for all peaks at once, after the freeze.

### Execution notes

**Task 0 (2026-11-07, Fable 5.1 xhigh; base `857d1b4`).** `date` 2026-11-07;
no stale `mforce_ui_*.exe`; the editor not running. The Release build was
clean and did not relink the CLI (no engine or tools file has changed since
`268fb0a`; the binary is dated 11-06 15:56), so `cli_pre39.exe` is that
binary (sha `e8566d11…`) and ab38's new arm is this checkpoint's old arm:
`renders/scratch/ab38/new/hashes.txt` → `renders/scratch/ab39/old/hashes.txt`
plus the replacement baseline's manifest line (`7ceff853…`), 132 lines; the
A/B list `renders/scratch/ab39/library_baselines.txt` is ab38's 131 plus
`patches/baselines/additive_bright_attack_test.json`, 132. `gates --fast` on
the base: ctest 5/5, the stamp current, the allocation gate PASS (128 s), the
roundtrip smoke FAIL on the known wiring diff (5/6 shapes). The scan (ruling
5): 63 in-scope `AdditiveSource`s hold a formant; `formantFloor` is 0.0 on 58,
1.0 explicit on 2 (`cello_full_range`, `viola_default`), absent (the 1.0
default) on 3 (`fadd_band_test`, `fadd_formant_test`, `FormantSequence1`);
none wired by a ref and no paramMap entry names one — every floor in scope is
a constant parsed as +0 or 1.0, so the negative-zero factor is unreachable.
The sub-bin (ruling 4): `fadd_band_test` renders a fixed 100 Hz fundamental
over a band starting at 100 Hz, so no in-scope partial falls in the bin below
a band's start; the fold is kept for the twin and for any patch that does.

The synthetic set (an Opus 5.5 executor): `renders/scratch/ab39/synth/
make_synth.py` wrote nine — `bands_edges` (mixer mode, 150 Hz over a band
300..1350 in eight gains, so the partials sit at 150 in the sub-bin, 300 at
the start, 1350 at the end and 1500–1800 beyond), `bands_edges_floor0`,
`bands_edges_notes` (the two-note form), `bands_moving` (`startFreq` on an
AR envelope 200–400), `peaks_overlap` (three overlapping peaks, one at gain
0, powers 0.5/2/6), `peaks_one`, `sequence_mixed` (a `FormantSequence` over a
spectrum and a band, `blend` on an envelope), `floor_weight` (sing_alto_A's
five peaks, floor 0.2, weight 0.8), `floor_env` (floor on an envelope) — and
nine formant-off controls; `cli_pre39.exe` rendered 9/9 and 9/9; every
synthetic's hash differs from its control's (`activity.txt`). Old arm
`synth/old/hashes.txt`. The executor also wrote `synth/vowel_peaks.json`,
`sing_alto_A` folded by hand into one `FormantPeaks` (spec's five rows), not
rendered.

**THE SURVEY GAP (found by the executor, confirmed by a scan of the tree):**
the 11-07 survey counted a peak's pin as wired only when its JSON value was a
`{"ref"}`. Eight in-scope library vowels — `sing_alto_A`, `sing_alto_U`,
`sing_bass_U`, `sing_soprano_A`, `sing_soprano_E`, `sing_tenor_O`,
`sing_tenor_U`, `speech_w_AW` — and two `patches/sweep/vowel_pitch_control/`
files drive the first peak's `frequency` through the instrument paramMap
(`{"target": "f1.frequency", "curve": [[800, 800] … [1100, 1100]]}`), which
`build_bindings` (matrix case 3/4) wires to a `CurveNode` over the voice's
`freqOut`: F1 is a live per-sample signal in those renders. The decided
`FormantPeaks` has no per-row pin, so the plan's fold would turn `f1` into a
row and the load would fail at `paramMap: unknown node 'f1'` (the null gate
at Task 3 would have caught it). A question went to Matt through the
coordinator session before any fold: (1) `FormantPeaks` keeps a multi slot
for a peak that is a node because something drives its pins, the four-pin
peak surviving as the smallest `Formant` leaf under a new name, the fold
putting constants into rows and a driven peak into the slot (byte-identical:
max is order-free); (2) rows only, the eight vowels lose the live F1, eight
REVIEW renders; (3) stop after Task 1 and redesign with him. Tasks 1 and 2
do not depend on the answer and proceed; Task 3's fold rule and Task 4's
deletion wait on it.

**Task 1 (Fable).** RED watched: the lifecycle test alone, including
`core/part.h`, failed the test build with C1083. GREEN found a collision the
survey missed: `mforce::Part` already exists — the music model's part
(`engine/include/mforce/music/structure.h`, a voice line across the Piece),
in the same namespace, and `patch_loader.cpp` sees both — so the first build
failed with C2011 (`'mforce::Part': 'struct' type redefinition`) and every
`Setting<float>` in `Partials` lost its conversion to `Component*`. Ruling 1
said the collision with "Part 3" was in prose only; it is in code. **Decided
under the 11-02 rule: the base is `DrivenPart`** (`core/driven_part.h`), the
plan's own alternative, rejected then only as redundant ("every part is
driven"); with `Part` taken, the redundancy is the price of a name that says
what the base does (the owner drives it through prepare, start and next).
The music `Part` keeps its name (Matt's domain word). A rename is one regex
if Matt prefers another; CLEANUP gets the row. Then: `core/driven_part.h`
(57 lines: the three non-virtual verbs over `pins()`, the three hooks, the
Param list) with the lifecycle moved from `partial_set.h`, the body-diff
against `857d1b4` showing one difference, the assert's wording ("a part
declares more Param pins than DrivenPart::kMaxParams"); `PartialSet :
DrivenPart` keeps its three work verbs (71 → 48 lines); the test
(`run_driven_part_lifecycle_tests`, a test-local `TwoPinPart` with a Setting
between two Params: counts 1/1/5 through the base, a second prepare relisting
without doubling, declaration order with the setting skipped, the unwired
constants). Proofs: `engine_tests` ALL PASS 7588 → 7603; null gate 78/78;
the nine synthetics identical to the pre-3.9 arm (9/9 rendered, 0 DIFFER);
`gates --fast` as before (ctest 5/5, the stamp current, the allocation gate
PASS, the known wiring diff). Harness note: the turn was stopped while a
build ran, a second build started beside it, and the two collided on the
UI's PDB (C1041, `/FS`); a third pass over the finished tree was clean.
Ruling 12 (Matt's "2") and the Global Constraints amendment were written at
this task; the Task 3 brief carries the paramMap fold rule.

**Task 2 (Fable).** RED watched: the twin, the load tests and the live
comparison alone failed the test build with C1083 (`formant_peaks.h`). GREEN:
`additive/formant_peaks.h` (80 lines; `FormantPeaks final : Component,
IFormant` for this task — the four `Array` pins in one group "peaks" with the
peak's ranges; `prepare` and `on_array_changed` call `cache_edges()`, which
sizes `lo_`/`hi_` to the shortest of the four arrays and writes `f − w × 0.5f`
and `f + w × 0.5f`; `contains` is any row's `freq > lo && freq < hi`;
`get_gain` is the max over the containing rows of `peak_gain`, the deleted
peak's `get_gain` body verbatim over the cached edges and the row's
frequency, power and gain, every operand a float so `std::pow(float, float)`
is the overload the peak called). Registered after `ExpandRule` with a
configurator that refuses four arrays of unequal length by name (a missing
array counts as none; four missing is an empty formant); label derived
"Formant Peaks"; `kPartTypes` 24, `kFamilyTable` Main 56, `kMenuTable`,
walked 78 for this task. Tests: `run_formant_peaks_twin_tests` (the peak's
`get_gain`/`contains` and the spectrum's max retyped over rows, five row
sets — one row, three overlapping with a gain-0 row and powers 0.5/2/6, a
vowel's five, two equal rows, the editor's default four — over 2,000
log-spaced frequencies plus every row's exact edges, the floats just inside
them and the centre: 10,075 rows, `contains` and `get_gain` both bit for bit;
an array change re-caching the edges), `run_formant_peaks_load_tests` (the
ragged load named "node 'pk' (FormantPeaks)" with the four lengths, a missing
array refused, two rows loading and rendering, an edited row reaching the
next note through prepare, an empty node gaining 0),
`run_formant_peaks_against_spectrum_tests` (a live `FormantSpectrum` over
peak nodes against the rows, the same grid; deleted with the old types at
Task 4). The editor: the hand-folded vowel `renders/scratch/ab39/synth/
vowel_peaks_E.json` (`sing_alto_E`, whose paramMap drives no peak) renders
to `107c4404…`, `sing_alto_E`'s old-arm hash; `mforce_ui --roundtrip` over it
writes one `FormantPeaks` node with four arrays of five and the roundtripped
file renders to the same hash — the generic grouped table carries the rows
with no editor code. Proofs: `engine_tests` ALL PASS 7603 → 7667; null gate
78/78; the nine synthetics identical (9/9, 0 DIFFER). Mutations, each caught
by the twin (8054) and the live comparison (8137) and nothing else: the
`contains` lower edge made inclusive; the max made a sum; the rising ramp's
numerator swapped for the falling one. Two mutations the reviewer might name
are unobservable by construction and are recorded instead: `get_gain`'s
outer edge comparison made inclusive is masked by the verbatim inner guard
(`freq <= lo` returns 0), and a swapped DENOMINATOR is invisible because
every row is symmetric (`f − lo == hi − f`); the twin's grid holds both exact
edges and the floats just inside them so the edge class is pinned where it is
observable. `gates --fast`: the first run FAILED the structure meter's
known-answer test (`test_loader_allowlist_covers_the_lint_scrape`: `width`
"a key the loader reads but its allowlist lacks") because the configurator
named its four keys as string literals and `lint_patches.py` scrapes
literals out of configurator code as loader keys; the configurator was
rewritten over the node's own `array_descriptors()` — no literal, and the
rule is generic ("takes its arrays at one length, one row per peak", the
message listing each array's length) — rather than adding an array key to
`kBranchKeys`. Rebuilt: ALL PASS 7667, null gate 78/78 again, `gates --fast`
as before (ctest 5/5, the stamp current, the allocation gate PASS, the known
wiring diff).

## Self-review notes

- Task 2 lands `FormantPeaks` beside the old types so the tool (Task 3) can rewrite the tree while both forms load; Task 4 then deletes the old types. The null gate at Task 3 therefore runs the new engine over the new files, and the close A/B runs old-over-old against new-over-new — the two together pin the rewrite and the engine.
- `Formant` cannot be both the peak and the base in one tree, which is why the peak class dies in Task 4, after the tree no longer names it (Task 3), and not before.
- `make_split_formant` in the tests builds a `Formant` peak for the 2.2-era split tests; it becomes a one-row `FormantPeaks` maker so those cases keep running.
- The strip's `formant_gain_at` mirror existed because the row tuples were not DSP until rebuilt; with the rows as `Array` pins on the live node, `gain(f)` on the node is the truth and the mirror has no reason to exist.
- `kBranchKeys` keeps `formants` (the `FormantSequence` configurator reads it to refuse) and `gains` (`FormantBands`' array, a descriptor — the scrape says).

# Part 3, checkpoint 3.6: one type per file — Implementation Plan

**Goal:** Every node header in `engine/include/mforce/source/` holds one type, named after it, in its family's folder; the family `.cpp` files sit under `engine/src/source/<family>/`; the shaped-draw sequence that white and red noise each carry becomes one helper; no render in `patches/library` + `patches/baselines` moves, and the meter's `files_over_limit` loses `wave_evolution.h`.

**Architecture:** Part 3 section 8's rule ("one type per file, the file named after the type, grouped in a folder per family") applied to the six headers that still hold several types — `wavetable/wave_evolution.h` (14 types, 962 lines), `noise/noise_sources.h` (6), `noise/wander_noise_source.h` (3), `filter/filters.h` (4), `combiner/combined_source.h` (5, two of them in the wrong family), `additive/formant.h` (6) — with `additive/partials.h` left for 3.8 (section 7) and `physical/ks_string.h` left as one class (section 8). Nothing is rewritten: a type's text moves to its own file with the includes it uses, the old header is deleted (no umbrella), and every include site names the file it needs. File names are the type's name in snake case, as the tree already does (`white_noise_source.h`, `hybrid_ks_source.h`): `pluck_evolution.h`, `bw_lowpass_filter.h`, `crossfade_source.h`. Two types change family folder because 3.2 changed their family: `DistortedSource` to `filter/`, `StaticVarSource` and `StaticRangeSource` to `utility/`. The five family `.cpp` files and `source_registrations.cpp` move under `engine/src/source/<family>/` and `engine/src/source/`, the module map's `source` module becomes two prefixes, CMake's list follows. The one piece of arithmetic is section 8's "shaping block copied between white and red noise": the ordered draw sequence `decide(density)` → `decide(zeroCrossTendency)` → sign flip or fresh sign → `range(boost, 1) * sign` → `range(last, v, last, min(continuity, 0.999))` that `WhiteNoiseSource::compute` and `RedNoiseSource`'s ramp target both perform on their `Randomizer`, differing in whether `boost` is clamped first and which "last value" feeds continuity; it becomes one free function in `noise/shaped_draw.h` that both call, proved by a twin test (3.3's method) over a deterministic parameter walk, bit for bit. Byte-identity is by construction everywhere else — the compiler sees the same tokens — and the null gate after every task, the A/B at the close and the meter are the proof.

**Execution (the three tiers, 2026-11-04):** this plan is Fable's; the tasks are Sonnet 5.5 executors' (one per task, in sequence, each with its gates), under a `claude-opus-4-8` coordinator (Matt, 2026-11-04: Fable is held for 3.7's plan and refuter and for 3.8; never Opus 5/5.5); the close review is an Opus 4.8 reviewer with this plan's tables as the oracle, mutation-testing the twin, reading every moved type's file and folder against the table and every include site; no Fable refuter and no re-review for this checkpoint (the gates are complete proofs for a move; 3.5's budget note). The weekly Fable window was at 79 percent when this plan was written; execution starts after its reset (2026-11-07).

**Tech Stack:** C++20 (MSVC, CMake 3.20+, `/fp:precise`), the `engine_tests` harness (`twin_signal`, `same_bits`, `same_value`, `CHECK`), `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/ab_render_set.py`, `python tools/structure/check.py` (`--delta`, `--snapshot`, `--modules-md`), `git mv`.

**Spec:** `docs/architecture/part3-nodes.md` section 8 (quoted above; "`wave_evolution.h` becomes the `WaveEvolution` base (landed in 3.4) and one file per evolution"; "`noise_sources.h` becomes one file per noise type; the shaping block copied between white and red noise goes to one place"; "`filters.h` becomes the Butterworth section, the three Butterworth filters, and the delay filter" — the section landed in `core/dsp` at 3.3, so three filters and the delay filter remain; "`partials.h` as in section 7"; "`ks_string.h` is one class and stays one file"), section 10 row 3.6 ("none"); 3.2's report section 7 ("the `.cpp` files outside family folders (3.6)"); CLEANUP 17 (the three part interfaces' naming, 3.8). Matt, 2026-11-02: easily-tweaked decisions are the session's (CLAUDE.md "Refactor verdicts"). Surveyed 2026-11-04 at `a2ff727`: the six multi-type headers above; include sites — `wave_evolution.h`: `wavetable_source.h`, registrations; `noise_sources.h`: registrations, tests; `wander_noise_source.h`: registrations, tests; `filters.h`: `fm_source.h`, loader, registrations, tests; `combined_source.h`: loader, registrations, tests; `formant.h`: `full_additive_source.h`, `partials.h`, loader, registrations, tests, the editor.

## Global Constraints

- **Byte-identity is total in scope**: `python tools/null_gate_perform_source.py --jobs 3` → `78/78 manifest entries identical` after EVERY task; the A/B at the close over `renders/scratch/ab31/library_baselines.txt` (132) against `renders/scratch/ab_tree/cli_pre36.exe` → 0 DIFFER. No patch changes; the manifest is untouched.
- **No text changes inside a moved type** beyond its `#include` lines and the file banner; a moved type's body is byte-identical to the deleted header's (the reviewer diffs them: `git show a2ff727:<old header>` against the new file). The one exception is the shaped-draw helper (Task 2), proved by the twin.
- **No umbrella headers**: a deleted header is not replaced by one that includes its parts; every include site names the file whose type it uses (the registrations file gains one include per type).
- **File names are the type's snake case**; the folder is the registry's family (`SourceRegistry` says which). An interface shares its primary implementation's file only where 3.8 will name the part interfaces together (ruling 3).
- **`git mv` where a file maps one-to-one** (the primary type keeps the path and the history: `wave_evolution.h` keeps `WaveEvolution`, `wander_noise_source.h` keeps `WanderNoiseSource`, `combined_source.h` keeps `CombinedSource` and `CombineOp`, `formant.h` keeps `IFormant` + `Formant`); the other types are new files; `noise_sources.h` and `filters.h` are deleted outright (no primary).
- **No new file over 600 lines, no new function over 80**; `files_over_limit` 20 → 19 (`wave_evolution.h`; `partials.h` stays until 3.8); `include_violations` +0; `unmapped_files` 0 after Task 4 (the module map precedes the move in the same commit); `dynamic_cast` +0; `duplicate_blocks` does not rise.
- **No dated changelog, backlog id, REVIEW, Part or checkpoint number in code comments or file names.**
- **Commit after every task; `python tools/gates.py --fast` before every commit**; the UI relinked before a stamp run (every task changes headers the editor compiles).
- **Work on main, no worktree.** Edits through script files; tracked files mix CRLF and LF (memory `project_crlf_edit_scripts`); a span edit ends at the nearest marker; a failed build's stale test binary is not a pass.
- **Dates:** `date` first; this plan is dated 2026-11-04.

## Review Focus

The reviewer's oracle. Each row names the task that pins it.

1. **Every type lands in the table's file and folder** (section "File structure"); nothing is left in a deleted header; no type appears twice. Reviewer: `grep -rn "^struct \|^class " engine/include/mforce/source` against the table; `git show a2ff727:<old>` body-diffed against each new file (Task 1–3).
2. **Every include site compiles against the file it needs**, not a neighbour that happens to include it. Reviewer: remove one new include at a time in the registrations file and watch the build fail (Task 1–3, one sample per task).
3. **The shaped-draw helper is the two sequences bit for bit.** The twin retypes the deleted white-noise block and the deleted red-noise block as lambdas in the test and compares `same_bits` against the helper over a parameter walk (density, zeroCrossTendency, boost, continuity, last value; a `Randomizer` seeded per row and advanced identically); white's `std::clamp(boost, 0, 1)` and red's raw `boost` are both expressed through the helper's arguments. Reviewer: mutate one draw's order in the helper and watch the twin fail; confirm `WhiteNoiseSource` and `RedNoiseSource` call it with the arguments the table gives (Task 2). In scope eleven patches carry a `WhiteNoiseSource` and four a `RedNoiseSource`; Task 0's scan says which leave white's fast path (a non-default density, boost, continuity or zeroCrossTendency) — red always draws through the block — so the null gate and the A/B pin the helper on real patches as well as the twin.
4. **The `.cpp` move changes nothing but paths**: CMake's list, the module map's two prefixes, `#include` lines inside the moved files (they include by `mforce/...`, so none change). Reviewer: `python tools/structure/check.py` shows `unmapped_files 0` and the same `include_violations`; the stamp's dependency set still covers the moved files (Task 4).
5. **The meter**: `files_over_limit` 19, `files` +N where N is the table's count, `lines` within ±40 of 0 (banners and include lines), everything else +0 (Task 5).
6. **The tests bite**: Task 2 mutates the helper (a draw reordered; the clamp dropped) and the twin fails each time; Tasks 1 and 3 have no arithmetic to mutate — the compiler is the test (an include removed fails the build) (Tasks 1–3).

---

## File structure

| Old file (types) | New files (one type each) |
|---|---|
| `wavetable/wave_evolution.h` (`WaveEvolution` + 13) | `wavetable/wave_evolution.h` (the base only, `git mv`-less: same path), `pluck_evolution.h`, `averaging_evolution.h`, `target_evolution.h`, `eks_evolution.h`, `reed_evolution.h`, `bowed_string_evolution.h`, `brass_evolution.h`, `reaction_diffusion_evolution.h`, `sort_erosion_evolution.h`, `cellular_automaton_evolution.h`, `histogram_equalize_evolution.h`, `bezier_pull_evolution.h`, `bit_rotate_evolution.h` — each includes `wave_evolution.h` and the `core/dsp` primitives it alone uses |
| `noise/noise_sources.h` (6) | `noise/blue_noise_source.h`, `violet_noise_source.h`, `velvet_noise_source.h`, `perlin_noise_source.h`, `crackle_noise_source.h`, `murmuration_noise_source.h` (the ones that build on pink or white include that file); `noise_sources.h` deleted |
| `noise/wander_noise_source.h` (3) | `noise/wander_noise_source.h` (`WanderNoiseSource`), `wander_noise2_source.h`, `wander_noise3_source.h` |
| `noise/white_noise_source.h`, `noise/red_noise_source.h` + `src/red_noise_source.cpp` (the shaped draw twice) | `noise/shaped_draw.h`: `inline float shaped_draw(Randomizer& rng, float density, float zeroCrossTendency, float boost, float continuity, float lastValue, float& lastSign)` returning the drawn value (0 when `decide(density)` fails), the caller clamping `boost` or not as it did; both call sites reduced to the call |
| `filter/filters.h` (4) | `filter/bw_lowpass_filter.h`, `bw_highpass_filter.h`, `bw_bandpass_filter.h`, `delay_filter.h`; `filters.h` deleted |
| `combiner/combined_source.h` (5 + `CombineOp`) | `combiner/combined_source.h` (`CombineOp`, `CombinedSource`), `combiner/crossfade_source.h`, `filter/distorted_source.h` (family Filters since 3.2), `utility/static_var_source.h`, `utility/static_range_source.h` |
| `additive/formant.h` (6) | `additive/formant.h` (`IFormant`, `Formant` — ruling 3), `formant_spectrum.h`, `fixed_spectrum.h`, `band_spectrum.h`, `formant_sequence.h` |
| `engine/src/{red_noise_source, wavetable_source, hybrid_ks_source, full_additive_source, additive_source2, source_registrations}.cpp` | `engine/src/source/noise/red_noise_source.cpp`, `source/wavetable/{wavetable_source, hybrid_ks_source}.cpp`, `source/additive/{full_additive_source, additive_source2}.cpp`, `source/source_registrations.cpp`; `engine/CMakeLists.txt` and `tools/structure/modules.json` (`source`: `engine/include/mforce/source/`, `engine/src/source/`) follow; `docs/architecture/MODULES.md` regenerated |
| Include sites | `wavetable_source.h` (the base), `fm_source.h` (`bw_lowpass_filter.h` or whichever it uses), `full_additive_source.h` and `partials.h` (`formant.h`, `formant_sequence.h` as used), `patch_loader.cpp`, `source_registrations.cpp` (one include per registered type), `tools/engine_tests/main.cpp`, `tools/mforce_ui/main.cpp` |

Untouched by rule: `additive/partials.h` (3.8), `physical/ks_string.h` (section 8), `envelope/envelope.h` (one type, 587 lines).

---

### Task 0: Date, base, the old arm

- [ ] `date`; sweep stale `mforce_ui_*.exe`; build; `gates --fast` confirms the stamp. Copy `build/tools/mforce_cli/Release/mforce_cli.exe` → `renders/scratch/ab_tree/cli_pre36.exe`. The old arm is `renders/scratch/ab35/new/hashes.txt` (the same engine: no engine file changed after `594df28`), copied to `renders/scratch/ab36/old/hashes.txt` and noted in the Ledger; no re-render (memory `project_ab_arm_reuse`). Record the sha.

### Task 1: `wave_evolution.h` → the base and thirteen files

- [ ] A script writes the fourteen files from the header's text (each class's span, verbatim, under a banner and the includes it uses), deletes the thirteen from `wave_evolution.h`, and rewrites the include sites (`wavetable_source.h` keeps the base; `source_registrations.cpp` gains thirteen includes; the tests include what they construct).
- [ ] Build; `engine_tests` ALL PASS (7460); null gate 78/78; `gates --fast`; the meter: `files_over_limit` 19. Commit: "nodes: one file per wave evolution; wave_evolution.h is the base alone".

### Task 2: the noise headers; the shaped draw

- [ ] **RED first:** `run_shaped_draw_twin_tests` — two lambdas retyping the white block (with the clamp) and the red block (without) over a parameter walk against the helper; fails to compile until the helper exists.
- [ ] `noise/shaped_draw.h` (the table's signature; the draw sequence exactly as the two sites have it); `WhiteNoiseSource::compute`'s shaped path and `RedNoiseSource`'s ramp-target block each become the call, with the clamp kept at white's call site and `lastVal_`/`lastValue_` passed as before; `noise_sources.h` → six files; `wander_noise_source.h` → three; includes.
- [ ] Build; ALL PASS; mutate: swap the sign draw and the magnitude draw in the helper (the twin fails); drop nothing else. Null gate 78/78 (`white`/`red` noise patches are in scope: the scan at Task 0 lists them); `gates --fast`. Commit: "nodes: one file per noise type; the shaped draw white and red noise shared is one helper, twin-tested".

### Task 3: the filter, combiner and formant headers

- [ ] `filters.h` → four files; `combined_source.h` → `CombinedSource` + `CombineOp` staying, `CrossfadeSource` beside it, `DistortedSource` to `filter/`, the two statics to `utility/`; `formant.h` → `IFormant` + `Formant` staying, four files beside it; include sites (`fm_source.h`, `full_additive_source.h`, `partials.h`, the loader, the registrations, the tests, the editor).
- [ ] Build; ALL PASS; null gate 78/78; `gates --fast`; the meter's `include_violations` +0. Commit: "nodes: one file per filter, combiner and formant type; Distortion and the statics in their families' folders".

### Task 4: the `.cpp` files under `engine/src/source/`

- [ ] `tools/structure/modules.json`: `source` → `["engine/include/mforce/source/", "engine/src/source/"]`; `git mv` the six files; `engine/CMakeLists.txt`'s list; `python tools/structure/check.py --modules-md`.
- [ ] Build; ALL PASS; null gate 78/78; `gates --fast` (the stamp's dependency set follows the moved files); `unmapped_files 0`. Commit: "engine: the family .cpp files under src/source/<family>/; the module map's source prefixes".

### Task 5: Close

- [ ] A/B: `python tools/ab_render_set.py render renders/scratch/ab31/library_baselines.txt renders/scratch/ab36 new` against the copied `old` → 0 DIFFER. `python tools/structure/check.py --delta <Task 0 sha>`; `--snapshot`. One timing run (`ab_render_time.py` against `cli_pre36.exe`) — a move cannot change it; reported for the series.
- [ ] The Opus 4.8 reviewer (Agent, model `claude-opus-4-8`, high effort; brief at `renders/scratch/ab36/review/brief_r13.md`: the six review-focus items, the table, the commit range; it reports only what it ran); the fix wave if any; no re-review unless a Major.
- [ ] Docs: report `docs/autonomy/refactor/reports/<date>-checkpoint-3.6.md` (ten sections; section 10's lessons: what `#include` really does and why an umbrella header is a shim; `inline` free functions in headers and the ODR; `git mv` and history across a split; a module map as a build-time contract); Part 3 section 8's landed paragraph and row 3.6; REVIEW R13 (nothing to decide: the Opus reviewer in place of the refuter is named); BACKLOG row 2 (3.6 DONE, NEXT plan 3.7); CLEANUP as found; STATUS; the plan's Ledger. Commit.

---

## Ledger

Rulings made in planning; execution notes are appended per task by the executor.

1. **Part 3 section 8 is applied as written to six headers, not four.** Since the section was written, `white`/`pink`/`red` noise and the Butterworth section already left their shared headers (3.2, 3.3), so `noise_sources.h` holds six types and `filters.h` four; `wander_noise_source.h`, `combined_source.h` and `formant.h` also hold several types and fall under the same rule. `partials.h` waits for 3.8 (section 7) and `ks_string.h` stays (section 8).
2. **No umbrella headers.** A header that only includes its parts is a shim the target architecture's "one type per file" would carry forever; the include sites are few (the survey above) and each names what it uses.
3. **`IFormant` shares `formant.h` with `Formant`** until 3.8 names the three part interfaces together (CLEANUP 17: `IPartials` and `IFormant` pure and I-prefixed, `WaveEvolution` a base). Naming `IFormant`'s file now would decide that question by accident.
4. **Two types change folder with their family:** `DistortedSource` (Filters since 3.2's registration) and the two statics (Utility). The registry's family is the folder; a folder that disagrees with the registration is the kind of drift the rule exists to end.
5. **The `.cpp` files mirror the include tree under `engine/src/source/<family>/`**, and `source_registrations.cpp` sits at `engine/src/source/` as the module's own file; the module map's `source` becomes two prefixes rather than a list of files, so the next moved file needs no map edit.
6. **The shaped draw is one helper in `noise/shaped_draw.h`**, a free `inline` function taking the `Randomizer` and the five scalars, not a `core/dsp` primitive: only the noise family draws this way. White's clamp on `boost` stays at white's call site (red never clamped; the helper takes what it is given) so neither path's arithmetic changes. The twin test is the proof; the A/B covers `white` and `red` noise patches in scope.
7. **Byte-identity by construction, proved anyway:** the null gate after every task, the A/B at the close against the reused old arm, the meter. The one timing run is for the series' record.
8. **The review is an Opus 4.8 reviewer, not a Fable refuter**, with this plan's tables as its oracle; the refuter's catches have all been where the gates are blind (the editor, tools, the record), and 3.6 touches none of those surfaces. The reviewer mutation-tests the twin and body-diffs every moved type.
9. **Execution is Sonnet 5.5 per task** under the session's coordinator; the plan dictates every file name, folder and the helper's signature so no design is left to the executor. The coordinator reruns the null gate before every commit (the 2.1/2.2 pattern).


### Execution notes

**Task 0 (2026-11-05, Fable 5.1 xhigh; base `400a587`).** `date` 2026-11-05;
no stale `mforce_ui_*.exe`; the editor not running. No engine or tools
file changed between `a2ff727` (the survey) and `400a587` (`tools/structure/metrics.json` only), and none
since `594df28`, so the ab35 new arm is this checkpoint's old arm:
`renders/scratch/ab35/new/hashes.txt` -> `renders/scratch/ab36/old/hashes.txt`
(132 lines, no re-render). `cli_pre36.exe` copied from the clean build.
`gates --fast`: ctest 5/5, the stamp current (dep set 105 files, build
11-04 18:37 @400a587), the allocation gate PASS, the roundtrip smoke FAIL
on the known wiring diff (5/6 shapes clean, as at 2.4 through 3.5). The
six headers hold exactly the plan's counts (14, 6, 3, 4, 5, 6 types).

**The Task 0 scan (review focus 3).** `WhiteNoiseSource`: 20 nodes in 11
files (the three pianos 4 each; bassoon1, flute1, oboe1, oboe2;
bend_mordent_test_pluck, bow_evolution_discovery, loop_hysteresis_bow,
loop_selfosc). **None leaves the fast path**: every in-scope white node
has density 1, boost 0, continuity 0, zeroCrossTendency 0, so the null
gate and the A/B see white's single-draw path only. `RedNoiseSource`: 9
nodes in 4 files (bow_evolution_discovery 1, hybrid_breath_horn_test 1,
repeating_segment_test 2, wander2_mod_test 5); red draws through the
block on every ramp, so those four pin the helper on real patches. The
twin is the proof for white's shaped path; ruling 10 adds one synthetic.

10. **Ruling 6 corrected from the code, two ways.** (a) *Both* sites clamp
`boost`: `red_noise_source.cpp` has `b = std::clamp(b, 0.0f, 1.0f)` before
its `range(b, 1)` exactly as `white_noise_source.h` does; the plan's "red
never clamped" was a misreading. The clamp therefore lives INSIDE the
helper, at the same point of the sequence, and neither call site clamps.
(b) The `decide(density)` stays at the call sites, outside the helper:
white's miss writes 0 and red's miss sets `zeroRamp_`, and a helper that
returned 0 on a miss could not tell red a miss from a drawn 0.0
(`range(0, 1)` can return 0). The helper is the block from the sign draw
on: `inline float shaped_draw(Randomizer& rng, float zeroCrossTendency,
float boost, float continuity, float lastValue, float& lastSign)` -- the
sign (flip on `decide(zct)`, else `sign()` with 0 -> 1), the magnitude
`range(clamp(boost), 1) * lastSign`, the continuity bias
`range(lastValue, v, lastValue, min(continuity, 0.999))` when continuity
is non-zero -- and each caller reads `if (rng_.decide(density)) v =
shaped_draw(rng_, zct, bst, cont, lastVal_, lastSign_); else <its own
miss>`. White passes `lastVal_` (its previous output, amplitude applied),
red passes `lastValue_` (the previous ramp target), as each does today.
The twin retypes both blocks from the sign draw on (white's with the
clamp, red's with the clamp) as lambdas and compares `same_bits` against
the helper over the parameter walk; a `Randomizer` per row, seeded and
advanced identically on both sides. The table row for `shaped_draw.h`
reads with this signature.

11. **One synthetic for white's shaped path.** Since no in-scope white
node leaves the fast path, Task 2 also writes
`renders/scratch/ab36/synth/white_shaped.json` (one `WhiteNoiseSource`
with density 0.7, boost 0.2, continuity 0.4, zeroCrossTendency 0.3, into
a SoundChannel, two notes, in the shape of an existing baseline) and
renders it with `cli_pre36.exe` and the Task 2 build: identical, or the
call site is wrong. The 3.3 method; it joins the close's A/B as a second
set file.

12. **The include sites, by the types each names** (surveyed at
`400a587`; the executor confirms at the build). `wavetable_source.h`:
`WaveEvolution`, `PluckEvolution` -> `wave_evolution.h`,
`pluck_evolution.h`. `hybrid_ks_source.h`/`.cpp`, `wavetable_source.cpp`:
no evolution type by name. `fm_source.h`: no filter type by name -> its
`filters.h` include goes (the build says whether a transitive include was
carrying something; then that header is named). `full_additive_source.h`:
`IFormant`, `BandSpectrum` -> `formant.h`, `band_spectrum.h`. `partials.h`:
`IFormant`, `Formant` -> `formant.h`. `patch_loader.cpp`: `BWLowpassFilter`,
`BWHighpassFilter`, `BWBandpassFilter`, `CombinedSource`, `CombineOp`; no
`DelayFilter`, no crossfade/distorted/static, no formant type -> three
filter files and `combined_source.h`; its `formant.h` include goes.
`source_registrations.cpp`: one include per registered type (13
evolutions + the base through `wavetable_source.h`; the six noise types,
three wander; four filters; the five combiner-table types; the five
formant-table types). `tools/engine_tests/main.cpp`: every evolution type
(all 13), the six noise types and `WanderNoiseSource` (not Wander2/3),
all four filters, all five combiner-table types, all five formant-table
types -- each include placed in the test block that uses it, where the
old include sits. `tools/mforce_ui/main.cpp`: `Formant`, `IFormant`,
`FormantSpectrum`, `FixedSpectrum`, `BandSpectrum`, `FormantSequence` ->
all five formant files. `noise_sources.h`'s own includes (pink, white) go
to the types that hold a `PinkNoiseSource`/`WhiteNoiseSource` member
(Blue holds `pink_`; the executor reads the other five).

13. **The coordinator model.** This session opened on Fable 5.1 xhigh
(the app's picker, not a choice); Task 0 and these notes were done on it.
The 3.6 combo (Opus 4.8 coordinator) is put to Matt at the end of Task 0;
the switch is his, in the model menu, since a session cannot re-price
its own turns.

## Self-review notes

- The thirteen evolution files average 70 lines; the largest (`TargetEvolution` with its synthesis, `BrassEvolution`) stay well under 600.
- `fm_source.h` includes `filters.h` but names no `BW*` type in its text; the executor reads what it uses (`DelayFilter`, or nothing — then the include goes) and includes that file alone.
- `noise_sources.h` includes `pink_noise_source.h` and `white_noise_source.h` because some of its six build on them (Perlin/Crackle/Murmuration use pink or white as a source); each new file includes only what its type uses.
- The editor includes `formant.h` for `Formant` and `FormantSpectrum` (the spectrum rows); after the split it includes both files.
- `git mv` keeps `git log --follow` for the one-to-one files; the split-out types' history is the deleted header's, reachable by the commit.

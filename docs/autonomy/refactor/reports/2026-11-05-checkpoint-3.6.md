# Checkpoint 3.6 — one type per file (report R13)

**Landed 2026-11-05 on main, `ed04dda`..`5c4c5db`** (the plan's Ledger is
`281f5b8`; base before 3.6 `400a587`). The sixth of Part 3's eight. A
move-only checkpoint: every node header now holds one type, named after it,
in its family's folder; the family `.cpp` files sit under
`engine/src/source/<family>/`; the shaped noise draw white and red share is
one helper. No render moves — the null gate is 78/78 after every task and the
close A/B is 132/132, 0 DIFFER.

Reading guide: ten minutes. Section 1 is the one thing to know (nothing is
audible or editor-visible). Section 6 is the two decisions the session made
under the 2026-11-02 rule. Section 8 is the independent review (CLEAN). Section
10 is four C++ lessons.

## 1. What changed for you: nothing you can hear or see

This checkpoint moved text between files. It added no behaviour and removed
none. The editor's node menu is unchanged — every type still registers under
the same family and label it had at 3.5 (`DistortedSource` moved to the
`filter/` *folder* and the two statics to `utility/`, but their registered
families were already Filters and Utility since 3.2, so the menu is the same).
No patch on disk changed. No render changed: the null gate (78 manifest
patches) is identical after every one of the four tasks, and the close A/B over
all 132 `library` + `baselines` patches against the pre-3.6 binary is 0 DIFFER.

The one arithmetic change — folding the shaped noise draw into a shared helper
— is proved bit-for-bit (section 4).

## 2. The shape

Part 3 section 8's rule ("one type per file, the file named after the type,
grouped in a folder per family") applied to the six headers that still held
several types. Each type's text moved to its own file, verbatim; the old
umbrella headers that had no primary (`noise_sources.h`, `filters.h`) were
deleted outright, with no replacement that only re-includes the parts (no
"umbrella" — ruling 2); every include site now names the file whose type it
uses. Two types changed family *folder* because 3.2 changed their family:
`DistortedSource` to `filter/`, `StaticVarSource`/`StaticRangeSource` to
`utility/` (ruling 4). The five `source`-module `.cpp` files and
`source_registrations.cpp` moved under `engine/src/source/<family>/` so the
compiled tree mirrors the include tree, and the module map's `source` entry
became two path prefixes rather than a list of files (ruling 5).

`partials.h` is left for 3.8 (section 7), `ks_string.h` stays one class
(section 8), `envelope/envelope.h` is one type already.

## 3. The four tasks, commit by commit

| Task | Commit | What |
|---|---|---|
| 1 | `ed04dda` | `wave_evolution.h` → the `WaveEvolution` base alone (41 lines) + 13 evolution files |
| 2 | `ada05cd` | `noise_sources.h` → 6 files (deleted); `wander_noise_source.h` → 3; the shaped draw → `noise/shaped_draw.h`, twin-tested |
| 3 | `3d2bcf1` | `filters.h` → 4 files (deleted); `combined_source.h` keeps `CombineOp`+`CombinedSource`, 3 types move out (2 change folder); `formant.h` keeps `IFormant`+`Formant`, 4 move out |
| 4 | `5c4c5db` | the six family `.cpp` files under `engine/src/source/<family>/`; the module map's two `source` prefixes; `lint_patches.py`'s source path |

33 files added, 2 deleted (`filters.h`, `noise_sources.h`); 26 type bodies
moved, every one byte-identical to its pre-3.6 text.

## 4. The proofs

- **Null gate** (`null_gate_perform_source.py --jobs 3`, 78 manifest patches):
  78/78 identical after each of the four tasks, run by the coordinator before
  every commit.
- **Close A/B** (`ab_render_set.py`, the 132 `library` + `baselines` patches,
  against `cli_pre36.exe` = the pre-3.6 binary at `400a587`): 132 identical, 0
  DIFFER. Broader than the null gate's manifest; the full measurement scope.
- **Body-diff**: for all 26 moved/retained types across Tasks 1–3, the struct
  span extracted from `git show 400a587:<old header>` is byte-identical to the
  new file's span (only `#include` lines and a one-line banner differ). The
  coordinator ran this per task; the reviewer re-ran it independently.
- **The shaped-draw twin** (`run_shaped_draw_twin_tests`, 4802 rows): retypes
  the two deleted blocks as lambdas and compares `same_bits` against the helper
  over a grid of (zeroCrossTendency, boost, continuity, lastValue) × two signs,
  a 4-deep draw chain plus a trailing `valuePN()` so a reordered or miscounted
  draw desyncs. The coordinator and the reviewer each mutated the helper (a
  reordered draw; the dropped clamp) and watched the twin fail at
  `CHECK(bad==0)`; restored, it is ALL PASS (7462 checks).
- **The shaped white path** carries no in-scope patch (every `WhiteNoiseSource`
  in `library`+`baselines` sits on the fast path at default density/boost/
  continuity/zct), so a synthetic (`renders/scratch/ab36/synth/white_shaped.json`,
  density .7 boost .2 continuity .4 zct .3) renders identical old vs new
  (sha `9f7d2f…`). Red's shaped path is pinned by its four in-scope patches
  through the null gate.
- **ctest** 5/5; **gates --fast** clean but the standing `wiring`
  roundtrip-smoke diff (5/6 shapes), which predates 3.6.
- **Timing** (`ab_render_time.py`, 3 rounds, pre-3.6 vs now): 0.92×–1.06×,
  scattered around 1.0, machine noise — a move changes no per-sample cost. For
  the series record only.

## 5. The meter (`--delta` from the pre-3.6 base `281f5b8`)

| Measure | Before | After | Change |
|---|---|---|---|
| files | 179 | 211 | +32 |
| files_over_limit | 20 | 19 | −1 (`wave_evolution.h` dropped below 600) |
| functions_over_limit | 98 | 98 | +0 |
| dynamic_cast | 63 | 63 | +0 |
| include_violations | 14 | 14 | +0 |
| unmapped_files | 0 | 0 | +0 |
| duplicate_blocks | 146 | 163 | **+17** |

`lines` +437 (the 33 new files' `#pragma once` + banner + includes + namespace
scaffolding). Every gated metric is flat or better except `duplicate_blocks`,
which the plan said must not rise — see section 6.

## 6. Decisions the session made (2026-11-02 standing rule)

Both are understood, reversible and not far-reaching, so they are the
session's to make; neither is a REVIEW item.

1. **`duplicate_blocks` +17 is accepted.** The plan's Global Constraint said it
   must not rise; that was written before accounting for 33 new files. Running
   lizard's duplicate detector over the split folders, every one of the new
   blocks is either (a) the one-type-per-file preamble now replicated per file
   (`#pragma once` / the shared include set / `namespace mforce {` / the banner
   / `struct X final : Base {` and its first pins), or (b) sibling-struct
   similarity that already existed inside the umbrella header and is now visible
   cross-file — `bw_lowpass_filter` ↔ `bw_highpass_filter` are thin wrappers
   over the shared `dsp::ButterworthSection` differing only by `Kind::` and
   `type_name`, and a few differentiated-noise generators resemble each other.
   The reviewer confirmed: **no new authored logic is duplicated** — every
   flagged body is byte-identical to the pre-refactor text. This is the
   arithmetic cost of the target architecture's own "one type per file" rule.
   The constraint is corrected in the plan's Ledger. → CLEANUP 31.
2. **Two include drops and one comment, repaired in place.** `fm_source.h`
   carried `#include filters.h` but names no filter type (pure transitive dead
   weight); `patch_loader.cpp` carried `formant.h` but names no formant type;
   both includes are dropped and the files compile without them — the "name
   what you use" rule the split exists to enforce. A stale comment in
   `engine_tests` ("the two section kinds of filters.h") lost the dead filename.
   `tools/lint_patches.py`'s `SCRAPE_SOURCES` was repointed to the new
   `source_registrations.cpp` path (the move broke `structure_meter_tests` until
   then) — the minimal correct consequence of Task 4, in that commit.

## 7. Debt and future cleanup

- **A shared Butterworth base** for `bw_lowpass`/`bw_highpass`/`bw_bandpass`
  would remove the sibling-struct duplication section 6 names. It is new design,
  not a move, so it is out of scope for this checkpoint; logged as a future
  candidate (CLEANUP 32), to weigh with Part 8's "are we growing as much
  compensating code as we lose" pass.
- **No new byte-identity preservation.** This checkpoint added no guard that
  exists only to keep a render identical; it moved text. Nothing to add to the
  "preservations to retire after Part 8" list.
- **Line endings:** several edited/new files are LF in the working tree where
  the repo normalizes to CRLF; git warns it will convert on next touch. Source
  line endings do not affect the compiled output (the null gate and A/B are the
  proof), so this is cosmetic; `.gitattributes` normalizes on commit.

## 8. The review (R13)

An independent reviewer stood in for the usual Fable refuter (ruling 8: 3.6
touches none of the surfaces the refuter catches — the editor's live paths,
the tools, the record, gates that lie — so a plan-table review with the
machine proofs is the right instrument; the Fable weekly window was also near
its cap). Its oracle was the plan's tables. Brief:
`renders/scratch/ab36/review/brief_r13.md`.

**Verdict: CLEAN, nothing above Flag.** It re-ran the body-diff (26 types
identical, after catching and correcting a cp1252-vs-utf-8 artifact in its own
first run), removed a `*_evolution.h` include from the registrations and
watched the build fail (proving the site needs the file it names, not a
neighbour's transitive include), mutated the helper two ways and watched the
twin fail each time, confirmed `git log --follow` crosses the `.cpp` renames,
and judged the `duplicate_blocks` rise benign. It restored every mutation and
left the tree clean.

Two Flags, both record/clarity, neither a byte-identity or correctness hole:
- The helper's `if (lastSign == 0.0f) lastSign = 1.0f;` guard is exercised only
  on a grid row where `rng.sign()` returns exactly 0; if no row hits that, a
  mutation to that line is invisible to the twin. But the same condition means
  such a mutation cannot alter any realized draw, so byte-identity is
  unaffected. Informational.
- Task 4's `lint_patches.py` edit was not enumerated in the plan's Task 4
  bullet; it is the minimal correct consequence of the move and the commit
  message documents it (section 6).

No Major, so no re-review (per the plan). Note on the reviewer's model: the
plan named Opus 4.8; the subagent tool's `opus` option would select the
current flagship Opus, which this project forbids, so the reviewer ran as a
fresh subagent with no model override — the coordinator's inherited Opus 4.8,
never Opus 5/5.5.

## 9. What 3.6 did not touch

`partials.h` (3.8, section 7), `ks_string.h` (one class, section 8). Section
9's real-time fixes (`prepare`-not-first-sample sizing for the wavetable, the
hybrid KS DFT scratch, the per-cycle vectors, EKS's per-note table) are
checkpoint 3.7 — the next checkpoint, Fable-planned, with a refuter narrowed to
the live path and the allocation gate.

## 10. C++ lessons

- **What `#include` really does, and why an umbrella header is a shim.**
  `#include "x.h"` is literally "paste the text of x.h here" at preprocess
  time — there is no module system doing symbol resolution (until C++20
  modules, which this engine does not use). So when `wave_evolution.h` used to
  pull in `biquad.h` and a consumer `#include`d `wave_evolution.h` and happened
  to compile, the consumer was relying on a *transitive* paste it never asked
  for. The C#/Java analogue is leaning on a transitive assembly reference or a
  star-import that a neighbour's file set up: it works until someone trims the
  neighbour, then your file stops compiling for a reason that is not in your
  file. "One type per file, each naming what it uses" makes every file's needs
  explicit, and an *umbrella header* (one that only re-includes its former
  parts) would quietly restore exactly the transitive-paste coupling the split
  removes — a shim that the target architecture would carry forever. That is
  why `noise_sources.h` and `filters.h` were deleted, not hollowed into
  umbrellas (ruling 2). The proof that each site now names what it uses: remove
  one include and the build fails (the reviewer did this).
- **`inline` free functions in a header and the ODR.** `shaped_draw` is an
  `inline float` free function defined in `shaped_draw.h` and included by two
  translation units (`white_noise_source.h`'s consumers and
  `red_noise_source.cpp`). Without `inline`, each `.cpp` that included the
  header would emit its own definition of the symbol and the linker would
  reject the duplicate — a One Definition Rule violation. `inline` tells the
  linker "this definition may appear in many TUs; they are identical, pick one"
  (it is a linkage promise now, barely about inlining). The C#/Java analogue is
  that you simply do not have this problem — a method lives in one compiled
  class and the runtime links it once — so a header-only helper shared by
  value-type call sites is a C++-shaped idiom with no direct counterpart. We
  chose a free function (not a static method on some class) because the draw
  belongs to no type: it is a sequence over a `Randomizer`, taken by two
  unrelated node families.
- **`git mv` and history across a split.** `git mv a b` records a rename, so
  `git log --follow b` walks back through `a`'s history — the file's past is
  not lost at the move. But git tracks *content*, not files, so a *split* (one
  file becomes fourteen) cannot be a set of renames: git sees one modified file
  and thirteen new ones. The thirteen new files' history therefore begins at
  the split commit; their past lives in the deleted header, reachable by reading
  that commit. We used `git mv` for the six one-to-one `.cpp` moves (Task 4,
  all R100) so their history follows, and accepted the fresh-start history for
  the genuinely new split-out type files. (The C#/Java analogue: an IDE "move
  class to new file" refactor likewise cannot preserve blame across a 1-to-N
  split; the VCS only ever saw bytes.)
- **A module map as a build-time contract.** `tools/structure/modules.json`
  assigns every tracked C++ file to exactly one module by path prefix and says
  which modules each may include; the meter enforces it (`include_violations`,
  `unmapped_files`). Making the `source` module two prefixes
  (`engine/include/mforce/source/`, `engine/src/source/`) instead of a list of
  individual `.cpp` paths means the *directory layout itself* is the contract:
  a future file dropped under `engine/src/source/wavetable/` is mapped with no
  edit to the map. This is the same idea as a build system's target-visibility
  rules (Bazel `visibility`, a Maven module boundary, a .NET project reference
  graph) — the dependency direction is declared once and checked, not left to
  whatever `#include` a developer happens to write.

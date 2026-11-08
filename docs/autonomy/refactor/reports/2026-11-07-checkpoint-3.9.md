# Checkpoint 3.9 — the formant family (report R15)

**Landed 2026-11-07 on main, `6e43ca8`..`f94695b` with the fix waves `67e8470` and `aa6a76a`** (base before 3.9 `857d1b4`;
plan `docs/superpowers/plans/2026-11-07-part3-checkpoint-3.9.md`, its Ledger
the record of every ruling and every task). The formant half of CLEANUP 17,
decided with you on 2026-11-07 (Part 3 section 11): `Formant` is the abstract
base with one verb, `gain(freq)`; `FormantPeaks` holds a vowel's peaks as rows
and replaces `FormantSpectrum` together with the per-peak `Formant` node;
`FormantBands` is `BandSpectrum` renamed; `FixedSpectrum`, `IFormant` and
`contains()` are gone; `FormantSequence` keeps its name and takes any
`Formant`; the lifecycle the two part bases share lives once, in
`DrivenPart`; every patch in the tree outside `patches/old/` was rewritten by
the tool; the editor lost its node-to-row bridge. One render in scope moved,
by your decision, and it is the one thing in this checkpoint for your ears.

Reading guide: fifteen minutes. Section 1 is the one audible item, the
capability that left the library with it, and the editor-visible changes.
Section 2 is the design. Section 6 is what the session decided under the
2026-11-02 rule — more than usual, because the plan had three holes the
execution found. Section 8 is the independent review. Section 10 is the C++
lessons.

## 1. What changed for you

- **One render moved, and a capability left the library.** Eight library
  vowels (`sing_alto_A`, `sing_alto_U`, `sing_bass_U`, `sing_soprano_A`,
  `sing_soprano_E`, `sing_tenor_O`, `sing_tenor_U`, `speech_w_AW`) and two
  `patches/sweep/vowel_pitch_control/` files drove their first peak's
  frequency through the instrument paramMap — a curve from the voice's
  frequency, clamped at its first knot, so F1 followed f0 once the note rose
  above the knee (the singer's formant tuning the sweep was built to try).
  The 11-07 survey missed it: it counted a peak's pin as wired only when the
  JSON value was a `{"ref"}`, and a paramMap target is a string. The decided
  `FormantPeaks` has no per-row pin, so you were asked (section 6, decision
  1) and chose rows only: the tool writes the peak's constant as the row and
  removes the curve. Seven of the eight play their rendered note below the
  knee, so F1 already sat at the constant and their renders are byte-identical.
  **`sing_soprano_E`** (note 69, 440 Hz, above its 350 Hz knee) is the one
  that moves: `renders/scratch/ab39/review/old/sing_soprano_E.wav` (the
  pre-3.9 CLI over the unrewritten file) against
  `renders/scratch/ab39/review/new/sing_soprano_E.wav` (the close build over
  the rewritten file). REVIEW R15 item 1: listen. What the library no longer
  has: a peak that follows pitch, in any of the ten files. IDEAS carries the
  way back — spectrum-wide offset and scale pins on `FormantPeaks`, which
  Part 3 section 11 already names as a later addition and which would restore
  pitch-following for all the peaks at once, after the freeze.
- **In the editor**, a vowel is one node. The hand-written formant row table
  under a `FormantSpectrum` is gone with the type; `FormantPeaks` shows its
  four arrays in the same grouped table `ExplicitPartials` uses, one row per
  peak, and editing a row's frequency moves the strip. The formant strip
  draws one curve from the live node for every kind; the start/end/scrub
  overlay it drew for a single peak node is gone with that node (section 6).
  A `FormantPeaks` created from the menu starts with no rows (the deleted
  branch seeded four vowel-like rows for a new `FormantSpectrum`); the strip
  is flat until a row is added in the grouped table (section 6, the review's
  finding).
  The Additive family's create menu reads "Formant Peaks", "Formant Bands",
  "Formant Sequence" where it read "Formant", "Formant Sequence", "Formant
  Spectrum", "Band Spectrum", "Fixed Spectrum" (the labels derived from the
  type names, as the registry test pins them).
- **Everything else is byte-identical**: 131 of 132 library and baseline
  renders, nine synthetic patches that exercise every edge the library does
  not (a band's sub-bin, its start, its end and beyond; a moving grid;
  overlapping peaks with a gain-0 row and three powers; a sequence over a
  spectrum and a band; the floor and weight knobs, one of them on an
  envelope), and `fadd_band_test`, `cello_full_range` and `viola_default` on
  `FormantBands`.

## 2. The shape

Part 3 section 11 as decided, with the one base the survey added.

**`DrivenPart`** (`core/driven_part.h`) is the abstract `Component` base of a
part the owner drives: non-virtual `prepare` and `start` that forward to every
pin, a non-virtual `next` that advances the `Param` pins listed at prepare,
each in declaration order, then the hooks `on_prepare`/`on_start`/`on_next`.
It is the lifecycle `PartialSet` has owned since 3.8, moved up verbatim so
that `PartialSet` and `Formant` derive from it instead of each writing it. The
plan called it `Part`; `mforce::Part` is the music model's part (a voice line
across the Piece, `music/structure.h`), in the same namespace, so the plan's
alternative stood (section 6). `WaveEvolution` keeps its own shape for now
(its prepare carries the owner's table length; CLEANUP).

**`Formant`** (`additive/formant.h`) is the abstract base: `DrivenPart` plus
one pure verb, `virtual float gain(float freq) const = 0`. The file comment
says what a formant is — a gain over absolute frequency imposed on the
source and invariant across fundamentals, the filter half of the
source-filter pair, where the partials are the source's spectrum and move
with the pitch. A formant answers 0 outside every band it shapes, which is
what lets `contains()` go: the consumer's factor is `floor + weight × gain`
everywhere.

| Leaf | File | Pins | `gain(freq)` |
|---|---|---|---|
| `FormantPeaks` | `additive/formant_peaks.h` | four `Array`s `frequency`, `gain`, `width`, `power`, one group "peaks", one row per peak | the largest gain of the rows whose band contains `freq`; each row a power ramp from its lower edge to its centre and down to its upper edge — the deleted peak node's formula verbatim — with the edges cached at prepare and on an array change, no pin read per sample; the load refuses rows of unequal length by name |
| `FormantBands` | `additive/formant_bands.h` | `startFreq`, `freqIncrement` (Manual Params), `gains` (Array) | the old `BandSpectrum::get_gain` with the old `contains` predicate folded in first: 0 outside the span, where the old table alone was not 0 in the one bin below `startFreq` (section 10) |
| `FormantSequence` | `additive/formant_sequence.h` | `blend` (Manual Param), `spectra` (a multi `Slot<Formant>`: any formant, a sequence included) | `g0 × (1 − w) + g1 × w` over the two neighbours' gains, which are 0 where the old gate was false |

**The consumer**: `AdditiveSource` holds `Slot<Formant> formant`, drives it in
`wave()` as before, and `Partials`' body computes
`fmtFactor = fmtFloor + fmtWt * formant->gain(pfreq)` where it computed
`contains ? fmtFloor + fmtWt * get_gain : fmtFloor`. The two agree to the bit
except when `formantFloor` is a negative zero and the partial is out of band
(the zero's sign), which no patch in scope can reach: every floor in scope is
a constant, 58 at 0.0, 2 at an explicit 1.0, 3 at the 1.0 default, none wired,
none a paramMap target (ruling 5; the twin counts the three differing rows).

**The tool** (`tools/migrate_deleted_types.py`) gained a file-level fold
before its per-node rules: a `FormantSpectrum` whose `formants` are
single-referrer `Formant` nodes with constant params becomes one
`FormantPeaks` with the four arrays in `formants` order, the children leave
the node list; a wired, shared or stray peak, a `FixedSpectrum` and the
inline form are refused by name; `BandSpectrum` is renamed. A paramMap curve
aimed at a consumed peak is folded to the peak's constant when its first
knot sits there, removed from the paramMap, and reported.

**Deleted**: `formant_spectrum.h`, `fixed_spectrum.h`, the peak node's class,
`IFormant`, `contains()` everywhere, `BandSpectrum`'s `gains` configurator
(the generic array pass already set it), the editor's bridge (444 lines of
`tools/mforce_ui/main.cpp`, 14,483 → 14,063), 77 → 75 registrations.

## 3. The tasks, commit by commit

| Task | Commit | Who | What |
|---|---|---|---|
| 0 | — | Fable; Opus 5.5 for the set | `cli_pre39.exe` (the 11-06 binary, sha `e8566d11…`), ab38's new arm reused as the old arm plus the replacement baseline's line, the 132-patch list; nine synthetics and nine formant-off controls, every synthetic differing from its control; the floor scan; the survey gap found (section 6, decision 1) |
| 1 | `6e43ca8` | Fable | `DrivenPart`, the lifecycle moved from `PartialSet`; the lifecycle test on a test-local part |
| 2 | `2e4d311` | Fable | `FormantPeaks` beside the old types; the twin and the load tests; the hand-folded vowel through the editor |
| 3 | `19c071c`, `be85f9c` | Opus 5.5; Fable's rename | `BandSpectrum` → `FormantBands` moved forward so the rewritten tree loads; the fold, its 12 tests, 91 nodes in 81 files, 63 tracked, one render moved |
| 5a | `fc4d584` | Opus 5.5 | the editor's bridge deleted while the old headers still compile (the order swapped with Task 4: section 6); the meter's known answer for `draw_properties_panel` moved 963 → 879 |
| 4 | `f94695b` | Fable | `Formant` the base; the leaves rebased; the deletions; the consumer's factor; three twins and the lifecycle test; the strip retyped |
| 6 | the docs commit | Opus 5.5 for the runs; Fable refuter; Fable | the close A/B, the meter, the timing; the review; this report |

Every executor task was verified by the coordinator before its commit: the
null gate rerun, the synthetic A/B rerun, the body-read of moved text, and —
for every twin — one mutation of the code watched to fail at the twin's own
`CHECK` and restored. `git status --short` before every commit showed only
the task's files staged (GOALS.md, your uncommitted edit, stayed unstaged
throughout).

## 4. The proofs

- **Null gate** (`null_gate_perform_source.py --jobs 3`, 78 manifest
  patches): 78/78 after each of Tasks 1, 2, 3 `[, 5a, 4]`. The manifest never
  listed `sing_soprano_E`, so the one moved render needed no manifest change.
- **Close A/B** (`ab_render_set.py`, the 132 library + baseline patches,
  the unrewritten files under `cli_pre39.exe` against the rewritten files
  under the close build): 132 of 132 rendered, 131 identical, 1 DIFFER
  (`sing_soprano_E`, its new hash `05ee0f7f…` the one `moved_hashes.txt`
  records). At Task 3 the same arms on the Task 2 build: 131 identical, 1
  DIFFER.
- **The synthetic set** (`renders/scratch/ab39/synth/`, nine instrument and
  mixer patches, rendered by `cli_pre39.exe` in the old forms and by every
  later build in the rewritten forms): 9/9 identical after Tasks 1, 2, 3
  `[, 4, 6]`; the nine formant-off controls 9/9 at Task 3.
- **Four twin tests** (3.3's method, the deleted block retyped as it stood,
  `same_bits` per row): the peaks twin (`FormantPeaks` against the peak's
  `get_gain`/`contains` and the spectrum's max over five row sets × 2,000
  log-spaced frequencies plus every row's exact edges, the floats inside them
  and the centre: 10,075 rows, the gain compared raw and gated to 0 where
  the old predicate was false), the bands twin
  (10,105 rows over five grids after the review, one with an inexact
  increment so the span's end is pinned; the sub-bin where the old table alone
  was not 0 counted and found), the sequence twin (16,000 rows over two kinds
  of child and two list lengths, the one-sided rows counted), the factor twin
  (48 rows, the three negative-zero rows counted — it proves ruling 5's
  arithmetic, not the consumer's line, which the render set below and the
  null gate cover). Mutations: the peaks
  twin's `contains` edge made inclusive, its max made a sum and its rising
  ramp given the falling numerator, each caught by the twin and by the live
  comparison against `FormantSpectrum` that stood until Task 4; the bands
  fold's `>=` made `>`, caught by the bands twin and by the sequence twin's
  one-child check; the crossfade's weights swapped, caught by the sequence
  twin alone; the consumer's factor rewritten `fmtWt + fmtFloor × gain` (a
  render-observable change, since the old-conditional form is unobservable in
  scope by ruling 5's own analysis), caught by a four-patch render set
  (`fadd_band_test`, `fadd_formant_test`, `sing_alto_E`, `speech_c_AH`: all
  four DIFFER, identical again once restored).
- **Allocation gate** (`alloc_gate.py --jobs 3`): 132 patches, 0 allocate,
  after every task.
- **`engine_tests`**: 7588 → 7603 (the lifecycle test) → 7667 (the peaks
  twin and loads) → 7669 (the rename's hint) → 7638 (the old types' tests
  out, the three twins and the formant lifecycle test in), ALL PASS.
- **`gates --fast`** after every task: ctest 5/5 (the tool's 30 tests among
  them), the stamp current, the allocation gate PASS, the one standing
  failure (the roundtrip smoke's `wiring` diff, 5/6 shapes, since 2.4).
- **The tool**: `--check` over every folder but `old/` exits 0 after the
  run; `--check patches/old` names 459 pending nodes in 236 files and
  refuses 28 (17 spectra with a wired peak frequency, six stray peaks, five
  `AdditiveSource2`); nothing in `old/` written; `lint_patches.py` PASS over
  library and baselines.
- **The editor**: `test_stable_roundtrip.py` over the 58 voice files 0 id
  changes / 0 render diffs, over baselines + library the six KNOWN_DIFFS and no
  new one; `sing_alto_A` roundtrips headless to one `FormantPeaks` of four
  arrays of five and renders to `157889bc…`, the library file's own hash;
  `sing_alto_E` the same through the Task 4 editor (`107c4404…`).
- **Timing** (`cli_pre39.exe` against the close build, three rounds). The
  tool's fixed twelve (`ab_render_time.py`): 0.98–1.06× wall on every valid
  row — flat; its `viola_default` row is void, because the old CLI cannot
  load the rewritten file (`FormantBands`) and the tool does not check exit
  codes (a harness note, section 7). The paired measurement — old CLI over
  the original file, new CLI over the rewritten one, interleaved, minimum of
  three:

  | Patch | Wall new/old | Note |
  |---|---|---|
  | `voice/sing_alto_A` | 0.83 | five peaks, pre-rendered at load |
  | `voice/sing_bass_A` | 0.82 | |
  | `voice/speech_m_AH` | 0.77 | |
  | `voice/words/liar1` | 0.67 | a sequence over four spectra |
  | `baselines/FormantSequence1` | 0.66 | |
  | `baselines/fadd_band_test` | 0.98 | `FormantBands`, render 0.95 |
  | `strings/viola_default` | 0.99 | `FormantBands` |

  The vowels are faster by wall clock, as expected: `FormantPeaks` reads no
  pin per sample where each peak node re-read four per sample (a vowel with
  five peaks and 72 partials made twenty `Param::next` calls per sample before
  the first partial). The `render=` line on the voice patches is the mixing
  pass alone (about 7 ms; the voices pre-render at load), so wall is the
  measure there; `FormantBands` patches are flat, their path unchanged.

## 5. The meter (`--delta` from the base `857d1b4`)

| Measure | Before (`857d1b4`) | After (`f94695b`) | Change |
|---|---|---|---|
| files | 221 | 221 | +0 (three added, three removed) |
| lines | 61,947 | 61,855 | −92 |
| files_over_limit | 18 | 18 | +0 |
| functions_over_limit | 98 | 97 | −1 |
| dynamic_cast | 63 | 64 | +1 (engine +0: the tests cast `FormantPeaks*` twice more, the editor one fewer) |
| const_cast | 10 | 10 | +0 |
| include_violations | 14 | 14 | +0 |
| unmapped_files | 0 | 0 | +0 |
| duplicate_blocks | 161 | 159 | −2 |

Added: `core/driven_part.h` (57), `additive/formant_bands.h` (39),
`additive/formant_peaks.h` (75). Removed: `band_spectrum.h`,
`fixed_spectrum.h`, `formant_spectrum.h`. `formant.h` 87 → 20,
`formant_sequence.h` 73 → 62, `partial_set.h` 89 → 48, `partials.h` 561 → 558;
`tools/mforce_ui/main.cpp` 14,483 → 14,058; `tools/engine_tests/main.cpp`
8,387 → 8,808 (the twins). New types: `DrivenPart`, `FormantBands`,
`FormantPeaks`, and three test-local (`Row`, `StepSource`, `TwoPinPart`);
removed: `BandSpectrum`, `FixedSpectrum`, `IFormant`, `FormantSpectrum`, the
editor's `FormantRow`. The committed `tools/structure/metrics.json` was 3.8's
until this close's `--snapshot`, so `--delta` measures the whole checkpoint.

## 6. Decisions the session made (2026-11-02 standing rule)

Each is understood; three are the plan's holes, found by the execution and
closed in the open.

1. **The driven peak went to you, and you chose rows only** ("2", 2026-11-07,
   through the coordinator session). The finding is section 1's. Three ways
   were put: a slot on `FormantPeaks` for a peak that must stay a node, with
   the four-pin peak surviving under a new name; rows only, the live F1 lost;
   stop and redesign. Your answer is ruling 12 and the amendment to the
   plan's Global Constraints; this is the one item of the checkpoint that was
   not the session's to decide, and REVIEW R15 carries its render.
2. **The base is `DrivenPart`, not `Part`.** `mforce::Part` is the music
   model's part, in the same namespace, and `patch_loader.cpp` sees both; the
   first GREEN build failed on the redefinition. The plan had weighed
   `DrivenPart` and rejected it as redundant ("every part is driven"); with
   `Part` taken, the redundancy is the price of a name that says what the base
   does. A rename is one regex if you prefer another (CLEANUP).
3. **`BandSpectrum`'s rename moved forward into Task 3, as its own commit.**
   The plan had the tool rename the type in the tree at Task 3 and the engine
   learn the name at Task 4, so the three band files and five synthetics would
   not have loaded between the two; the executor found it at the A/B. The
   class, its registration (the `gains` configurator dropped, ruling 7) and
   the hint moved to Task 3; the rebase onto the base and the predicate fold
   stayed Task 4's.
4. **Task 5 ran before Task 4.** The editor includes the headers Task 4
   deletes and casts to the peak class and `IFormant`; under the plan's order
   the UI would not have built at Task 4's commit, and every commit must build
   and pass the gates. The executor deleted the bridge against the old headers
   (Task 5a), and Task 4 retyped the strip and the includes with the engine
   change.
5. **`FormantPeaks`' load check is over the node's own array descriptors.**
   The first configurator named its four keys as string literals and the
   structure meter's known-answer test failed: `lint_patches.py` scrapes
   literals out of configurator code as loader keys, and `width` was a key the
   loader's allowlist lacked. Rather than add an array key to `kBranchKeys`,
   the rule became generic — every array of the node at one length, one row
   per peak — and names no key.
6. **One render moves, not eight.** The amendment first said eight; the
   executor's A/B showed the curve clamps at its first knot and seven vowels
   play below it. Corrected in the plan with the reason; the seven are named
   in REVIEW as files whose live F1 is gone without a render moving.
7. **The strip's snapshot overlay went.** It existed only for the single peak
   node, whose four pins were snapshotted per render; a row has no pin. The
   generic array snapshot does not serve `Array` pins, so the overlay has no
   source; the strip draws one curve from the live node for every kind
   (ruling 10 said this would be the one editor-visible change if so).
8. **The executor's two additions to the fold, kept**: the graph's `output`
   counts as a referrer of a peak; a consumed peak's id leaves `ui.positions`.
   And its one deviation, kept: the fold raises its own refusal rather than
   leaving the spectrum for the per-node walk, which would have met the
   unconsumed child first and said "a stray peak".
9. **Menu order in the Additive family**: `FormantPeaks`, `FormantBands`,
   `FormantSequence` after `ExpandRule` — the two leaves, then the sequence
   over them.
10. **`FormantPeaks`' gain pin is the member `peakGain`.** The plan gave the
    pin the member name `gain` and the base the verb `gain(freq)`; a data
    member and a member function cannot share a name (the fourth hole, found
    by the Task 4 build). The JSON key stays `gain` — the tree is rewritten
    with it — and the C++ member says what it is.
11. **The meter's known answer for `draw_properties_panel` moved** (963 → 879
    lines) with the row table's deletion; `main` unchanged at 1,738.
12. **A new `FormantPeaks` from the menu has no rows** (the review's finding).
    The old editor seeded four rows into a new `FormantSpectrum` by hand; the
    generic path seeds nothing, as it seeds nothing for any other part. Left
    as it is: a seed belongs to the part's type or to Part 7's editor rules,
    not to a hand-written branch; CLEANUP 45.
13. **The tool folds a paramMap curve under the `frequency` key only** (the
    review's Major). Ruling 12's argument — the curve clamps at its first
    knot and the note sat below the knee — holds for a curve fed the voice's
    frequency; a `velocity` curve is read along its length, so folding it to
    the first knot would move a render while saying "folded to a constant".
    No in-scope file had one (the ten were all `frequency`); the fold now
    refuses any other key by name and the test that enshrined the velocity
    shape is a refusal.

## 7. Debt and future cleanup

- **F1 following f0 through a paramMap curve is gone from the library** (ten
  files; decision 1). The capability's way back is IDEAS: spectrum-wide
  offset and scale pins on `FormantPeaks` (Part 3 §11's later addition),
  after the freeze.
- **Nine `tools/gen_*.py` sweep generators still emit the old form**
  (`Formant` + `FormantSpectrum`): `gen_fable1_patches`, `gen_formantseq_sweep`,
  `gen_liar2`, `gen_liar3`, `gen_viola_patches`, `gen_vowel_baseline`,
  `gen_vowel_grid`, `gen_vowel_patches`, `gen_vowelseq2`, and two readers,
  `ablate_additive`, `verify_vowel_grid`. Their output loads only after the
  tool's rewrite; CLEANUP 42.
- **`test_stable_roundtrip.py`'s `ids_of` and `rt_smoke.py` exclude Formant
  children consumed into rows** — dead code now that no file has them and the
  editor synthesizes none (the executor confirmed); CLEANUP 43.
- **The editor's paramMap residue** (`s_loadedParamMap`) existed mainly for
  the one class of entry the bridge could not convert, a curve into an owned
  peak; that class is gone. It still keeps a name other than "frequency", an
  entry with no target and a target naming no node, so it stays; whether those
  deserve a mechanism is Part 7's (CLEANUP 44).
- **`WaveEvolution` onto `DrivenPart`** (its `prepare(ctx, maxLen)` carries
  the owner's table length and its per-sample verb is `evolve`): CLEANUP 40,
  the plan's ruling 1. The base's name is CLEANUP 41.
- **`patches/old/`**: 17 spectra there drive a peak from a wired pin
  (`{"ref": "f0freq"}`, `{"ref": "F1_freq"}`) and six peaks stand alone; the
  tool refuses them by name and they fail to load with the hint, as every
  retired form in `old/` does.
- **The tool re-renders a touched file's numbers through `json.dumps`**
  (`cello_full_range.json`'s `1.0391311645507813` prints as `…812`, the same
  double; no non-ASCII in scope to escape): 3.5's behaviour, harmless, noted.
- **`FormantPeaks::peak_count()` is public and used only by the tests**; the
  editor may want it. `FormantBands::gain`'s `idx < 0` is dead after the folded
  predicate (kept verbatim by the rule). Both record-only.
- **`ab_render_time.py` does not check the CLI's exit code**: a patch the old
  arm cannot load shows as a 25 ms "render" and a 0.02× speed-up. The paired
  measurement rendered the originals instead; the tool wants an `rc != 0`
  guard.
- **Line endings**: the edited files are LF in the working tree where the
  repo normalises to CRLF (git warns at every touch); cosmetic, as before.
- **No new byte-identity preservation.** Nothing joins the "retire after
  Part 8" list: `FormantBands`' folded predicate is the old behaviour, not a
  guard.

## 8. The review (R15)

A Fable refuter with a fresh context, read-only: it built nothing, ran no
binary and performed no mutation (the coordinator's six mutations in section
4 are the mutation record). Its oracle was the plan's eight Review Focus
items, the ten-item brief and the Ledger; it read every new header beside the
deleted original (`git show 857d1b4:…`), the diff `857d1b4..f94695b` whole,
the tool's fold and the editor's diff, and re-ran the floor census.

**Verdict: the two claims hold against the files — 0 Critical, 1 Major,
8 Minor, 7 Flag.** In its words: every moved expression keeps its operations
and order (`DrivenPart`, `peak_gain`/`cache_edges`/`gain`, the bands
predicate, the crossfade, the consumer's factor), each new `gain` is 0 exactly
where the old `contains` was false, and the one arithmetic departure is
unreachable in scope. It confirmed line by line: `DrivenPart` is the deleted
`PartialSet` lifecycle with only the assert's wording changed; `peak_gain` is
`Formant::get_gain` with the cached edges for the members, the same guard,
the same centre comparison, the same operand order, float × float `pow`;
`FormantBands::gain` is the old predicate then the old body; the sequence's
two gates drop because every child kind returns 0 outside; the registrations
75 and every list in the tests against `source_registrations.cpp`; the
manifest never listed `sing_soprano_E`. On Review Focus 4: an `Envelope` emits
−0 only if a stage level or `minValue` is itself −0 (a `-0.0` literal or a
wired source already emitting it); other sources can, and nothing between a
wired source and `fmtFloor` scrubs it — reachable by wiring in general,
unreachable in scope, as ruled.

**The Major (M1), fixed in the wave:** the tool's paramMap fold applied to
every paramMap key. Ruling 12's argument is a property of a `frequency` curve
fed by f0; a `velocity` curve is read along its length, so a render would move
while the note said "folded to a constant". Zero in-scope exposure (all ten
folded entries were `frequency`), but the tool is the standing instrument for
`old/` and external files. The fold now takes `frequency` alone and refuses
any other key by name; the test that enshrined the velocity shape as intended
is a refusal.

**The Minors, taken in the wave:** the spectrum's node-level keys beyond
id/type/params refused rather than dropped silently (m1); `count_refs`
counting any object that carries a `ref`, as the loader reads one, not only
one-key objects (m2); the strip's comment naming `gain` (m4); four test
comments saying the fact instead of a ruling or checkpoint number (m6); the
report's stale "contains and gain both" (m7); the two log grids one helper
(f6). Record-only: `json.dumps` re-rendering a number's text (m3); a new
`FormantPeaks` from the menu has no rows (m5 — section 6, decision 12); the
floor census counts three explicit 1.0 across every `AdditiveSource` where the
Ledger's two counted those holding a formant (`clarinet1` holds none; the
conclusion unchanged) (m8).

**The twins' blind spots, named by the reviewer and recorded:** the peaks
twin cannot see `freq < f` made `<=` (both ramps give `pow(1, p) × g` at the
centre — a third unobservable class beside the two the coordinator recorded),
nor `on_prepare`'s `cache_edges()` deleted (every test reaches the cache
through `set_array`), nor the four-array `min` made one array's size (lengths
are always equal outside the editor's one-column-at-a-time edits); the bands
twin could not see the span end's `<=` made `<` because every grid's end was
exact — the wave adds an inexact increment (33.3 Hz) that pins it; the
sequence twin cannot see either clamp (`std::min(int(b), n − 2)`,
`std::min(fPtr + 1, n − 1)`): at blend 1 they give the same gain, and the
one-child case would crash rather than fail a check; the factor twin compares
two lambdas and never calls the consumer's line — the render set in section
4 and the null gate cover that line, and the report says so now. Flags left
as record, the reviewer agreeing: `peak_count()` test-only; the dead `idx < 0`
after the fold; `formant_peaks.h`'s redundant include of
`dsp_value_source.h` (it names `Array` and `RenderContext`, the convention);
two more Python files on the old form (CLEANUP 42); the two predicate lists
in the editor (CLEANUP 36, "shrank, did not vanish"); the strip advancing the
live node once per frame (pre-existing); a channel-level paramMap would be
neither folded nor refused, and none exists.

**Re-review (the same refuter): closeable.** Every taken item is in the
diff as described and nothing the wave introduced changes behaviour in
scope; the tightened rules refuse nothing real (it grepped the eleven kept
old-form originals: every ref object one-key, no spectrum with a stray key).
Three record items, taken in a second small wave (`aa6a76a`): the fold
paragraph's refusal list gains the stray-key clause; the bands twin checks
that its inexact pair really lands the span's end inside the last bin
(`old_get_gain(end) != 0`, so the end comparison is pinned by a CHECK, not by
reasoning); the shared grid helper sits above the comment it had split.

## 9. Reading guide

1. `engine/include/mforce/core/driven_part.h` (57 lines) — the lifecycle
   once; then `additive/formant.h` (the base: one verb and the comment that
   says what a formant is).
2. `engine/include/mforce/source/additive/formant_peaks.h` — the rows, the
   cached edges, `peak_gain`; then `formant_bands.h` for the folded predicate
   and `formant_sequence.h` for the hooks.
3. `tools/engine_tests/main.cpp`, `run_formant_bands_twin_tests` — the
   sub-bin case in full, and why the fold is load-bearing.
4. `tools/migrate_deleted_types.py`, `fold_spectrum` — the refusals and the
   paramMap rule.
5. `patches/library/voice/sing_soprano_E.json` beside
   `renders/scratch/ab39/review/before/sing_soprano_E.json`.

## 10. C++ lessons

- **An abstract base, the third time, and why `DrivenPart` exists now.**
  3.4 gave `WaveEvolution` a lifecycle in its base; 3.8 gave `PartialSet` the
  same one; 3.9 needed it for `Formant`. The 11-02 rule ("never a third
  copy") is a rule about the moment a pattern is about to be written again:
  the second copy is where the shape is confirmed, the third is where it
  moves up. In C# or Java you would have reached for an abstract class the
  first time; C++ lets a `struct` with `= 0` members play both the interface
  and the abstract class, so the discipline is in the design, not the
  keyword. `Formant : DrivenPart` says "a formant is a driven part with one
  more verb" and nothing else.
- **A name collision is a namespace fact, not a prose fact.** The plan
  weighed "Part" against "Part 3" in the documents and missed `mforce::Part`
  in `music/structure.h`. C++ has one namespace per name; a music-model type
  and a DSP type in the same `namespace mforce` collide however far apart the
  folders are. The build found it in seconds (C2011, a redefinition); the
  lesson is the grep before the ruling — `grep -rn "struct Part\b"` — and the
  broader one that a shared namespace across domains is itself a design
  smell the target architecture may want to address (`mforce::music::`).
- **"Equivalent" predicates and byte-identity: the sub-bin.** Dropping
  `contains()` was safe only if every `gain` returned 0 exactly where the old
  predicate was false. Three of four did by construction; the band table did
  not: for a frequency one bin below `startFreq`, `(freq − sf) / fi` is
  −0.5, `int(−0.5)` truncates toward zero to 0, and the interpolation runs
  with a negative fraction — a non-zero gain where the old code never asked.
  No in-scope render crosses that bin (`fadd_band_test` starts at the
  fundamental), so the null gate would have stayed green with the bug in;
  the twin's grid holds the sub-bin and counts the rows where the old table
  alone was not zero, which is the proof that the fold is load-bearing.
  Java's `(int)` truncates the same way; the trap is identical there.
- **Negative zero, again, as a departure listed rather than hidden.**
  `−0 + w × 0` is `+0`; the old branch returned the floor itself, `−0`. The
  factor twin runs the forty-eight rows and counts exactly the three that
  differ, all in the sign of a zero. The departure is unreachable in scope
  (every floor is a constant parsed as `+0` or `1.0`) and listed anyway,
  because a byte-identity refactor's record is the set of places where the
  arithmetic is *not* the same, with the proof that no patch reaches them.
- **A file-level migration pass versus a per-node rule.** 3.5's tool had one
  rule per deleted type, applied node by node. A `FormantSpectrum`'s rewrite
  consumes *other* nodes, so a per-node rule cannot express it: the fold runs
  first over the whole document, checks every spectrum before changing
  anything, and leaves nothing for the per-node walk to misread. The
  executor's refinement — refuse from inside the fold, not from the walk —
  came from running it: the walk would have met the unconsumed child first
  and blamed a stray peak.
- **A curve that clamps is a constant below its first knot.** Seven of the
  eight "moving" renders did not move because `CurveNode` holds its first
  knot's output for any input below it, and those vowels never sang above the
  knee. A capability can be real in the engine and absent from every render
  in scope at once; the byte-identity instrument measures renders, not
  capabilities, which is why the loss is recorded in section 7 and IDEAS
  rather than only in the A/B's one DIFFER line.
- **Scraped literals.** `lint_patches.py` reads configurator code for string
  literals and treats them as keys the loader consumes; a configurator that
  spells its keys to check their lengths looked like a loader reading `width`.
  The fix was not another allowlist entry but a check written over the node's
  own descriptors — the data the code already had — which is the general
  answer to a tool that reads your source: say it in the data, not the string.

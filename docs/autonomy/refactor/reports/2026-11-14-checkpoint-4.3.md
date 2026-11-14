# Checkpoint 4.3 — tables: Envelope and Curve (report R18)

**Landed 2026-11-14 on main, `4af294a`..`2765aef` with the review's two fix
waves, `d910f27` and `fb4e453`** (base before 4.3 `9e9af05`, the commit that carries this
checkpoint's plan; plan
`docs/superpowers/plans/2026-11-10-part4-checkpoint-4.3.md`, its Ledger the
record of every ruling and every task). Part 4 section 8.1's third row, as
decided with you on 2026-11-09 and started on your "Go now" of 2026-11-10: an
Envelope's stage list and a Curve's knots are no longer forms the loader reads
by hand. Each node declares its table as columns, the way `FormantPeaks` and
`ExplicitPartials` already did; one new kind, the enum column, lets a table
keep its words (`"Linear"`, `"Sine"`) on disk. `tools/convert_patch.py`
rewrote every stage list and knot list in `patches/library`,
`patches/baselines`, `patches/audition` and `patches/pending` as columns; then
the old forms' readers were deleted, and a file that still carries one is
refused by the engine and by the editor, naming the converter. No render in
`library` + `baselines` moved. Nothing in this checkpoint is for your ears.
What you can see is in your files and in the editor, and section 1 lists it.

Reading guide: twenty minutes. Section 1 is what changed in your folders and
in the editor, item by item, and what no command exercised. Section 2 is the
shape: what a table is now, in the file and in the node. Section 4 is how a
change with no audible result was proved in three separate steps. Section 6
is what the session decided under the 2026-11-02 rule. Section 8 is the
independent review, which found the editor's weak spots. Section 10 is the
C++ lessons.

## 1. What changed for you

- **Nothing is audible.** The 131 library and baseline patches render
  byte-identically (131 of 131, 0 differ) against renders made by the base
  engine from the unconverted files, measured after every task and at the
  close. The nine instrument patches that carry no score, eight of which hold
  stage lists, were rendered with a score appended: 9 of 9 identical. The 21
  composition templates: 21 the same. Twenty-four synthetic patches written
  for the forms the scope does not hold (a `Hold` stage, seconds mode, a
  pushed `timeScale`, an empty table, every curve mode): 24 of 24 identical.
- **444 patch files were rewritten** by the converter: 23 in library, 15 in
  baselines (38 tracked), 373 in audition and 33 in pending. Your pending
  files as you last saw them are copied at
  `renders/scratch/ab43/pending_before/` (44 files). An Envelope's stages and
  a Curve's knots now read down instead of across:

  ```
  before                                   after
  "stages": [                              "startVal": [0.0, 1.0, 0.7, 0.7],
    {"startVal": 0.0, "endVal": 1.0,       "endVal":   [1.0, 0.7, 0.7, 0.0],
     "type": "Linear", "percent": 0.05,    "power":    [0.0, 0.0, 0.0, 0.0],
     "minSec": 0.05, "maxSec": 1.0,        "type":     ["Linear", "Linear", "Linear", "Sine"],
     "power": 0.0, "holdPct": 0.0},        "percent":  [0.05, 0.1, 0.0, 0.2],
    … three more …                         "minSec":   [0.05, 0.025, 0.0, 0.0],
  ]                                        "maxSec":   [1.0, 0.5, 0.0, 0.0],
                                           "nominal":  [0.0, 0.0, 0.0, 0.0]

  "knots": [[100, 0.2], [1000, 0.9]]       "x": [100, 1000], "y": [0.2, 0.9]

  "exprKnots": [{"x": 440, "form":         "exprX": [440], "exprForm": ["linear"],
    "linear", "a": 8, "b": 0}]             "exprA": [8], "exprB": [0]
  ```

  (`patches/baselines/BaselineSIN.json`'s envelope, numbers shortened.) A
  stage's `holdPct`, the field removed in September and 0 in every file, is
  gone. Thirteen keys that no form ever read were dropped with a note: the
  twelve `release` and `releaseMax` keys on the six `algev_test_*` baselines'
  `ar` envelopes (CLEANUP 24), and one `attack` on a stage-list envelope in
  audition. One audition envelope said `"timeMode": "percent"`, which the
  engine read as `fraction`; the file now says `fraction`.
- **What stops loading, and the one command that brings a file back.** A file
  that still holds an Envelope `stages` list or a CurveNode `knots` or
  `exprKnots` list fails to load, in the engine and in the editor, with

  `node 'env1' (Envelope): 'stages' is no longer read: python tools/convert_patch.py <patch> rewrites it as the eight stage columns`

  The four converted folders hold none. The three that are not converted do:
  954 of sweep's 1,370 files, 54 of scratch's 71 and 124 of old's 822 (by the
  converter's count, 64 of old's 124 loaded until now; the rest already met
  4.2's refusals).
  `python tools/convert_patch.py <file or folder>` rewrites them in place (in
  `patches/old`, 28 files are refused by an older rule, as at 4.2). About
  fifty `gen_*.py` generators still write the old forms; their output meets
  the same refusal. The seven that already pipe through the converter now
  stop if it refuses a file.
- **A hand-written `ExplicitPartials` with only `mult1` and `ampl1` is
  refused now**, where it loaded. A table's columns are all there or all
  absent, at one length, for every type (Part 4 §4.4). No file in the tree
  has that shape; two fixtures in the tests did.

**In the editor**, item by item. None of this was exercised by hand (no
desktop control in this session); the headless modes cover load, save,
preset Apply and the refusals, and the list at the end of this section says
what only reading covers.

- **Envelope: `timeMode` is a dropdown among the settings** (`fraction`,
  `seconds`), in place of the "seconds (timeMode)" checkbox above the stage
  table. The checkbox's tooltip is gone.
- **The stage table looks and edits as it did.** What it edits is the node's
  eight columns; the Curve dropdown's names come from the `type` column's own
  labels. Add Stage appends a row that continues the last row's end value;
  Remove deletes the row.
- **An ADSR whose four sustain cells you have made unequal is no longer
  overwritten by the `sustainLevel` knob.** The knob is a view of those four
  cells: when they are equal it shows and sets them; when they are not it
  reads 0 and does nothing. Before, an envelope laid out by the ADSR preset
  carried a hidden flag that let the knob overwrite them anyway.
- **Enums are written as words on the next save.** `"operation": "Sum"`,
  `"mode": "Lowpass"`, `"timeMode": "fraction"`, `"interp": "logx"`, where the
  editor wrote `3`, `0` and so on. A file changes only when you save it; the
  engine reads both. (4.5 rewrites the remaining numbers on disk.)
- **A saved Envelope always holds all eight columns**, `nominal` included
  (the old saver wrote `nominal` only when non-zero), and an Envelope with no
  stages saves as eight empty columns and reloads with none.
- **CurveNode, three faults mended in passing**, each older than this
  checkpoint and each in the editor only: switching a curve's mode back to
  *points* never reached the live node (it stayed in expressions mode until a
  reload); Save As Node Graph wrote no knots, interp, mode or expression rows
  for a CurveNode at all; copy and paste produced an empty default curve. All
  three now go through the same columns as everything else.
- **A new CurveNode's two seed knots now reach the live node.** They were in
  the editor's copy and in the saved file but not in the node, so a fresh
  curve passed its input through untouched in the editor and clamped it to
  0..1 once saved or Generated. It now clamps in both.
- **Duplicate** carries an Envelope's `timeMode` (it did not); it still does
  not carry an Envelope's stages or a Curve's rows, as before.
- **During a stream**: a `timeMode` you change while a stream plays is kept
  when the stream stops (the review found the first version of this
  checkpoint reverting the node and not the editor's copy); a save during a
  stream no longer writes the stream's temporary hold stage.
- **The editor refuses** a file with a retired key (before it clears the
  open graph), and a file whose table is missing a column or has columns of
  different lengths — the engine's rule and sentence. Before, the editor
  loaded such a file and its next save filled the gaps from the default ADSR.
- **A load that fails partway leaves an empty, untitled graph**, where it
  left a half-built one that still carried the refused file's name and would
  have been written over that file by a Save. (A paramMap or a retired key is
  refused earlier still, before the open graph is touched.)
- **Array edits take the audio lock**, a whole table in one lock. The stage
  table and the curve editor used to write into the live node unlocked.

**Not exercised by any command, read only:** the live stage table (cell
drags, the Curve dropdown, Add, Remove on a sounding node); the live curve
editor (cells, the mode and interp dropdowns, "+ point", remove); the shape
editor's Curve client; copy and paste of a node; Duplicate and Replace-with;
a new node's seed; everything about a running stream beyond one headless
stream dump; the status line on File > Open of a refused file. The mutation
run confirmed the cost of this: one deliberate fault in the curve editor's
push (section 4) is caught by nothing, because no headless path drives the
mode dropdown.

**Found in passing, not fixed:**
- `python tools/lint_patches.py patches/audition` reports 36 files
  (`ks_piano_v5`, `ks_piano_v6`) whose instrument block carries `release`,
  a key retired in August. Older than this checkpoint; audition had never
  been linted. The files load.
- Replace-with copies settings by name between unrelated types, so replacing
  an `SVFSource` with a CurveNode carries the filter's `mode` number into the
  curve's `mode`.
- A neither-form Envelope (no table, no preset) is an `ar` in the engine and
  the editor's default ADSR in the editor (CLEANUP 26, unchanged). No file
  has one.

## 2. The shape

**A table of rows became a table of columns, in the file and in the node.**
Until this checkpoint an Envelope's stages reached the engine through code
written for that one form, in four places:

| Who read or wrote a stage list | Where | Now |
|---|---|---|
| the engine, at load | a 25-line loop in `patch_loader.cpp`'s Envelope branch | the generic array pass every node goes through |
| the editor, at load | `envelope_stages_from_json` | the generic array restore |
| the editor, at save | `envelope_stages_to_json`, called from both savers | the generic array emission |
| the editor's clipboard | its own `stages` block | the generic arrays |

and a Curve's knots the same way, through the loader's CurveNode branch and
four members of the editor's `GraphNode`. All of that is gone. What replaced
it is nothing new: the node *declares* its columns, and the code that
already loads, saves, lists and edits any declared array does the rest.

```cpp
// envelope.h — what an Envelope declares for its table
Array colStartVal{this, "startVal", "stages", 0.0f, -100000.0f, 100000.0f};
Array colEndVal  {this, "endVal",   "stages", 0.0f, -100000.0f, 100000.0f};
Array colPower   {this, "power",    "stages", 0.0f, 0.0f, 100.0f};
Array colType    {this, "type",     "stages", 0.0f, 0.0f, 4.0f, kStageTypeLabels};
Array colPercent {this, "percent",  "stages", 0.2f, 0.0f, 1000.0f};
Array colMinSec  {this, "minSec",   "stages", 0.0f, 0.0f, 1000.0f};
Array colMaxSec  {this, "maxSec",   "stages", 0.0f, 0.0f, 1000.0f};
Array colNominal {this, "nominal",  "stages", 0.0f, 0.0f, 1000.0f};
```

Each line is a pin: a name in the file, a group (the table it belongs to), a
default for a new row, a range. The second argument that repeats, `"stages"`,
is what makes eight arrays one table.

**The columns are the store; the layout the render walks is derived from
them.** The render does not read eight vectors. It walks a
`std::vector<Stage>`, as it always has, and that vector is rebuilt from the
columns by one private function, `rebuild_stages_()`, whenever a column
changes:

| Door | What it does |
|---|---|
| `set_array(name, values)` (the loader, the editor) | replaces a column, then derives |
| `add_stage`, `remove_stage` | write a row into / out of all eight columns, then derive |
| `set_curve(i, type, power)` | writes two cells, then derives |
| the `sustainLevel` setting | writes four cells, then derives |
| `replace_stages` (the Preset combo) | takes another envelope's columns, then derives |

Nothing else writes `stages_`, and since the fix wave the columns are
private members, so nothing outside the node *can* write a column except
through `set_array`. The proof that the sound did not move is then short: the
functions that make the sound — `next_raw_`, `on_start`, `gate_release` —
have three changed lines between them, each the same read of the timing flag
spelled as a call.

`CurveNode` has the same shape with six columns in two tables (`x`, `y`; and
`exprX`, `exprForm`, `exprA`, `exprB`) and two derived vectors; `map` and
`map_expr` changed in the names they read and nothing else.

**The enum column.** A column of a table could only hold numbers. The stage
`type` is a word in every file, so `ArrayDescriptor` gained `enum_labels`:
the column holds indices in memory (0 for Linear … 4 for Hold) and the file
holds the labels. On the way in a cell may be a label in any case or a whole
number inside the labels; anything else fails the load naming the node, the
column, the value and the labels. On the way out the editor writes labels.
Settings had this already, with a hand-written match in each of two loaders;
both now call one function, `enum_index`.

**Three things that used to be special are settings now.** `timeMode` on an
Envelope (it was a bool the stage half of the loader and the ADSR preset
each set in their own way); `interp` and `mode` on a CurveNode. They load,
save and show in the editor like any other setting.

**`sustainLevel` is a view, and that fixes the order of a load.** The
setting has no storage of its own: reading it returns the four sustain cells'
value when they are equal, and setting it rewrites those four cells. A load
applies a node's settings first and its arrays second, in both hosts, so a
`sustainLevel` key in a file is applied before the table exists and the
table stands. (The converter refuses a stage list whose `sustainLevel`
differs from its cells, because the old loader applied it *after* the stages
and the meaning would change; no file has one.) Part 4's builder at 4.6 must
keep that order, and a test now fails if either host reverses it.

**The Envelope always maps its output** through `minValue` and `maxValue`
(Part 4 §5.3): `lo + (hi − lo) · raw`, where it used to return `raw`
untouched when neither pin was wired. With the defaults 0 and 1 that is
`raw` exactly, with one exception by construction, a raw −0.0 leaving as
+0.0, which section 4 shows no WAV can hold.

**What the loader lost.** Its CurveNode branch is deleted; its Envelope
branch is "the stage columns, or a preset"; the `FormantPeaks` configurator,
which was the equal-length rule for one type, is deleted in favour of the
same rule for every table (eleven configurators left of twelve). The list of
keys the loader reads by hand went from 113 to 99.

## 3. The tasks, commit by commit

| Task | Commit | What landed |
|---|---|---|
| 0 | `9e9af05` (the plan) | the base's gate results; the old arm (4.2's, spot-checked); a census of all 3,146 patch files; 24 synthetic patches and 30 controls rendered under the base engine in the old forms; pending copied |
| 1 | `4af294a` | the enum column; one label match for settings and columns; the ragged-table rule for every type; the editor writes an enum as its label and pushes arrays under the audio lock |
| 2 | `cabda0c` | the Envelope: eight columns the store, `stages_` derived; `timeMode` a setting; the output mapped always; the editor's stage table on the columns |
| 3 | `76ae90e` | the Curve: six columns, two settings; the editor's curve editors on the columns |
| 4 | `1d73de8` | the converter's two rules, `envelope_table` and `curve_table`, and the driver's edges |
| 5 | `03e1454` | the tree converted — 38 tracked files, 373 in audition, 33 in pending — under Task 3's binary, not rebuilt |
| 6 | `2765aef` | the old forms refused by name in both hosts; the loader's Curve branch, the stage half of its Envelope branch and the editor's readings of both deleted |
| 7 | `d910f27`, `fb4e453`, then the documents | the close: a mutation run, the review and its fix wave, the re-review, the measurements, this report |

Each commit builds every target. Tasks 2 and 3 taught the engine and the
editor the columns while both still read the lists, so that Task 5 could
convert the tree under a binary that was not otherwise changing, and Task 6
could delete the old reading with the files already converted.

Execution was as 4.2's: Opus 5.5 executors on every task, each from a brief
file, none of them committing; the coordinator read each diff, reran the
gates on its own build, edited where the reading called for it (once:
`make_glide`), and committed. The review ran beside the mutation run, the
re-review beside the close's measurements. You paused the session on Tuesday
evening with Task 1 nearly verified and resumed it on Friday evening; the
tree was untouched in between. Usage: the weekly Fable window stood at 82
percent at the start and 92 when Task 6 was committed on Friday night; it
reset on Saturday at 02:00 HST, before the fix wave. After the reset the fix
wave, the re-review, the measurements, the second wave and these documents
took it from 3 to 7 percent: about fourteen Fable points for the checkpoint,
where 4.2 cost thirteen. The all-models window went 46 to 56 percent and then
2 to 6. Extra usage was not touched.

**The verification is amended from 4.4 on (your ruling, 2026-11-14).** Near
the end of this checkpoint you asked why it was taking hours, and observed
that a little code was being followed by a great deal of checking. The
numbers agreed: the 131 were rendered about eight times and never differed;
the coordinator repeated each executor's full gate pass seven times and never
got a different answer; and everything real that was found (section 8's last
paragraph) was found by reading or by the mutation run. Four rules, which
you accepted, go into every plan from 4.4:

1. The coordinator reruns gates only when it edited something or a hash
   disagrees; otherwise it checks the executor's command-line binary by hash
   and runs the null gate.
2. Gates are scoped by what changed: a command-line binary byte-equal to the
   last one that passed skips every render gate; an editor-only change runs
   the build, the tests, the smoke and the roundtrip gate.
3. The mutation run is sized to the tests the checkpoint added.
4. One review wave. What a re-review finds in editor code that 4.7 or Part 7
   deletes is booked, not fixed, unless it loses data.

What stays: the independent read, and one proof per kind of change (here:
the node change, the conversion, the deletion). Part 4 section 8.6 carries
the amendment.

## 4. The proofs

The claim has three halves, and the plan kept them apart so that a
difference would name its cause: *the nodes with their new storage do what
they did*, *the converted files say what the old forms said*, and *the
engine without the old readers does what it did for a converted file*.

**The old arm** is 4.2's close: no engine or patch commit lay between it and
the base but `MPXTest3`'s retirement, and six spot renders under the base
binary matched. Every comparison below is against the base engine's renders
of the *unconverted* files.

| Step | What changed | What was rendered | Result |
|---|---|---|---|
| Tasks 2 and 3 | the nodes and the editor; files untouched | the 131, the nine and the 24 synthetics, all in their old forms | identical, each task |
| Task 5 | the files; the binary not rebuilt (one sha256 before, after and at the coordinator's rerun) | the 131 converted; the nine and the 24 converted in scratch | identical |
| Task 6 | the old readers deleted | the same converted files | identical; the old forms refused |
| the fix wave, the close | the review's fixes | the same | identical: 131 of 131, 9 of 9, 24 of 24, 21 templates the same |

- **The synthetic set** exists because the 131 do not hold a `Hold` stage, a
  seconds-mode envelope with a stage longer than the note, a `timeScale`
  pushed per note into a stage list, a range pin fed by a signal, an empty
  stage list, or every curve mode and interpolation. Each of the 24 has
  controls — the same patch with the feature altered — and 35 relations were
  checked under the base engine before anything changed: 31 controls that
  must differ do, and four that must be identical are (absent stage keys
  against the same keys written as zeros; `"percent"` against no `timeMode`;
  no `interp` against `linear`; expression rows changed under `mode:
  points`). So each feature is known to reach the render.
- **The conversion, three ways.** An independent script written from the
  plan and the old loader, importing nothing of the converter, checked each
  of the 38 tracked files against its committed form: they differ only in
  the lists become columns with the loader's defaults, the `holdPct` and the
  twelve dead keys gone (38 PASS; 64 stage lists, 74 knot lists, 30
  expression-row lists). The converter's executor re-derived, from each
  converted file's columns, the stages and knots the old loader would have
  built, over 53 files chosen for their shapes: 0 mismatches. And the
  reviewer rebuilt the converter from its commit, re-derived all 38 files
  from their base blobs — byte-identical to what was committed — and rendered
  each base file under the base engine against its converted file under the
  Task 3 engine: 38 of 38 identical.
- **The node change.** For the Curve, `map(x)` over 18 curves and 19 values
  of x was recorded as float bits from the unchanged header before any edit;
  the test holds those bits as literals, and a node filled through the
  setters, one through the columns and one through the columns in reverse
  order all give them. For the Envelope the twin compares a node built by
  `add_stage` with one built by `set_array` over 67 stage lists; the review
  pointed out that both arms derive through the same function, so what the
  twin proves is that the order columns arrive in does not matter, and the
  field-by-field mapping is pinned by separate checks. The base-anchored
  proof for the Envelope is the renders.
- **The departure.** An Envelope whose raw value is −0.0 now leaves as +0.0;
  a test pins it. The command-line tool writes 16-bit PCM
  (`engine/src/wav_writer.cpp`: clamp, then `int16_t(x < 0 ? x*32768 :
  x*32767)`), so both zeros write `00 00`, and every sum on the way starts
  from a buffer of +0.0, which turns a −0.0 into +0.0 before the writer sees
  it.
- **The refusal.** Of the 24 old-form synthetics, the 23 that hold a stage
  list or a knot list are refused by the command-line tool and by `mforce_ui
  --roundtrip`, each naming the same node and key, writing nothing; of the
  nine old-form score-less files, the eight that hold a stage list. The two
  that hold no retired key load and render their old hashes.
- **The standing gates**, each at the base's result after every task: the
  null gate 77 of 77; the allocation gate 0 of 131 (`viola_default` drives
  `sustainLevel` per note into a table, so the rewrite of four cells at a
  note's start is covered); the roundtrip gate through the editor, 0 new
  render diffs with its 6 known; the composition gate 21 the same.
- **The mutation run**: 34 single deliberate faults in what this checkpoint
  wrote, each checked against the standing detectors and restored. Thirty
  were caught: 15 of 16 in the engine by `engine_tests`, 11 of 11 in the
  converter by its unit tests, 4 of 7 in the editor by the roundtrip smoke.
  The four that were not: the Envelope's rebuild taking its first column's
  length (now a test); the Preset combo not refreshing the editor's copy of
  the table (now a smoke case); a refresh at load that turned out to be
  redundant (deleted, and the property it was for given a smoke case); and
  the curve editor's push not carrying `mode`, which only the live mode
  dropdown depends on — left uncaught and booked with the editor's other
  unexercised paths.

**What is not proved.** Audition's 373 converted files and pending's 33 are
rendered by nothing; their proof is the converter's unit tests, the
re-derivations above and `--check`. The editor's live paths, as section 1
lists.

## 5. The meter (`--delta` from the base `9e9af05`)

| Measure | Before | After | Change |
|---|---|---|---|
| files | 223 | 223 | +0 |
| lines | 61,134 | 62,370 | +1,236 |
| files over the limit | 18 | 19 | +1 |
| functions over the limit | 93 | 96 | +3 |
| file-level mutable variables | 118 | 118 | +0 |
| `const_cast` | 10 | 10 | +0 |
| `dynamic_cast` | 65 | 59 | −6 |
| changelog comments | 228 | 219 | −9 |
| duplicate blocks | 157 | 156 | −1 |
| include violations | 12 | 12 | +0 |
| unmapped files | 0 | 0 | +0 |

- **Lines.** This checkpoint added more than it removed. Most of it is the
  test file, 9,374 → 10,212 lines (four new test families); without it the
  tree grew by about 400 lines: the editor +164, `curve_node.h` +84,
  `envelope.h` +72, the loader +38, `descriptors.h` +33, `patch_loader.h`
  +19, `source_registrations.cpp` −14. The hand-written readers that went
  (the loader's Curve branch and stage loop, the editor's two stage-list
  functions and its savers' and clipboard's Envelope blocks) were smaller
  than the declarations, the derivations and the doors that replaced them.
  What was bought is not fewer lines but one path where there were four.
- **Files over the limit, +1**: `envelope.h`, 586 → 658 lines against a
  limit of 600. Its six factories are the part that would move to a header
  of their own (section 7).
- **Functions over the limit, +3**, all three in the test file:
  `run_envelope_table_tests` (323 lines), `run_curve_table_tests` (211) and
  `run_retired_key_refusal_tests` (82). Each is one function with numbered
  parts and should be split at the parts (section 7). In the loader
  `build_graph` fell from 338 lines to 288 and `wire_params_generic` from
  149 to 146; in the editor the load fell from 721 lines to 660 (now
  `load_graph_json`, behind a 41-line `load_graph_from_path` that holds the
  refusals and the guard), `serialize_patch_graph` 402 → 362, `save_node_graph` 164 → 148, and
  `draw_properties_panel` rose 867 → 926 (the stage table finds its
  columns by name). All of those were over and still are.
- **`dynamic_cast`, −6**: eight fewer in the editor, whose savers,
  clipboard, load and curve editors each cast a node to `Envelope` or
  `CurveNode` to reach its stages or knots (one cast in `push_curve` does
  now), and two more in the test file.
- **Changelog comments**: they went with the deleted code and with the
  comments this checkpoint reworded.
- **Duplicate blocks**: the two savers' identical Envelope blocks are gone,
  and the largest block in the editor, the two savers' shared tail, fell
  from 47 lines to 40. One block is shared between the loader and the
  editor's loader by decision (the enum column's read; 15 lines).
- **Timing.** The base binary on the base's files against the close binary
  on the converted files, each of the 131 patches timed three times per arm,
  the arms interleaved and alternating which goes first, every timed render's
  hash checked; the whole run twice. Totals: 76.6 s against 77.2 s (1.007),
  then 72.5 s against 72.6 s (1.001). Per patch the median ratio is 1.001 and
  0.999; the five slowest ratios of one run share no patch with the other's,
  and no patch is above 1.05 in both. Flat. The Envelope's output now takes
  two reads and a multiply-add where an unmapped envelope returned its value
  directly; that does not show.

## 6. Decisions the session made (2026-11-02 standing rule)

Numbers in brackets are the plan's Ledger rulings.

1. **One function derives the stage layout** [31]. The plan had five doors
   each writing the layout and the columns side by side; instead every door
   writes the columns and calls `rebuild_stages_()`. A reader checks one
   function to know what a stage is made of, and the sustain rewrite, which
   runs at a note's start, allocates nothing because the sizes do not change.
2. **The stage table's Add and Remove edit the editor's copy and push**
   [32]; the engine's `add_stage_default` had no other caller and is deleted.
3. **The editor's copy of a table and the node are refreshed in both
   directions** [33, and the fix wave]: after a table push the cached
   `sustainLevel` is re-read; after a settings push the cached table is
   re-pulled, through one function, `pull_arrays`, which leaves the stream's
   temporary stage out.
4. **The retired keys are refused by node type and key, before the
   unknown-key test** [39]. The plan had the old keys simply become unknown.
   `stages` cannot: `PhasedValueSource` reads a `stages` of its own until
   4.4, and the loader's list of known keys is global, so an Envelope's
   `stages` would have passed and loaded as the default preset.
5. **444 files, 38 tracked — the plan said 438 and 32** [29]. The six
   `algev_test_*` baselines hold no list, only two dead keys each.
6. **The converter writes a word out only on a node that holds an old
   list** [30], and reads an Envelope with neither form as the `ar` preset
   the loader builds for it [fix wave].
7. **An `ExplicitPartials` with two of its four columns is refused** [27],
   as the table rule says; two test fixtures gained the columns.
8. **The curve editor stays the one surface for a curve** [36]; the editor
   pushes a curve whole and sorted from one function [37].
9. **`interp` outside its labels reads as linear** [38, fix wave], by making
   the node's own reader total, not by a clamp or a loader refusal: the count
   of out-of-range settings across the tree is 4.6's first task.
10. **One table rule for both hosts** [fix wave]: `ragged_table_refusal`
    takes the node and a function that answers "how long is this column in
    the file", so the loader and the editor share the rule and its sentence
    without sharing a JSON type.
11. **Both nodes' columns are private** [fix wave].
12. **The 5-hour window was paced**: executors two at a time at most [26].
13. **Synthetic (k), the seconds-mode ADSR preset, holds no retired key**
    and loads at Task 6 [25].
14. **`make_glide` sets the setting's value directly** [35].
15. **The stream's hold stage is known, not guessed** [second wave]: a table
    edit or a preset Apply makes the hold forget its stage, and the shape
    test is gone.
16. **A load that fails after the graph was cleared leaves an empty, unnamed
    graph** [second wave], for every caller; the paramMap and retired-key
    refusals still come before anything is touched and leave the open graph
    as it was.

Reversible presentation calls, logged in CLEANUP: the `timeMode` tooltip;
the curve editor's own dropdowns kept in place of generic rows.

## 7. Debt and future cleanup

- **The editor's live paths have no headless driver**: the curve editor's
  mode dropdown (the uncaught mutation), the stage table on a sounding node,
  paste, Duplicate, Replace-with, a new node's seed, a stream. Part 7's.
- **The stream's hold is matched by "the last stage, an expand"**: a preset
  applied during a stream whose last row is an expand at the same index is
  taken for the hold stage and removed at stop. Older than this checkpoint.
- **Duplicate and Replace-with do not carry an Envelope's stages or a
  Curve's rows**; Replace-with copies settings by name across unrelated
  types, and the settings row clamps what it shows, so a carried `timeMode`
  of 2 shows as "seconds" and plays as fraction.
- **Two sentences and one small function exist in both hosts** (the enum
  column's refusal, the two-forms sentence), by ruling 9; the editor's
  loader goes at 4.7.
- **`make_glide` builds an Envelope at a held line's continuation** and now
  appends to eight more vectors there (Part 5's hand-off owns that
  allocation).
- **Fifteen entries of the loader's key list are yielded by no source scrape**
  and were left; the list goes at 4.7.
- **`"dynamicPins": null` inside `params`** on 87 audition and sweep
  envelopes (4.5's unknown-key pass).
- **The 36 audition files with an instrument `release`** (section 1).
- **About fifty generators write the old forms** (7.5); the seven piped ones
  stop on a refusal.
- **A new CurveNode's seed is two knots at 0 and 1**, which clamps every
  input outside 0..1. It is now the same in the editor and the file; whether
  a new curve should start as the identity is a presentation call.
- **The Envelope's twin shares its code under test** (section 4); a
  base-recorded table like the Curve's would need the base engine.
- **`envelope.h` is over the file limit** (658 lines of 600); the six
  factories are the natural cut.
- **Three test functions are over the function limit** (323, 211 and 82
  lines); each has numbered parts to split at.
- **`python tools/test_stable_roundtrip.py` with no folder walks nothing and
  reports clean.** `gates.py` passes the folders; a bare run should refuse.

## 8. The review (R18)

An Opus 5.5 refuter, read-only at the commits `9e9af05..2765aef` while the
mutation run was changing the working tree under it (brief
`renders/scratch/ab43/review/brief_r18.md`; its scripts and outputs in
`renders/scratch/ab43/review/r18/`). Its verdict: **closeable on the engine,
the converter and the tracked tree; 0 Critical, 1 Major, 10 Minor, 5 Flag.**

**What it did that no gate does.** It rebuilt the converter from its commit
and re-derived all 38 tracked files from their base blobs: byte-identical to
what was committed, and unchanged by a second pass. It rendered each base
file under the base engine and its converted file under the Task 3 engine:
38 of 38 identical. It ran nineteen adversarial old-form inputs under both
binaries against the two rules. It read the hot-path diff, the retired-key
refusal on every caller of `build_graph`, and the key list's 113 → 99, and
could not break them.

**What it found.** The claim that did not hold was mine: that the editor's
copy of a table and the node "cannot drift". By reading — no command
exercises these paths — it found four ways they could, one of them new in
this checkpoint.

| # | Severity | Finding | What was done |
|---|---|---|---|
| 1 | Major | A `timeMode` changed during a stream: stopping the stream set the node back to its pre-stream value and left the editor's copy at the new one, so the save and Generate wrote one mode and the live node played the other | fixed: restore hands the node the editor's value |
| 2 | Minor | A save during a stream could write the stream's temporary hold stage (any settings push copied the node's table, hold stage included, into the editor's copy) | fixed: one `pull_arrays`, which leaves that row out |
| 3 | Minor | A new CurveNode's seed knots lived in the editor's copy only; and a comment said an empty curve "maps everything to 0" (it is the identity) | fixed: the seed is pushed; the comment corrected |
| 4 | Minor | The node clipboard's paste set a curve's columns one by one, unsorted | fixed: paste pushes the curve whole |
| 5 | Minor | The converter left an Envelope with neither form untouched, but the new engine reads a `timeMode` on it that the old one never did (rendered: differs). No file has one | fixed: read as the `ar` preset the loader builds |
| 6 | Minor | A bool `sustainLevel` on a four-cell stage list (rendered: differs). No file has one | fixed: compared as the 1 or 0 the loader read |
| 7 | Minor | "Settings before arrays" was pinned at node level only; reversing the two passes in either loader passed every test | fixed: a loader test and a smoke case, each seen failing with the passes reversed |
| 8 | Minor | An `interp` number outside its labels took the log path in expressions mode; the editor's dropdown shows a clamped value | half: the node's reader is total; the display against a carried value is booked |
| 9 | Minor | A table pushed shorter under a sounding envelope read past the end of two vectors | fixed, in the rebuild only |
| 10 | Minor | The editor had no ragged-table rule: a file the engine refuses loaded, and its save invented the missing columns | fixed: one rule both hosts ask |
| 11 | Minor | The columns were public members | fixed: private |
| 12 | Flag | The Envelope twin's two arms share the function under test | left; recorded in section 4 |
| 13 | Flag | The Ledger said a test pins the preset key list against the header; it compared two hand copies | fixed: the test reads the header |
| 14 | Flag | Tasks 2 and 3's engine read an old file's `timeMode`, `interp`, `mode` case-blind where the base compared exactly | left: every file's spelling is exact (the census); the converter writes exact words |
| 15 | Flag | The enum column's refusal and the two-forms sentence exist in both hosts | left until the editor's loader goes at 4.7 |
| 16 | Flag | `make_glide` allocates at a held line's continuation | left, booked for Part 5 |

**The fix wave** (`d910f27`) took findings 1 to 11 and 13 and the mutation
run's four misses (section 4). Every new test was seen failing first: the
load's order with the two passes reversed; the rebuild's size under the
mutation run's own fault; a table set to two rows under a note in its
release (1,299 of 1,300 values outside the remaining stages' range before
the fix); `interp` 5 in expressions mode. The roundtrip smoke went from five
side cases to ten. 8,526 → 8,549 engine checks, 177 → 180 tool tests, and
every render set identical again.

**The re-review**, by the same refuter on the fix wave: closeable. Every
first-pass finding is closed, half-closed by decision (8) or left by a
recorded decision (12, 14, 15, 16). It re-derived the 38 files again with the
fix wave's converter (byte-identical) and measured its two converter gaps as
closed under both binaries. It could check the engine and editor halves of
the fix wave by reading only: its saved binaries are older. It found two
more things in the editor and two wordings:

| # | Severity | Finding | What was done |
|---|---|---|---|
| R1 | Minor | The stream's hold stage was recognised by its shape ("the last stage, an expand, at the hold's index"), and since the fix wave that guess also decided what the editor's copy of the table holds: a preset applied during a stream, or a stage added and set to 0 percent, could be taken for the hold stage and dropped from a save | fixed in the second wave: any edit that replaces the node's table makes the hold forget its stage |
| R2 | Minor | A load that fails after the graph was cleared left a half-built graph bound to the refused file and marked clean, so a Save would write it over the file. Three of this checkpoint's refusals throw at that point, beside older ones | fixed in the second wave: such a failure leaves an empty, unnamed graph, for every caller |
| R3 | Minor | The smoke has ten side cases; its docstring and the Ledger said eleven | corrected |
| R4 | Flag | A test comment described cells a default envelope does not have | corrected |
| R5 | Flag | "1,299 of 1,300 values outside the range before the fix" is the executor's run and cannot be reproduced from the commit | left; the raw output is in `renders/scratch/ab43/t7c/red_b2_b3.txt` |

**The second wave** (`fb4e453`) is those two editor fixes and the two
wordings. It touches no engine file: the command-line tool came out of its
build byte-equal to the close's, so no render could have moved and none was
re-measured (the rule you set on 2026-11-14, section 3); the build, the
tests, the smoke and the roundtrip gate through the editor were rerun.

**What was found by reading that no gate found**, this checkpoint: the
editor's four drift paths and the two converter gaps (the reviewer); that
`stages` is also `PhasedValueSource`'s key, so the plan's refusal, which
relied on the key becoming unknown, would never have fired for an Envelope
(the coordinator, writing Task 6's brief); that 444 files change and not 438
(Task 4's executor); that the roundtrip gate run with no folder walks nothing
and reports clean (Task 1's executor). The gates proved the renders; none of
these would have moved one.

## 9. Reading guide

1. One converted file: `git show 03e1454 -- patches/baselines/BaselineSIN.json`
   (a stage list), `patches/library/winds/oboe1.json` (expression rows).
2. `engine/include/mforce/core/descriptors.h`: `ArrayDescriptor`,
   `enum_index`, `enum_label` — thirty lines.
3. `engine/include/mforce/source/envelope/envelope.h`: the settings and
   `absolute_time()` near the top; then the private section — the eight
   columns, `rebuild_stages_`; then the doors (`add_stage`, `remove_stage`,
   `set_curve`, `on_setting_changed`, `replace_stages`). `git diff
   9e9af05..HEAD -- engine/include/mforce/source/envelope/envelope.h` shows
   how little of the render changed.
4. `engine/include/mforce/source/modulator/curve_node.h`, whole (180 lines).
5. `engine/src/patch_loader.cpp`: `retired_key_refusal`,
   `ragged_table_refusal`, `enum_column_element`, the array pass in
   `wire_params_generic`, and the Envelope branch.
6. `tools/convert_rules/envelope_table.py` and `curve_table.py`: each is a
   docstring that states the rule, a `plan` that decides every refusal, and a
   `write`.
7. `tools/mforce_ui/main.cpp`: `GraphNode::push_table`, `pull_arrays`,
   `push_curve` and its accessors; the stage table in
   `draw_properties_panel`; `stream_envelopes_restore`.
8. `tools/engine_tests/main.cpp`: `run_enum_column_tests`,
   `run_envelope_table_tests`, `run_curve_table_tests`,
   `run_retired_key_refusal_tests`; `tools/rt_smoke.py`'s cases.

## 10. C++ lessons

- **A table of rows against a table of columns, and which one a loop
  wants.** There are two ways to hold a table of four stages with eight
  fields each. *Rows*: one array of four records, each record eight fields —
  `std::vector<Stage>`, the C# `List<Stage>` (of structs) or `Stage[]`.
  *Columns*: eight arrays of four numbers — eight `std::vector<float>`, the
  C# `float[] startVal, endVal, …`. The names people use are
  array-of-structs and struct-of-arrays. They hold the same numbers; what
  differs is what is next to what in memory, and so what is cheap. The
  envelope's render looks at *one stage at a time and all of its fields*
  (start, end, type, power, then the timing), so rows are its shape: the
  whole stage sits in one cache line. A file, a generic table editor and a
  generic loader look at *one column at a time* ("here is a list of numbers
  called `percent`"), so columns are theirs — and a column is something the
  engine already knew how to declare, load, save and edit, where a list of
  records was a special form with code of its own in four places. This
  checkpoint keeps both, each where it fits: columns are what the node holds
  and exposes, rows are what its render walks. The cost of keeping both is
  the next lesson. (A SIMD loop over thousands of rows would want columns in
  the hot path too; four stages walked one at a time do not.)
- **A store and a cache of it, and the one rule that keeps them the same.**
  Two copies of one truth drift unless something makes drift impossible.
  The discipline used here has three parts, and each is worth recognising.
  *One direction*: the columns are the store, `stages_` is derived, never the
  other way. *One function*: `rebuild_stages_()` is the only code that
  writes `stages_`; every operation that changes a stage writes the columns
  and then calls it, even the one that changes four numbers. *The compiler
  as the guard*: the columns are `private`, so code outside the node cannot
  write `colType.values[2] = 3` and skip the rebuild — it has to go through
  `set_array`, which rebuilds. In C# you would write the same thing as
  private backing arrays, a private `Rebuild()` and public methods that end
  in it; the difference in C++ is only that nothing is private unless you
  say so in a `struct` (members of a `struct` default to public, of a
  `class` to private; that is the whole difference between the two
  keywords). The review caught that the first version left the columns
  public: the invariant held because nobody happened to break it, which is
  not the same as holding. The mutation run then showed the other half of
  the lesson: a rebuild that sized the layout by its *first* column instead
  of the shortest passed every test, because no test ever handed it columns
  of different lengths. An invariant needs a test at its edge, not only in
  its middle. The same pattern appears a level up in the editor, where the
  editor's copy of the table (`arrayValues`) and the node are two copies
  again, kept one by `push_table` in one direction and `pull_arrays` in the
  other — and that is exactly where the review found its drift. The names
  say the direction from the editor's side: *push* hands the editor's copy
  to the node, *pull* takes the node's.
- **A setting that is a view.** `sustainLevel` looks like any other setting
  and stores nothing. `Envelope` overrides the virtual `get_setting` so that
  the name answers with the four sustain cells' value, and reacts to
  `set_setting` by writing those cells. In C# this is a property with no
  backing field: `float SustainLevel { get => HasAdsrShape ? endVal[1] : 0;
  set { if (HasAdsrShape) { endVal[1] = startVal[2] = … = value; } } }`. Two
  consequences follow, and both cost something in this checkpoint. First, a
  view makes *order* matter: set it before the table exists and nothing
  happens, set it after and it rewrites four cells. That is why "settings,
  then arrays" is now a pinned rule of a load, and why the converter refuses
  the one old-form input whose meaning the order would change. Second,
  anything that caches a view's value holds a copy that goes stale the moment
  the thing viewed changes — the editor caches every setting, so after a
  table edit it re-reads this one. When you see a getter that computes
  instead of returning a field, ask who caches it.
- **An enum is a number in memory and a word on disk, and C++ will not write
  the mapping for you.** `enum class RampType { Linear, Expo, InverseExpo,
  Sine, Hold };` is five integers, 0 to 4, with names the compiler forgets:
  C++ has no `Enum.Parse` and no `ToString()` that gives `"Sine"`, because
  it has no reflection. So the node carries the words itself, as
  `static constexpr const char* const kStageTypeLabels[] = {"Linear", "Expo",
  "InverseExpo", "Sine", "Hold", nullptr};`. Read the type right to left: an
  array of constant pointers to constant characters — five C strings and a
  null pointer that marks the end, so that code handed only the pointer can
  count them. Two guards keep the words and the numbers together. A
  `static_assert` beside the labels states `int(RampType::Hold) == 4` and its
  neighbours, so reordering the enum without the labels is a compile error
  instead of envelopes that quietly change shape. And `enum_index`, the one
  function that turns a word into an index, is the only place a label is
  compared — whole-string, ignoring case — where before there were two
  hand-written loops that could disagree. On a miss the two kinds differ on
  purpose: a table cell with an unknown word fails the load (a wrong stage
  type is a different sound), a setting keeps its default and says so on
  stderr (the long-standing rule for settings). Why words on disk at all,
  when numbers are shorter: `"type": [0, 3]` means nothing to a reader, and
  it changes meaning if an enum ever gains a member in the middle.
- **Sharing a rule without sharing a type: a function that takes a
  function.** The table rule ("all columns, one length") is needed by the
  engine's loader and by the editor's, and each holds the file as a JSON
  object of its own. A shared function that took the JSON would force a JSON
  header into a place the module map keeps it out of. So
  `ragged_table_refusal(node, length)` takes the node and a *callable*,
  `std::function<int(const char* column)>`, and each host passes a small
  lambda that answers from its own JSON: `[&](const char* c) { return
  params.contains(c) ? int(params[c].size()) : -1; }`. The rule knows
  nothing about JSON; the hosts know nothing about the rule. In C# the
  parameter would be a `Func<string, int>`; the `[&]` is the capture list —
  it says the lambda may use the surrounding function's variables by
  reference, which is safe here because the lambda does not outlive the
  call. `std::function` costs an indirect call and sometimes an allocation,
  which is why it is not used on any per-sample path in this engine; at load
  time, once per node, it is the right tool. The same idea, one level
  simpler, is `retired_key_refusal`: it returns a *sentence* and lets each
  host decide how to throw it, so the engine and the editor cannot word the
  refusal differently.

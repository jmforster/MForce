# Checkpoint 4.4 — every type a declaration (report R19)

**Landed 2026-11-17 on main, `a66d60f`..`590a819` with the review's fix
wave `96539b8` and a second wave of comments and tests, `6b0e17f`** (base
before 4.4 `d4d5ed3`, the commit that carries this checkpoint's plan; plan
`docs/superpowers/plans/2026-11-17-part4-checkpoint-4.4.md`, its Ledger the
record of every ruling and every task). Part 4 section 8.1's fourth row,
started on your "Go" of 2026-11-17 with rulings 1, 3, 5, 7 and 8 shown as
the five to notice. Before this checkpoint the loader filled eight node
types by hand, in six branches that each read a type's keys with their own
defaults, and ran a second reader, a "configurator", after the generic pass
for eleven types. The six branches and ten of the configurators are gone:
each of those types now says what it holds in its own header, as pins, and
the generic pass fills it like any other node. Three new kinds in `core`
made that possible: a string setting, a list of signal inputs, and a check a node runs
on itself after it is filled. `tools/convert_patch.py` rewrote the 30 files
in `patches/library`, `patches/baselines`, `patches/audition` and
`patches/pending` that held one of the old forms; then the old forms'
readers were deleted, and a file that still carries one is refused by the
engine and by the editor, naming the converter. No render in `library` +
`baselines` moved. Nothing in this checkpoint is for your ears. What you can
see is in your files and in the editor, and section 1 lists it.

Reading guide: twenty minutes. Section 1 is what changed in your folders and
in the editor, item by item, and what no command exercised. Section 2 is the
shape: the three kinds and the pattern every converted node follows.
Section 4 is how a change with no audible result was proved, including the
one place it stopped. Section 6 is what the session decided under the
2026-11-02 rule. Section 8 is the independent review, which found a crash
in the editor that no gate could have. Section 10 is the C++ lessons.

## 1. What changed for you

- **Nothing is audible.** The 131 library and baseline patches render
  byte-identically (131 of 131, 0 differ) against renders made by the base
  engine from the unconverted files, measured after every engine task and
  after each of the review's two waves. The nine instrument patches that carry no score — the
  three electric pianos among them, which are three of the six tracked files
  the converter rewrote — were rendered with a score appended: 9 of 9
  identical. The 21 composition templates: 21 the same. Fifty-two synthetic
  patches written for the forms the scope does not hold: 51 identical, and
  the one that differs is the first departure below, equal to its control.
- **30 patch files were rewritten** by the converter: 3 in library, 3 in
  baselines (6 tracked), 22 in audition and 2 in pending. Your pending files
  as you last saw them are copied at `renders/scratch/ab44/pending_before/`
  (44 files). No copy of audition's 22 was taken (Part 4 §8.4 has audition
  rewritten in place); what changed there is one written-out number on
  fourteen nodes and one word on eight, both below. The five old forms and
  what each became:

  ```
  before                                    after
  MultiSource: a weight on each feed        the feeds, and the weights beside them
  "source": [                               "source": [{"ref": "fmBody"},
    {"ref": "fmBody", "weight": 0.5},                  {"ref": "fmTine"},
    {"ref": "fmTine", "weight": 0.5},                  {"ref": "clack"}],
    {"ref": "clack",  "weight": 0.6}]       "weights": [0.5, 0.5, 0.6]

  PhasedValueSource: a list of rows         the feeds, and four columns
  "stages": [                               "sources": [{"ref": "saw"}, {"ref": "pulse"}, {"ref": "sin"}],
    {"source": {"ref": "saw"},              "percent": [0.33, 0.33, 0],
     "percent": 0.33, "min": 0.5,           "minSec":  [0.5, 0.5, 0.0],
     "max": 3.0, "gain": 1.0},              "maxSec":  [3.0, 3.0, 0.0],
    … two more …]                           "gain":    [1.0, 1.0, 1.0]

  a second name for one setting             the setting's own name
  "cutoff": {"ref": "sweep"}                "cutoffFreq": {"ref": "sweep"}     (three filter types)
  "gap": 0.3                                "gapDuration": 0.3                 (RepeatingSource)

  a default the file relied on              the number, written
  VarSource with no "val"                   "val": 0.0
  "operation": "add"                        "operation": "Mix"

  a rule written inside its host            a node of its own, wired
  "expandRule": {"count": 4, …}             {"id": "p_expand", "type": "ExpandRule", "params": {"count": 4, …}}
                                            … "expandRule": {"ref": "p_expand"}
  ```

  Which files: the weights are the three library pianos (`ep_std`,
  `ep_bright`, `ep_dark`) and `pending/inst_fm_rhodes_clack`; the stage list
  is `baselines/phased_test`; `cutoff` is `baselines/filter_sweep_test`;
  `gap` is `baselines/repeating_segment_test`; `"add"` is
  `audition/afp31/afp31_v5` to `_v12` and `pending/inst_fm_rhodes_dual`; the
  `VarSource` with no `val` is the seven `bwg_perc1` and seven `bwg_perc2`
  files in audition. No file in the four folders has an inline rule; that
  conversion exists for scratch and old. The three baselines were hand-laid
  with objects on one line and are now laid one value per line, as five
  files were at 4.3.
- **A file that still holds an old form no longer loads**, in the engine or
  in the editor, and the message says which key on which node and names
  `tools/convert_patch.py`. This is the fate of every unconverted file in
  `patches/sweep`, `patches/scratch` and `patches/old` that holds one: 10
  files in scratch (4 with a `cutoff`, 6 with an inline rule), 67 in old
  (all inline rules) and none in sweep. Most of them already held a stage
  list or a knot list and were refused since 4.3; the converter rewrites
  every old form in one pass. It is the way back in: run on a copy of one
  such file from scratch and one from old, the copy loads and renders.
- **Two files-as-written now play what they say, where the old loader
  played something else.** Both are departures by construction; neither is
  in a library or baseline file.
  1. A `RepeatingSource` that gives `gapDuration` and no `gap`. The old
     loader ran a second reader after the generic pass that wrote `gap`'s
     default, 0.2, over whatever `gapDuration` said. Such a file now plays
     the gap it states. One file in the four folders has the shape,
     `pending/exp_evolution.json`, and it states 0.2, so it plays as before.
  2. A `VarSource` with no `val` that the converter has **not** visited.
     The old branch read it as 0; the declared default is 1. The converter
     wrote `"val": 0.0` into the fourteen audition files, so they play as
     before. Outside the four folders the shape is in four files, all in
     sweep (`stk_bandedwg_*_struck`), and none of them loads today for
     older reasons (a knot list; and a key, `value`, that the engine never
     knew). So this departure reaches no file that plays.
- **What the editor gained, by declaration alone** (nobody designed these;
  they fall out of the node saying what it holds):
  - The three Butterworth filters show a `sections` row, and the node you
    hear in the editor has that many sections. Before, the editor always
    built two.
  - `HybridKSSource`'s `targetPartials` are played by the editor. Before,
    the editor carried them through a save and played none.
  - `MultiSource`'s weights survive a load. Before, **the editor dropped
    every weight on load and saved the file without them** — shown once at
    Task 3 on a copy of `ep_std`, which came back from a round trip
    rendering differently.
  - `PhasedValueSource` shows its feeds as links and its four columns as a
    table, with `overlap` as a row.
  - Every Partials node has an `expandRule` pin to link an `ExpandRule`
    node to.
  - A `NameGate`'s name survives Duplicate and the node clipboard, and any
    string setting gets a text row and a line on the node's face.
- **What loads now that used to throw.** None of these is in any file in
  the tree; each is a hand-written shape.
  - A `RangeSource`, a `Vibrato` or a `VarSource` with no `params` object
    loads at its declared defaults (each branch threw "requires params").
  - `targetPartials` or a `SegmentSource`'s `values` given as something
    other than an array is skipped without a word, where the branch threw.
  - `"oneShot": 1` (a number where a bool goes) loads.
  - An `operation` word that matches no label loads as the default with a
    line on stderr, where the configurator threw.
  - A Shaper with a `values2` of four numbers and no `values`: the node
    holds the default curve's four numbers, so the two lengths agree. The
    old loader compared against the file's absent array and refused.
- **One number is written longer.** A float setting on the Static pair
  that the editor used to carry through a save as text is now a declared
  setting, and the editor writes every declared float setting widened from
  its 32 bits: `0.6` becomes `0.6000000238418579`. The value is the same.
- **What no command exercised.** The headless modes prove what a load, a
  save and a render do. They do not prove what the panel looks like. Not
  seen by anyone: the text row for a string setting; the `sections` row;
  the Phased table's Add and Remove; the `weights` and `delays` lists on a
  `MultiSource`; linking an `ExpandRule` node to the new pin by hand. A
  string longer than 127 bytes is cut at its first edit in the panel.
- **Known and left in the editor** (section 7 has the list): the weights
  are not kept in step with the links, so removing a middle feed leaves
  the later weights one place off until 4.8; with a rule linked, any link
  change restarts the per-partial phases of a sounding note.

## 2. The shape

### 2.1 Three new kinds in `core`

Until now a node could declare five kinds of thing: a `Param` (a number
that can be a signal), an `Input` (a signal), a `Slot<T>` (a part, such as
a set of partials), a `Setting<T>` (a number, a bool or a choice that is
not a signal) and an `Array` (a list of numbers). Everything a file can say
to a node had to fit one of the five or be read by hand. Three things did
not fit, and each was the reason for one or more hand-written readers:

| What did not fit | Who read it by hand | The new kind |
|---|---|---|
| a word (`NameGate`'s `name`) | the `NameGate` branch; the editor's own text field at four sites | `StringSetting` |
| a list of signals (`MultiSource`'s feeds, the Phased stages' sources) | `MultiSource`'s three overrides; the Phased configurator | `InputList` |
| a rule over several of a node's values (the Shaper's curve B must match curve A) | a block inside the loader's generic pass that asked "is this a Shaper?" | `Component::validate()`, with `warnings()` beside it |

**`StringSetting`** is a pin that holds a `std::string`. It is filed with
the settings, so everything that walks a node's settings meets it, and its
descriptor says `SettingType::String` and `structural` (it cannot be driven
per note). The doors are `set_string` and `get_string`. It is deliberately
not a `Setting<std::string>`: a `Setting`'s contract is a float in and a
float out, with a range and optional labels, and a string has none of
those (section 10 has the lesson).

**`InputList`** is a pin that holds a list of signals in order. Its door
reads like a multi slot's: `set_param` replaces the list with one feed,
`add_param` appends, `clear_param` empties, and a part offered where a
signal goes is refused with the list unchanged. After each change the node
gets one call, `on_input_list_changed`. The list is always *Manual*: the
node's automatic sweep does not prepare, start or pull its feeds, because
both nodes that use it pull each feed in their own way (one mixes them
with delays, the other plays them one after another).

**`validate()`** is asked of every node once it is built and filled — on
the voice path, the plain-sound path and inside each Multiplex clone. A
non-empty answer fails the load, and the loader adds the node's id and
type in front. `warnings()` is the same question for things worth a line
on stderr that should not stop the load.

### 2.2 The pattern every converted node follows

Each node that used to be filled by hand now has the same three-part
shape, which is 4.3's for the Envelope:

1. **The declared pins are the store.** What the file says goes into pins;
   the file, the editor's panel and a save all read and write those.
2. **The render walks a private layout derived from them.** `MultiSource`
   mixes from `entries_` (a feed, its weight, its delay in samples);
   `PhasedValueSource` plays from `stages_`; `HybridKSSource` reads
   `targetAmpl_`. The hot loops read what they read at the base.
3. **One function derives the layout, and nothing else writes it.**
   `rebuild_entries_()`, `rebuild_stages_()`, and the copy in
   `HybridKSSource::on_array_changed`. Every change hook calls it.

An array that sits beside a list — `weights` beside `source`, the four
columns beside `sources` — is read by position and is tolerant in length:
a feed past the array's end takes the declared default (weight 1, delay 0;
a stage of 0, 0, 0 and gain 1), and entries past the last feed are not
read. That is what the two configurators did for an absent key. The strict
rule (empty, or exactly one per feed) is the better one and waits for 4.8,
because today the editor removes a link at five places that do not know
which position it had.

`Partials` takes its expand rule through a slot, `Slot<ExpandRuleNode>
expandRule`, and reads the rule's numbers once, when the slot changes —
which is when the old loader read them.

### 2.3 One table of what is refused, asked in three places

4.3 introduced `retired_key_refusal`: a table of (node type, key) pairs
that the loader refuses by name, each with a sentence that says what the
key became. This checkpoint's old forms do not all sit under a key of the
node. A weight sat on a *feed* (`{"ref": "a", "weight": 0.5}`), under no
key the unknown-key test sees; without a rule for it, an unconverted piano
would load and play all three feeds at weight 1. An inline rule sat where
a *link* now goes. So a row of the table now also says where the form sat:

```
enum class RetiredAt { Key, FeedKey, InlineSlot };
```

Thirteen rows. The engine asks it from `reject_unknown_keys` (a key) and
from one new function, `refuse_feed_shape`, which the generic pass calls
on every value it is about to wire. The editor asks the same table for
the same three places before it clears the graph it is showing, so a
refused file leaves your work on screen.

### 2.4 The converter: five rules, by kind

| Rule | Kind | Files in the four folders |
|---|---|---|
| `key_renames` | a key renamed (`cutoff`, `gap`) | 2 |
| `multi_weights` | an attribute of a feed moved beside the list | 4 |
| `phased_table` | a list of rows become columns | 1 |
| `old_defaults` | a default the old reader supplied, written out | 23 |
| `expand_rule_node` | an inline object become a node | 0 |

Each is a docstring that states the rule, a `plan` that decides
everything, and a `write`. The converter is idempotent: a second pass over
its own output writes nothing. Two of the rules have an edge worth
knowing, both in section 6: a lone `gapDuration` is left alone, and the
`VarSource` rule cannot tell an old file from a sparse new one.

## 3. The tasks, commit by commit

| Commit | Task | What landed |
|---|---|---|
| `d4d5ed3` | plan | the plan; the base. Its binary is byte-equal to 4.3's close binary |
| `a66d60f` | 1 | the three kinds in `core`; the loader's string pass and its `validate()` call; the editor's string cache. No registered type uses a new kind yet; the descriptor dump is byte-identical to the base's |
| `c04ae43` | 4 | the converter's five rules (run beside Task 1; it touches no engine file). Tool tests 180 → 264 |
| `2b351ed` | 2 | eight node types declare what their branch read; five loader branches and the Shaper block deleted; two configurators deleted. No file changes |
| `0b28fdd` | 3 | `MultiSource`, `PhasedValueSource` and `Partials` on the new kinds; their two configurators reduced to readers of the old form |
| `dbe0bc9` | 3b | one line: `RepeatingSource`'s configurator writes `gapDuration` from `gap` only where the file holds `gap` (section 4.3) |
| `36c8fb4` | 5 | the six tracked files converted, under Task 3b's binary, not rebuilt |
| `590a819` | 6 | the old forms refused from one table; the `VarSource` branch, the `expandRule` block, `Partials::set_expand_rule` and eight configurators deleted |
| `96539b8` | 7 | the review's fix wave (section 8) |
| `6b0e17f` | 7 | the second wave: comments and tests only, after the re-read |

Between them, `8aa1a28` and `5afc100` are the Ledger's notes.

What is left of the hand-written loader after this checkpoint: four
branches (the Envelope's preset form, `PerformNode`, `NameGate`'s name
resolution, `MultiplexSource`'s template) and one configurator
(`FormantSequence`), of 75 registered types. Each has a later checkpoint:
4.5, 4.6 and 4.7.

`engine_tests`: 8,549 checks at the base, 9,368 after Task 6,
9,456 after the fix wave, 9,464 after the second. Tool tests: 180 → 264.

## 4. The proofs

### 4.1 Three steps, each with one thing changing

As at 4.3, the old forms were read for three tasks and then refused, so
that no step changed the engine and the files at once:

1. **The nodes change; the files do not** (Tasks 2, 3, 3b). Every file is
   still in its old form. The 131, the nine and the 52 synthetics render
   their old hashes under each new binary.
2. **The files change; the binary does not** (Task 5). The converter runs
   under Task 3b's binary, whose sha256 was checked at the start, at the
   end and by the coordinator. Same hashes.
3. **The old readers are deleted** (Task 6). The converted tree renders the
   same hashes; the unconverted forms are refused.

### 4.2 The instruments

- **The 131** (library + baselines) against arm `old`, rendered by the
  base engine from the unconverted files: identical after Tasks 1, 2, 3,
  3b and 6 and after both of the review's waves. At Task 5 the binary had
  not changed, so by your second rule only the six tracked files that
  changed were rendered: 6 identical.
- **The score-less nine.** Three of the six tracked files that were
  converted are the electric pianos, which carry no score and render
  silence in the 131. They are heard in the score-less arm: each of nine
  such patches with a score appended, 9 of 9 identical at every step. A
  converted `ep_std` with one weight changed renders differently — the
  control that shows the weights are played.
- **The synthetic set.** The scope lacks twelve of the forms this
  checkpoint touches, and the code that read the old ones is gone at the
  end, so the old arm had to be rendered first: 52 patches, each rendered
  once under the base binary at Task 0, with 48 twins and controls and 62
  stated relations between them (this renders the same as that; this
  differs from that), all of which held at the base. After the last task:
  51 identical to the base, and k2 — the lone `gapDuration` — equal to its
  `gap` 0.5 control, as section 1's first departure says.
- **The twins.** For each node whose filling moved, the base's answers
  were recorded *before any edit* by a temporary printer in `engine_tests`:
  one 64-bit hash of the output's float bits per case, 64 cases at Task 2
  and 40 at Task 3. The tests hold those numbers as literals, so they are
  the old engine's answers and not the new code's opinion of itself. Each
  `MultiSource` and Phased case is filled in both orders (arrays before
  the feeds, and after) and equals its literal.
- **The standing gates** after every engine task: the null gate 77 of 77,
  the allocation gate at 0, the roundtrip gate at its 6 known, the comp
  gate 21 the same, `rt_smoke` at its standing shape.
- **The descriptor dump** changed from the base to the end by exactly the
  declarations in the Ledger and nothing else.

### 4.3 Where it stopped: the null gate at 76 of 77

Task 5's first run converted the 30 files and the null gate failed on one,
`repeating_segment_test`. The executor stopped, as its rules say, and
found the cause with four files: the converted file rendered the bytes of
the old form with `gap` at its default.

The cause was in the plan, not in the code the executor ran. The plan
says "the engine reads both forms" during the three tasks. For
`RepeatingSource` that was false: its configurator still ran after the
generic pass and wrote `gap`'s default over `gapDuration`, so a converted
file, which holds `gapDuration` and no `gap`, lost its value. The survey
at planning had recorded exactly this shadowing. The task list did not
carry it into Task 5.

The fix was one line (the configurator writes from `gap` only where the
file holds `gap`), four checks, and the whole gate set run by the
coordinator because the coordinator had edited. The six tracked files
were put back, the fix committed as Task 3b, and Task 5 run again under
the new binary. The gate did what a gate is for: the mistake reached no
commit.

The lesson is in section 10: a "reads both forms" claim is one claim per
node, and the cheap proof — the synthetic set's *converted* copy rendered
under the transitional binary before the tree is touched — would have
shown it at Task 3.

### 4.4 The mutation run

43 deliberate one-line faults in code this checkpoint wrote, each applied,
tested and restored: 30 in the engine, 7 in the converter, 6 in the
editor. **40 were caught by a standing detector**, most by `engine_tests`
on the first run. Three were not:

- **Mutation 9**: the loader asks `validate()` only when the node has a
  `params` object. Nothing failed, because every test probe carried
  `params`. The fix wave added the case — a probe that
  refuses as built, given with no `params` — and showed it fails under the
  mutation.
- **Mutation 40**: the editor does not push a string setting into its live
  node. Nothing a saved file shows can catch it: the save is written from
  the editor's cache. For `NameGate` it also changes nothing that plays,
  because the name is resolved against the instrument's onsets by the
  loader, and the editor's live node has never had it. Booked (CLEANUP
  86).
- **Mutation 43**: a string setting also filed among the float settings.
  It would add a stray numeric row to the panel and nothing else. Booked
  with 40: both need a headless view of the editor's live nodes, which the
  editor's rewrite brings.

### 4.5 The close's measurements

Taken on `96539b8` by an Opus executor (`renders/scratch/ab44/final/`).
The binary's sha256 equalled the one the fix wave's gates ran on, so by
your second rule none of those renders was repeated.

| Measure | Result |
|---|---|
| `engine_tests` | ALL PASS, 9,456 checks (9,464 after the second wave) |
| ctest | 5 of 5 |
| tool tests | 264, OK |
| comp gate, `post43` against `post44` | 21 the same (the tool's "1 differ" is `test_k467_walker`, a prose file missing from both) |
| old forms through the engine and the editor | 20 of 20 refused by both with one sentence each, nothing written (17 synthetics, the 3 pianos); the 33 synthetics that hold no old form and the 6 other score-less files render their old hashes |
| converter `--check`, the four folders | 0 pending, 0 refused |
| retired forms left in the four folders | 0 of each |
| the editor's stamp | current with the engine build |

**The other folders, counted and not converted** (the converter's own
`--check`, all rules, so most of this is 4.1 to 4.3's work waiting):

| Folder | Files | Would be rewritten | Refused | Of this checkpoint's forms |
|---|---|---|---|---|
| sweep | 1,370 | 1,057 | 0 | 4 `VarSource` with no `val` |
| scratch | 71 | 60 | 0 | 4 `cutoff`, 6 inline rules |
| old | 822 | 379 | 28 | 67 inline rules |

The 28 refusals in old are all from earlier checkpoints' rules (a wired
formant frequency no row can hold; two deleted types).

**Timing.** The base engine cannot read the six converted files, so each
arm rendered its own files of the same 131 patches: the base binary on the
base's files, the new binary on today's, three rounds, arms alternating,
best of three, every render's exit code and hash checked. Two whole runs.

| | Run 1 | Run 2 |
|---|---|---|
| ratio new/old, total | 1.000 | 0.999 |
| median per patch | 1.002 | 1.002 |
| min, max per patch | 0.880, 1.063 | 0.903, 1.080 |

The five slowest ratios are different patches in the two runs, none is
over 1.05 in both, and eight of the ten are renders of under a tenth of a
second. Flat, with noise.

**After the second wave.** `6b0e17f` changed comments and tests. Because
the coordinator had edited, the whole gate set ran once more on it
(`renders/scratch/ab44/t7e/gates.txt`): the 131 identical, the nine, the
synthetic set, the 21 composition templates the same, the null gate 77 of
77, the allocation gate at 0, the roundtrip gate at its 6 known. That
build is the one kept as `renders/scratch/ab_tree/cli_post44.exe`.

## 5. The meter (`--delta` from the base `d4d5ed3`)

| Measure | Before | After | Change |
|---|---|---|---|
| files | 223 | 223 | 0 |
| lines | 62,370 | 63,944 | +1,574 |
| files over the limit | 19 | 18 | -1 |
| functions over the limit | 96 | 96 | 0 |
| `dynamic_cast` | 59 | 61 | +2 |
| `const_cast` | 10 | 10 | 0 |
| changelog comments | 219 | 215 | -4 |
| duplicate blocks | 156 | 155 | -1 |
| include violations | 12 | 12 | 0 |

Read with the file counts beside it, the growth is the tests:

| File | Lines before | After | Change |
|---|---|---|---|
| `engine/src/patch_loader.cpp` | 1,384 | 1,296 | -88 |
| `engine/src/source/source_registrations.cpp` | 607 | 470 | -137 |
| `engine/src/component.cpp` | 33 | 63 | +30 |
| `core/pins.h` | 194 | 221 | +27 |
| `core/component.h` | 182 | 199 | +17 |
| `multi_source.h` | 80 | 75 | -5 |
| `phased_value_source.h` | 149 | 184 | +35 |
| `partials.h` | 558 | 579 | +21 |
| `tools/mforce_ui/main.cpp` | 13,430 | 13,489 | +59 |
| `tools/engine_tests/main.cpp` | 10,213 | 11,812 | +1,599 |
| `tools/convert_rules/*.py` (7 files, then 12) | 1,121 | 1,592 | +471 |

- **The loader's two longest functions shrank**: `build_graph` 288 → 207
  lines, `wire_params_generic` 146 → 82. `register_all_sources` 517 → 382.
  All three are still over the 80-line limit, which is why the count of
  functions over the limit did not move; they go at 4.6 and 4.7.
- **`dynamic_cast` +2 is the tests.** The loader lost three (the Shaper
  block's, and the two of the `expandRule` block); `engine_tests` gained
  five, each a test asking a built node for its concrete type.
- **The editor grew by 59 lines**, most of them the string cache and the
  refusal walk; `draw_properties_panel` is 926 → 950. It is the file
  Part 7 rewrites.
- The totals and the two files the second wave touched are as at
  `6b0e17f`; the function lengths and the other files were measured at
  `96539b8`, and the second wave changed comments and one test loop.

## 6. Decisions the session made (2026-11-02 standing rule)

Numbers in brackets are the plan's Ledger rulings.

1. **`set_string` and `get_string` are not virtual** [22]. Nothing needs to
   override the door; a node that wants to react overrides
   `on_setting_changed`, which the door calls.
2. **The converter leaves a lone `gapDuration` alone** [23]. The plan had
   it rewritten to 0.2, since that is what the old loader played. But the
   converter's own output is a `gapDuration` with no `gap`, so the rule
   could not tell its output from its input and a second pass would undo
   the first. The price is section 1's first departure.
3. **The `VarSource` rule cannot tell an old file from a sparse new one**
   [24]. A `VarSource` with no `val` meant 0 to the old loader and means
   the declared 1 now; the rule writes 0 into either. It is for files the
   old loader read, and says so. Every file the editor saves holds `val`.
   The alternative — declare the default as 0, so there is no such case —
   changes what a new Var node in the editor starts at; it is CLEANUP
   81 for you to weigh.
4. **A weight on a single feed object is dropped with a note, not moved**
   [26]. The old configurator did nothing unless `source` was a list, so
   such a feed always played at weight 1. Writing its weight out would
   give the file a weight it never had. The executor caught this in the
   brief. No file has the shape.
5. **The inline form is skipped only on the `expandRule` slot** [28]; any
   other slot given an object where a link goes is refused as before.
6. **The slot restores what a note expanded when it is emptied, and sizes
   the note arrays only when a rule arrives** [29]. Without the first, the
   next note played the last note's expansion, frozen. Without the
   second, the editor's rewire of every node zeroed a rule-less part's
   arrays mid-note.
7. **`RepeatingSource`'s configurator writes from `gap` only where the
   file holds it** [31], section 4.3.
8. **A Phased node's laid-out walk does not move when its list changes**
   (the fix wave; section 8, finding 1). The reviewer's smallest fix kept
   the walk's vectors the list's length, which stops the crash but resets
   a sounding note's layout on any link change anywhere in the graph. The
   rule chosen instead: the walk is by position and covers the positions
   that are both listed and laid out; each plays the feed listed there now
   over the span the note laid out for it. A rewire that clears and refills
   a list with the same feeds then leaves a sounding note exactly as it
   was, which is what the base did, and a feed appended to the list joins
   at the next start. What it does not do, and the header now says so: a
   feed unlinked from the *middle* of the list mid-note leaves each later
   feed playing the span before its own until the next note. That is a
   sounding-note artefact of an edit in the editor, not a wrong render.
9. **The editor refuses a list that holds anything but links** (the fix
   wave; finding 4). The engine accepts a number in a list of feeds. The
   editor can only draw links, so it dropped the number on load and wrote
   the file without it. Carrying the whole list unseen through a save was
   the alternative; it would then silently win over any link you made on
   that pin. A refusal that names the node and the pin loses nothing. No
   file has the shape.
10. **Eleven keys left the loader's allowlist; the rest wait for 4.7**
    (the fix wave; finding 2). `weight`, `delay`, `sections`,
    `targetPartials`, `oneShot`, `normalized`, `absolute`, `baseValue`,
    `bias`, `varPct` and `name` are now admitted only on the types that
    declare them: each had lost its only hand-written reader here. A scan
    of audition and pending found no file that carried one on another
    type, so nothing in use is newly refused. Six more that nothing ever
    read stay until the list itself is deleted.
11. **The Shaper's check reads the decoded segment lists** (the fix wave;
    finding 3), so it refuses what the old loader refused. One acceptance
    is kept and pinned by a test: `values2` of four with no `values`.
12. **Mutations 40 and 43 are booked, not tested** [36] (section 4.4).
13. **The re-read's comments and its one thin test were mended, not
    booked** [37]. Your fourth rule books what a re-read finds in editor
    code that is going away. These were sentences in engine headers that
    said more than the code does — one of them my own ruling — and a test
    that covered four keys of eleven. The edit touches no line that reaches
    a render; the gates ran once on it all the same, because the first
    rule says they do when the coordinator edits.

## 7. Debt and future cleanup

New rows 81 to 93 in `CLEANUP.md`; rows 76 and 80 brought up to date. Each
is also in the Ledger. By where it gets paid:

- **4.6, the builder.** The Shaper's second advisory ("`morph` is ignored
  under hysteresis") is not printed until then: under today's loader a 0
  in the file and a signal look alike to the node. It printed for all 109
  hysteresis Shapers in the four folders and was true of none (84).
  Wrong-shape values that load in silence — a string where a number goes,
  something that is not an array where an array goes, a number for a bool
  — get one refusal for every type (85). A feed's extra key inside
  `dynamicPins` (88); a warning printed once per voice and per clone
  (89); two refusals that name less than they could (90).
- **4.7, the flip.** The loader's allowlist of keys is deleted whole;
  until then six keys on it are read by nothing (76). The feed rule is
  written once in the engine and once in the editor's loader (87).
- **4.8, the operations.** The strict length rule for an array beside a
  list, and the editor removing the row with the feed (83).
- **Part 7, the editor.** The rewire that clears and refills every pin on
  any link change is the root of three sounding-note artefacts, none of
  them in a file (82). The editor's own list (86): Replace-with and a
  string setting; a saver that can write a shorter list than is playing;
  no headless view of its live nodes (mutations 40 and 43); it never asks
  a node to `validate()`; it refuses a list holding a number. An edit to
  an `ExpandRule` node reaches its host only at the next rewire (91).
- **For you to weigh, no date.** The `VarSource` default (81, decision 3).
  A reproducible build, so a hash stands for the source (92).
- **Outside the measurement scope, noted in passing.** The generators that
  write an old form (80). Four sweep files that carry a key the engine
  never knew (93). sweep, scratch and old are not converted, as before.

## 8. The review (R19)

An Opus 5.5 refuter, read-only at the commits `d4d5ed3..590a819` while the
mutation run was changing the working tree under it (brief
`renders/scratch/ab44/review/brief_r19.md`; its scripts and outputs in
`renders/scratch/ab44/review/r19/`). Its verdict: **closeable after the
fix wave; 0 Critical, 2 Major, 3 Minor, 7 Flag.**

**What it did that no gate does.** It rebuilt the converter from its
commit and re-derived the six tracked files from their base blobs:
byte-identical to what was committed, and unchanged by a second pass. It
rendered all 52 synthetics, the three weighted pianos and the three
baselines in their old form under the base engine and in their converted
form under the final engine: every pair identical but the lone
`gapDuration`, as predicted. It fed every unconverted old form to the
final engine, including inside a Multiplex template: each refused with
the table's sentence. It compared the descriptor dump base to final
against the record: exactly the list.

**What it found.**

1. **Major — `PhasedValueSource` under any editor rewire.** I had found a
   narrow form of this at Task 3 (a node prepared empty, then given a
   stage) and held it for the fix wave. The reviewer showed it is the
   general case: the editor clears and refills every list on *any* link
   change, the node cut its walk to nothing on the clear and never grew it
   back, and the guard in `compute` tested the list where it should have
   tested the walk. The next sample reads past the end of an empty vector.
   In C++ that is not an exception; it is whatever happens to be in memory
   (section 10). A gate could not have found it: every gate loads a file,
   and a file fills the list before the node is prepared. Fixed as
   decision 8, with five tests; under the old header the test program
   itself crashes in that section, which is the defect shown.
2. **Major — "`validate()` refuses exactly what the loader refused" was
   false.** My claim, in the brief I gave the reviewer. The node holds its
   segment lists normalised (a list in which nothing is overridden is
   stored empty), so a file with an all-default `segs` beside a mismatched
   `segs2` slipped past a check that the old loader made on the file's own
   arrays. Fixed as decision 11. The claim as it now stands: for
   well-formed arrays the node refuses what the loader refused, with the
   one pinned exception. A sweep of 192 well-formed shapes against the
   base binary found no other disagreement.
3. **Minor — the allowlist** (decision 10).
4. **Minor — the editor drops a number in a list on load** (decision 9).
   The one editor finding that could lose data, so it was fixed and not
   booked.
5. **Minor — `on_start` could allocate after a list grew.** Closed by the
   same change as finding 1: the vectors' capacity is reserved when the
   list changes, at wiring time.
6. **Flags.** A comment on `DrivenPart` promised that no pin a part adds
   is left unprepared, which a list on a part would break (reworded, and
   guarded). A feed's extra key inside `dynamicPins` is not checked (true
   before; booked). The record of what loads now that threw was missing
   four shapes (now in section 1). Replace-with and a string setting; a
   saver writing a shorter list than is playing; two writings of the feed
   rule (booked, section 7). One grep's stated remainder was worded
   loosely (corrected in the Ledger).

**The re-read of the fix wave.** The same reviewer, at `96539b8`:
**nothing that loses data, crashes, moves a render or locks a file out; 1
Minor, 5 Flag.** It rendered its own conversion of the 52 synthetics, the
pianos and the baselines under the fix wave's binary (59 of 59 equal to
its earlier hashes), traced the Phased walk through every order of
prepare, start, list change and compute and found nothing indexed out of
range, and showed the editor's new refusal cannot meet a file the editor
itself saves. What it found was in what the comments and tests *said*:

- The header's rule for the Phased walk, which I wrote, was true of
  appends only. The walk is by position, so a feed removed from the middle
  or replaced mid-note is played against a span laid out for another feed
  until the next start. The comment claimed "a stage removed mid-note
  leaves the walk". Reworded to what the code does (decision 8).
- Only four of the eleven keys taken off the allowlist had a test. Now all
  eleven.
- The `DrivenPart` guard is an `assert`, which a Release build compiles
  out; the comment now says "in a Debug build".
- The editor never asks a node to `validate()`: it opens a Shaper the
  engine would refuse and meets the refusal when it generates. True before
  this checkpoint; booked.

These went in as a second wave of comments and tests only
(`6b0e17f`), with the gate set run once because the coordinator had
edited.

**The verification rule.** This is the first checkpoint under your four
rules of 2026-11-14: the coordinator ran the gate set itself twice, both
times because it had edited (Task 3b and the second wave); every other
task was accepted on the executor's gate file, the binary's sha256 and the
null gate. One review wave; the mutation run sized to the tests added.
One thing the rules assumed turned out not to hold: a full rebuild of
unchanged sources gives a different binary hash on this machine, so a
hash names a build and not a source state. The check still does its job,
because the executor's binary and the coordinator's are the same file in
`build/`; but a comments-only edit cannot be proved by hash, which is why
the second wave paid for a whole gate run (CLEANUP 92). The weekly Fable
window stood at 11 percent when the plan was written and at 24 percent at
the close: thirteen points, where 4.3 cost fourteen.

## 9. Reading guide

1. One converted file: `git show 36c8fb4 --
   patches/library/keys/electric_piano/ep_std.json` (fourteen lines), then
   `patches/baselines/phased_test.json` at HEAD beside `git show
   d4d5ed3:patches/baselines/phased_test.json`.
2. `engine/include/mforce/core/pins.h`: `InputList` and `StringSetting`,
   ten lines each, beside `Input` and `Setting<T>`.
3. `engine/include/mforce/core/component.h` and `engine/src/component.cpp`:
   the door for a list in `set_param`, `add_param` and `clear_param`;
   `set_string`; `validate()` and `warnings()`.
4. `engine/include/mforce/source/combiner/multi_source.h`, whole: the
   smallest example of the store, the derived layout and the one function.
5. `engine/include/mforce/source/envelope/phased_value_source.h`:
   `rebuild_stages_` and its comment, then `compute`'s first lines — the
   fix wave's rule.
6. `engine/include/mforce/source/additive/partials.h`: `on_slot_changed`.
7. `engine/src/patch_loader.cpp`: `retired_key_refusal` and its table,
   `refuse_feed_shape`, the string pass in the settings loop, the
   `validate()` call at the end of `build_graph`'s node loop. `git diff
   d4d5ed3..HEAD --stat -- engine/src/patch_loader.cpp
   engine/src/source/source_registrations.cpp` for how much left.
8. `tools/convert_rules/multi_weights.py` and `phased_table.py`.
9. `tools/mforce_ui/main.cpp`: `GraphNode::stringValues`; the refusal walk
   at the top of `load_graph_from_path`.
10. `tools/engine_tests/main.cpp`: the `three_kinds` probes, the
    `node_twins` tables, `run_phased_list_twin_tests` (its last five
    blocks are the fix wave's), `run_branch_key_tests`.

## 10. C++ lessons

- **A read past the end of a `std::vector` is not an error. It is
  nothing — and that is worse.** The review's first finding came down to
  one expression, `boundaries_[stageIdx]`, evaluated when `boundaries_`
  was empty. In C# or Java, `list[0]` on an empty list throws
  (`ArgumentOutOfRangeException`, `IndexOutOfBoundsException`): the
  runtime checks every index, and the program stops at the line that is
  wrong. In C++, `v[i]` does no check. The standard calls an out-of-range
  index *undefined behaviour*, which means the language makes no promise
  at all about what happens next: the usual result is that it reads
  whatever bytes sit after the vector's storage and carries on with a
  garbage number, and the crash, if there is one, comes later and
  somewhere else. (MSVC's Debug build does check and stops; the Release
  build that plays audio does not.) C++ has the checked form too —
  `v.at(i)` throws `std::out_of_range` — and the reason engine code does
  not use it in a render loop is cost and the no-exceptions-on-the-audio-
  thread rule: the check is a compare and a branch per access, and a throw
  on the audio thread is its own failure. So the guarantee has to come
  from the code's structure instead of from the runtime. That is what the
  fix does. The node now keeps one number, the count of stages its walk
  may touch, computed in the two places the walk or the list can change;
  the loop's bound, the guard and the "is there a next stage" test all
  read that one number. The original bug was a guard on the *wrong
  vector* — `stages_.empty()` when the read was into `boundaries_` — and
  the two had been the same length in every path a file can take. The
  habit to take from it: when two containers are indexed by the same `i`,
  the bound must be something that is true of both, and it should be
  written once.
- **Size and capacity, `resize` and `reserve`.** A `std::vector` has two
  lengths. Its *size* is how many elements it holds; its *capacity* is how
  many it has room for before it must ask the heap for a bigger block. C#'s
  `List<T>` has the same pair (`Count` and `Capacity`). `resize(n)` changes
  the size, and allocates only if `n` is more than the capacity.
  `reserve(n)` changes the capacity and never the size. The engine's rule
  is no heap allocation between a note's start and its last sample, and
  `on_start` runs on the audio thread. It calls `resize`, which is safe
  only if the room is already there. The fix wave makes that true by
  construction: the function that runs when the list changes — at wiring
  time, off the audio thread — calls `reserve` for the list's length, so
  the later `resize` in `on_start` finds the room and allocates nothing.
  "Reserve where you may allocate, resize where you may not" is the idiom.
- **A public door that is not virtual, and a virtual hook behind it.**
  `Component::set_string` is an ordinary member function; it stores the
  string and then calls `on_setting_changed(name)`, which is `virtual`. A
  node that cares overrides the hook, never the door. The idiom's name is
  the *non-virtual interface* (it is the Template Method pattern at its
  smallest). The C# shape is the one you know from events and from
  WinForms: a public non-virtual `SetString` that calls `protected virtual
  void OnSettingChanged(string name)`. The alternative is to make the door
  itself virtual and let nodes override it. The reason not to: a door has
  duties every caller relies on — store the value, then notify, in that
  order, exactly once — and an override can forget any of them. With the
  door fixed in the base, the only thing a node can change is what happens
  *after* the value is stored. Mutation 5 of the mutation run removed the
  hook call from the door and one test failed, which is the whole contract
  pinned in one place. (The plan's table had said `virtual void
  set_string`; ruling 22 is the correction.)
- **Taking a string by value and moving it.** The door's signature is
  `void set_string(std::string_view name, std::string value)`, and inside,
  `s->value = std::move(value);`. In C# a `string` is a reference to an
  immutable object, so passing one and storing it copies a pointer and the
  question never arises. A C++ `std::string` *owns its characters*:
  copying one copies the buffer. A function that is going to keep the
  string — a *sink* — has three ways to take it. By `const std::string&`,
  and copy inside: always one copy. By `std::string&&`: no copy, but the
  caller must write `std::move` or pass a temporary, and a caller holding
  a string it wants to keep cannot call it. By value, then `std::move`
  into the member: the caller's expression decides — a temporary or a
  moved string is moved all the way in with no copy, a named string is
  copied once at the call. `std::move` itself moves nothing; it is a cast
  that says "I am done with this, you may take its buffer". The name
  `value` is the same word `Setting<T>` uses for what it holds, so the two
  kinds of setting read alike at the point of use.
- **Why `StringSetting` is its own type and not `Setting<std::string>`.**
  `Setting<T>` is a template: C#'s `Setting<T>`, near enough. It would
  compile for `std::string`. But a template is also a promise about what
  every `T` supports, and `SettingBase`, which every `Setting<T>` derives
  from, promises a float in and a float out (`set_setting(name, float)`),
  a minimum, a maximum, a default as a float and optional labels — that is
  what lets one loop load, save and draw every setting without knowing its
  type. A string keeps none of those promises. C++ would let us
  *specialise* the template for `std::string` and write a different body,
  which is the trap: the name would say "a Setting" while the thing behind
  it broke the base's contract, and every generic loop would need an "is it
  the string one?" test. A separate type with its own `PinKind` makes the
  difference visible where pins are sorted, and the 31 places in the
  editor that handle a float setting did not have to change. The C# reader
  will recognise the rule: do not derive from a base whose contract you
  cannot keep (Liskov), even when the compiler lets you.
- **A process lesson, from the stop at Task 5.** "The engine reads both
  forms during the transition" is not one claim. It is one claim per node
  type, and it can be false for one of them while every gate passes,
  because until the tree is converted no file in scope *is* in the new
  form. The planning survey had the fact that broke it. The plan's task
  list did not use it. What would have caught it two tasks earlier costs
  almost nothing: when a task teaches the engine a new form, convert the
  synthetic set in scratch and render the converted copies under that
  task's binary, then and there. That step is in the handoff for 4.5's
  plan.

# Checkpoint 4.5 — the terminal types and the document's spellings (report R20)

**Landed 2026-11-18 on main, `061a606`..`60ae3c1` with the review's fix
wave `f9a7e43`** (base before 4.5 `e96e245`, the commit that carries
this checkpoint's plan; plan
`docs/superpowers/plans/2026-11-18-part4-checkpoint-4.5.md`, its Ledger the
record of every ruling and every task). Part 4 section 8.1's fifth row,
started on your "Go" of 2026-11-18 with rulings 4, 10, 14 and 16 shown as
the four to notice. Before this checkpoint three node types were known to
the files and to the editor but not to the registry: the Channel and the
Mixer that end a plain sound, which the loader read out of the JSON by hand
and the editor drew from hand-built pins, and the Note field, a loader
branch with six words spelled out. All three are registered types now. The
loader's plain-sound path reads the Channel's and the Mixer's declared pins;
the Note field's six words live once, on its type. And four spellings the
coming codec will not read have left the files: `tools/convert_patch.py`
rewrote 784 files in `patches/library`, `patches/baselines`,
`patches/audition` and `patches/pending`, and a file that still carries the
old ones is refused by name. No render in `library` + `baselines` moved.
Nothing in this checkpoint is for your ears. What you can see is in your
files and in the editor, and section 1 lists it.

Reading guide: twenty minutes. Section 1 is what changed in your folders and
in the editor, item by item, and what no command exercised. Section 2 is the
shape: a type that is nothing but its declarations, the one list of note
fields, the third shelf, and what a file looks like now. Section 4 is how a
change with no audible result was proved. Section 6 is what the session
decided under the 2026-11-02 rule. Section 8 is the independent review.
Section 10 is the C++ lessons.

## 1. What changed for you

- **Nothing is audible.** The 131 library and baseline patches render
  byte-identically (131 of 131, 0 differ) against renders made by the base
  engine from the unconverted files, measured with the engine changed and
  the files not, with the files converted under binaries that were not
  rebuilt, with the old readers deleted, and after the review's fix wave.
  The nine instrument patches that carry no score, rendered with a score
  appended: 9 of 9. The 21 composition templates: 21 the same. 4.4's
  fifty-two synthetic patches: 52 of 52. Ninety-one synthetic patches
  written for the forms the scope does not hold: 89 identical and 2 refused
  on purpose (the first item under "what loads differently" below).
- **784 patch files were rewritten** by the converter: 18 in library, 39 in
  baselines (57 tracked), 689 in audition and 38 in pending. Your pending
  files as you last saw them are copied at
  `renders/scratch/ab45/pending_before/` (44 files), and this time
  audition's 689 as well, at `renders/scratch/ab45/audition_before/`. What
  each old spelling became:

  ```
  before                                    after
  a channel's and a mixer's "inputs"        feeds in "params", like every other wire
  {"id": "ch1", "type": "SoundChannel",     {"id": "ch1", "type": "SoundChannel",
   "inputs": {"source": "delay"},            "params": {"volume": 0.8, "pan": 0.0,
   "params": {"volume": 0.8, "pan": 0.0}}               "source": {"ref": "delay"}}}
  {"id": "mix", "type": "StereoMixer",      {"id": "mix", "type": "StereoMixer",
   "inputs": {"channels": ["ch1"]}, …}       "params": {…, "channels": [{"ref": "ch1"}]}}

  the editor's data in two places           one block
  "ui": {"positions": {…, "Note1": [x, y],  "layout": {"positions": {… file nodes only …},
          "__output": [x, y]},                          "output": [x, y],
         "panning": [x, y],                             "panning": [x, y],
         "noteFaces": [{"label": "Note1",               "groups": [ … ],
                        "fields": {…}}]},               "faces": [{"label": "Note1", "fields": {…},
  "groups": [ … ]                                                  "pos": [x, y]}]}

  a comment under an underscore             "comment"
  "_comment": "Repro pair, filed …"         "comment": "Repro pair, filed …"
  "_provenance": {"source": "…", "note":    "comment": "source: …\nnote: 69\nwhy: …\nrun: …"
     69, "why": "…", "run": "…"}

  an enum as a number, or in another case   the declared label
  "operation": 3      "operation": "sum"    "operation": "Sum"
  "mode": 4           "mode": "lowpass"     "mode": "Highpass1P"      "mode": "Lowpass"
  "attackCurve": 1    (a preset Envelope)   "attackCurve": "Expo"

  two keys nothing reads                    gone
  "instrument": {…, "release": 0.35}        "instrument": {…}
  "params": {…, "dynamicPins": null}        "params": {…}
  ```

  How many of each, as library / baselines / audition / pending: `inputs`
  2 / 25 / 351 / 3 files; `ui` or `groups` 13 / 6 / 230 / 35; a comment
  3 / 10 / 4 / 0 (`_provenance` is the three `sing_soprano_I`, `_O`, `_U`);
  an enum as a number 7 / 3 / 207 / 27 files (2,153 nodes); a label in
  another case 0 / 8 / 101 / 0 (`multiply`, `sum`, `lowpass`); a preset
  curve as a number 2 / 1 / 97 / 13; `instrument.release` in 36 audition
  files (the `ks_piano_v5` and `v6` sets); a null `dynamicPins` inside
  `params` on 62 Envelopes in 14 audition files. Nineteen audition files
  and six pending files needed nothing. A baseline that was hand-laid with
  several values on a line is now laid one value per line, as the converter
  has done since 4.2, so those files' diffs are larger than their rewrite.
- **A Note node's position moved with it.** A Note node on your canvas is
  several file nodes, one per field you wired, and its position used to sit
  in `ui.positions` under the Note's label, a key that names no node in the
  file. It now sits on the Note's own entry in `layout.faces`, as a group's
  position sits on the group. 489 Note nodes carry one. (Ruling 10, shown
  before your "Go".)
- **Fifteen stale positions were dropped**: `__param_frequency`, where the
  editor's Parameter node once stood, in `library/winds/clarinet1`,
  `baselines/BaselineSIN`, `baselines/FormantSequence1`, nine audition
  files and three pending ones. No node in those files answers to it.
- **A file that still holds an old spelling:**
  - `inputs` on a Channel or a Mixer: refused by the engine and by the
    editor, naming the node and `tools/convert_patch.py`. 107 files in
    `patches/sweep`, 13 in `scratch` and 548 in `old` hold it. Most of
    those did not load before either: sweep's 107 all hold a type that is
    not registered (`GrayScottSource`), and 488 of old's hold a deleted
    type or a form an earlier checkpoint retired. Run under both binaries,
    the files that rendered yesterday and are refused today are **7 in
    scratch and 60 in old, none in sweep**.
  - A top-level `ui`, `groups`, `_comment` or `_provenance`: refused by the
    editor, before it touches the graph you have open; **rendered by the
    command-line tool as before**, which has never read those keys and goes
    on ignoring them until the codec lands (4.7). So between now and then a
    sweep or old file whose only old spellings are those plays from the
    command line and will not open in the editor until converted. 1,120
    sweep files, 57 in scratch and 46 in old hold such a key and no
    `inputs`; about 200, 3 and 4 of them have no older reason to be
    refused, so those are the ones that opened in the editor yesterday and
    will not today (inferred from the converter's rules, not run under
    yesterday's editor). The editor's refusal is the safer of the two
    readings: it stopped reading `ui`, and opening such a file would have
    shown it with no positions and saved it with none.
  - The converter rewrites every old spelling in one pass and is the way
    back in.
- **What the editor shows differently.** None of this was clicked in a
  running editor this session; "what no command exercised" is below.
  - A Mixer has one `channels` pin that takes any number of Channels. It
    had `ch 1`, a `+` button on its title bar, and `ch 2`, `ch 3` as you
    pressed it. The `+` button is gone.
  - A Mixer's outputs read `out` and `tap`, as every part's do; they were
    `outL` and `outR`. A Channel gains a `tap` output. Neither `tap` does
    anything, on these or on any part (section 7).
  - A Channel's unlinked `source` is drawn as a gray name without a number
    and has no value row in the properties panel. It is an input now; the
    hand-built pin was treated as a value.
  - **A node-graph Save keeps the file's `seconds`.** It wrote `5` whatever
    the file said: open a ten-second plain sound, save it, and it was a
    five-second one.
  - **A comment survives a Save.** The editor wrote no comment at all
    before: a baseline's `_comment` was dropped the first time the file was
    saved from the editor (13 in-scope files carried one). It now carries a
    patch's comment and a node's through a load and a save, and shows them
    nowhere — showing them would be a feature.
- **What loads differently.** None of these is in any file of the four
  folders; each is a hand-written shape.
  - **A node pulled by two loops is refused.** In a plain sound each
    channel pulls its source on a clock of its own, and the mixer pulls the
    gains, volumes and pans on a third. A node reached from two of them was
    advanced by both. The old loader caught the simplest case, two channels
    naming one source, and let the rest through: two channels whose sources
    read one LFO rendered, with one of them following the other's clock.
    Such a file now fails to load: `'lfo' is reached from channel 'chA' and
    from channel 'chB': each pulls its nodes on its own clock, so a node
    belongs to one`. A Multiplex's template is not part of this: a
    Multiplex plays copies it builds of its template, so two channels may
    each multiply one template, and a template may read a node that another
    channel reads. (The refusal as first built got that wrong, and the
    review caught it: section 8, F1.)
  - A Channel or a Mixer with a key no declaration knows fails, where the
    loader never looked.
  - A Channel listed by a Mixer with no source fails naming the Channel,
    where it failed as a JSON error that named nothing.
  - A Mixer with no channel list at all loads and renders silence, where it
    failed as a JSON error. (A list written empty rendered silence before
    and does now.)
  - Shapes only the new spelling can write, loading as any pin's feed does:
    a number as a Channel's `source` plays that constant; a tap as a
    Channel's `source` loads and is silent, since nothing pulls its target.
    Neither is something to want; both are booked (section 7).
  - A tap wired into a gain, a volume or a pan loads, where it failed as
    "not supported in this position".
  - A Note field given in another case (`"Frequency"`) or as its index
    loads, where it failed; a preset curve given as a label loads, where
    it failed.
- **What no command exercised.** The headless modes prove what a load, a
  save and a render do. Not seen by anyone: the Mixer's and the Channel's
  faces; linking a Channel into `channels` by hand, and the refusal of
  anything else there; the create menu, paste, Duplicate, Replace-with on
  either node; the node-graph stream's channel order (the editor's stream
  reads a gain once per sample, not once per channel, so nothing headless
  can hear the order); a node-graph Save with a window open (the headless
  save writes no panning).

## 2. The shape

### 2.1 A type that is nothing but what it declares

`SoundChannel` (`engine/include/mforce/source/output/sound_channel.h`) is
nine lines of code:

```cpp
struct SoundChannel final : Component {
  Input source{this, "source", Input::RequiredInput, nullptr, Pull::Manual};
  Param volume{this, "volume", 1.0f, 0.0f, 4.0f, nullptr, Pull::Manual};
  Param pan{this, "pan", 0.0f, -1.0f, 1.0f, nullptr, Pull::Manual};

  const char* type_name() const override { return "SoundChannel"; }
};
```

and `StereoMixer` beside it is the same size: a slot that takes
`SoundChannel`s, in order, and two gains. Neither computes a sample. Each
says what a channel and a mix *are* — a signal, its level, its place
between left and right; some channels and two output gains — and the code
that plays a plain sound reads those pins and assembles the live `Mixer`
from them (`engine/src/patch_loader.cpp`, the end of `load_patch_file`).

| | Before | Now |
|---|---|---|
| Who knows a channel has `source`, `volume`, `pan` | the loader's plain-sound path, in string literals; the editor, in a second hand-written list | the type |
| How a mixer's channels are written | `"inputs": {"channels": ["ch1"]}`, plain ids, a form no other node has | `"channels": [{"ref": "ch1"}]` in `params`, the form of every list of feeds |
| Who checks the keys | nobody | the generic pass, as for every node |
| The defaults (1, 0, 1, 1) | four literals in the loader, four more in the editor | the four pins' declared constants |

The parts are `Component`s and not `DrivenPart`s: a `DrivenPart` is a part
its owner prepares once, starts per note and advances per sample, and
nobody advances these. Their pins are `Manual` for the same reason every
part's are: the base's per-sample sweep runs on signals only. The design
alternative would have kept them out of the registry as the builder's own
record types; Part 4 §5.8 chose registered parts so the codec and the
editor meet them like any type, and the editor's half of this checkpoint is
what that bought: three hand-written readers and two hand-written pin lists
deleted, with nothing written in their place.

They sit in a new family, **Output**, the eleventh
(`engine/include/mforce/source/output/`): they are neither utilities nor
processors.

### 2.2 The Note field: six words, written once

`PerformNode` in a file means "this node is the note's frequency" (or its
velocity, wheel, pressure, duration, onset). The engine never builds an
object for it: under that node's id a voice's graph holds the voice's own
adapter for the field, so several `PerformNode`s of one field are the same
object. That stays. What changed is where the six words live. The loader's
branch compared the file's `field` against six string literals, and a
second reader further down compared it against `"frequency"` again. Now:

- `PerformNode` is a registered type
  (`engine/include/mforce/source/modulator/perform_node.h`) with one enum
  setting, `field`, over `kFieldLabels`. It exists so the registry — and
  through it the descriptor dump, the converter and, at 4.6, the codec —
  can say what the node declares.
- One function, `perform_field`, reads a file's `field` against those
  labels; the branch and the bend check both call it.
- The order of the labels and the order of `PerformOut::Field`, the enum
  the voice makes its adapters by, are tied at compile time by six
  `static_assert`s in the loader, the one file that sees both (section 10).

### 2.3 A third shelf

A registration says three things about a type: its family, its label, and
its *shelf* — `Main` for the create menu's family menus, `Experimental` for
the parked. The create menu and Replace-with are generated from the
registry by shelf. These three types must be in the registry and must not
be in those menus: a Channel and a Mixer belong to a plain sound only, a
graph has one Mixer, a Note belongs to an instrument only, and none of them
is something another node can be replaced with. The editor already has an
item of its own for each, which carries those rules. So there is a third
shelf, `Shelf::None`: registered, and offered by no generated menu
(`engine/include/mforce/core/source_registry.h`). The alternative was two
shelves and three type names skipped by hand where the menus are drawn,
which is the hand list beside the registry that 3.2 removed.

### 2.4 A node belongs to one pull loop

`refuse_shared_nodes` (`engine/src/patch_loader.cpp`) takes its *roots*
from the built parts — for each channel the mixer lists, the node its
`source` holds; for the mixer, the nodes wired into its gains and into each
channel's volume and pan — and its *edges* from the file: every
`{"ref": id}` under a node's `params`, lists included. A `{"tap": id}` is
not an edge, because a tap reads its target's last value and never asks it
to compute. Nor is the ref under a `MultiplexSource`'s `source`: the
Multiplex plays copies it builds of everything that ref reaches and never
pulls the file's own nodes there. The first node, in the file's order, that
two loops reach fails the load, naming the node and both loops in the
mixer's channel order.

Finding "every feed in a JSON value" is one function now, `for_each_feed`,
with three callers: this walk, the Multiplex's copy of its template, and
the list of nodes that only taps read. It was written out three times.

### 2.5 The file after this checkpoint

At three levels a file in the four folders now holds only keys the codec's
record has a place for: at the top `sampleRate`, `seconds`, `comment`,
`instrument`, `score`, `graph`, `layout`; on a node `id`, `type`, `params`,
`dynamicPins`, `comment`; in the instrument block `polyphony`, `volume`,
`sustaining`, `onsets`, `glideMs`. A scan written apart from the converter
finds nothing else. Inside `params` every wire is a feed, every enum is one
of its type's declared labels, and what remains outside a type's own
declarations is the Envelope's preset form (a read-time form by Part 4
§4.5) and four pending files that have not loaded for older reasons. That
is 8.1's sentence for this row: every file in the four folders is in the
codec's form before the codec exists.

### 2.6 The converter: five rules, by kind

| Rule | Kind | Files in the four folders |
|---|---|---|
| `terminal_inputs` | a block beside `params` folded into it | 381 |
| `enum_labels` | a value written as the word its type declares | 333 |
| `dead_keys` | a key nothing reads, dropped | 50 |
| `comment` | a key renamed (and an object become text) | 17 |
| `layout` | the editor's data gathered under one key | 284 |

They run after the ten older rules, in that order: after every rule that
adds or removes a node or writes an enum, and `layout` last, because it
keeps a position only for an id the graph holds. `enum_labels` is the one
rule that needs the engine: it reads each enum's labels from
`mforce_cli --dump-descriptors`, which prints them from this checkpoint on.

### 2.7 One table of what is refused, two more places

`retired_key_refusal(type, key, where)` is the table 4.3 began: every
spelling the engine once read, what it became, and one sentence that names
the converter. A spelling is found by *where it sat*. 4.4 had three places
(a key of `params`, a key on a feed, an object where a slot takes a feed);
this checkpoint adds two: `NodeKey`, a key of the node beside `params`
(`inputs` on the two parts), and `TopKey`, a key of the document (`ui`,
`groups`, `_comment`, `_provenance`). The engine asks every place but the
last; the editor asks them all.

## 3. The tasks, commit by commit

| Commit | Task | What landed |
|---|---|---|
| `e96e245` | plan | the plan; the base. Its binary is byte-equal to 4.4's close binary |
| `061a606` | 3 | the converter's five rules (run beside Tasks 0 and 1; it touches no engine file). Tool tests 264 → 346 |
| `09aa437` | 1 | the three registered types, `Family::Output`, `Shelf::None`; the plain-sound path on the parts' pins, reading the old `inputs` too; the sharing refusal; labels in the descriptor dump and in the preset's curve keys. No file changed |
| `cfb46e7` | 2 | the editor: Channel and Mixer from the registry; `layout` and `comment` read and written, the old spellings still read; `--roundtrip` by graph mode; the roundtrip gate rendering plain sounds |
| `e953579` | 4 | the 57 tracked files converted (784 in all), under Task 1's and Task 2's binaries, not rebuilt |
| `60ae3c1` | 5 | the old spellings refused from the one table; `wire_part_inputs`, the plain-id scan and the editor's three old readers deleted |
| `f9a7e43` | 6 | the review's fix wave (section 8) |

Two docs commits, `9ebc807` and `6b1a431`, carry the Ledger between them.

## 4. The proofs

### 4.1 Three steps, each with one thing changing

A rewrite of 784 files and of the code that reads them cannot be proved in
one step, because a difference could not be assigned to either. So, as at
4.3 and 4.4:

1. **The engine changes; the files do not** (Task 1). The new plain-sound
   path reads the old `inputs` form through a small reader kept for the
   purpose. 131 of 131 identical.
2. **The files change; the binaries do not** (Task 4). The converter runs
   under Task 1's command-line binary and Task 2's editor, their hashes
   the same before, after and at the coordinator's check. 131 of 131.
3. **The old readers are deleted** (Task 5). 131 of 131.

4.4's lesson was that "the engine reads both forms" is one claim per node
type and is proved where the reading is added. So Task 1 did not wait for
the conversion: a script written from the brief, not from the converter,
rewrote scratch copies of all 27 in-scope plain sounds into the new form,
and Task 1's own binary rendered them. 27 of 27 at their old hashes.

### 4.2 The instruments

- **The A/B over the 131**, the null gate (77 of 77), the allocation gate
  (0 of 131), the 21 composition templates, at every engine task.
- **The score-less nine**, with a score appended.
- **A synthetic set of 91** (89, and two the review's F1 added) for what
  the scope lacks, each rendered once
  under the base binary, with a control beside each feature that must
  differ. What only this set hears: two and three channels; a wired
  volume, pan and gain (the mixer's own loop); the two shapes of sharing;
  every enum setting as an index, as its label and in lower case; the six
  presets with each curve; all six note fields in one voice; a file
  carrying every editor spelling at once. Writing it found one fact about
  the base: **reversing a mixer's channel list changes no byte while the
  gains are numbers** (two channels summed into a zeroed buffer). Channel
  order is heard only through a wired gain, which the mixer pulls once per
  sample per channel; the executor added that pair, and it is what proves
  the channels are built in the file's order.
- **4.4's fifty-two synthetics**, riding along.
- **A twin for the plain-sound build**: seven small plain sounds rendered
  by the base's loader before any edit, each recorded as one hash of the
  float bits; the tests hold the seven numbers and the new build must
  reproduce them.
- **The value diff** (Task 4): a script written from the plan, importing
  nothing of the converter, predicts each of the 784 converted files from
  its original and compares. 784 of 784. With its layout transform switched
  off, 284 fail; with the provenance text altered, 3.
- **A scan and a census** after the conversion: no retired spelling in the
  four folders, and no key outside the codec's set at the three levels.

### 4.3 The roundtrip gate reaches the plain sounds

The roundtrip gate loads each in-scope patch in the editor, saves it, and
compares ids and renders. For a plain sound it compared ids only: the
headless mode saved every file through the instrument saver, so a plain
sound came back as something else. That left the 27 in-scope plain sounds —
exactly the files whose reading in the editor this checkpoint rewrites —
with no render proof through the editor. `--roundtrip` now saves as
File > Save does, by the graph's mode; the node-graph saver keeps the
file's `seconds`; and the gate renders all 131: `0 id changes, 0 NEW render
diffs, 6 known diffs`. The six are the ones it has always listed.

### 4.4 The mutation run

Fifty-seven deliberate faults, one at a time, in code this checkpoint
wrote: 35 in the engine and the command-line tool, 7 in the converter's
rules, 15 in the editor. For each: apply the fault, build what the detector
needs, run the repo's standing detectors cheapest first, record the first
that fails, restore the file from a pristine copy and compare it byte for
byte. A fault that only a scratch instrument catches, or that nothing
catches, is a finding.

| Where | Faults | Caught by | Not caught |
|---|---|---|---|
| engine, command-line tool | 35 | `engine_tests` 31; the compiler 1; `convert_patch.py --check` 2 | 1 |
| converter | 7 | the tool tests 7 | 0 |
| editor | 15 | the roundtrip smoke 11 | 4 |

52 of 57. What the caught ones say about the tests: every default (a
volume of 0, a pan of 0.5, a right gain of 0.5), every exchange (volume
with pan, left gain with right, the channel order), every edge of the
sharing walk and every row of the refusal table has a check that fails
without it. The compiler caught one: with two labels exchanged in
`kFieldLabels` the build stops at the `static_assert`s (section 10.3). Two
faults in the descriptor dump (no `labels`; no `arrayLabels`) pass
`engine_tests` and are stopped by the converter itself, which will not
convert under a table that has no labels: decision 9's stop, doing what it
was added for.

The five nothing catches:

- **The bend check compared against the wrong field** (fault 20). The check
  decides whether to print a "bend inert" warning. Nothing else changes,
  and no test reads what the loader prints. Booked (section 7): the
  loader's own warnings become a returned list at 4.6 and are pinned then.
- **The editor treating the Channel and the Mixer as its own special nodes
  again** (fault 43). The two nodes then have no pins, and the round trip
  still writes the file whole and renders its bytes, because whatever the
  editor does not model in a node's `params` it carries through a load and
  a save untouched. So a round trip that reproduces the bytes does not
  prove the editor modelled the node. The one headless place the fault
  shows is the stream, which refuses to start. The fix wave added that as a
  smoke case and showed it failing under the fault.
- **Three in what no command reaches**, listed in the run as expected: the
  stream's channel order, the Mixer's row in the link-compatibility check,
  the Replace-with exclusion.

### 4.5 The close's measurements

Once, on the fix wave's tree. The command-line binary is byte-equal to the
one the fix wave's gates ran on (`69eaf251…`), so no render gate was
repeated. It is kept as `renders/scratch/ab_tree/cli_post45.exe`, the
editor as `ui_post45.exe`.

| Measurement | Result |
|---|---|
| The 131 against the base's renders | 131 identical, 0 differ |
| The score-less nine, converted | 9 of 9 |
| This checkpoint's 91 synthetics, converted | 89 identical, 2 refused as intended |
| 4.4's synthetics, converted | 52 of 52 |
| Null gate; allocation gate; roundtrip gate | 77 of 77; 0 of 131 allocate; 0 id changes, 0 new render diffs, 6 known |
| Composition templates, stage `post45` against `post44` | 21 same |
| `engine_tests` | ALL PASS, 9,889 checks (9,464 at the base) |
| Tool tests | 346 (264 at the base) |
| Roundtrip smoke | 29 side cases, all ok (16 at the base); the six shapes as at the base, 5 of 6, the known `wiring` one |
| `convert_patch.py --check`, the four folders | 0 pending, 0 refused |
| The old forms through both hosts, 152 unconverted scratch files | the command line refuses the 21 that hold `inputs` and renders the other 131 at their old hashes; the editor refuses those 21 and the 3 that hold an old top-level key, and round-trips 128 |
| Timing, 131 patches, two runs of three rounds, base binary on the base's files against the close binary on today's | totals new/old 0.997 and 1.001; medians 1.003 and 0.999 |
| UI stamp | current, `f9a7e43` |

The timing's slowest ratios do not repeat: no patch is among the five
slowest of both runs, and the first run's slowest (`filter_sweep_test`,
1.21) is the second run's fastest (0.92). The longest render,
`cello_full_range`, is 1.010 and 1.003. Flat, as expected: the live mixer
has no diff line, and the plain-sound load reads pins where it read JSON.

**Sweep, scratch and old**, counted and not converted:

| | sweep | scratch | old |
|---|---|---|---|
| files | 1,370 | 71 | 822 |
| `--check`: pending / refused | 1,365 / 0 | 71 / 0 | 792 / 28 |
| hold `inputs` on a Channel or a Mixer | 107 | 13 | 548 |
| … of which rendered at the base and are refused now | **0** | **7** | **60** |
| hold an old top-level key and no `inputs` | 1,120 | 57 | 46 |
| … of which have no older reason for the editor to refuse (inferred) | 200 | 3 | 4 |
| neither; rewritten only by the enum or the dead-key rule, and load either way | 132 | 1 | 25 |

The "refused now" row was measured by running the base binary and the
close binary over every file that holds `inputs`. Most of them did not
load before: all 107 in sweep hold `GrayScottSource`, a type that is not
registered, and 488 of old's hold a deleted type or a form an earlier
checkpoint retired. Old's 28 `--check` refusals are all older ones (a
deleted type; a wired formant peak). One file of each kind from each
folder was probed on a copy: refused with the sentence, converted, loads
and renders; for a top-level key, the converted copy round-trips through
the editor and renders what the original rendered. The exception is
sweep's `inputs` files, where no probe was possible: the unregistered type
stops both binaries first.

## 5. The meter (`--delta` from the base `e96e245`)

| Measure | Before | After | Change |
|---|---|---|---|
| files | 223 | 226 | +3 |
| lines | 63,944 | 64,773 | +829 |
| files over the limit | 18 | 18 | 0 |
| functions over the limit | 96 | 96 | 0 |
| file-level mutable variables | 118 | 120 | +2 |
| `const_cast` | 10 | 10 | 0 |
| `dynamic_cast` | 61 | 63 | +2 |
| changelog comments | 215 | 213 | −2 |
| duplicate blocks | 155 recorded; 152 measured | 156 | +4 against the measured |
| include violations | 12 | 12 | 0 |
| unmapped files | 0 | 0 | 0 |

- **Lines.** Of the 829, 607 are tests (`engine_tests` 11,812 → 12,419).
  The engine grew 191: the three new headers 63, `patch_loader.cpp` 87
  (1,296 → 1,383), the registrations 20, `envelope_json.h` 11, the registry
  5, the loader's header 5. The editor grew 17 lines net (13,489 → 13,506)
  and the command-line tool 13. Outside the meter: the converter's rules
  1,592 → 2,086 lines (12 → 17 files), their tests 3,065 → 4,101,
  `rt_smoke.py` 494 → 794.
- **The loader grew, and this checkpoint was not where it shrinks.** What
  left it is the hand-reading of the two parts (`load_patch_file` 149 → 140
  lines); what came is the sharing walk, `perform_field`, the feed walk and
  six table rows. The file is deleted whole at 4.7.
- **The editor's functions.** `serialize_patch_graph` 360 → 336,
  `save_node_graph` 152 → 140, `draw_node` 135 → 126, and
  `GraphNode::build_pins` left the over-limit list (the hand-built pin sets
  went). `load_graph_from_path` joined it at 90 lines: the refusal walk
  grew by the new places it asks. Net 31 → 31.
- **The two file-level variables** are the editor's remembered comment and
  remembered output (`s_loadedComment`, `s_loadedOutputNode`), beside the
  other values a load keeps for the save.
- **`dynamic_cast` +2**: one is the plain-sound build asking whether the
  output is the mixer (lesson 10.4).
- **Duplicate blocks.** The snapshot recorded at 4.4's close says 155; the
  meter run over the base's own sources gives 152 (why the two differ was
  not established). Among the new ones is one this checkpoint's fix wave
  made knowingly: **the engine's `perform_field` and the editor's
  `perform_field_index` are the same fourteen lines.** The fix-wave brief
  asked for a function in the editor; the pattern 4.3 and 4.4 set is one
  function exported by the loader and called by both
  (`ragged_table_refusal`, `retired_key_refusal`). Both copies are deleted
  at 4.7, with the loader and the editor's own load; it is booked there
  (CLEANUP 87) and was not worth a second wave and a second full gate run.

## 6. Decisions the session made (2026-11-02 standing rule)

The four shown before the "Go" (rulings 4, 10, 14, 16) are in sections 1
and 2. The rest, each reversible and each in the plan's Ledger:

1. **The two parts check nothing; the build refuses a listed channel with
   no source** (ruling 24, made while writing Task 1's brief, replacing the
   plan's ruling 3). The plan gave each part a `validate()`: a channel with
   no source and a mixer with no channel would fail a load. A test from 4.1
   pins that an instrument may hold a mixer nobody reads, with an empty
   channel list; an empty mixer in a plain sound renders silence today; and
   an unwired input is legal on every other node. A check on the part would
   have refused all of that. So the part says nothing and the one reader
   that needs a source says so.
2. **The pins' ranges**: `volume` and the gains 0 to 4, `pan` −1 to 1.
   Advisory, as every pin's. The tree holds 0.5 to 1, 1 and 0.
3. **`PerformNode` keeps the family and the label Part 4 §5.4 gave it**
   (Modulators, "Note Field") and sits on no shelf.
4. **The sharing walk's edges are the file's refs; a tap is not one**
   (rulings 7 and 25).
5. **A tap into a gain, a volume or a pan loads** (ruling 30): a
   consequence of the generic pass wiring those pins, not a decision to
   allow it; it reads a node of another loop and is booked with the tap
   across two channels.
6. **`_provenance` becomes text**, one `key: value` line per entry
   (ruling 13).
7. **A position that names nothing is dropped, with a note** (ruling 12).
8. **A group's member may be a Note's label** (ruling 11): 133 files have
   it, and Part 4 §6.9's sentence is corrected to say so.
9. **The enum rule stops on a descriptor table that has no labels**
   (ruling 27, the coordinator's addition after reading the rule): a
   command-line tool built before this checkpoint prints `isEnum` and no
   labels, and the rule as delivered would have skipped every enum and
   reported nothing pending.
10. **What each host refuses** (ruling 15): the engine, what it stopped
    reading; the editor, that and what it would drop at the next save.
11. **An id is not a spelling** (ruling 19, closing CLEANUP 63):
    `__perf_freq` from the converter and `__perf_frequency` from the editor
    both stay.
12. **The editor carries a comment and shows none** (rulings 17 and 26). A
    comment on a Note's file node rides with its field. Nothing was added
    to copy a comment on Duplicate or to strip it on a text-clipboard copy.
13. **`--roundtrip` saves by graph mode and a node-graph Save keeps
    `seconds`** (ruling 18).
14. **The bend check reads `field` through the loader's own function**
    (ruling 29, the executor's).
15. **A node's comment that is not a string is refused by the editor**
    (ruling 31, the executor's).
16. **The generators are not edited** (ruling 22): nine write `inputs`,
    twenty-three the old layout, two `_comment`; their output meets the
    refusal. Four tools that write or read a retired spelling were mended.
17. **The sharing walk stops at a Multiplex's `source`** (the fix wave,
    amending decision 4): what a Multiplex plays is its copies, so the
    file's template nodes belong to no loop through it.
18. **What the one fix wave fixed and what it booked** (ruling 34), by the
    fourth of your verification rules: an editor finding that the editor's
    rewrite deletes is booked unless it loses data. Fixed: the two editor
    findings that lost data, the engine's false refusal, the converter's
    stop, the triple scan, the comments. Booked: everything in section 7
    under "the editor".
19. **The mutation run's two findings** (ruling 35): the stream's start on
    a plain sound became a smoke case; the bend check's warning is booked
    until the loader's warnings are a returned list.
20. **The Note field's reader is written twice until 4.7, and that is
    booked, not mended** (ruling 37; section 5). The fix wave gave the
    editor its own copy of the loader's fourteen lines where it should have
    called one exported function. Mending it is an engine edit and the
    whole gate set again, for code that 4.7 deletes on both sides.

## 7. Debt and future cleanup

Eleven new rows in `docs/autonomy/refactor/CLEANUP.md` (94 to 104), one
row amended, three closed.

**Closed here.** Row 25: a preset Envelope's curve keys are labels, the
last enum that was written as a number in one form and as a word in
another. Row 63: an id is not a spelling (decision 11). Row 74:
`instrument.release` and the null `dynamicPins` are out of the files.

**The engine.**
- **The mixer advances a wired gain once per channel** (row 94). The live
  `Mixer::render` ticks its clock inside the per-channel loop, so with two
  channels a wired gain runs twice as fast. It is the base's behaviour and
  has no diff line here; it is also the only reason channel order is
  audible at all. Mending it moves a render, so it waits for Part 8.
- **The compiler does not list the switches a new family must answer**
  (row 95, lesson 10.2): one flag would make it.
- **Shapes of a plain sound that load and that nobody could mean** (row
  98): a tap or a number as a channel's source; a tap across two channels;
  a tap into a gain, a volume or a pan. The codec's `check()` at 4.6 is
  where a plain sound's shape is judged.
- **The loader prints its warnings and returns none** (row 99), so no test
  can read them; the bend check's warning is the one this checkpoint's
  mutation run found unguarded. 4.6 gives a build a list of warnings.
- **`field` is still on the loader's key allowlist** (row 103); the list
  goes whole at 4.7.
- **Two things the re-read left** (row 104): whether every ref under a
  node's `params` is in fact pulled, which the sharing refusal assumes; and
  a tap inside a Multiplex template to a node outside it, which fails to
  load and did before this checkpoint. Both meet 4.6, where a Multiplex's
  template becomes a graph of its own.

**The editor, for its rewrite** (rows 96 and 97). A Channel's `tap` output
can be linked into the Mixer and saved as a file the engine refuses. A
graph with two Mixers saves one and streams the other, and Duplicate can
make the second. The load is tolerant of a face's position in the old
place; an unknown key inside `layout` and a Note with nothing wired are
dropped at a save; Replace-with drops a node's comment. And three paths no
headless command reaches (the stream's channel order, the link check's
Mixer row, the Replace-with exclusion). The reviewer rated these as flags;
the two editor findings it rated as losing data were fixed.

**The tools and the tree.**
- `clarinet1.json` is `Clarinet1.json` in git's index (row 100): one
  `git mv`.
- `--check` over audition takes about five minutes (row 101).
- The converter's `deleted_types` rule would miss a position in a
  hand-written `layout` (row 102).
- The generators (row 80, amended): nine write `inputs`, twenty-three the
  old layout, two `_comment`. Their output meets the refusal, which names
  the converter.

**Carried, as planned.** The engine still ignores a top-level key it never
read, so an unconverted file whose only old spellings are `ui`, `groups`,
`_comment` or `_provenance` renders from the command line and is refused by
the editor, until the codec refuses every unknown key at 4.7 (section 1).

## 8. The review (R20)

An independent Opus 5.5 reviewer, briefed to refute, read the code at the
commits while the mutation run held the working tree, so it never saw a
mutated file. It could run three saved binaries — the base's, Task 1's and
Task 5's — on files of its own making. Its brief listed twelve claims of
the checkpoint to break and the surfaces no gate reaches.

**What it could not break.** The three-step render proof. The conversion:
it read the converter out of its commit, ran it again over the base's files
and over the copies taken before, and got the files on disk — 57 of 57
tracked, 733 of 733 in audition and pending. The plain-sound build's
defaults and order against the base, shape by shape (twenty hand-made plain
sounds under three binaries). The registry's vocabulary: every switch and
loop over a family or a shelf answers `Output` and `None`. The refusal
table and both hosts' use of it.

**What it found.**

| # | Severity | Finding | What was done |
|---|---|---|---|
| F1 | Major | **The sharing refusal refused two shapes that cannot double-pull.** A Multiplex plays copies it builds of its template, so the template's nodes in the file are pulled by nobody through the mux; the walk followed the mux's `source` into them anyway. Two channels each a mux over one template node, and a mux whose template reads a node that another channel reads directly, rendered at the base and were refused from Task 1 on | fixed: the walk stops at a Multiplex's `source`; both shapes are now synthetics that must render at their base hashes |
| F2 | Major, loses data | **The editor read a Note field's `field` as an exact string.** An index threw a raw JSON error after the open graph had been cleared. `"Velocity"` loaded, and the save dropped that file node: the saved file rendered silence. The engine had read both since Task 1 | fixed: one function in the editor reads `field` against the type's labels, and the refusal comes before anything is cleared |
| F3 | Major, loses data | **A plain sound whose output is a bare signal**, with no Channel and no Mixer, came back from a node-graph Save with `"output": ""`, a file the engine refuses. Reachable headless since Task 2 made `--roundtrip` save by the graph's mode | fixed: the saver keeps the output the file named. The other half is booked: a graph with two Mixers saves the last and streams the first, and Duplicate makes a second Mixer where the create menu refuses one |
| F4 | Minor, the record | Three sentences of the Ledger said more than the code did: the list of what loads differently lacked four shapes, and "an empty mixer renders silence today" was true of a channel list written empty and not of one left out | the Ledger corrected (rulings 32 and 33) |
| F5 | Minor | Decision 9's stop covered an enum setting, and not a labelled column or a Note's `field` | fixed: the stop is on the table, once |
| F6 | Flag | A Channel's `tap` output can be linked into the Mixer's `channels`: the stream plays it and the save writes a file the engine refuses. The plan had called the `tap` pin on a part inert | booked (section 7); the plan's sentence corrected |
| F7 | Flag | Shapes that load and that nobody could mean: a tap or a number as a channel's `source`. And the base's mixer advances a wired gain once per channel | booked |
| F8 | Minor | Two comments naming what is gone | fixed |
| F9 | Minor | One scan for `{"ref"}` values written three times in the loader; the Note face's six pins hand-listed in the editor beside the type's labels | fixed: one function with three callers; the pins built from the labels |
| F10 | Flag | Three tolerances in the editor's reading and writing of `layout` | booked |

Of the generic ten-item brief (`docs/architecture/REVIEW_BRIEF.md`): item 4
failed on F9, item 9 on F8, item 10 on there being no test for F1 to F3;
the rest passed.

**Why the instruments missed F1 to F3.** Each is a shape no file in the
four folders has: a Multiplex in a plain sound beside a second channel; a
Note field by index or in another case; a plain sound with a bare output.
The gates measure the scope, and the synthetic set was written from a
census of the scope's forms plus the forms the plan knew it was changing.
F1 is the instructive one. The refusal was new behaviour, and the plan's
question for it was "does any file in the tree meet it" (none does). The
question that mattered was "what does it refuse that the base played", and
the reviewer asked it with a base binary and files of its own.

**The re-read.** The same reviewer then tried to break the fix wave, with
the wave's two binaries. On the code it found nothing. The Multiplex rule
held in both directions over twelve shapes under three binaries: a
Multiplex whose `source` is a tap, a number or absent; `count` at 0, 1 and
50; a template node also read through a pan; a Multiplex inside a template;
and the Multiplex itself as two channels' source, a real double pull, which
is still refused. The one feed walk gives the old three loops' results on
every malformed feed it tried. The two editor fixes lost no data over
thirteen round trips. Its five findings were wording: three sentences of
the plan's Ledger (corrected), a docstring in the converter's enum rule
that claimed more than its check does (reworded), and a comment older than
this checkpoint (booked). So there was no second wave.

It left one question open and found one older fault on the way, both booked
(CLEANUP 104): the sharing refusal follows every ref under a node's
`params`, and whether every such ref is in fact pulled was not settled
beyond the Multiplex; and a tap inside a Multiplex template to a node
outside it fails to load.

## 9. Reading guide

1. One converted file: `git show e953579 -- patches/baselines/delay_test.json`
   (a plain sound: the two `inputs` blocks become feeds), then
   `git show e953579 -- patches/library/voice/sing_soprano_I.json` (the
   provenance become a comment), then `patches/library/winds/oboe1.json`'s
   last forty lines at HEAD (`layout`: positions, groups, three faces each
   with its `pos`).
2. `engine/include/mforce/source/output/sound_channel.h` and
   `stereo_mixer.h`, whole: twenty lines each.
3. `engine/include/mforce/source/modulator/perform_node.h`, whole.
4. `engine/include/mforce/core/source_registry.h`: the comment above
   `Family`, `Shelf`.
5. `engine/src/patch_loader.cpp`: `for_each_feed`; `perform_field` and the
   six `static_assert`s under it; `refuse_shared_nodes` with its comment;
   `pin_signal`; then the end of `load_patch_file` from "The output as
   built". `git diff e96e245..HEAD -- engine/src/patch_loader.cpp` shows
   the hand-reading that left.
6. `engine/include/mforce/render/patch_loader.h`: `RetiredAt` and the
   comment on `retired_key_refusal`.
7. `tools/convert_rules/layout.py` and `terminal_inputs.py`: the two rules
   that change a file's shape most.
8. `tools/mforce_ui/main.cpp`: `layout_position` and `layout_groups`; the
   layout block at the end of `serialize_patch_graph`; the refusal walk at
   the top of `load_graph_from_path`.
9. `tools/engine_tests/main.cpp`: `namespace plain_parts` and
   `run_plain_part_twin_tests`; `run_plain_sharing_refusal_tests`;
   `run_perform_node_field_tests`.

## 10. C++ lessons

### 10.1 A member that registers itself: `Input source{this, "source", …}`

`SoundChannel` has no constructor, no method that does anything, and still
arrives fully formed. The line

```cpp
Input source{this, "source", Input::RequiredInput, nullptr, Pull::Manual};
```

is a *default member initializer*: it says how the member `source` is
constructed whenever a `SoundChannel` is. The member's own constructor
receives `this`, the channel being built, and adds the pin to that
component's pin list. Three members, three pins, in the order written:
C++ constructs members in declaration order, which is why the descriptor
dump lists `source`, `volume`, `pan` in the order of the header.

- **In C#** a field initializer may not mention `this` (error CS0027); the
  same design needs a constructor that builds the pins, or attributes on
  properties read by reflection (`[Param(Default = 1f)] float Volume`).
  Java allows `this` in an instance field initializer and so can do what
  C++ does here.
- **The alternative in C++** is the reflection-like one too: a table of pin
  descriptions beside the type, and a second path that builds pins from the
  table. The pin that registers itself is the project's "explicit registry,
  no reflection" rule applied one level down: the declaration *is* the
  registration.
- **Why a type with no behaviour is still a class.** The registry makes an
  instance by name and reads its pins (that is the descriptor table); the
  loader's generic pass wires an instance from the file. A channel has to
  be an object for either to happen. What it does not need is a method.
- **The names.** `struct` is `class` with members public by default, used
  here because everything is. `final` is C#'s `sealed`.

### 10.2 A `switch` with no `default`, and a compiler that says nothing

`family_name` (`engine/src/source_registry.cpp`) is a `switch` over the
`Family` enum with a `case` for each of the eleven and no `default`. The
idea behind leaving `default` out is that the compiler then knows the
switch means to answer every enumerator, and warns when a new one has no
`case`: the warning becomes the list of every place a new family must be
answered. The plan expected to teach it that way.

It is true of GCC and Clang, where `-Wall` includes `-Wswitch`. **It is not
true of the compiler this repo builds with.** MSVC's warning for it is
C4062, and C4062 is off by default, at `/W4` too. When `Family::Output` was
added nothing was flagged. The mutation run then removed `case
Family::Output` from `family_name`: it compiled, and the function fell
through to the `return "Utility"` that follows the switch. A test caught it
(`family_name(Family::Output) == "Output"`), and tests are what found the
other places — a table of every family in the tests, the walk that counts
78 types.

So today the list of places is kept by tests, not by the compiler. One
flag, `/we4062` (treat C4062 as an error), would make it the compiler's on
MSVC as well; booked in section 7, since it is a build-wide change that may
flag switches elsewhere.

- **C#**: a `switch` *statement* over an enum is never checked for
  completeness; a switch *expression* draws warning CS8509 when it does not
  cover every value.
- **Java**: a switch *expression* over an enum must be exhaustive or it
  does not compile; a statement need not be.

### 10.3 `static_assert`: two lists in two files, held to one order

The note's six fields exist as two lists. `PerformNode::kFieldLabels` is
the six words a file may write. `PerformOut::Field` is the enum the voice
makes its six adapters by. The loader turns a word into an index and uses
the index to pick an adapter, so it relies on the two being in one order.
Nothing in either header says so. The loader does:

```cpp
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Wheel)]) == "wheel");
```

six times, once per field. `static_assert(condition)` is checked by the
compiler: if the condition is false the build stops. It can be checked then
because the labels are `constexpr` (known at compile time) and
`std::string_view`'s `==` can run at compile time. The mutation run
exchanged `"wheel"` and `"pressure"` in the header, and the build stopped
with `error C2607: static assertion failed` at this line.

- **Where it lives.** In the one file that sees both lists and depends on
  their agreement. An assertion belongs beside the code that would be wrong
  without it; neither header has a reason to include the other.
- **The alternative** is to generate both lists from one (an "X-macro": one
  macro listing the six, expanded once as enumerators and once as strings).
  That is one source instead of two tied ones, at the cost of a macro and a
  shared header between a node type and the voice's adapter. Two lists and
  six assertions keep the two types apart and are as safe.
- **C# and Java** have no compile-time assertion; the same guarantee is a
  unit test, or a check in a static constructor that fails at start-up.
- **The name.** "Static" here means "at compile time", as in static versus
  dynamic typing, and has nothing to do with the `static` keyword on a
  member or a function.

### 10.4 `dynamic_pointer_cast`, at the one place a builder must know a type

The build's map holds every node as a `shared_ptr<Component>`. The
plain-sound path needs to know one thing about the output node: is it a
`StereoMixer`?

```cpp
const auto mixPart = std::dynamic_pointer_cast<StereoMixer>(outIt->second);
if (!mixPart) { /* the bare form: a signal in one unity channel */ }
```

`dynamic_pointer_cast<T>` asks at run time whether the object is a `T`. If
it is, the result is a `shared_ptr<T>` to the same object, sharing its
ownership; if not, an empty pointer. It is C#'s `as`, and Java's
`if (node instanceof StereoMixer m)`.

- **The alternative** is a virtual function on `Component`
  (`virtual bool is_mix() const`), so that nobody casts. That puts one
  builder's question on the root class of every node. A downcast scattered
  through code is a sign the base class is missing something; a downcast at
  the one seam where a builder assembles a concrete thing from a concrete
  type is the plain way to write it.
- **And where there is no cast at all.** The mixer's channels are a
  `Slot<SoundChannel>`: a slot that checks the type once, when the file is
  wired (`'channels' does not accept a SineSource; 'channels' takes a
  SoundChannel`), and from then on holds `SoundChannel*`. The loop that
  builds the live channels reads `view->volume` with no cast, because the
  template parameter already said what the slot holds. That is the generic
  doing the cast's work at the boundary: `List<SoundChannel>` against
  `List<object>`.

### 10.5 `if` with an initializer

```cpp
if (std::string id = id_of(view->source.source.get()); !id.empty()) loop.roots.push_back(id);
```

Since C++17 an `if` may declare a variable before its condition: declare,
then a semicolon, then the test. The name lives only inside the `if`, so
the next line cannot use a stale `id` by accident. C# reaches the same
place with a pattern (`if (IdOf(x) is { Length: > 0 } id)`) or an `out var`;
Java declares the variable on the line before and leaves it in scope.

### 10.6 What the session's own mistakes teach

- **A gate script must stop at a failed build.** Task 1's first gate run
  sat on a build that had failed on one line of test code. Every gate after
  it ran against the previous test binary and reported PASS. The executor
  caught it and reran everything; the script still runs every gate whatever
  the build said. It will stop at the build from the next checkpoint on.
- **A test of order needs something that makes order matter.** Reversing a
  mixer's channel list changes no byte while the gains are numbers. The
  first version of the channel-order synthetic would have passed with the
  channels built backwards. The pair that proves order has a wired gain.
- **A new refusal needs the question "what does it refuse that the base
  played".** "No file in the tree meets it" was asked and was true, and the
  refusal was still wrong about two shapes (F1).
- **Reproducing a file's bytes through the editor does not prove the editor
  understood the file** (fault 43): what it does not model, it carries
  untouched.

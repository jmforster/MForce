# Checkpoint 4.2 — the paramMap goes (report R17)

**Landed 2026-11-10 on main, `496bb42`..`0e6648d` with the review's two
fix waves, `82b03f6` and `f07c51e`** (base before 4.2
`dc2f9a0`, the commit that carries this checkpoint's plan; plan
`docs/superpowers/plans/2026-11-09-part4-checkpoint-4.2.md`, its Ledger the
record of every ruling and every task). Part 4 section 8.1's second row, as
decided with you on 2026-11-09: `instrument.paramMap` no longer exists as a
form. One converter, `tools/convert_patch.py`, rewrote every paramMap in
`patches/library`, `patches/baselines`, `patches/audition` and
`patches/pending` as graph wiring; then the three things that each read a
paramMap in their own way — the engine's load-time conversion, the editor's,
and the Python tool with its gate — were deleted. A file that still carries
one is refused by the engine and by the editor, naming the converter. No
render in `library` + `baselines` moved. Nothing in this checkpoint is for
your ears. What you can see is in the editor, and section 1 lists it.

Reading guide: fifteen minutes. Section 1 is what changed in the editor and in
your folders, and what was found in passing. Section 2 is what a note can
drive now that there is one way to say it. Section 4 is how a change with no
audible result was proved, including the two shapes that do play differently
and that no file has. Section 6 is what the session decided under the
2026-11-02 rule. Section 8 is the independent review. Section 10 is the C++
lessons.

*Verdicts, 2026-11-10 (after this report was written): `MPXTest3` is retired
to `patches/old/`, so where the report says 132 patches the scope is 131 from
here; and frequency to frequency is a wire to a pin, direct before and after.
REVIEW R17 has both, and the one question left.*

## 1. What changed for you

- **Nothing is audible.** The 132 library and baseline patches render
  byte-identically (132 of 132, 0 differ) against renders made before any
  file was touched, three times: with the files converted and the engine
  unchanged, with the engine's paramMap code gone, and at the close. The nine
  instrument patches that carry no score, eight of which had a paramMap and
  none of which makes a sound in any ordinary gate, were rendered with a
  three-note score appended: 9 of 9 identical. The 21 composition templates:
  21 the same.
- **The editor is this checkpoint's one visible surface.** None of the items
  below was clicked in a running editor (this session had no desktop
  control); each was proved through the editor's headless modes or, where
  marked, by reading.
  - **A file that carries a paramMap does not open.** The status line reads
    `Failed to load <path>: instrument.paramMap is no longer read: python
    tools/convert_patch.py <patch> rewrites it as wiring`, and the graph on
    screen stays as it was. `mforce_cli` gives the same sentence. No file in
    `library`, `baselines`, `audition` or `pending` meets it: all were
    converted. What does: 103 files in `patches/sweep` and 223 in
    `patches/old`. The command is `python tools/convert_patch.py <file>` (or
    a folder; `--check` reports without writing). It rewrites all 103 in
    sweep and 217 of old's 223; the other six it refuses by name (a formant
    peak outside a spectrum, a shape 3.9 removed). Whether a rewritten sweep
    or old file then loads was not checked: those folders are outside the
    measurement scope.
  - **Edit > Parameter mapping shows and no longer edits.** It lists every
    pin fed from a Note node and every dynamic pin (a setting promoted in
    Properties, wired or not): the target, its kind (a fixed pin, pulled
    every sample; a dynamic pin, set once per note), what drives it, and a
    Shape column. Gone from it: the "Legacy paramMap entries" table with its
    delete buttons, the legacy curve shapes under it, and the "Add binding"
    row (the node and parameter lists, "Add (follows note)" and "Add with
    curve"). A binding is made in the graph, as it has been for wired patches
    since August: wire a Note node's output to the pin; put a CurveNode
    between for a curve; for a setting, promote it to a dynamic pin in
    Properties and wire a CurveNode to the gold pin (the editor lets only a
    Curve feed one). Since 3.9 the legacy table had nothing to show for any
    file in the tree, so what you lose in practice is the Add row — and with
    it one thing the graph cannot draw: a setting driven *straight* from the
    note, with no curve ("Add (follows note)" on a setting). The engine still
    reads that form; no file in any folder uses it; a CurveNode that passes
    its input through says the same (section 7).
  - **Properties loses the green `<frequency>` and `<curve>` badges.** They
    marked a row driven by a hidden paramMap entry. A wired pin still shows
    `<-- source`, and a dynamic pin its gold `<- source`.
  - **Edit > Convert to Patch graph wires what it used to hide.** Converting
    a plain sound made the first free `frequency` pin follow the note by
    writing a paramMap entry you could see only in the Mappings dialog. It
    now puts a Note node to the left of that node (or reuses the graph's
    first Note node) and draws the wire.
  - **File > Open and Ctrl+O report a file they cannot read.** Before, a file
    that failed to load through the dialog — a paramMap would now be one; a
    malformed JSON always was — fell through to `main`'s last handler, which
    wrote the crash log and closed the editor. It now shows on the status
    line, as Open Recent and a file named on the command line already did.
    (Read from the code; not run.)
  - **`mforce_ui --dump-curve-opts` is gone.** It printed the choices of the
    Add-with-curve list, which no longer exists. Part 4 section 7.6 had left
    all five diagnostic modes for Part 7; this one went now, as the plan said
    when you approved it (Ledger 7).
- **Your working folders were rewritten, as section 8.4 decided.** 111 files
  in `patches/audition` and 9 in `patches/pending` (`bowed_cello`,
  `buzzy_bow`, `fm_gritty_bass`, `funky_recorder`, `harsh_pluck`,
  `inst_fm_rhodes_base`, `inst_fm_rhodes_clack`, `inst_fm_rhodes_dual`,
  `slow_attack_horn`) were converted in place. All 44 pending files as they
  stood before are in `renders/scratch/ab42/pending_before/`. Each converted
  file gained a `__perf_freq` node, and in audition `__curve_N` and `__mul_N`
  nodes for its curves, none with a saved canvas position: on first open the
  editor lays them out in a strip below the layout the file saved.
- **There is one converter.** `python tools/convert_patch.py [--check]
  <file-or-folder>...` applies three rules in order — node types the engine
  no longer has, a wavetable's string-typed evolution, the paramMap — and
  writes a file only if a rule changed it. The three tools it replaces are
  deleted, as are five scripts of the paramMap era that had nothing left to
  do. Seven of the nine formant generators now run the converter over what
  they write; the other two write a shape nothing can read or convert since
  3.9 (section 7).
- **Two shapes play differently once converted, and no file has either.**
  Both are a Multiplex, and both are in section 4.
  - *A Multiplex voice under a bend.* The paramMap pushed the note's
    frequency into each clone once, at the note's start; a wire is read every
    sample, so the clones now follow a bend. `MPXTest3`, the one Multiplex
    voice in scope, has no bend in its score and renders the same. (The plan
    said "a bend or a glide". A glide belongs to a held line's continuation,
    and there the two forms do differ in mechanism — the push jumped to the
    new pitch, the wire glides to it — but there is nothing to hear in
    either: a held line on a Multiplex voice is silent after its first note,
    before and after. It is in the list of things found in passing, below.)
  - *A Multiplex that is not the voice's output* (one that feeds a filter,
    say). The paramMap never reached its clones at all: every note played at
    the file's own constant. Converted, the clones follow the note. So here
    the conversion mends something the paramMap could not do. Of the 3,146
    patch files, every Multiplex in a file that has or had a paramMap is the
    voice's output, so this is a fact about the form, not about a file.
- **Found in passing, none of it fixed here.**
  - `MPXTest3` renders NaN samples and always has. Its `Triangle1` sets
    `"bias": 0.0`, the lower bound of that pin's own range; on the rising leg
    the oscillator computes `position × (4 / bias)`, and at the sample where
    the position is exactly 0 that is `0 × ∞`. The WAV's hash is stable, so
    every gate has passed it; `mforce_cli` prints `rms=nan` for it. It is the
    only Triangle in the four folders with a bias of exactly 0 or 1. Mending
    it moves that baseline's render (section 7).
  - Edit > Convert to Node graph and back drops the instrument block's other
    keys. On `oboe1` the round trip loses `"volume": 0.5` and the result
    renders at a peak of 0.98 where the file's is 0.62; by reading, the
    Output node's `sustaining`, `onsets` and `glideMs` go the same way. The
    lines are older than this checkpoint. 4.8 rewrites that pair of functions
    as operations and its plan now carries this (section 7).
  - A held line on a Multiplex voice plays its first note and then nothing,
    in either form (a probe, byte-identical in both). The engine means to
    deliver such a line as ordinary notes, because the envelopes it would
    have to hold are inside the clones; but its test for that is "the voice
    has no envelope it can reach", and it can reach the template's own
    envelope, which nobody hears. So it holds that one, and the clones'
    envelopes run out. No in-scope, audition or pending file has a held line
    on a Multiplex voice.
  - A setting driven per note on a node *inside* a Multiplex template does
    nothing, in either form: the binding lands on the template's own node,
    which makes no sound, and the clones are built without it. No file in the
    four folders has the shape. Part 4 section 5.5's template (a sub-graph
    over every wire kind) is where that changes, at 4.6.

## 2. The shape

A paramMap entry said one thing: *at each note, drive this target from the
note's frequency* — straight, through a curve, or through a curve scaled by a
curve of the note's velocity. A wire from a Note node says the same thing, and
has since `PerformNode` arrived in August. Until this checkpoint the engine
kept reading both: three pieces of code each held a full understanding of the
paramMap form, and each turned it into wiring in its own place.

| Who read a paramMap | Where | What it did with it |
|---|---|---|
| the engine, at every load | `build_bindings`, `patch_loader.cpp` | built the curve nodes in memory and wired or pushed them, per voice |
| the editor, at every load | `convert_parammap_to_wiring`, `main.cpp` | rewrote the document as nodes before building the graph, and kept a stash for what it could not convert |
| a tool, on demand | `tools/parammap_to_wiring.py` | rewrote the file; `null_gate_wiring.py` proved it against the engine's reading |

All three are gone. The conversion was done once, on disk, by the third; what
remains of the form is two refusals that name the converter.

**What a note can drive, and how, after 4.2.** There are two ways and only two.

| | Written in the file as | Delivered | Follows a bend or a glide |
|---|---|---|---|
| a pin (a `Param` or an `Input`) | `"params": {"frequency": {"ref": "__perf_freq"}}` | pulled: the pin reads its source every sample | yes |
| a setting (a dynamic pin) | `"dynamicPins": {"t60": {"ref": "__curve_1"}}` | pushed: the chain is evaluated once at the note's Setup and handed to `set_setting` | no: held for the note, and across a held line |

The paramMap had a third, which is what the engine's deletions remove: a
value *pushed into a pin's constant* at note-on, used for every entry on a
Multiplex voice so that the same number could be copied into each clone. That
third way is why `PushBinding` carried seven members, three of them live only
for it:

```cpp
// before                                      // after
struct PushBinding {                           struct PushBinding {
  std::shared_ptr<Component>      consumer;      std::shared_ptr<Component>   consumer;
  std::string                     paramName;     std::string                  paramName;
  std::string                     targetNodeId;  std::shared_ptr<ValueSource> chain;
  std::shared_ptr<ValueSource>    chain;       };
  std::shared_ptr<ConstantSource> cs;
  bool                            isSetting{false};
  int                             cloneSlot{-1};
};
```

With every binding a setting's, `apply_note_bindings`'s push loop is two
lines (`b.chain->step();` then `b.consumer->set_setting(b.paramName,
b.chain->current());`), `deliver_continuation` pushes nothing, and
`PitchedInstrument` no longer knows that `MultiplexSource` exists:
`instrument.h` lost the include. (The loader, which builds the clones, still
names it.)

**A converted file.** `MPXTest3`, the whole of its change:

```
+      {"id": "__perf_freq", "type": "PerformNode", "params": {"field": "frequency"}},
       {"id": "Var1", "type": "VarSource", "params": {
-          "val": 1.0,
+          "val": {"ref": "__perf_freq"},
   "instrument": {
-    "paramMap": {"frequency": "Var1.val"},
```

Every one of the 82 tracked files changed in exactly that way: the map gone,
one `PerformNode` first in the node list, one pin per entry now a ref. None of
them had a curve. The curves are in `audition` (457 of them), where an entry
with a `curve` became a `CurveNode` reading `__perf_freq`, and one with a
`vcurve` as well became a `CombinedSource` multiplying that by a linear
`CurveNode` reading `__perf_vel`.

**The converter.** A driver and a folder of rules.

| File | What it is |
|---|---|
| `tools/convert_patch.py` (126 lines) | walks files, applies the rules in order to one parsed document, writes a file only when a rule changed it (the file's own indent, LF, a final newline), never writes a file a rule refused, exits 1 from `--check` while anything is pending or refused |
| `tools/convert_rules/__init__.py` | `RULES = (deleted_types, string_evolution, parammap)` and `Context`, through which a rule asks for the registry's descriptor table |
| `convert_rules/deleted_types.py` | `migrate_deleted_types.py`'s functions, moved |
| `convert_rules/string_evolution.py` | `string_evolution_to_node.py`'s, moved |
| `convert_rules/parammap.py` | `parammap_to_wiring.py`'s, moved, with the refusals it lacked (section 6, decisions 4, 7 and 20) |

A rule is a module with one function, `apply(doc, notes, ctx)`, which changes
the document, appends anything a person should read to `notes`, returns a
count that is empty exactly when it changed nothing, and raises to refuse the
file. The order matters once: the deleted-type rule folds paramMap entries
aimed at the formant peaks it consumes and renames types the paramMap rule
must look up in the registry, so the paramMap rule runs after it. Part 4's
later checkpoints add rules here (tables at 4.3, the document's spellings at
4.5), not tools.

**Deleted.** Engine: `build_bindings`, `attach_perform_source`,
`VoiceGraph::topMultiplex`, four members of `PushBinding`, the Multiplex's
`clone_param_slot`, `set_clone_param`, `CloneParam`, `cloneParams_` and their
two helpers, both `ensure_constant` overloads. Editor: the two stashes
(`s_loadedParamMap`, `s_convParamMap`), `convert_parammap_to_wiring`,
`eval_map_curve`, `eval_map_vcurve`, `apply_param_map`, `curve_eval`,
`curve_sort`, `curve_exists_for`, `curves_add_entry`, `draw_one_curve`,
`TargetOpt`, `eligible_targets`, `target_exists_for`, `mapping_badge`, the
`--dump-curve-opts` mode. Tools: `parammap_to_wiring.py`,
`migrate_deleted_types.py`, `string_evolution_to_node.py` (as tools; their
functions are the rules), `null_gate_wiring.py`, `add_instrument_block.py`,
`rewire_instrument_output.py`, `fix_over_rewire.py`, `migrate_fullAdditive.py`,
`migrate_legacy_additive_params.py`, and the lint's paramMap curve-endpoint
rule.

| File | Lines at the base | Now |
|---|---|---|
| `engine/src/patch_loader.cpp` | 1,537 | 1,346 |
| `engine/include/mforce/render/instrument.h` | 832 | 794 |
| `engine/include/mforce/source/combiner/multiplex_source.h` | 194 | 119 |
| `engine/include/mforce/core/dsp_value_source.h` | 351 | 324 |
| `tools/mforce_ui/main.cpp` | 14,058 | 13,266 |

## 3. The tasks, commit by commit

| Task | Commit | What landed |
|---|---|---|
| 0 | `dc2f9a0` (the plan) | the base's gate results; the old arms (4.1's close, spot-checked); the census; sixteen synthetic patches and twelve controls rendered under the base engine in paramMap form |
| 1 | `496bb42` | `convert_patch.py` and its three rules, moved from the three tools and proved equal to them over every patch file; the tools deleted; the engine's four hints name the one converter |
| 2 | `eec5aa2` | the tree converted — 82 tracked files, 111 in audition, 9 in pending — and proved under the *unchanged* engine, which still read both forms; the wiring null gate retired |
| 3 | `bef576e` | the editor: the refusal, the Mappings dialog as a view, make-instrument wiring a Note node; fifteen definitions and one headless mode gone |
| 4 | `e2e442d` | the engine: the refusal; `build_bindings`, the non-setting push, the clone fan-out and `ensure_constant` gone; the paramMap's tests rewritten on wiring; two new test families |
| 5 | `0e6648d` | the tools: five scripts deleted; seven generators piped through the converter; the lint's curve-endpoint rule deleted; the roundtrip smoke's refusal case; two gate scripts' comments describe the files as they are |
| 6 | `82b03f6`, `f07c51e`, then the documents | the close: a mutation run, the review and its two fix waves, the measurements, this report |

Each commit builds every target. The order is section 8.1's and each step
needed the one before: the editor had to stop *writing* a paramMap (its
Generate path serializes the graph and hands the text to the engine) before
the engine could refuse one, and the files had to be converted while an
engine that read both forms was still there to compare them.

Execution was as 4.1's: Opus 5.5 executors on every task, none of them
committing; the coordinator read each diff against the commit before it, reran
the gates on its own build, edited where the reading called for it, and
committed. The review ran beside the mutation run, the re-review beside the
close's measurements. Usage, measured at the documents' commit: the weekly
all-models window 33 to 42 percent, the weekly Fable window 61 to 74 (the
plan guessed ten Fable points; 4.1 cost eight).

## 4. The proofs

The claim has two halves, and the plan kept them apart so that a difference
would name its cause: *the converted files say what the paramMaps said*, and
*the engine without its paramMap code does what it did for a wired file*.

**The old arm** is 4.1's close: no engine or patch commit lay between
`e0cb2e2` and the base, and six spot renders under the base binary matched.
Every comparison below is against renders of the *unconverted* files.

**The conversion, under the unchanged engine (Task 2).** No rebuild: the
binary's hash was the same at the start and the end of the task.

| What | Result |
|---|---|
| the null gate (78 manifest patches, 28 of them converted) | 78/78 |
| the 132 against the old arm | 132 identical, 0 differ |
| the score-less nine, a score appended | 9 identical |
| the sixteen synthetics | 15 identical; (n) differs, as built to |
| each tracked file's JSON against its committed form (`t2/value_diff.py`) | 82 of 82 differ in exactly: the map gone, one `PerformNode` at index 0, one ref per entry (85) |
| the editor's round trip over the 132 | 0 id changes, 0 new render diffs, the 6 known |
| a second run of the converter | wrote nothing: hash and mtime of all 896 files unchanged |

**The deletion, over the converted tree (Task 4).** The same sets under the new
engine: null gate 78/78; the 132, 0 differ; the nine identical; the synthetics
as before; the comp template gate against 4.1's close, 21 the same; the
allocation gate, 0 of 132 allocate. And the other direction: all sixteen
synthetics in paramMap form are now refused by `mforce_cli`, each naming the
converter.

**The close**, on the first fix wave's binary, and the renders, the null gate
and the fast gates again on the second's: the 132, 132 identical; the nine,
identical; the sixteen synthetics as at Task 2 and Task 4; the sixteen
paramMap forms refused by `mforce_cli` and by `mforce_ui --roundtrip`, sixteen
of sixteen each; the comp gate `post42` against `post41`, 21 the same; the
converter's `--check` over the four folders, nothing pending and nothing
refused; and every gate as at the base — ctest, the stamp, the allocation
gate, the null gate at 78/78, the whole-corpus roundtrip at 0 id changes, 0
new render diffs and its 6 known, the roundtrip smoke with its one standing
shape failure and its two new lines `ok`.

**The synthetic set: every form audition holds, and two it does not.** No
in-scope file has a curve, a `vcurve`, a setting target or two entries on one
node; audition has 457 curves and 285 setting targets, and no gate renders
audition. So sixteen small patches were written in paramMap form, one per
form, each with four notes over two octaves at two velocities, every curve's
knots at 100 and 1000 Hz so that all four notes fall between them
(`renders/scratch/ab42/synth/make.py`). Each was rendered under the base
engine, converted, and rendered again.

| | Form | Converted render |
|---|---|---|
| a | bare, on `SineSource.frequency` | identical |
| b | a target with no `.` | identical |
| c | a pin the file leaves unset | identical |
| d | `curve` on `SVFSource.cutoffFreq` | identical |
| e | the same, `interp: loglog` | identical (and differs from d) |
| f | `curve` + `vcurve` on `BWLowpassFilter.cutoffFreq` | identical |
| g | `curve` into `CombinedSource.source2`, an input left unset | identical |
| h | the KS piano's shape: bare `frequency`, curves on the settings `t60` and `brightness` | identical |
| i | `curve` on `Envelope.timeScale` | identical |
| j | `curve` + `vcurve` on `Envelope.timeScale` | identical |
| k | bare, on a setting (`KSString.t60`) — a form no file has | identical |
| l | a Multiplex voice of four, bare | identical |
| m | a Multiplex voice with a curve on a filter inside it | identical |
| n | (l) with a `Bend` on every note — the departure | **differs** from frame 4 |
| n0 | (n) without the bend | identical |
| o | `curve` + `vcurve` + `loglog` | identical (and differs from f) |

Twelve controls show that each curve reaches the render: remove a `curve` or
a `vcurve` and the hash changes, every time. All sixteen converted documents
equal what the old tool wrote from the same files.

**The two departures.** *(n), a Multiplex voice under a bend.* In paramMap
form (n) is byte-identical to (n0): the bend never reached a pushed voice.
Converted, it differs from the fourth frame (RMS 0.047105 against 0.047179),
and it gave the same hash at Task 2, at Task 4 and at the close. The same bend
on (a), which is not a Multiplex, already moved the render in paramMap form.
*A Multiplex that is not the voice's output*, found while Task 4 ran: a probe
(`synth/probe_nested/`) with a Multiplex of four feeding a filter and a bare
entry on a node inside the template. Under the base engine the paramMap form
plays its four notes at 220, 220, 220 and 220 Hz, the file's constant; the
converted form at 131, 262, 392 and 524 Hz. `build_bindings` fanned an entry
into the clones only when the voice's output was itself the Multiplex; further
in, it wired the template's own node, and the clones, built from the
template's JSON, never saw it. A second probe measured the limit that is the
same in both forms: a curve onto a *setting* inside a Multiplex voice renders
the same hash as the paramMap form, the converted form and a control with the
entry removed.

*One more difference of form, with nothing to hear* (the review's): where a
paramMap drove two settings of one node, the engine pushed them in the map's
order; the wiring pushes them in node order and, within a node, in the order
of their keys. Those setters store and clamp, and what a node derives from
its settings is computed at the note's start, so the order cannot be heard.
Synthetic (h) drives `t60` and then `brightness` on one string, the reverse
of key order, and is identical. *And one of mechanism*: on a held line's
continuation the engine re-pushed the new pitch into a Multiplex voice's
clones at once, where the wire follows the voice's glide. A third probe
(`synth/probe_nested/make_probe_r.py`: a two-note held line on a Multiplex
voice, `glideMs` 60) renders byte-identically in both forms, because nothing
sounds after the first note in either (section 7).

**The converter is the old tools, moved.** Each rule's functions were sliced
from its tool by a script, so a diff of rule against tool shows the docstring,
the imports and the wrapper. Then every `.json` under `patches/` — 3,146
files, all seven folders, read-only — went through the old function and the
new rule side by side (`t1/equiv.py`):

| Rule against its tool | Both unchanged | Changed, equal | Both refuse | Mismatch |
|---|---|---|---|---|
| paramMap | 2,616 | 515 | 15 | 0 |
| deleted types | 2,882 | 236 | 28 | 0 |
| string evolution | 3,136 | 8 | 2 | 0 |
| the three chained, over the four folders (884 files) | 682 | 202 | 0 | 0 |

and the wiring null gate, pointed at the new driver for its last run, gave
78/78 before the old tools were deleted and after. The tool tests went from
49 to 88 by Task 5: the 44 moved cases all kept, 28 for the paramMap rule, 11
for the driver, beside the allocation gate's own 5. The two fix waves added eight (96).

**The editor**, through its headless modes (`renders/scratch/ab42/t3/`). A
sweep file with three entries, two of them curves, is refused by
`--roundtrip`, `--dump-layout`, `--convert-roundtrip` and `--rename`: exit 1
with the sentence, no output file. After `convert_patch.py` it round-trips and
renders. Make-instrument on `distortion_test.json`: the instrument block is
`{"polyphony": 1}`, a `__perf_frequency` node feeds `sin.frequency`, and
single-note renders at MIDI 48 and 72 differ, with about 130 and 523 cycles a
second. `--convert-roundtrip` and `--rename` on `viola_default.json` render
the original's hash.

**The engine's new tests.** `run_param_map_refusal_tests` (both entry points;
an empty map is refused too; the same document without the key loads) failed
six checks before the refusal existed. `run_dynamic_pin_tests` pins what the
surviving push path does and no test had: a setting takes the curve's value at
the note's frequency at Setup; across a held line's continuation it keeps the
first note's; the curve node, which nothing but the binding reaches, reports
no fault (on either of two voices, since the fix wave). Each was watched
failing under a one-line change of the *unchanged* engine before any deletion:
a re-push in `deliver_continuation`; the binding's `prepare` removed. 7,792
checks became 7,802, and 7,811 with the fix wave's (the `null` map, the
message checked against the constant, the second voice, and
`run_multiplex_bend_tests`: a three-clone Multiplex voice under a bend of
seven semitones crosses zero 1.500 times as often as without it, where
2^(7/12) is 1.498).

**The mutation run.** Eighteen single changes, each applied to a pristine
file, its detector run, and the file restored by copy
(`renders/scratch/ab42/t6/`). All eighteen were caught.

| | Mutation | Caught by |
|---|---|---|
| E1 | the engine's refusal deleted | `run_param_map_refusal_tests`, six checks |
| E2 | `set_setting` dropped from `apply_note_bindings` | `run_dynamic_pin_tests`, four checks; and synthetics h, i, j and k move (i and j to the bare sine's hash) |
| E3 | a re-push added to `deliver_continuation` | the held-line check (`1.578` where `1.127` is wanted) |
| E4 | the binding's `prepare` removed | the fault check (`ts faults: 4`, never prepared) |
| T1–T8, T12 | the paramMap rule: the slot decision inverted; `logx` and `loglog` swapped; the prepend made an append; the default pin changed; each of four refusals removed; the map left in the file | the tool tests, one to fourteen failures each |
| T9–T11 | the driver: every file written; the non-object guard removed; the rule order reversed | the tool tests |
| U1 | the editor's refusal deleted | the roundtrip smoke: `refusal FAIL loaded, converter not named, wrote refusal_rt.json` |
| U2 | make-instrument's link removed | the headless check: the two single-note renders become equal |

It showed two things beyond caught or not. With the editor's refusal gone, the
editor loads a paramMap file, carries the map through untouched among the
instrument block's pass-through keys, and saves it with exit 0: a file the
engine then refuses. So that refusal is the only thing between a paramMap file
and a successful save of something nothing can play, which is why it has a
standing check (section 6, decision 10). And the reversed rule order was
caught by its consequences — the paramMap rule meeting types it cannot look
up — not by the one assertion that names the order, which never ran.

After the fix wave the coordinator removed or loosened each of its new
refusals in turn — a map that is not an object, the two-knot minimum, a bool
counted as a number, entries under other names left unread, a target with no
such pin — and the tool tests caught all five
(`t6c/coordinator_spot_mutations.py`). The new bend test was not run against
a mutant; what stands behind it is the synthetic pair (n) and (n0), which
under the base engine's push rendered byte-identically, a ratio of exactly 1
where the test wants 1.5.

**What no gate saw.** The editor's interactive paths (above). The 111
audition and 9 pending files were converted and not rendered: the synthetic
set stands for their forms. The nine generators were given the pipe and not
run (they write into folders at the tree's root). Sweep and old were counted,
not converted or loaded: of sweep's 103 paramMap files the converter would
rewrite all; of old's 223, 217, refusing six. It would also rewrite 46 files
in old that have no paramMap (deleted types, string-typed evolutions) and
refuse 22 (17 a spectrum whose peak is wired, 5 an `AdditiveSource2`).

## 5. The meter (`--delta` from the base `dc2f9a0`)

| Measure | Before | After | Change |
|---|---|---|---|
| files | 223 | 223 | +0 |
| lines | 62,100 | 61,134 | −966 |
| files over the limit | 18 | 18 | +0 |
| functions over the limit | 96 | 93 | −3 |
| file-level mutable variables | 120 | 118 | −2 |
| `const_cast` | 10 | 10 | +0 |
| `dynamic_cast` | 64 | 65 | +1 |
| changelog comments | 238 | 228 | −10 |
| duplicate blocks | 156 | 157 | +1 |
| include violations | 12 | 12 | +0 |
| unmapped files | 0 | 0 | +0 |

- **Lines.** The editor −792, the loader −191, the Multiplex −75, the
  instrument −35, `dsp_value_source.h` −27; the test file +147.
- **Functions over the limit, −3**: `build_bindings` (146 lines) is gone from
  the loader, `convert_parammap_to_wiring` (122) and `draw_one_curve` (91)
  from the editor. `draw_mappings_dialog` fell from 291 lines to 92 and is
  still over.
- **File-level mutable variables, −2**: the editor's two stashes.
- **Changelog comments, −10**: they went with the deleted code and with the
  comments this checkpoint reworded.
- **`dynamic_cast`, +1**: in the test file's new bend test. The engine's count
  is unchanged.
- **Duplicate blocks, +1**: not attributed. The count is the token detector's
  over the whole tree and is not additive (CLEANUP 50). The entries that name
  the four files this checkpoint cut: one in the loader (18 lines, shared with
  the editor's loader), the instrument's ring-out loop (19 lines, standing),
  29 in the editor.
- **Timing.** The base binary against the close binary over the converted
  tree, twice: rows from 0.96× to 1.10×, the outlier a different row each time
  (40 sines at 1.10× and then 0.99×; 40 envelopes at 1.10× in the second run).
  That is the machine; nothing on a per-sample path was touched. `MPXTest3`
  paired — the paramMap form under the base binary against the converted file
  under the close binary, five interleaved runs each, the minimum of five —
  3,352 ms against 3,435 ms: the close 2.5 percent slower. That is inside the
  spread above, and it is also the direction the change predicts: each of the
  fifty clones now pulls the voice's frequency every sample where it read a
  constant.

## 6. Decisions the session made (2026-11-02 standing rule)

Numbers in brackets are the plan's Ledger rulings.

1. **A sixteenth synthetic, `curve` + `vcurve` + `loglog`** [19]. Fourteen
   audition entries have all three keys; (e) and (f) covered the two
   mechanisms apart, and the combination cost one patch.
2. **The Multiplex synthetics use a sine** [20]. `MPXTest3`'s triangle, with
   its bias at 0, renders NaN samples, and a filter after it turns the whole
   render to NaN; a set built to compare renders cannot stand on that.
3. **An early warning the plan did not ask for** [21]. At Task 0, before any
   new code, each synthetic was converted by the *old* tool and rendered by
   the base engine. It showed that the set was sound and that the departure
   patch departs, a task before the plan would have.
4. **The paramMap rule refuses a duplicate target** [22, 6]. Two entries
   naming one target were refused by the engine at load when the target was a
   pin (the second found it wired); the old tool let the last one win. A
   converter that did so would write a loadable file from one that never
   loaded. Counted over all 528 paramMap files: none has a duplicate, a target
   already wired, a synthesized id already taken, an empty map, or a name
   besides `frequency`.
5. **The editor's refusal is a throw** [23], which is how
   `load_graph_from_path` already reported a file it could not read: every
   headless mode and three of the four interactive callers caught it (Open
   Recent, a file named on the command line, and paste's reload). File > Open
   caught nothing and was given the same catch.
6. **The driver leaves a JSON file alone when its top level is not an object**
   [24]. Two `_manifest.json` lists under `old/` would have stopped a folder
   run with a traceback.
7. **An entry that is neither a string nor an object with a target is refused
   by name** [25], where the moved text raised an error the driver does not
   catch. No file has one.
8. **Every refusal is decided before anything is written** [26]: a refused
   document is unchanged in memory as it is on disk.
9. **The convert round trip's loss of `volume` was found and left** [27]: a
   behaviour fix outside the plan, in functions 4.8 rewrites.
10. **The editor's refusal has a standing check** [28]. `rt_smoke.py`, which
    the fast gates run, writes a small paramMap patch to its temp folder and
    requires `mforce_ui --roundtrip` to exit non-zero, name the converter and
    write nothing; it prints `refusal ok` on a line of its own and is not
    counted among the six shapes. The engine's refusal and the converter each
    had tests; the editor's had only this checkpoint's scratch scripts, and an
    editor that silently dropped a map would strip a patch's pitch tracking
    on its next save. Shown to bite: given a wired copy of the fixture the
    same check reports `FAIL loaded, converter not named, wrote
    refusal_rt.json`.
11. **The second departure is not refused by the converter** [29]. The
    entry's meaning is plain and the wiring gives it; no file has the shape.
12. **A dynamic pin inside a Multiplex template is recorded as inert, not
    mended** [30]: equal in both forms, and 4.6's subject.
13. **`--dump-curve-opts` went now** [7], the one place the plan departs from
    a table you had decided.
14. **Three sentences of the plan's census said more or less than the files**,
    and are corrected in its Ledger: audition's `vcurve` entries are 54 `logx`
    and 14 `loglog`, not 14 of 54; its pin targets include five bare
    `WavetableSource.frequency` entries; of the 22 files not already laid out
    as the driver writes, 14 change only in line endings and a final newline
    and 8 are re-laid (4 of them tracked: `bend_mordent_test_pluck`,
    `bend_test`, `inst_chord_test`, `inst_spacy`). And Task 1's "528 files
    expected to change" is 515 for the paramMap rule alone: thirteen `old/`
    files sit on types only the deleted-type rule can rename, and two are
    manifests.
15. **Comments beyond the plan's list.** Eight engine headers said a paramMap
    could drive this or that per note; each now says it of a wire or a
    dynamic pin. The rules' docstrings were put in the present tense.
16. **Two of the nine generators got no pipe.** `gen_liar3.py` and
    `gen_vowel_baseline.py` write no paramMap, and what they do write — a
    `FormantSpectrum` whose peaks have wired pins — the converter refuses by
    name, file for file (measured on documents built from each generator's
    own functions, without running it). A pipe there could only refuse. They
    are left as they stood and listed in section 7.
17. **Two generators call the converter inside their loop.** `gen_viola_patches.py`
    and `gen_vowel_patches.py` render each patch right after writing it, so a
    call at the end of `main()` would come after renders the loader refuses.
18. **The lint's curve-endpoint rule went with the form** [8]. The 2026-08-13
    convention — a curve spans 20 Hz to 16 kHz, a `vcurve` 0 to 1, clamps
    written as repeated end values — has no checker now: the curves are
    `CurveNode` knots, and the lint retires at 4.7.
19. **The roundtrip smoke's docstring describes the six files as they are**,
    and its counts are corrected: the full gate walks 132 patches, not 199.
20. **The paramMap rule refuses what the engine that read a paramMap
    refused** (the fix wave), and one shape more: a knot of three or more
    numbers, which the engine read by its first two and the old tool could
    not unpack. The review named a curve with fewer than two
    knots and a malformed entry under another name; the coordinator added a
    target that names neither a pin nor a setting of its node's type, which
    the old tool wrote into `params` and the engine refused as unresolvable.
    Of the 326 files that still carry a paramMap, none has any of these.
21. **The roundtrip smoke's two editor checks print after its shapes**, so
    that a failure of either shows in the three lines `gates --fast` prints
    for a gate. The price is that the standing `wiring RENDER DIFF` line no
    longer shows there, only `5/6 shapes clean` (section 7).

## 7. Debt and future cleanup

- **`MPXTest3`'s NaN** (section 1). `TriangleSource::wave` divides by `bias`
  and by `1 − bias`, and the pin's range is 0 to 1 inclusive. The mend is a
  render-moving change to a baseline, so it is listed with the preservations
  to retire after Part 8 (CLEANUP).
- **Convert to Node graph and back** loses `volume` and, by reading,
  `sustaining`, `onsets` and `glideMs` (CLEANUP 54; 4.8's plan).
- **`MultiplexSource::instanceNodes_` is written and read by nothing** (the
  plan's Ledger 3 kept it for 4.6, where the template builder changes shape).
- **A dynamic pin inside a Multiplex template is inert** (4.6).
- **A held line on a Multiplex voice is silent after its first note**
  (section 1). The guard in `PitchedInstrument::play_note` that should hand
  such a line on as ordinary notes tests `allEnvelopes.empty()`, and the
  template's own envelope is in that list; its comment says the list is empty
  for this shape, which is false. Part 5's, with the voice (CLEANUP
  65).
- **Two `node_container` helpers** in the converter's rules, one raising and
  one returning `None`, moved as they stood (CLEANUP 53).
- **The editor's interactive paths were not exercised**: File > Open's status
  line, Open Recent on a refused file, the new Note node's placement in a live
  editor.
- **The nine generators' pipe was read and compiled, not run.**
- **What still writes a paramMap**: 74 other generator scripts, 15 of which
  emit one, are the sweep campaigns' record and are untouched; their output
  now meets the loader's refusal, which names the converter.
- **`patches/sweep` and `patches/old`** are not converted (section 8.4).
- **`gen_liar3.py` and `gen_vowel_baseline.py`** emit the shape 3.9 removed
  (a formant peak whose frequency is wired; a row cannot hold a driven pin).
  The loader refuses their output and the converter refuses to rewrite it.
  They are dead until that capability returns (IDEAS: spectrum-wide offset
  and scale pins) and are candidates for deletion (CLEANUP 57).
- **`gen_vowel_grid.py --f1-retune`** writes the curve-into-a-peak entry that
  3.9's fold turns into a constant, with a note, as it did for the eight
  library vowels.
- **A setting cannot be driven straight from the note by anything the
  editor offers.** Only a Curve may feed a dynamic pin, and the Add row's
  "Add (follows note)" on a setting was the one path. The engine reads the
  form; no file uses it. Related, and the base's behaviour: a Note node that
  only a dynamic pin refers to is dropped by the editor's save (it writes a
  file node for each *wired* Note pin, and a dynamic pin's ref is not a
  wire), which leaves a file the engine refuses.
- **The Mappings dialog's Shape column** reads "direct" for a setting fed
  through a curve-times-velocity product: for a dynamic pin it looks only at
  the node wired to the pin.
- **That a refused load leaves the open graph untouched has no check.** No
  headless mode loads one file and then another; it rests on the throw's
  place in the function, which the review read.
- **`gates --fast` prints FAILED on a clean tree**, as it has since 3.x: the
  smoke's `wiring` shape is one of the roundtrip gate's six known diffs, and
  the smoke has no list of known diffs. A new failure there shows only in the
  gate's three-line tail.
- **The converter's rough edges no file meets**: a file with a UTF-8
  byte-order mark is refused as malformed though the engine reads one; a node
  without an `id`, or a descriptor dump that fails, raises an error the
  driver does not catch; the generators ignore the converter's exit code.
- **The refusal's sentence lives in `render/patch_loader.h`**, a header 4.7
  deletes; it moves with the codec's refusal then.

## 8. The review (R17)

An Opus 5.5 refuter with a fresh context, read-only, reading code only at the
commits while the mutation run edited the working tree. Its brief
(`renders/scratch/ab42/review/brief_r17.md`) gave it four claims to break —
no render moved and the converted files say what their paramMaps said; the
converter is the old tools moved; the surviving push path delivers what it
delivered and nothing else reads a paramMap; the editor refuses before it
touches the graph and can write none — with the plan's nine Review Focus
items, the blind surfaces, and a list of what the coordinator already knew.

**Verdict: none of the four claims broken — 0 Critical, 0 Major, 6 Minor, 8
Flag.** What it tried, and what held:

- It ran the close's converter, its sources read out of the commit, over
  every patch blob at Task 1's commit and reproduced Task 2's 82 files byte
  for byte.
- It checked all 75 registered types for a name that is both a pin and a
  setting. There is none, so the rule's test ("a setting, else a pin") agrees
  with the order the base loader tried them in.
- It read the surviving push path against the base call for call: the same
  calls in the same order for a setting, and the loop the continuation lost
  had skipped every setting.
- It found every caller of `load_graph_from_path` and confirmed each catches;
  only the file's open and its parse run before the refusal.
- It counted the fifteen deleted editor definitions at both commits and found
  each reachable only from paramMap code.

**The Minors, all taken in the first fix wave** (`82b03f6`):

1. *The converter stopped with a traceback on shapes no file has* — a
   `paramMap` that is `null`, a list or a string; a knot that is not two
   numbers. Worse than untidy: the engine refuses `"paramMap": null` by name
   and sends the user to the converter. Each is a refusal by name now.
2. *It wrote loadable wiring from maps the engine had refused*: a `curve`
   with no knots becomes a curve node that passes the frequency through, one
   with a single knot a constant, and a malformed entry under a name other
   than `frequency` was dropped unread. The engine refused all three. They
   are refused now, and by the same reasoning (the coordinator's addition) so
   is a target that names neither a pin nor a setting of its node's type.
3. *The editor's new behaviour had no standing test.* Make-instrument's wire
   was checked only by this checkpoint's scratch runs; the roundtrip smoke
   now runs `--convert-roundtrip` on a plain sound and requires the Note node
   and the ref. (That the open graph is untouched by a refusal still has no
   check a headless mode can make: section 7.)
4. *The refusal's sentence was two hand-copied literals.* It is one constant
   in `render/patch_loader.h`, thrown by the loader and by the editor.
5. *The unit tests' registry table did not have the registry's shape.* It
   listed an input pin under `params` and had no `inputs` key, so a slot rule
   rewritten as "`params` if listed there, else a dynamic pin" would have
   passed every test and misrouted audition's 66 `CombinedSource.source2`
   entries. The table has the real shape and a case drives an input.
6. *Seven sentences of this report were wrong*, each corrected in place: what
   the Mappings dialog lists; that only a Curve may feed a gold pin; that two
   generators call the converter inside their loop; that the converter does
   not bring back every file in `old/`; that `apply_note_bindings` is more
   than its push loop; that the interactive callers are four, three of which
   caught; and a sum that left five tests unexplained.

**The Flags.** Taken: the driver walks folders in sorted order, as its
docstring said; `curve_node.h`'s "pulled like any signal" is not true of a
curve that feeds a dynamic pin and is reworded; a test now pins the one
mechanism this checkpoint changes for a file that could exist — a Multiplex
voice follows a bend; the dynamic-pin test covers a second voice; the refusal
test has a `null` case; the order in which two settings of one node are
pushed is recorded as a difference of form with nothing to hear (section 4).
Its Flag on the departure's glide half was first answered wrongly here ("a
Multiplex voice cannot hold a line"); the re-review showed the engine does
treat such a line as held, and a probe then showed why neither reading can be
heard (sections 1 and 4). Recorded and not taken (section 7): a setting can no longer be
driven straight from the note by anything the editor offers; a Note node
referenced only by a dynamic pin is dropped by the editor's save (the base's
behaviour; no file has the shape); the Mappings dialog's Shape column reads
"direct" for a setting fed through a curve-times-velocity product; a file
with a UTF-8 byte-order mark is refused as malformed though the engine reads
it; the generators ignore the converter's exit code; `kBranchKeys` still
lists `paramMap` (the lint's scrape requires every key the loader reads).

**The ten items.** 1 separation PASS (one rule per file; each refusal in its
host's loader). 2 single responsibility PASS (`Context`, `PushBinding`, the
Multiplex without the fan-out). 3 dependency direction PASS (`instrument.h`
and the loader each lost an include; none added). 4 duplication FAIL, minor:
the refusal's literal twice (fixed), `node_container` twice (CLEANUP). 5
encapsulation PASS. 6 ownership PASS (a binding's `consumer` and `chain` are
`shared_ptr`s to nodes the voice also holds; no cycle). 7 abstraction fit
PASS (Part 4 sections 3.5, 5.5 and 8.1; the plan's three departures
recorded). 8 real-time rule PASS (the push runs at Setup, never in `next()`;
throws only at load). 9 names and comments PASS. 10 tests FAIL, partial:
Minors 3 and 5 (fixed).

**The tests' blind spots it named**, beyond the mutation run's list, and
what was done: the synthesized-id test did not assert that a refused document
is unchanged (it does now); no test had a curve with fewer than two knots
(refused and tested); the refusal test had no `null` (added); the dynamic-pin
test read the setting back but would not see a push delivered *after* the
voice's `start` — the null gate does for the two of its patches whose
*envelopes* are driven per note (the viola and one baseline; an envelope lays
out its stages in `start`), though not for the four pianos, whose strings
read their settings at their first sample; recorded rather than doubled; and
nothing exercised a bend on a Multiplex voice (the new test).

**Re-review (the same refuter, on `82b03f6`): closeable on the code.** Every
Minor was closed at the commit, and every Flag taken but one: the report's
answer to the glide half of the departure was wrong, as told above. Three of
the report's sentences were also still wrong — the sweep-and-old sentence was
an unfilled marker, "prepare" stood where the code says `start`, and "refuses
what the engine refused" was too broad by a knot of three numbers — and all
are corrected in place. It found two things the first wave had left. Three
comments in `instrument.h`, two of them reworded in this checkpoint, said a
setting's state is rebuilt "at prepare", where an envelope lays out its stages
in `start` and a string reads its settings at its first sample. And a target
ending in a dot, which the engine refused, the rule converted as `frequency`.
Both went into a second small wave (`f07c51e`), after which the tests (7,811
checks; 96 tool tests), the null gate, the 132, the nine and the sixteen
synthetics were run again with the same results. It also confirmed, with the
wave's rules run in memory over every folder, that the new refusals refuse no
file anyone needs: sweep converts 103 of 103, old 263 with the same 28 refused
as before, and the nine pending originals and the synthetic set all convert.

## 9. Reading guide

1. `tools/convert_patch.py` and `tools/convert_rules/__init__.py` — the
   driver is eighty lines of code; the rule list and the context are twenty.
2. `tools/convert_rules/parammap.py` — `convert`, one function: the three
   small builders, then the loop that decides every refusal, then the writes.
3. `engine/include/mforce/render/instrument.h`: `PushBinding`, the loop in
   `prepare`, `apply_note_bindings`, `deliver_continuation` —
   `git show e2e442d -- engine/include/mforce/render/instrument.h`.
4. `engine/src/patch_loader.cpp`: `bind_wiring` (what makes a dynamic pin) and
   the first lines of `build_instrument_json` (the refusal).
5. `tools/mforce_ui/main.cpp`: `load_graph_from_path`'s first ten lines,
   `load_graph`, `draw_mappings_dialog`, and the `else` branch of
   `convert_node_to_patch_graph`.
6. `tools/engine_tests/main.cpp`: `run_param_map_refusal_tests`,
   `run_dynamic_pin_tests`, `run_note_wire_tests`.
7. One converted file: `git show eec5aa2 -- patches/baselines/MPXTest3.json`.

## 10. C++ lessons

- **A pushed value and a pulled wire: a number against a function of time.**
  The whole checkpoint turns on this, and it is why one synthetic patch had to
  differ. *Push*: at the note's start somebody computes a number and stores it
  where the consumer will find it (`cs->set(v)`, or `set_setting(name, v)`).
  The consumer reads a constant for the rest of the note. *Pull*: the consumer
  holds a pointer to its source and asks it for a value every sample
  (`pin.source->next()`), so whatever the source does over time — a bend, a
  glide, a vibrato — the consumer sees. In C# terms a push is assigning a
  field; a pull is holding a `Func<float>` (or an `IEnumerator<float>`) and
  invoking it each time. The paramMap on a Multiplex voice was a push, because
  a pushed number is trivially the same in fifty clones; the wire is a pull,
  and each clone's copy of the Note node reads the voice's one frequency, bend
  and all. Settings stay pushed for a real reason, not a historical one:
  `set_setting` rebuilds state the node sizes at `prepare`, so there is no
  pointer a setting could pull through, and re-running it mid-note would tear
  that state. That is the line in `deliver_continuation`'s comment, and the
  test that now pins it.
- **A flag that says which of a struct's fields are alive.** The old
  `PushBinding` had `bool isSetting`, and beside it `cs`, `targetNodeId` and
  `cloneSlot`, which meant something only when the flag was false. Every
  reader had to know that, and the compiler could not help: `b.cs->set(v)` on
  a setting's binding compiles and crashes. C++ has two honest shapes for "one
  of two things": two types (and two lists), or `std::variant<SettingPush,
  PinPush>`, where reading the wrong alternative is an error the type system
  sees — the analogue of a C# abstract base with two subclasses and a `switch`
  on the type, or of a discriminated union in F#. Here neither was needed,
  because one alternative died: with every binding a setting's, the flag and
  its three companions went and the struct says one thing. Worth noticing in
  the other direction too: when a `bool` appears in a struct and some fields
  start carrying comments like "non-setting delivery", a second type is trying
  to be born.
- **`std::vector<GraphNode>` owns its nodes by value, so adding one can move
  them all.** Make-instrument may create a Note node, and the code around it
  says "capture ids, not pointers — `emplace_back` below may reallocate
  `s_nodes`". In C# a `List<GraphNode>` of class instances holds *references*:
  the list's backing array may be reallocated as it grows, but the objects
  stay where they are on the heap and every reference you hold stays good.
  The editor's `std::vector<GraphNode>` holds the objects *themselves*, end to
  end in one block. When the block is full, `emplace_back` allocates a bigger
  one, moves every node into it and frees the old block — and any
  `GraphNode*`, `GraphNode&` or iterator taken before is now pointing at freed
  memory. Nothing warns you; the bug shows up as a crash somewhere else, or
  not at all in a short test. So the new branch finds the pin and the node,
  keeps their integer ids, adds the node, and only then looks the Note node up
  again (`find_node_by_id(noteId)`) to take its output pin. The alternatives
  C++ offers are `std::vector<std::unique_ptr<GraphNode>>` (the C# layout,
  one allocation per node), `std::deque` or `std::list` (which never move
  existing elements on append), or `reserve` when the final size is known.
  The editor's model is Part 7's; until then the rule in that file is the
  comment's.
- **Refuse before you touch: the strong guarantee.** An operation that can
  fail has a choice about what it leaves behind. C++ names three levels: the
  *no-throw* guarantee (it cannot fail), the *strong* guarantee (if it fails,
  nothing has changed — commit or roll back), and the *basic* guarantee (if it
  fails, the program is still valid, but what state it is in is not promised).
  `load_graph_from_path` stops playback, clears the graph and rebuilds it from
  the file, so a throw halfway leaves a half-built graph: basic. The paramMap
  refusal was put *above* `stop_playback()`, the first line that touches the
  open graph, so for this one failure the function is strong: the status line
  shows the reason and the graph on screen is exactly what it was. (Paste,
  which reloads a merged document, gets the same effect the expensive way: it
  catches the throw and reloads the pre-merge file.) The idea is the same in
  C# — validate, then mutate — and so is the mechanism (`throw`, `try`,
  `catch`); what is particular to C++ is that the vocabulary is standard and
  that a destructor must never throw at all, which bit this refactor at 4.1.
  The converter's paramMap rule does the same thing in Python: it queues its
  writes, makes every check, and only then applies them, so a refused document
  is the document that came in.
- **An `#include` is a dependency you can delete.** `instrument.h` included
  `multiplex_source.h` because `VoiceGraph` held a
  `shared_ptr<MultiplexSource>`. With that member gone the include went, and
  that is a larger change than it looks: in C++ `#include` pastes the other
  file's text in, so every translation unit that included `instrument.h` —
  the loader, the hosts, the editor, the tests — also compiled the Multiplex
  and everything *it* includes, and would be recompiled whenever any of that
  changed. The module map lets `render` depend on `source`, so this was never
  a violation; it was a header every host includes carrying a node type that
  only the loader, which builds the clones, has any business naming. C# has no
  equivalent cost: `using` only shortens names, and what a class depends on is
  decided per assembly by the compiler. In C++ the include list at the top of
  a header *is* the dependency graph, which is why the structure meter checks
  each one against the module map and why three more includes the cut had
  orphaned (`combined_source.h`
  in the loader, `<cassert>` in the Multiplex, `<string_view>` in
  `dsp_value_source.h`) were removed rather than left.
- **`0 × ∞`, or why a baseline has rendered NaN all along.** `MPXTest3`'s
  triangle computes `-1 + pos * (4 / bias) / 2` with `bias` at 0. In IEEE
  arithmetic a float division by zero does not trap: `4.0f / 0.0f` is
  `+infinity`, and the program carries on. For every `pos` above zero,
  `pos × ∞` is still `∞`, and the branch that would use it is not taken
  (`pos <= bias` is false). At the one sample where `pos` is exactly `0`, the
  branch *is* taken and the product is `0 × ∞`, which has no answer: NaN. A
  NaN then poisons every sum it enters, compares false with everything
  including itself (which is why a `peak` computed with `>` stayed finite
  while the `rms`, a sum, came out `nan`), and is written to a 16-bit WAV as
  whatever the conversion makes of it, deterministically, so the file's hash
  never moved and no gate noticed. C# floats behave identically
  (`4f / 0f` is `float.PositiveInfinity`, no exception; only *integer*
  division by zero throws there). Two habits follow: a pin whose value is a
  divisor needs a range that excludes zero or a guard at the division, and a
  byte-identity gate proves that a render did not *change*, never that it was
  right.
- **Proving a move by running both.** The three converters became rules by
  moving their functions, and "it's the same code" was not taken on reading:
  a script ran the old function and the new rule on all 3,146 patch files and
  compared the documents. That is the Python side's version of the twin tests
  this refactor uses for moved DSP primitives, and it is cheap exactly because
  the rules are functions from a document to a document with one injected
  dependency. That dependency is the registry's descriptor table, which the
  old tool fetched by running the built CLI from inside the function. The
  rule takes it from a `Context` object instead, so a unit test hands in a
  three-entry dictionary and needs no build at all — constructor injection,
  as you would do it in C# with an interface, here with nothing more than an
  object that has a `descriptors()` method.

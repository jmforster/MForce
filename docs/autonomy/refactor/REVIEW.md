# Refactor lane — REVIEW (Matt's interface)

Format, same as dsp/ and comp/: what to review → what a verdict decides.
Each refactor checkpoint lands here with its architecture report. Decisions
on the shape of the code happen in docs/architecture/ (the target
architecture and the per-area parts); this file is where finished
checkpoints wait for Matt's read.

**Standing rule (Matt, 2026-11-02):** for the remainder of the refactor,
easily-tweaked items (colours, menu dividers, labels, strip colours, any
reversible presentation or grouping call) do not come here. If the cause is
understood and the implication is not far-reaching, the session decides,
records the decision in the report's section 6 and adds the item to
`CLEANUP.md` (the post-refactor suggested cleanup master list). An item
lands here only when it is far-reaching: an interface Matt should own, a
render he should hear, a deletion. The oboe1 A/B at 0 moved is accepted
standing. R6, R8 and R9 below were resolved under this rule the day it was
given.

## Awaiting

**R32 — the editor, slice 4: see** (2026-11-24, commits `593f559`,
`0439168`, fix wave `50b5aa9`; page `docs/architecture/editor-slice4.md`,
signed 2026-11-24 (R31), its section 7 the forty rulings made while
building; report
`docs/autonomy/refactor/reports/2026-11-24-editor-slice4.md`). Every gate
passes; no engine file changed. Unlike slices 1 to 3 when they reached
you, every gesture below runs under a test that moves the mouse or
presses the key, and seven were switched off to see their tests fail.
What no test reaches is the sound card: that part of this list is the
slice's only proof.
- **Start it** from the repo root:
  `build\editor\ui_app\Release\mforce_editor.exe patches\library\winds\oboe1.json`
- **The pane.** Under the canvas: Waveforms, Spectrum, Keyboard. The
  arrow at its top-right corner collapses it and restores it. Q still
  plays with any tab shown.
- **Strips.** Generate: an Output strip, then a strip a node, envelopes
  in yellow. Uncheck Envelopes. Set Columns to 2, 3, 4. About three rows
  show at once at one column; the pane scrolls.
- **Moving about.** The two zoom arrows, Fit, the slider. Over the
  strips: the wheel scrolls, Ctrl and the wheel zooms, a left drag
  scrolls; Left and Right, Shift with them for a page, Home, End, `=`,
  `-`, `0`.
- **One job per key.** With the mouse over the strips `=` and `0` zoom
  and fit and play no note; with it over the canvas they are notes
  again.
- **Pop a strip out** with the arrow at its right end: a window of its
  own with its own zoom. Generate again: it follows. Pop out a second
  and close the first: the second stays where it is.
- **A shorter render.** Zoom in, press End, set Duration to 0.1 and
  Generate: the strips show the short note (the review found them blank
  here; mended).
- **Spectrum.** The tab after a Generate: move the mouse over the plot
  for the frequency and level under it.
- **Listen.** Right-click a node mid-chain, "Listen here": the keys play
  that node, which wears a green mark; Generate and Play still give the
  whole patch. "Stop listening here" ends it. On a node that gives no
  signal the status line says why not.
- **Restart audio.** Edit > Restart audio, or Ctrl+Shift+A: the status
  line says "Audio stream restarted (by hand)".
- **At the sound card, which no test can try:**
  - Play a video in the browser, come back to the editor after a few
    seconds with nothing sounding, and press a key: it should sound (the
    stream is restarted quietly on the return).
  - Change the default output device while the editor is open, come
    back: the keys should follow to the new one.
  - If the sound ever dies while you are in the window, the status line
    should say the stream was reopened, within a few seconds.
- **Decided without you, each reversible** (the report's section 6): the
  pane spans the window's width at the old 28 percent; a strip is at
  least three lines of text high, so fewer show at once than before; the
  old strips stay on screen until the new render replaces them; a
  touchpad's partial wheel notch scrolls; Shift with Left or Right does
  not pan the canvas while the mouse is over the strips; a strip drag
  must begin on the strips; pop-outs open cascaded; a failed restart
  changes nothing and is said once.
- **Yours to say:**
  1. What is wrong or missing at the window, in any words.
  2. **Coming back to the window while something is sounding.** The page
     you signed said the return would never restart a healthy stream. Its
     premise left out why the old editor always did: after another
     program reconfigures the device, a stream can go on running while
     nothing is heard, and nothing inside the program can tell. So now
     the return restarts the stream whenever nothing is sounding, and
     leaves it alone when something is (a restart would cut it). In that
     one case a silent stream stays silent until Ctrl+Shift+A. Leave it
     so, or restart on every return as the old editor did?
  3. The pane's height is fixed. The old one could be dragged taller.
     Say if you want an edge to drag.
  4. Lines to strike from or add to the parity list
     (`docs/architecture/editor-parity.md`). Slice 4's 19 lines were each
     checked against the code (A8).
  5. Slice 5 (Arrange: groups, wormholes, the clipboard, several nodes
     selected, MIDI in) is built too: R34, below.

**R34 — the editor, slice 5: arrange** (2026-11-24, commits `ba975cae`,
`550b3216`, `0bf3a0c4`, `6a3bdab5`, fix wave `843992d8`; page
`docs/architecture/editor-slice5.md`, signed 2026-11-24 (R33), its section
6 the thirty-five rulings made while building; report
`docs/autonomy/refactor/reports/2026-11-24-editor-slice5.md`). Every gate
passes (17,276 checks); nothing that renders changed. Every gesture
below runs under a test that moves the mouse or presses the key, except
where the list says not. It sits on top of slice 4 (R32), which you have
not tried yet either: one run of the editor shows both.
- **Start it** from the repo root:
  `build\editor\ui_app\Release\mforce_editor.exe patches\library\winds\oboe1.json`
  Press F first: the file's saved view frames its groups partly off
  screen.
- **Groups.** The oboe has three (Breath, Drive, Delay), each one purple
  node with a pin for each wire that crosses its edge. Double-click one
  to go in; the breadcrumb above the canvas goes back up. Inside Drive,
  `Drive_inputs.source1` reads "<- WhiteNoise": it is fed from outside
  the group.
- **Select several.** Drag a box on empty canvas; Ctrl and a click adds
  or takes out, on a node or on one of its pins; drag any of them and
  all move, one Undo. The panel says "N selected".
- **Make and change groups.** Select two nodes, right-click one, "Group
  selection (2)". Then "Add to group", "Remove from", "Ungroup" (a
  group's menu), and a group's name in the panel (Enter applies it).
  Select a group and press Delete: it goes with everything in it, and one
  Undo brings it all back.
- **Wormholes.** Right-click a wire: it now runs through two small violet
  nodes, and the status line says so. Select one to see the span to its
  twin; double-click to jump to the twin. The oboe has a pair already,
  one half in Delay and one in Drive.
- **Copy, cut, paste.** Ctrl+C, Ctrl+V, Ctrl+X, the Edit menu, "Copy (n)"
  on a node's menu, "Paste" at the top of the create menu (it lands where
  you clicked). Open a second editor and paste there: it goes through
  the system's clipboard. Ctrl+V while typing in a field pastes into the
  field.
- **Duplicate and Replace with** on a node's menu. Replace a WhiteNoise
  with a RedNoise: its wires stay, and the status names anything the new
  type had no place for.
- **Listen to a group**: "Listen here" on its menu, or the "Listen: Patch
  | Group" switch at the breadcrumb's right while inside one.
- **MIDI, which no test can try at a real port.** With a keyboard plugged
  in before the start, its keys should hold notes (Live checked or not),
  the wheel and aftertouch should reach the patch, and the transport's
  "MIDI:" box should name it. Plugged in later: open the box and choose
  it.
- **The build stamp.** The window's title ends "[build MM-DD HH:MM
  @commit]". The red banner should appear only when a source is newer
  than the last build; if you see it now, say so.
- **Decided without you, each reversible** (the report's section 6): a
  new group is selected when made; F inside a group frames it at the
  top-left rather than centring it; a pin fed from outside the level you
  are in names its source in grey; a failed paste says "Paste failed:"
  and why; the node list in the panel also lists groups; the stale
  banner measures against the last build, not the program's own time
  (the page's ruling 14, amended: a header that nothing compiles would
  otherwise have said STALE for ever).
- **Yours to say:**
  1. What is wrong or missing at the window, in any words.
  2. **Cut on a group.** The page you signed said Delete and Cut on a
     group both take everything in it. I changed Cut: a group cannot be
     copied, so cutting one would destroy what never reached the
     clipboard. Cut now leaves the group and says so; Delete takes it.
     Say if you want Cut to take it anyway.
  3. **A copy carries its seed** (the page's ruling 2, as you signed): a
     copied or duplicated noise node plays the same noise as its
     original. Still yours to turn round.
  4. Lines to strike from or add to the parity list
     (`docs/architecture/editor-parity.md`). Slice 5's 38 lines were each
     checked against the code (A9).
  5. Slice 6 (Shape: tables, the Envelope, Curve, Segment and Shaper
     editors, the shape editor window, partials and formants with the
     scrubber, the mapping table, audition, the crash log) is the last.
     Its page is not written yet.

**R24 — Part 8: build** (2026-11-23, commits `5df7dee`, `270bfaa`,
`97e24ad`, `e5b0760`, `6445a39`, `4f99fab`, fix wave `3ba6354`; design
`docs/architecture/part8-build.md`, signed 11-22; report
`docs/autonomy/refactor/reports/2026-11-23-part8-build.md`). Nothing to
hear: no render and no composed piece moved at any step. This closes the
render, music and build parts; the editor's design is next.
- **What is different when you sit down.** Build with
  `cmake --build build --config Release --parallel`. Test with
  `python tools/gates.py --fast` (under a minute) or `python tools/gates.py`
  (about three). `engine_tests.exe` and `test_figures.exe` are gone: there
  is a test program per module. Every engine file has a new folder and
  the same `#include` line. `README.md` has all of it.
- **Your `build/` has shelved on; everyone else's default is off.** On a
  default build seven of the baseline templates are refused by name, and
  the full gate run fails saying the comp gate cannot run there. That is
  the 2026-10-05 decision as the design restated it.
- **A clean build takes twice as long** (354 s to 635-745 s). The price
  of a compile per module and of 113 test files where there was one; a
  rebuild after an edit is quicker than it was. CLEANUP 147 has the two
  ways to win it back.
- **Eight corrections to the design you signed**, in its section 14;
  seven were made before any code. The three that change what you would
  see: each module compiles all its own headers (without it the compiler
  would refuse a forbidden include only in a `.cpp`); duplicated blocks
  are printed and not gated, because the detector's count moves when no
  duplication does; the extra test program for tests no one module can
  reach turned out not to be needed. Say so if you want any another way.
- **One limit you signed that the review sharpened.** "No file-level
  mutable variable" was being measured by a pattern that missed
  `int counter = 0;` at file scope. It is now measured by a reader that
  knows namespace scope, with its three remaining misses written down
  (CLEANUP 151). A `static` inside a function is still invisible to it
  (CLEANUP 134). The editor's limits rest on this rule.
- **One question: where does the workaround cleanup go?** BACKLOG row 8
  (retire the byte-identity preservations and the until-X guards; renders
  may change there, oboe1 excepted) was slated for "after Part 8". The
  reset's order goes from the build part straight to the editor's design
  and does not name it. My read: it waits with the rest of CLEANUP until
  the editor's first slices are usable, since it changes renders and you
  have no editor to hear them in. Say so if you want it first.
- Try: `python tools/gates.py`; `python tools/structure/check.py --gate`.

**R23 — Part 6: music** (2026-11-22, commits `4d3ad66`, `983a241`,
`6e400c7`, `795eacf`, fix wave `2c52e0e`; design
`docs/architecture/part6-music.md`, signed that day; report
`docs/autonomy/refactor/reports/2026-11-22-part6-music.md`). No composed
piece in a gate moved: the 21 comp baselines and the null gate's 77 are the
same at every step.
- **For your ears, two renders.** `--dun` plays again.
  `renders/comp/audition/part6_dun/` holds `k467_bars_1_to_12.wav` and
  `k467_bars_1_to_27.wav`, on oboe1. The second is the one that uses a new
  rule: its first section has a 4-beat truncated tail, and the second
  section now enters at the cut, where it would have entered 4 beats
  later. "Fine" closes it; if the join at the cut is wrong, say so.
- **Ten of your template files were rewritten**, three of them in
  `scores/pending` (`test_jazz_turnaround_drift`, `_random`, `_scripted`).
  Only the strategy settings changed shape (`"strategy": {"name": ...,
  "settings": {...}}`), and each composes to the same piece as before.
  The originals are in `renders/scratch/ab6/s3/scores_before/`. A template
  that still holds an old key is refused and names
  `python tools/convert_template.py <file or folder>`.
- **Three corrections to the design you signed**, made before any code and
  written into its section 14: the JSON codecs stand beside `ComposeKit`
  and not inside it; `Composition` has no `warnings` list yet; the four
  file readers are defined in `.cpp` files. Say so if you want any of the
  three another way.
- **What I read your "Proceed" as covering, and you did not say:** the
  shelved list (eleven strategies are now under `engine/shelved/`;
  `phrase_aware_figure` and `wrapper_phrase` stayed in compose), and
  leaving four generator faults to the comp lane (comp backlog 26). Either
  is a small change to reverse.
- **One decision of mine you can reverse.** The review found that a part
  with no passage for a section was composed as if it were part 0 in
  section 0, and overwrote part 0's passage in the plan. It is fixed. A
  template of that shape composes different music now; no baseline has
  the shape.
- Try: `python tools/comp_template_null_gate.py check` (22 of 22 as
  frozen; Part 8 replaced the stage-pair form of this gate with a frozen
  manifest, which equals the last Part 6 stage); `mforce_cli --dun scores/baselines/k467_bars_1_to_12.dun
  patches/library/winds/oboe1.json renders/scratch/dun.wav`.

**R21 — checkpoint 4.6: the module, beside the loader** (2026-11-19, commits
`c74ba17`..`37f7ba8`, fix wave `a994e18`; report
`docs/autonomy/refactor/reports/2026-11-19-checkpoint-4.6.md`). Nothing for
your ears: 0 of the 131 moved. No file of yours was written. One question,
at the end.
- What exists now: a second reader of patch files beside the loader, in
  `engine/include/mforce/patch/`. It reads a patch into plain data, checks
  it, and builds nodes from it, as three separate steps. Nothing you run
  uses it until 4.7. The proof that it agrees with the loader is a new gate
  that renders every file through both readers in one process and compares
  the floats: 131 of 131 the same, and the same again from the new writer's
  save of each file.
- A new command: `mforce_cli --check <file or folder>` reads and builds each
  file with the new reader and prints what it refuses, playing nothing.
  Over all 883 files of your four folders it takes under twenty seconds and
  refuses four, all in `pending` and all files that do not load today
  either (`gs_chaotic`, `gs_spots`, `gs_waves`: there is no
  `GrayScottSource` type; `slow_attack_horn`: `formants` on a
  `FormantSequence`). Nothing was done to them.
- What will stop loading at 4.7, when the editor and the command line move
  to the new reader: nothing in your four folders. The report's section 1
  lists every shape the loader plays today and the new reader refuses, with
  the sentence you would see. They are hand-written oddities: a setting
  outside its declared range, `true` where a number goes, an enum word in
  the wrong case, two nodes with one name.
- Look, when you next open the editor (not clicked in a running editor this
  session): a Combiner's `gainAdj` row takes −100 to 100 where it took −1 to
  10, and its plus and minus buttons step by 2 where they stepped by 0.11.
  It was widened because six of your trombone files hold values down to
  −18.088, which the new reader would otherwise refuse as out of range.
- Built as the plan's ruling 20 said, and cheap to change at 4.7, which
  touches every call site anyway: a build's warnings come back as data.
  `build_sound` returns them inside its result; `build_instrument` returns
  the instrument and fills a list the caller passes. Say so if you want one
  shape for both.
- The review found no render at risk and three things wrong in the record,
  all mended in one fix wave: my list of what stops loading was short by
  eleven shapes; a proof I had named could not have failed; "the one case"
  of a setting a node overwrites was three. It also found, by reading, a
  bug older than the refactor: an `ExplicitPartials` reads past the end of
  a list while a file with many rows loads. Both readers do it and no
  render shows it. It is 4.7's first task.
- 115 faults were put into the new code, one at a time. Of the 108 in the
  module, 99 were caught by a test, and the other nine now are. Of seven
  put into the new gate itself, five passed unnoticed; the gate now counts
  what each of its comparisons did and fails when one did nothing.
- **The question.** Three settings are numbers a node works out from its
  rows: an Envelope's `sustainLevel`, an `ExplicitPartials`' `maxPartials`,
  a `LayeredRedNoiseSource`'s `count`. The new record still stores the
  file's number for each, beside rows that overrule it, and a save writes
  it. No render hears it. Should a declaration be able to say "this setting
  is derived", so the record leaves it out? My read: not now. Only
  `sustainLevel` is purely derived; the other two size the default rows
  when a file gives none, so some of the time they are real settings. What
  makes every reader agree is one rule, "settings before arrays", which the
  builder follows and 4.7's editor layer will, with a test that fails when
  it does not. The stored number is harmless, and CLEANUP 110 keeps the
  question for after Part 8. A verdict the other way adds a field to the
  declarations (Part 2's interface) before 4.7.

**R20 — checkpoint 4.5: the terminal types and the document's spellings**
(2026-11-18, commits `061a606`..`60ae3c1`, fix wave `f9a7e43`; report
`docs/autonomy/refactor/reports/2026-11-18-checkpoint-4.5.md`). Nothing for
your ears: 0 of the 131 moved. Nothing to decide. The four rulings you were
shown before your "Go" were built as written.
- 784 of your patch files were rewritten (18 library, 39 baselines, 689 in
  audition, 38 in pending). The originals are kept: pending's at
  `renders/scratch/ab45/pending_before/` and, this time, audition's 689 at
  `renders/scratch/ab45/audition_before/`. Only spellings changed: a
  channel's and a mixer's `inputs` are feeds in `params`; `ui` and `groups`
  are one `layout` block; `_comment` is `comment`; an enum written as a
  number is its word (`"operation": 3` is `"Sum"`). The report's section 1
  shows each before and after.
- In `patches/scratch` and `old`, 7 and 60 plain sounds that rendered
  yesterday are refused today until `python tools/convert_patch.py <file
  or folder>` rewrites them; the refusal names that command. None in
  sweep.
- Until 4.7, an unconverted file whose only old spellings are `ui`,
  `groups`, `_comment` or `_provenance` renders from the command line and
  will not open in the editor (about 200 sweep files, 3 in scratch, 4 in
  old). The editor refuses it before touching the graph you have open; it
  would otherwise have opened with no positions and saved with none. The
  same command converts it.
- Look, when you next open the editor (none of it was clicked in a running
  editor this session): a Mixer has one `channels` pin that takes any
  number of Channels, and the `+` button on its title bar is gone; its
  outputs read `out` and `tap` where they read `outL` and `outR`; a
  Channel's unlinked `source` is a gray name with no number; File > Save on
  a plain sound keeps the file's `seconds` (it wrote 5 whatever the file
  said); a baseline's comment survives a Save (the editor dropped it
  before). If any of that is otherwise, say so.
- One thing in CLEANUP you may want an opinion on some day (row 94): the
  mixer advances a wired gain once per channel, so with two channels a
  gain LFO runs twice as fast. It is the engine's behaviour from before
  the refactor and nothing in library or baselines has the shape; mending
  it would move a render, so it waits for Part 8.
- The review found one wrong refusal and two ways the editor could lose
  data, all three in shapes none of your files has: two channels sharing a
  Multiplex's template were refused; a Note field written by index or in
  another case was dropped at a Save; a plain sound with no Mixer was saved
  with no output. Fixed, with tests; the report's section 8 has them.

**R19 — checkpoint 4.4: every type a declaration** (2026-11-17, commits
`a66d60f`..`590a819`, fix wave `96539b8`, a second wave of comments and
tests `6b0e17f`; report
`docs/autonomy/refactor/reports/2026-11-17-checkpoint-4.4.md`). Nothing for
your ears: 0 of the 131 moved. Nothing to decide. The five rulings you were
shown before your "Go" were built as written.
- 30 of your patch files were rewritten (3 library, 3 baselines, 22 in
  audition, 2 in pending; pending's originals are at
  `renders/scratch/ab44/pending_before/`). In audition the change is one
  written-out number, `"val": 0.0`, on the fourteen `bwg_perc1` and
  `bwg_perc2` files, and `"add"` spelled `"Mix"` on `afp31_v5` to `_v12`.
  A file in `patches/scratch` or `old` that holds a `cutoff`, a feed's
  `weight` or an inline `expandRule` (10 and 67 files) no longer loads
  until `python tools/convert_patch.py <file or folder>` rewrites it; the
  refusal names that command.
- Look, when you next open the editor (none of it was clicked in a running
  editor this session): the three Butterworth filters have a `sections`
  row, and the node you hear has that many (the editor always played two
  before); a `MultiSource` shows `weights` and `delays` lists and keeps its
  weights through a load (it dropped them before, and saved the file
  without them); a `PhasedValueSource` shows its feeds as links and its
  four columns as a table; every Partials node has an `expandRule` pin; a
  `NameGate`'s name is a text row among its settings and survives
  Duplicate. If any of that is otherwise, say so.
- One thing in CLEANUP you may want an opinion on some day (row 81): a
  `VarSource` with no `val` meant 0 to the old loader and means 1 now. The
  converter wrote the 0 into the files that relied on it. Declaring 0 as
  the default instead would remove the question, and would change what a
  new Var node in the editor starts at.
- The review found a crash that only the editor could reach: any link
  change while a `PhasedValueSource` was sounding read past the end of an
  empty vector. Fixed, with tests; the report's sections 8 and 10 have it.

**R18 — checkpoint 4.3: tables, Envelope and Curve** (2026-11-14, commits
`4af294a`..`2765aef`, fix waves `d910f27` and `fb4e453`; report
`docs/autonomy/refactor/reports/2026-11-14-checkpoint-4.3.md`). Nothing for
your ears: 0 of the 131 moved. Nothing to decide; one ruling of yours
recorded, and things to look at when you next open the editor.
- Recorded, your ruling of 2026-11-14: the verification is lighter from 4.4
  on. The four rules are in the report's section 3 and in Part 4 §8.6.
- 444 of your patch files were rewritten as columns (38 tracked, 373 in
  audition, 33 in pending; pending's originals are at
  `renders/scratch/ab43/pending_before/`). A file in `patches/sweep`,
  `scratch` or `old` that holds a stage list or a knot list (954, 54 and 124
  files) no longer loads until `python tools/convert_patch.py <file or
  folder>` rewrites it; the refusal names that command.
- Look, when you next open the editor (none of it was clicked in a running
  editor this session): an Envelope's `timeMode` is a dropdown among its
  settings, where the "seconds" checkbox was; the stage table and the curve
  editor should behave as they did; a copied or newly created Curve node
  carries its curve; a newly created Curve node clamps its input to 0..1 in
  the editor as its saved file always did (its two seed knots now reach the
  live node — CLEANUP 70 if you would rather a new curve start as the
  identity); a file from sweep or old with a stage list puts `… 'stages' is
  no longer read: python tools/convert_patch.py <patch> rewrites it as the
  eight stage columns` on the status line and leaves your graph as it was.
  If any of that is otherwise, say so. The report's section 1 has the whole
  list, with three curve-editor faults older than this checkpoint that were
  mended in passing.

**R17 — checkpoint 4.2: the paramMap goes** (2026-11-10, commits
`496bb42`..`0e6648d`, fix waves `82b03f6` and `f07c51e`; report
`docs/autonomy/refactor/reports/2026-11-10-checkpoint-4.2.md`). Your verdicts
of 2026-11-10 are in; one question is left, and it does not block 4.3.
- Verdict 1, "delete MPXTest3": done. The file is in
  `patches/old/baselines_retired_2026-11-10/` (patches are moved, never
  deleted) and out of the null gate's manifest. The scope is 131 patches and
  the manifest 77. No Multiplex voice is left in library or baselines; the
  engine tests keep three Multiplex cases (a wired note inside a template, a
  bend, the allocation count). The oscillator still divides by a `bias` of
  zero; with the file gone, mending that moves no render in scope (CLEANUP
  66).
- Verdict 2, "we do need a direct path … mapping Frequency to Frequency,
  don't want to have to put a dummy curve in between": that case has a direct
  path and always had. `frequency` is a pin on every type that has one (15 of
  75; no type has a `frequency` setting), and a Note node wires straight to a
  pin, in the editor and in every converted file (83 frequency pins in
  library, 30 in baselines, 471 in audition, 56 in pending, each fed by the
  note with nothing between). The converter interposed no curve anywhere: a
  curve node exists only where the paramMap entry had a `curve`. And no file
  in any folder drives a *setting* without a curve.
- Decide, when you like: **do you still want a setting — a gold pin, such as
  a string's `t60` or an envelope's `timeScale` — to take the note
  directly?** That is the only place a curve is required, by the editor's
  rule since 2026-08-19 ("Only a Curve may feed a dynamic pin"). The engine
  and the file format already take the direct form; the cost is in the editor
  alone and is in CLEANUP 58.
- Look, when you next open the editor (none of it was clicked in a running
  editor this session): Edit > Parameter mapping is a view with no Add row;
  Edit > Convert to Patch graph on a plain sound draws a wire from a Note
  node; a file from `patches/sweep` or `patches/old` that carries a paramMap
  puts `instrument.paramMap is no longer read: python tools/convert_patch.py
  <patch> rewrites it as wiring` on the status line and leaves your graph as
  it was. If File > Open on such a file does anything else, say so.

**R16 — checkpoint 4.1: the seam's render half; `hiBoost`** (2026-11-09,
commits `e5518ef`..`7b32205`, fix waves `e0cb2e2` and `3c012ea`). One pair for your ears, by your 2026-10-05
decision, and one question the close raised that is yours and does not block
4.2.
- Read: `docs/autonomy/refactor/reports/2026-11-09-checkpoint-4.1.md`
  (fifteen minutes; section 1 — what moved and what did not, and why the pair is a
  second take rather than a re-balance; section 2 — `BuiltSound`, `Mixer` and
  `play_embedded_score` as the first half of Part 4's seam; section 6 — the
  session's decisions, 17 to 22 from the review and the close; section 8 —
  the review; section 10 — the C++ lessons).
- Listen: `renders/scratch/ab41/review/old/template_ode_to_joy_1.wav` (the
  base build, `hiBoost` 0.3) against
  `renders/scratch/ab41/review/new/template_ode_to_joy_1.wav` (the close
  build, the field gone). What changed by the rule: every note above 100 Hz
  enters the mix lower, by 1.0 dB at middle C up to 3.4 dB at the top of the
  keyboard. What you will actually be comparing: the first two notes are the
  old ones scaled by exactly that rule, and from the third note on every note
  is a different realisation (the same pitch, length and velocity, a
  different waveform), because `piano_default` plays the whole piece on one
  voice whose random streams run on from note to note and whose notes now end
  at different samples. The other twenty templates' pairs are in
  `renders/scratch/walk3_null_gate/pre41/` and `post41/`. A verdict decides
  only whether anything in the new take is wrong; the deletion itself is
  decided.
- Decide, when you like (non-blocking; BACKLOG row 11): **should an ordinary
  note re-anchor its random draws when it starts**, as a held line's
  continuations have since 2026-09-20? Today a pre-rendered note's waveform
  depends on how many samples every earlier note on its voice rendered, so
  one early note's level can change every later note's realisation; on a
  one-voice patch that is the whole piece. Re-anchoring would make a note's
  realisation a function of the note alone and would move every multi-note
  render in the library, so it is a Part 5 or post-freeze decision, not a
  refactor step. Report section 6, decision 22.
- For your information, nothing to decide: Part 4 section 5.8's sentence on
  `frames` was wrong for a file with no `seconds` and is corrected in place
  (the review caught it; 4.6 would have been written from it); an
  instrument-form file with a `StereoMixer` node named `mix` and gains would
  now have them ignored (no file has that shape); two channels naming one
  source is refused by name; `scores/baselines/test_k467_walker.json` is
  prose, not JSON (CLEANUP 49, your file).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78);
  `python tools/comp_template_null_gate.py compare pre41 post41` (21 DIFF,
  one MISSING: the walker file); `mforce_cli --test-ornaments` (the usage
  line).

**R15 — checkpoint 3.9: the formant family** (2026-11-07, commits
`6e43ca8`..`f94695b`). One render for your ears, by your own decision; nothing
else to decide under the 11-02 rule.
- Read: `docs/autonomy/refactor/reports/2026-11-07-checkpoint-3.9.md`
  (fifteen minutes; section 1 — the one render that moved and the capability
  that left the library with it, the editor-visible changes; section 2 — the
  design: `DrivenPart`, `Formant` with one verb, the three leaves, the fold;
  section 6 — the session's decisions, the plan's four holes among them;
  section 8 — the review; section 10 — the C++ lessons).
- Listen: `renders/scratch/ab39/review/old/sing_soprano_E.wav` (the pre-3.9
  CLI over the unrewritten file: F1 following the 440 Hz note above its 350 Hz
  knee) against `renders/scratch/ab39/review/new/sing_soprano_E.wav` (the
  close build over the rewritten file: F1 at 350 Hz). This is the one in-scope
  render 3.9 moved — your "2" on 2026-11-07, rows only. The other seven
  paramMap-driven vowels (`sing_alto_A`, `sing_alto_U`, `sing_bass_U`,
  `sing_soprano_A`, `sing_tenor_O`, `sing_tenor_U`, `speech_w_AW`) and the two
  `sweep/vowel_pitch_control/` files lost the same capability without a render
  moving (they sing below their knees). IDEAS holds the way back (spectrum-wide
  offset/scale pins on `FormantPeaks`). A verdict decides: does the soprano E
  stay in the library as it now renders, or is pitch-following wanted back
  before Part 4 (then IDEAS' entry becomes a backlog row)?
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open
  `patches/library/voice/sing_alto_A.json` in the editor: one `FormantPeaks`
  node, its five rows in the grouped table; drag a row's frequency and watch
  the strip; the Additive menu reads Formant Peaks / Formant Bands / Formant
  Sequence.

**R14 — checkpoint 3.8: `Partials` split by responsibility** (2026-11-06,
commits `2dfabe2`..`e27f7ad`) — Part 3 complete. One render for your ears
and one non-blocking interface question.
- Read: `docs/autonomy/refactor/reports/2026-11-06-checkpoint-3.8.md`
  (fifteen minutes; section 1 — the one audible change and the two
  editor-visible ones; section 2 — the design: `PartialSet`, six value-type
  layers, `PartialLayout`; section 6 — the session's decisions, the executor
  and reviewer tier on Opus 5.5 among them; section 8 — the review,
  0 Critical, 0 Major, 10 Minor, 11 Flag — the Minors fixed in the wave, re-review closeable; section 10 — five C++ lessons: composition versus
  inheritance for a layer, an abstract class versus an interface, `reserve`
  as an owner–layer contract, signed zero, the most vexing parse).
- Listen: `renders/scratch/ab38/review/as2_bright_attack_test.wav` (the
  retired `AdditiveSource2` baseline, rendered by the pre-3.8 CLI) against
  `renders/scratch/ab38/review/additive_bright_attack_test.wav` (its
  replacement, `patches/baselines/additive_bright_attack_test.json`: two
  `ExplicitPartials` under a `CompositePartials`, per-group envelopes, the
  old amplitude wander as shimmer; ruling 8).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open
  `patches/library/strings/viola_default.json` in the editor and play it:
  the Additive family's menu now reads Additive / FullPartials / … with
  no Basic or Alternate.
- Decide: (1) the retired `AdditiveSource2` baseline's replacement — keep
  `additive_bright_attack_test.json` as a baseline as authored, re-author
  it differently, or drop it (the deletion itself is the target's, decided
  2026-10-05; the old file stays in `patches/old/additive_source2/`); (2)
  non-blocking: the name of the formant part base when the formant half of
  CLEANUP 17 moves (`Formant` is taken by the concrete peak; the
  candidates are `FormantShape` or a spectrum-flavoured name) — the
  convention itself is recorded in ruling 2 and needs no verdict.
  RESOLVED 2026-11-07 in session: Part 3 §11 — `Formant` the base,
  `FormantPeaks` rows, `FormantBands`, `FormantSequence`; checkpoint 3.9.

**R13 — checkpoint 3.6: one type per file** (2026-11-05, commits
`ed04dda`..`5c4c5db`) — nothing to decide. A move-only checkpoint: the six
multi-type node headers each became one file per type, `noise_sources.h` and
`filters.h` deleted (no umbrella), the shaped noise draw white and red share
folded into one `inline` helper, the six family `.cpp` moved under
`engine/src/source/<family>/`. No render moved.
- Read: `docs/autonomy/refactor/reports/2026-11-05-checkpoint-3.6.md` (ten
  minutes; section 1 — nothing is audible or editor-visible; section 6 — the
  two session decisions; section 8 — the CLEAN independent review; section 10
  — four C++ lessons: what `#include` really does and why an umbrella is a
  shim, `inline` free functions and the ODR, `git mv` and history across a
  split, a module map as a build-time contract).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open the
  editor and right-click the canvas — the Noise, Wavetable, Filter, Combiner
  and Additive menus list exactly what they did at 3.5 (one type per file is
  invisible there). `git show 400a587:engine/include/mforce/source/filter/filters.h`
  against the four new `filter/bw_*`/`delay_filter.h` files — byte-identical
  bodies.
- Decided, not asked (2026-11-02 rule): (1) `duplicate_blocks` rose 146→163
  (+17) against the plan's "must not rise"; the rise is the one-type-per-file
  preamble replicated per file plus the `bw_lowpass`/`bw_highpass` sibling
  similarity that already existed inside the umbrella header — no new authored
  logic (the reviewer confirmed). Accepted; CLEANUP 31. (2) `fm_source.h` and
  `patch_loader.cpp` dropped transitive-only includes; a stale `filters.h`
  comment and `lint_patches.py`'s source path were repaired in place.
- Future cleanup: a shared Butterworth base for the three BW filters would
  remove the sibling duplication (new design, out of a move-only checkpoint;
  CLEANUP 32).
- Review: an independent reviewer in place of the Fable refuter (ruling 8; 3.6
  touches none of the refuter's surfaces). Verdict CLEAN, nothing above Flag —
  it body-diffed all 26 types, failed the build on a removed include, failed
  the twin under two helper mutations, crossed the `.cpp` renames with
  `git log --follow`, and judged the duplicate rise benign. Two Flags
  (record-only): the helper's `lastSign==0` guard is a twin blind spot that
  cannot affect byte-identity; the `lint_patches.py` edit was not in the plan's
  Task 4 bullet (it is the move's minimal consequence). No Major, no re-review.

**R12 — checkpoint 3.5: the envelope presets become one type; the `KSPianoString` alias goes**
(2026-11-04, commits `750f70e`..`a2ff727`) — under the standing rule,
nothing blocks the landing; one interface question is yours (item 1,
answer any time), and the consequences of the two deletions are named.
- Read: `docs/autonomy/refactor/reports/2026-11-04-checkpoint-3.5.md`
  (its reading guide is ten minutes; section 1 names the one correction to
  Part 3 section 6 — the preset was a load-time form, never a setting —
  and the three editor-visible differences; section 6 item 3 is the table
  of the six layouts and the one default that differs; section 8 the
  refutation and the wave; section 10 four C++ lessons: a table of
  function pointers as a dispatch, static factories versus subclasses to
  name a layout, `inline constexpr` in a header, the most vexing parse met
  in a test).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open the
  editor and right-click the canvas — Envelopes lists one Envelope; create
  one and look above its stage table: a Preset combo with Apply; pick ASR
  and Apply, the table lays out three stages. Open
  `patches/library/winds/bassoon1.json`: `Morph_env` is an Envelope whose
  stage table holds what its AS knob group held. Load any file from
  `patches/old/` that names a preset class: the status line names
  `tools/migrate_deleted_types.py`.
- Decide, yours, non-blocking: (1) **the editor's face of an Envelope.**
  The six preset types are gone from the menu and with them the preset
  knob groups you laid out on 2026-08-29 (Duration, Shape); every Envelope
  is the stage table plus the Preset combo, and the 174 rewritten nodes
  show the table where they showed knobs. If you want the knobs back, the
  way is `Setting<int> preset` plus the layout knobs as settings on
  `Envelope` (inert on an authored shape) — additive on top of this
  checkpoint; the files on disk already carry exactly the keys that design
  would read, so there is no second rewrite (plan ruling 1). The code as
  landed is the recommendation: the stage list is the one truth, a preset
  is a way of writing one, which is what your own note in `envelope.h`
  says ("presets are UI conveniences").
- Decided, named here and not asked: (2) `patches/old/`'s 59 nodes (54
  preset-class, 5 alias) in 41 files fail to load naming the tool; one
  command rewrites them if ever wanted. (3) `patches/pending/` was
  rewritten under Part 3 section 6's own count (23 files; originals at
  `renders/scratch/ab35/pending_before/`), and `sweep/` and `scratch/`
  too (whitespace reformatted to indent 2 on the first run; the tool keeps
  a file's indent since). (4) `sustainLevel` reads 0 on an `as`/`asr`/`ads`
  shape where the class showed its stored level — the knob was inert
  there (CLEANUP 22). (5) The `adsr` form's `release` default 0.0 against
  the class's 0.1: every node on disk carries the key; the tool wrote 0.1
  where a class node lacked one (CLEANUP 23). (6) Two generator scripts'
  AR ramp drivers now apply the `attackMin`/`attackMax` they always carried
  (dead on the class). (7) The lint's selftest was passing nothing (its
  bad key is in the global allowlist); fixed in Task 1's commit.
- Refuter: byte-identity upheld with the refuter's own A/B (the eleven rewritten scope files and three controls 14/14, eight synthetics of its own 8/8, five archived copies through the tool); no Critical; two Majors, both in the editor's new Preset Apply (the cached `sustainLevel` — 0 on every two- or three-stage node — pushed over a fresh ADSR's four slots, and that push outside the audio lock); three Minors (the tool carried keys a class never read into a form that reads them, seven archived files would have moved; the refusal test pinned one knob of eleven; the whole-corpus roundtrip gate permanently red since 3.2); two Flags. The wave (`b1d0222`) fixed all but the standing Flag — Apply re-pulls under the lock through one function with a headless `--apply-preset` twin that proved it on `bassoon1` and `reed_test`; the tool drops the dead keys by name; the gate's known set is the backlog-79 six and the run is green. Re-review: closeable, nothing introduced, four Flags — two one-line fixes taken (an upper-case preset name; an ASCII note), one coverage correction accepted (the three in-scope `as` nodes are dead in the render, so `as` is render-pinned by the synthetic sets and the tests, not by the scope — CLEANUP 30), one stale scratch label.

## Resolved

**R33 — slice 5's page: arrange** — SIGNED 2026-11-24 (Matt): "Go ahead.", after he named the session's list of selected things (`selections`: "I would put an S on it"). Read as accepting the page's seventeen rulings as written (`docs/architecture/editor-slice5.md`); he spoke to none of the four put first (one clipboard, so Cut is new on it; a copy carries its seed; Delete and Cut on a group take what is in it; a dead group member refuses the file), and that reading is the session's, not his words. The build began the same day, from the read of the old code kept at `renders/scratch/ed5/old_read.md`.

**R30 — the editor, slices 1 to 3 at the window** — TRIED BY HAND 2026-11-24 (Matt), in two passes. First pass: open, wiring, undo, redo and a key's sound worked; a right click gave no menu, no node could be selected, a click on the piano did nothing. The first two were one fault (after drawing the graph the canvas asked whether the mouse was over it of the wrong window; the wheel and every node drag died with it), fixed in `7c6d278` with two more found on the way (the Output node could never be dragged; a running plain sound did not follow an edit). Second pass: "Almost all tests worked, including loud/soft piano keys"; he named nothing else that failed, and of the two lines he asked about (the phrase marks on the piano; "pre=" against "post=") said "Working as designed. Ok to proceed." Why the piano did nothing on the first pass is not known; nothing in the fix is on its path. The canvas's gestures and the piano's press now run under tests that move a mouse (`editor/ui_views/tests/mouse_tests.cpp`). Report `docs/autonomy/refactor/reports/2026-11-24-editor-slice3.md`; commits `b341e96`..`9875f8a`, `7c6d278`. Slice 4's build began the same day.

**R31 — slice 4's page: see** — SIGNED 2026-11-24 (Matt): "Signed, proceed", the page's thirteen rulings as written (`docs/architecture/editor-slice4.md`). Built the same day: R32. The page's section 7 holds the forty rulings made while building; its ruling 34 amends ruling 3.

**R29 — slice 3's page: generate** — SIGNED 2026-11-23 (Matt): "Signed, proceed", the page's eleven rulings as written. Built the next day: R30. The page's section 7 holds the twenty-six rulings made while building.

**R28 — the editor, slice 2: build** — SUPERSEDED 2026-11-24 by R30 (slice 3, built on it before a verdict). Its list of what to try is carried at the end of R30's. Report `docs/autonomy/refactor/reports/2026-11-23-editor-slice2.md`; commits `dcba0b3`..`34febff`.

**R27 — what "done" is, and slice 2's page** — SIGNED 2026-11-23 (Matt): "Signed, proceed", the page's eleven rulings as written. The parity list (`docs/architecture/editor-parity.md`, 229 capabilities of the old editor) is the measure of done and stays his to strike lines from. Slice 2 was built that day: R28.

**R26 — the editor, slice 1: open, play, tweak, hear, save** — SUPERSEDED 2026-11-23 by R28 (slice 2, built on it the same day, before a verdict). Its list of what to try is folded into R28's; its third question was answered that day ("I want the refactor to be entirely done before we resume adding features"). Report `docs/autonomy/refactor/reports/2026-11-23-editor-slice1.md`; commits `947d021`..`e40ad75`, fix wave `98603b6`.

**R22 — Part 5: render, the contract, play** — VERDICT 2026-11-22 (Matt): "That's all good, let's continue to be a little forgiving of minor regression stuff, eg, don't worry about --josie." Accepted as landed: the replay fix, the embedded score read in score order, the live player's limits. `--josie` is left as it is, unmended and undeleted (CLEANUP 124). The standing reading for what follows: a minor regression in a peripheral thing gets a line in the report and in CLEANUP, not a fix and not an item here. As it was put to him:
R22 (2026-11-22, commits
`ad463d8`..`a9e5677`, fix wave `fa085c3`; design
`docs/architecture/part5-render.md`, signed that day; report
`docs/autonomy/refactor/reports/2026-11-22-part5-render.md`).
- **For your ears, one render.** `renders/comp/audition/part5_replay_fix/`
  holds `template_binary_before.wav` and `template_binary_after.wav`. The
  piece has two sections and every part was being played once per section;
  it is played once now. Nothing else in a gate moved (null gate 77 of 77,
  the other 20 comp baselines the same). A verdict of "fine" closes it; if
  the old one is the one you want, say so and I look at why.
- **One deletion that is yours.** `mforce_cli --josie` fails before it
  plays and did before this part; its four patches are in no folder.
  "Delete it" removes the mode; saying nothing leaves it for Part 6.
- **One decision of mine you can reverse.** A patch's embedded score is
  now read in score order. The old reader delivered an unphrased note late
  when a phrased note followed it. No score in library or baselines mixes
  the two, so nothing you have heard changed.
- **For the editor's design, not now.** A held line makes a live
  instrument monophonic, because the engine has one held line per
  instrument.

**3.7 — the real-time fixes** (2026-11-06) — closed without a checkpoint,
Matt's verdict "skip to 3.8". Part 3 §9's seven items were already landed by
2.6 and 3.4 (the tables and every evolution buffer reserved at `prepare`,
fitted within capacity per note); the allocation gate is 0 of 132 and
`engine_tests` covers each named node. The live-edit class (a structural
setting read live on the audio path after a prepare-time sizing) stays booked
to Part 7 (CLEANUP 33); the gate's blind spots are CLEANUP 34. No report:
nothing was built.

**R11 — checkpoint 3.4: each evolution a `Component`; the holders and the string form gone**
(2026-11-03, commits `efd10c7`..`0d1e826`) — under the standing rule,
nothing to decide; listed so the chain of reports is unbroken, and so two
consequences of a deletion you signed on 2026-10-05 are in front of you.
- Read: `docs/autonomy/refactor/reports/2026-11-03-checkpoint-3.4.md`
  (its reading guide is ten minutes; section 1 names the two live-edit
  differences and the deletion's consequences, section 6 the three
  comparison tables — the string form against the node form, each
  evolution's moved members, `TargetEvolution`'s synthesis — section 8 the
  refutation and the wave, section 10 four C++ lessons: an abstract base
  versus a pure interface, a member that files itself versus a reference to
  another's, a return nobody reads, idempotence as a contract).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); open the
  editor and right-click the canvas — Experimental > Wavetable now ends with
  Target Evolution: create one, wire it into a Wavetable's `evolution`, give
  it two or three partials, play; point the CLI at
  `patches/old/baselines_culled_2026-09-14/pluck_test.json` — it refuses the
  load naming `tools/string_evolution_to_node.py`; run that tool on a copy
  and the copy loads; open
  `engine/include/mforce/source/wavetable/wave_evolution.h` — one base,
  thirteen evolutions, no wrappers.
- Decided, named here and not asked: (0) the A/B, 0 of 132; (1)
  `WaveEvolution` is an abstract `Component` base rather than a pure
  interface beside it (the pin lifecycle written once; Part 3 §5's words
  kept; CLEANUP row 17 holds the naming question); (2) a Pluck or Averaging
  setting edited mid-stream in the editor no longer restarts its draw stream
  — the old wrapper rebuilt its evolution on every change, Pluck's with a
  constant seed that ignored the registered one; no render in scope; one
  `reanchor()` in a hook brings the restart back if you ever want it
  (CLEANUP row 21); (3) the eight archived string-form patches in
  `patches/old/baselines_culled_2026-09-14/` no longer load — never deleted,
  the converter rewrites one on demand — and `targetWave` has no node form
  (no patch in the tree ever used it); (4) `patches/pending/harsh_pluck.json`,
  your sandbox, was converted in place under the target's 2026-10-05 line
  that names it as one of the two live string forms; the original is at
  `renders/scratch/ab34/harsh_pluck_before.json`; (5) six `algev_test_*`
  baselines lost a dead `evolutionSeed` key (only the deleted branch ever
  read it) rather than the loader tolerating it; (6) `TargetEvolution` sits
  on the Experimental shelf with the derived label and session-chosen ranges
  (CLEANUP row 18). The refuter: no Critical, no Major; six Minors — the converter's averaging defaults, the refusal's wording for a slot, a stale `component.h` comment, the Part 3 section 2 diagram, and two missing test pins (Averaging's `autoAdjust` path, `TargetEvolution`'s morph and resample) — and ten flags; the wave took all six and the cheap flags with tests first, the same refuter re-reviewed it (closeable: every finding closed at the cited lines, nothing introduced — its spot A/B of the twelve in-scope wavetable patches, the synthetic seven, the eight archived forms and the pending patch all identical, the 132 re-compared; its one new flag, an unpinned converter default, closed by a test the same hour), and its own renders agreed with the arms: every in-scope wavetable patch, the synthetic seven, and all eight archived string forms converted by the tool render byte-identical across the checkpoint.

**R10 — checkpoint 3.3: the shared DSP primitives in `core/dsp`**
(2026-11-02, commits `0d500fc`..`b3c6160`) — under the standing rule,
nothing to decide; listed so the chain of reports is unbroken.
- Read: `docs/autonomy/refactor/reports/2026-11-02-checkpoint-3.3.md`
  (its reading guide is ten minutes; section 1 names the one departure by
  construction and the bounded sign-of-zero class, section 6 the seven
  comparison tables — where "exact" needed an argument — section 8 the
  refutation and the wave, section 10 four C++ lessons: value members, free
  functions and `inline`, what float arithmetic promises, two initialisation
  traps).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78); play
  `patches/library/winds/oboe1.json` and any KS piano patch on the keyboard —
  nothing changed (the A/B is 0 moved over 132); open
  `engine/include/mforce/core/dsp/` — nine headers, one primitive each.
- Decided, named here and not asked: (0) the A/B, 0 of 132; (1) EKS's
  stiffness allpass clears both memories at a note's start — a one-LSB
  difference over the rest of every note after the first, only with
  stiffness > 0, and no such patch in library or baselines (the alternative
  was a method on a shared primitive for one caller's bytes); (2)
  `BiquadSource`'s resonance mode runs the general section (the sign of a
  zero only, which 16-bit renders cannot carry); (3) `TuningAllpass` keeps
  the design table's name (CLEANUP row 15).

**R9 — checkpoint 3.2: families at registration; the menu generated; `Theme`; folders by family**
(2026-10-31, commits `67071f8`..`d5ebbf3`) — VERDICT 2026-11-02 (standing rule): (0) accepted, byte-identical; (1) the seven colours stand (CLEANUP: colour follows family, one Theme entry or one family word moves any); (2) the generated menu stands, the dividers are not restored (CLEANUP: a grouping fact on the tag if ever wanted); (3) the reading stands — the `music` → envelope edge is carried visibly until Part 5's split of `pitch_bend.h`, as the target's 3.7 already schedules; (4) the alias stays creatable until 3.5 deletes it; (5) the Vibrato and Segment strips stand as the Envelope family's (CLEANUP); (6) the timing is accepted on the mechanism (no render-path file changed except its include lines; the refuter traced it).
- Read: `docs/autonomy/refactor/reports/2026-10-31-checkpoint-3.2.md`
  (its reading guide is ten minutes; section 1 names the four things the
  editor shows differently — seven colours, eight Experimental newcomers,
  the lost dividers, two strips — and the meter's +1, section 5 the gates,
  the A/B and the timing, section 6 the decisions taken on your behalf —
  rulings 26 and 27 are the ones a verdict below decides — section 7 the
  debt, section 8 the refutation and the wave, section 10 four C++ lessons).
- Try: open the editor and right-click the canvas — the create menu reads
  Curve, Envelope, Var, Range, then Oscillators, Noise, Wavetable, Additive,
  a line, Physical, Filters, Envelopes, Modulators, Combiners, Utility, then
  Experimental with the same families inside; every family is one flat list
  (the dividers that used to sit inside Generators, Additive and the rest
  are gone); create a Shaper (lavender now, was teal) and a Vibrato (pale
  gold, was teal); load a patch with a SegmentSource and untick "Envelopes"
  in the waveform header — its strip hides with the envelopes; play
  `patches/library/winds/oboe1.json` on the keyboard — nothing you can hear
  changed (the A/B is 0 moved over 132); `python tools/structure/check.py`
  lists fourteen include edges outside the map, the new one
  `music/pitch_bend.h` → `source/envelope/envelope.h`.
- A verdict decides: (0) **the A/B**: 0 of 132 moved — accept the
  checkpoint as byte-identical; (1) **the colours**: seven types changed
  colour because colour follows the family now (HybridKS teal→tan,
  PierceFilter lavender→teal, Distortion and Shaper teal→lavender, Vibrato
  teal→pale gold, Segment reddish-orange→pale gold, Wormhole teal→grey) —
  accept, or name one that belongs in another family (one word on its
  registration moves it, colour, menu and folder together); (2) **the
  menu**: generated from the registry — the families read as Part 3
  section 3 says ("Generators" is Oscillators, "Loop" is Physical), each
  family is one list without the ten dividers the hand-written menus drew
  inside families, Experimental mirrors the families and lists eight types
  no menu had, the Curve/Envelope/Var/Range quick block stays — accept, or
  want the dividers back (a grouping fact on the registration, not added
  unasked); (3) **the meter's +1**: `include_violations` 13 → 14 where the
  plan said +0 — the new edge is `music/pitch_bend.h` compiling a PitchCurve
  into an Envelope, which existed before and was hidden by the envelope
  header's old address in `core/`; the map was not widened (the target says
  music depends on core only) and the function was not moved early (the
  target's 3.7 moves it to render when `pitch_bend.h` splits) — accept the
  reading, with the edge carried visibly until Part 5, or want
  `compile_pitch_curve` moved to render now; (4) **the `KSPianoString`
  alias** is creatable from Experimental > Physical until 3.5 deletes it
  (your "throw them all under experimental" swept it in; the refuter notes
  an alias is not a type) — accept, 3.5 migrating any patch saved under it,
  or want it hidden (a third shelf value, "registered, not offered"); (5)
  **the Vibrato and Segment strips** draw in the envelope trace's yellow
  and hide with the "Envelopes" toggle, being the Envelope family's (Segment
  drew green as audio before) — accept, or want Segment drawn as audio,
  which is a family exception the registry would have to carry; (6) **the
  timing** — two runs of the
  render-time A/B, neither on a quiet machine (the base arm's own walls
  ran 9 to 25 percent above 3.1's clean run, higher on the second run
  with nothing of this session's running; rows swing 0.87x to 1.10x); the
  mechanism argument stands (no file on a render path changed except its
  include lines; the refuter traced it) — accept it for a checkpoint that
  touched no render path, or want a rerun on a quiet machine before Part 3
  continues.

**R8 — checkpoint 3.1: `Component` root; the parts leave `ValueSource`**
(2026-10-30, commits `ba097f6`..`f1f8244`) — VERDICT 2026-11-02 (standing rule): (0) the malformed-wire refusal stands as 2.5's "a rejected wire says so" applied to the two kinds (no patch in scope carries one); (1) accepted, byte-identical; (2) the undocumented `"formants"` key stays unread (only `patches/old/` carries it, and the loader has refused it by name since the wave); (3) the part strips' empty capture stands for Part 7 (CLEANUP); (4) `Component::on_slot_changed` stands as the third hook — one owner today; a second owner that keeps a view into its part reuses it, and if the hook then wants a payload that is Part 7's design; (5) the unlink fix stands, pinned by its test; the manual try is not required; (6) the editor's hand lists are Part 7's (3.1 report section 7); (7) the timing is accepted on the mechanism.
- Read: `docs/autonomy/refactor/reports/2026-10-30-checkpoint-3.1.md`
  (its reading guide is ten minutes; section 1 names the one behaviour
  change — a malformed wire now fails the load — and the hook the plan did
  not foresee, section 5 the gates, the A/B and the timing, section 6 the
  decisions taken on your behalf — rulings 27, 25, 19 and 29 departed from
  the plan or corrected it — section 7 the debt, section 8 the refutation,
  section 10 four C++ lessons).
- Try: copy any library additive patch, point its AdditiveSource's
  `frequency` at its FullPartials node (`"frequency": {"ref": "<parts id>"}`)
  and load it — the load fails naming the node, the pin and "a part, not a
  signal", where it used to render a silent zero; wire a constant into a
  WavetableSource's `evolution` in the editor — the slot refuses it; play
  `patches/library/winds/oboe1.json` and any formant patch on the keyboard —
  nothing you can hear changed (the A/B is 0 moved over 132).
- A verdict decides: (0) **the behaviour change on malformed wires** (a
  signal into a slot, a part into a signal pin, a part as output or channel
  source now fail the load with the reason; no patch in library or baselines
  carries one) — accept as 2.5's "a rejected wire says so" applied to the two
  kinds; (1) **the A/B**: 0 of 132 moved — accept the checkpoint as
  byte-identical; (2) **`FormantSequence`'s undocumented `"formants"` key**
  (read beside the descriptor's `"spectra"` by the deleted branch) is no
  longer read: a `patches/old/` sequence carrying it loads without its
  spectra — accept, or have the loader refuse the key by name; (3) **the
  editor's part strips** capture nothing where they drew a flat zero line
  (a part has no signal); the Partials and Formant spectra strips are
  unaffected — accept for Part 7, or want the zero line back now; (4)
  **`Component::on_slot_changed`** as the third hook beside
  `on_setting_changed` and `on_array_changed` (section 6 ruling 27): the
  design, or a different answer to an owner that keeps a view into its part.
Two more from the refutation (section 8): (5) **the editor's unlink** —
  before the wave, unlinking a holder from a WavetableSource (or a formant
  from an AdditiveSource) left the part in place until reload, a regression
  the roundtrip gate cannot see; the rewire pass now clears every pin first.
  Try it: wire a PluckEvolution into a WavetableSource, play, delete the
  link, play — the pluck must be gone; (6) **the editor's part-type lists**
  (`pin_type_compat_error`, the strips' two predicates) are three hand
  lists beside the registry, and a link the engine refuses is drawn and
  inert — left for Part 7 (the editor asking the door), or wanted sooner.
  And one from the close: (7) **the timing** — three runs of the
  render-time A/B, none on a quiet machine (the base arm's own walls
  spread 35 percent between runs of one binary; rows swing 0.83x to
  1.31x); the mechanism argument stands (no per-sample path changed, the
  refuter traced it) — accept it for a checkpoint that touched no render
  path, or want a rerun on a quiet machine before Part 3 continues.

**R6 — checkpoint 2.5: no throw on a render path; fault flags; a rejected
wire says so** (2026-10-26, commits `cb1b4ab`..`1481d15`) — VERDICT 2026-11-02 (standing rule): (0) the five patches that stop loading stay as listed (`sweep/` is regenerable, `old/` is never deleted; both outside the measurement scope); (1) the wrap list's renders are the frozen truth (the two `double_advance` baselines are regression fixtures, refrozen; the rest are queue and sweep patches outside the scope); (2) the fault reports' shape stands; a fault count on the UI stamp is a CLEANUP suggestion; (3) `prepare()` throwing on a zero rate stands as the host contract; (4) the narrower sentence is accepted into Part 2 section 6 (edited 2026-11-02): the clamp applies to a non-positive frequency only.
- Read: `docs/autonomy/refactor/reports/2026-10-20-checkpoint-2.5.md`
  (its reading guide is ten minutes; section 1 names the three latent host
  bugs the fault bits found on the way in, section 5 the patches that stop
  loading and the twenty-five renders the DelayLine wrap moved, section 6
  the decisions taken on your behalf — rulings 17, 20 and 25 are the ones
  that departed from the plan or corrected the ledger — section 7 the debt,
  section 8 the refutation, section 10 three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78; the two
  `double_advance` baselines were refrozen for the wrap and nothing else
  moved); render `renders/scratch/ab25_tree/zero_freq.json` with the CLI —
  it used to terminate the process from the audio thread, now it renders
  and prints `[faults] osc (SineSource): frequency clamped` after the
  stats; copy any library patch, misspell one key on one node, and load it
  — the load fails naming the node, the pin and the reason instead of
  rendering wrong in silence.
- A verdict decides: (0) **the patches that stop loading** (section 5):
  four `sweep/stk_bandedwg_port/*_struck` and `old/Squeaker.json`, each a
  stale key on a hand-built or legacy node, left as they are and listed
  (`sweep/` is regenerable, `old/` is never deleted) — accepted, or any to
  be repaired by hand; (1) **the wrap list's renders as the frozen truth**
  (`double_advance_bug` and `double_advance_ok` refrozen; `Trombone_att1`,
  the fourteen `bwg_perc1`/`bwg_perc2` queue patches and eight sweeps
  changed — re-listen any before accepting); (2) **the fault reports'
  shape**: the CLI's `[faults]` block after the stats and the UI's
  transport status line — enough, or a fault count on the stamp too; (3)
  **`prepare()` throwing on a zero rate** as the host contract (the host
  that forgets the rate fails loudly at build, never on the audio thread);
  (4) **ruling 2's narrowing of the signed text** (report section 6; the
  refuter's I11): the clamp applies to a non-positive frequency only, a
  positive frequency below the pin's minimum is left as it was, so no
  non-terminating render moved — accept the narrower sentence into Part 2
  §6, or schedule the wider clamp as a behaviour checkpoint with its own
  measured list.

**R7 — checkpoint 2.6: the allocation test over every baseline patch**
(2026-10-30, commits `6a37849`..`d3df3ea`) — VERDICT 2026-10-30: (0) the gate stays in `gates.py`'s slow list; a `--jobs` split across processes is a plan 3.1 tooling task (Part 3 touches every node, so the gate runs per task there); (1) accepted — byte-identical over the sample; (2) the queues were measured once after the fixes (752 patches, 0 allocate, 3 fail to load: the GrayScott three) and are not measured again — library + baselines is the standard for every gate, A/B and measurement from here (Matt: too much time on patches that will likely never be used; a promising old patch that breaks is diagnosed when he loads it); (3) the 20 Hz floor stays; (4) a structural setting takes no per-note push: the mark is on the pin and its descriptor (`SettingRole::Structural`; every int and bool by type, as pin_model_design §6 already said; the Partials shaping floats declared), the loader refuses a paramMap or dynamicPins wire into one naming the node, the setting and the reason, the UI offers no pin for one, and the two Partials handlers read the mark instead of a name list — landed `6007b29`.
- Read: `docs/autonomy/refactor/reports/2026-10-30-checkpoint-2.6.md`
  (its reading guide is ten minutes; section 1 names the one live-path fix
  and the three offline-host allocations left to Part 5, section 5 the gate's
  numbers — 97 allocating patches to 0 — and the A/B, section 6 the decisions
  taken on your behalf — rulings 12, 15 and 18 departed from the plan —
  section 7 the debt, section 8 the refutation, section 10 four C++ lessons).
- Try: `build/tools/alloc_gate/Release/alloc_gate.exe patches/library patches/baselines`
  (132 patches: 0 allocate; about four minutes); point it at any queue patch,
  `alloc_gate.exe --verbose patches/audition/<family>/<patch>.json`, for its
  count and the first allocation's call stack with file and line; play
  `patches/baselines/double_advance_ok.json` and `feedback/loop_tap_starved.json`
  on the keyboard — a live note-on on a loop patch no longer copies the
  voice's tail list (nothing you can hear changed: the A/B is 0 moved).
- A verdict decides: (0) **the gate's place**: `gates.py`'s slow list, where
  its 78 to 230 seconds put it (78 at the seal with nothing to symbolize), or a ctest after a `--jobs` split across processes
  (not done here); (1) **the A/B**: no render moved over the 1,615-patch
  sample — accept the checkpoint as byte-identical; (2) **the queue folders**:
  398 of 749 audition and pending patches allocated on the first arming, in
  the same families, and are not re-measured after the fixes — left as
  information, or a run over them worth doing now; (3) **the floor,
  `WaveSource::kMinTableHz` = 20 Hz**, the length every frequency-dependent
  table (and BasicAdditive's partial count) is reserved for: the survey found
  220 Hz the lowest constant in use and A0 (27.5 Hz) the lowest key, and a
  note below the floor gets the floor's length — keep, lower it, or make it a
  setting; (4) **a per-note push into a Partials shaping setting** (`evenWeight`,
  `minMult`, `incr`, `unitPO` through a paramMap or a dynamic pin): the array
  rebuild moved from `start` to `prepare`, so such a push is now ignored until
  the next prepare, where the old `start` honoured it (within capacity for a
  value-only change). No patch outside `old/` carries one. Honour it at `start`
  within capacity (size for the setting's declared maximum), or have the loader
  refuse a push binding into a shaping setting (2.5's "a rejected wire says so").

**R5 — checkpoint 2.4: taps reading B** (2026-10-19, commits
`a8d5aa9`..`403269f`) — VERDICT 2026-10-26: (0) the forty changed renders
are the frozen truth (Matt: the renders pointed out sound great, the
trombone improved); (1) `previous()` and `claimed_tick()` stay public on
every node until Part 3's pins narrow the door; (2) `feedback_loop_design.md`
section 2's "positional" paragraph stays as the 08-31 design with the
header delta.
- Read: `docs/autonomy/refactor/reports/2026-10-19-checkpoint-2.4.md`
  (its reading guide is ten minutes; section 5 holds the forty renders that
  changed by family and the render-time table, section 6 the decisions taken
  on your behalf — ruling 18 is the one that departed from the plan —
  section 7 the debt, section 8 the refutation, section 10 three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3` (78/78 — nothing in
  library or baselines moved); open `patches/audition/trombone1/trombone_attempt4.json`
  and play — the nonlinear bore's ratio modulation now follows the previous
  sample (the loop period is unchanged); open
  `patches/baselines/feedback/loop_hysteresis_bow.json` — unchanged, as every
  loop closed through a tap on its forward path is.
- A verdict decides: (0) **the forty changed renders** (section 5, Appendix
  A) accepted as the frozen truth — they are your listening queues and sweeps
  (`audition/trombone1` ×4, `audition/pierce1d_1` ×6, `pending/brass/Trombone_att1`,
  the `sweep` trombone, onemass, nlbore and stk_blowhole families,
  `old/nlbore_brass1` ×5), nothing in library or baselines, so no manifest was
  refrozen; re-listen any before accepting; (1) **`previous()` and
  `claimed_tick()` on every node** for the tap edge alone (section 7): leave
  them until Part 3's pins narrow the door, or narrow it now; (2)
  **`feedback_loop_design.md` section 2's "positional" paragraph**: left as the
  08-31 design with the header delta (Task 2's choice), or rewritten.

**R4 — checkpoint 2.3: the per-tick memo** (2026-10-18, commits
`22bf3be`..`fdf7536`) — VERDICT 2026-10-19: (0) the render-time cost is
accepted as the price of one `next()` for every node; Part 3's node pass
is not asked to recover it; (1) the changed renders are the frozen
truth; (2) the `loop_root_sensitivity` pair is retired — moved to
`patches/old/baselines_retired_2026-10-19/` (never deleted), its two
manifest entries dropped (the null gate is 78 entries), dsp backlog 65
closed to that file's ledger; (3) `step()` stays for now, with a
post-refactor revisit listed (refactor BACKLOG row 9a); (4) ShaperSource's
Auto pins stand for now, with a post-refactor revisit listed (row 9b).
- Read: `docs/autonomy/refactor/reports/2026-10-18-checkpoint-2.3.md`
  (its reading guide is ten minutes; section 5 holds the renders that
  changed and the render-time tables, section 6 the decisions taken on
  your behalf, section 7 the debt, section 8 the refutation, section 10
  three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3`; open the UI,
  wire one envelope to two nodes' pins and play — both read it, in whatever
  order you wired them; play `patches/baselines/feedback/loop_root_sensitivity_fires.json`
  and its `_dead` sibling: they are the same sound now.
- A verdict decides: (0) **render time** (section 5): the checkpoint itself
  is neutral against the tree before it, and the sweep experiment bought
  8 to 16 points where pins are wired, but the 2.1 cost stands — oboe1 at
  0.73x, piano_default at 0.88x of the pre-2.1 binary. The rest is the
  generic per-node `next()` (a call layer, a loop and a store per node
  where the old bodies loaded their pins at fixed offsets). Accept it as
  the price of one `next()` for every node, or let the hot leaf types pull
  their own pins inside `compute()`, decided per family in Part 3's node
  pass; (1) **the renders that changed** (section 5, Appendix A): the two
  `double_advance` baselines, `loop_root_sensitivity_fires`, and 106
  gitignored audition/sweep patches that share a stateful node — accepted
  as the new frozen truth, or any you want re-listened first; (2)
  **`loop_root_sensitivity_fires` / `_dead`**: the pair no longer shows
  what it was written for (the "root sensitivity" was the double advance;
  both are dead now) — keep them as the memo's regression pair, rename,
  or retire; (3) **`step()`** as a permanent verb on `ValueSource` (one
  sample now, whatever the tick; two callers, the table fills), or a
  Part 3 item that gives those two nodes a private excitation source
  instead; (4) **ShaperSource's `breakaway` and `capture`** are pulled
  every sample with hysteresis off (Auto pins), where before they were
  pulled only in hysteresis mode — stands (a threshold modulator switched
  on mid-note has been running since the note began), or Manual again.

**R3 — checkpoint 2.2: prepare(ctx) / start(frames)** (2026-10-18, commits
`7e20e5f`..`5980873`) — VERDICT 2026-10-18: (0) the DelayLine zero slot
stays listed for 2.5; (1) the UI preparing its editor graph at every
play is fine; (2) the report's section-7 list is the work list for
checkpoint 2.6's allocation test — every listed allocation moves into
prepare within 2.6, the test lands with no tolerance list; (3)
`render_context()` and `prepared()` become protected — a host that makes
a node after prepare hands it the context it prepared the owner with
(`ensure_constant`'s three-argument form), the node is not asked. Items
2 and 3 are my recommendations, taken on Matt's "take your
recommendations and proceed"; item 3 landed as a fix on top of 2.2.
- Read: `docs/autonomy/refactor/reports/2026-10-17-checkpoint-2.2.md`
  (its reading guide is ten minutes; section 6 holds the decisions taken
  on your behalf, section 7 the debt, section 8 the refutation and the
  seven per-task reviews, section 10 three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3`; open the UI,
  play a key, Generate — every node is now prepared once by the loader
  and only started per note; a patch saved from the UI records the rate
  the audio stream actually opened with.
- A verdict decides: (0) **DelayLineSource's stray read** (section 7,
  first item): a pre-existing one-past-the-end read is kept byte-identical
  through an extra always-zero ring slot; the correct fix (wrap to slot 0)
  changes the bytes of at least one baseline — leave it listed for
  checkpoint 2.5, or take the render change now with that baseline
  refrozen; (1) whether the UI preparing its editor graph again at every
  play (not per note; idempotent) is the behaviour you want, or whether a
  node should be prepared once when it is created; (2) the list of
  `start()` bodies that still allocate (section 7) as the starting list
  for checkpoint 2.6's test; (3) `render_context()` and `prepared()` as
  public read-only getters on every node (section 6, item 13).

**R2 — checkpoint 2.1: pins declared once** (2026-10-17, commits
`1b9b69f`..`5f9aeea` plus the close)
- Read: `docs/autonomy/refactor/reports/2026-10-16-checkpoint-2.1.md`
  (its reading guide is ten minutes; section 6 holds the decisions
  taken on your behalf, section 7 the debt, section 10 the lessons).
- Try: open any patch in the UI — the Properties panel is served from the
  pin registry now; `python tools/null_gate_perform_source.py --jobs 3`.
- A verdict decides: (0) **the render-time cost** — oboe1 renders about
  30-40% slower than before the checkpoint, piano 25-45%, viola 2-9%
  (report section 5: measured, bisected, experimented; a few nanoseconds
  per node per sample that the hand-written next() bodies did not pay).
  Carry it to checkpoint 2.3, where the per-sample path is rewritten
  anyway, or stop and recover it first; (1) whether the two tolerated
  byte-identity classes (section 6, items 6 and 7 — wiring that no stored
  patch has, retired by the memo in 2.3) are acceptable as named, or must
  be closed by Manual pins now; (2) whether the eight stored patches that
  now state the RedNoise defaults they relied on should keep those keys
  (they do today); (3) the descriptor-order changes the UI shows
  (WaveSource's amplitude first; the presets' and Partials subclasses'
  inherited settings first).

**R1 — checkpoint 0: the structure meter** (2026-10-05, commit `637207a`) — VERDICT 2026-10-16: format accepted; R2 (checkpoint 2.1) uses it unchanged.
- Read: `docs/autonomy/refactor/reports/2026-10-05-checkpoint0-meter.md`
  (the first architecture report in the agreed format; ten minutes via
  its reading guide).
- Try: `python tools/structure/check.py` from the repo root, and
  `python tools/gates.py --fast`.
- A verdict decides: whether the report format gives you what you asked
  for (types and files with responsibility, users, dependencies, the
  design choice and its alternative, the name, lines to read; generated
  metrics; decisions; debt; the independent review; a reading guide; a
  C++ lesson), before checkpoint 2.1 produces the next one.


# The editor, slice 2: build (report R28)

2026-11-23, refactor session XXVI. The page: `docs/architecture/editor-slice2.md`,
signed that day ("Signed, proceed"). What it restores is row 2 of
`docs/architecture/editor-parity.md`. Base `bd4b20d`; commits `dcba0b3`
(the edits), `d7a06de` (history and the control), `9ac1e59` (the views),
`94a7c60` (the keyboard's missing parts), fix wave `f4b4212`, parity wave
`34febff`. Fable 5.2
wrote the page and the briefs, read each report and diff, looked at the
pictures and committed; one Opus 5.5 executor built every step and both
waves; a second Opus 5.5 agent reviewed the whole range and then checked
the parity list line by line.

## 1. What is on your desk

From the repository root:

```
build\editor\ui_app\Release\mforce_editor.exe
```

With no file it now starts on a new patch (a Sine, an Envelope and the
Output node, unwired). REVIEW R28 has the list of what to try. What slice 2
added to slice 1:

- **Build a patch from nothing.** File > New. Right-click the canvas for
  the create menu (Note; Curve, Envelope, Var, Range; then the registry's
  families and the Experimental shelf). Drag from an output to a pin to
  wire, from a node's small teal pin for a tap, onto the Output node to
  make it the patch's output. Drag a wire off its pin, or select it and
  press Delete, to remove it; Delete or a right click on a node removes
  the node.
- **Notes as one node.** A Note shows the six fields of the note as pins.
  When a file is opened its loose note nodes are gathered into Notes, as
  the old loader did (87 of the 131 measured patches had some).
- **A setting driven per note**: the small mark beside a setting in the
  panel shows its pin on the node; wire a signal to it.
- **Rename** a node or a Note in the panel. **The instrument's settings**
  (polyphony, sustaining, onsets, glide) in the panel when nothing is
  selected.
- **Undo and redo**, Ctrl+Z and Ctrl+Y, 200 steps; a drag is one step;
  undoing back to the saved state clears the title's star.
- **Find your way**: the wheel pans; Shift with an arrow pans; F or the
  Fit button centres the graph; the status line counts nodes and wires.
- **The keyboard's missing parts**, found by checking slice 1 against the
  old editor: the PC Keyboard button, a piano as wide as the window with
  note names and the computer keys on it, velocity from where you click a
  key, the voice count and a green, yellow and red level.
- **The old looks**: a node's title in its family's colour, taps teal,
  per-note pins gold, the old wire width.

What you hear is the patch on the screen or nothing: a patch the engine
cannot build (a new one has no output yet) is silent and the status line
says why.

## 2. The shape

Four ideas carry the slice. Each is small enough to read in one sitting.

**An edit refuses exactly what `check()` refuses**
(`engine/patch/src/edit_support.cpp`, `adopt`). Slice 1's one Major was a
refusal I wrote that the reader did not have. So every structural edit is
made the same way: find what it names (its own sentence when the thing is
not there), apply the change to a copy of the patch, ask `check()` of the
copy, and either throw `check()`'s sentence or let the copy become the
patch. No rule of `check()` is written a second time, so the edits cannot
drift from the reader. The two exceptions are named in the header: a
number that is not finite, and a note field that is not one of the six.

**Undo is a stack of copies** (`editor/ui_model/history.h`). The patch is
a plain value, so "the patch before" is `Patch before = patch;`. "Unsaved"
is no longer a flag someone must remember to set: it is "the patch is not
the one in its file" (`savedAt`), which is why undoing back to the saved
state clears the star.

**One way to change the patch, one way for the sound to follow**
(`editor/ui_control/src/stepping.h`, `playing.h`). Every control function
goes through `step`: one undo step, a drag is one step, several engine
edits inside one step are all or nothing. After any change the build reads,
`sound_follows` gives a new player or silence with the reason.

**The canvas draws from a table** (`editor/ui_views/canvas_table.h`). Each
frame a plain function turns the patch into what the canvas shows: the
nodes (a Note as one node, its hidden nodes left out), each pin as the
`PinEnd` the control understands, each wire as its reader and feed number.
The drawing code and the gesture code both read the table, and the table
has tests that need no window: over all 131 patches every wire is exactly
one link, and each link's ends decode back to the record's own entry.

Sizes: the editor is 4,999 lines of code and 2,614 of tests in 5 modules;
its largest code file is 225 lines (`placement.cpp`). The edits added 612
lines of code and 867 of tests to `engine/patch`, and nothing else in the
engine changed.

## 3. The steps

| Step | What | Commit |
|---|---|---|
| 1 | Slice 1's 23 unvouched lines of the parity list checked against the code (read only) | `bca5db9` (the list) |
| 2 | The rest of the edits in `patch/edit.h`, with the proofs | `dcba0b3` |
| 3 | History, note faces gathered at open, placement that knows faces, the control | `d7a06de` |
| 4 | The views: canvas table, gestures, create menu, panel, menus, status line | `9ac1e59` |
| 4b | The keyboard's missing parts and a node's Delete menu | `94a7c60` |
| 5 | Review, fix wave | `f4b4212` |
| 6 | The parity list's slice 2 lines checked one by one; the parity wave | `34febff` |

## 4. What was proven, and what no gate proves

Proven, by tests that run in the gates:

- **The edits can make everything the library holds.** Each of the 131
  measured patches is made again from nothing through the edits alone and
  equals the patch as read: 1,033 nodes added, 975 feeds connected, 0
  refusals, 0 unequal. Three things a file may lack that an edit never
  leaves out are removed by one named helper (863 seeds, 721 positions, 1
  starting table), and two things no edit of this slice makes are copied
  (a node's comment, the groups).
- **Removing a node never breaks a patch.** Each of the 1,033 nodes
  removed from its patch in turn leaves `check()` empty, and the result
  survives a write and a read.
- **Rename and unwire are exact.** 1,033 renames to a fresh id and back,
  and 901 feeds unwired and wired again, each give the patch as read.
- **Gathering Notes at open** leaves every note node named by exactly one
  field of one Note and changes nothing else (131 patches, 87 gathered in,
  88 Notes made); placement after it leaves no two things overlapping in
  a column.
- **Undo**: by hand, and over four patches (20 wires taken away and
  undone: the patch equal to the opened one, no star).
- **What the canvas shows decodes back**: 131 patches, 1,126 links, 0
  faults. Drawing changes nothing: 131 patches and a new one, 1,266 draws.
- **The engine did not move**: the null gate, the allocation gate and the
  comp gate pass, and `mforce_cli.exe` has one hash from step 2 on. At the
  close `python tools/gates.py` prints ALL GATES PASS, 15,449 checks.
- **The parity list's lines, one by one.** After the build the reviewer
  checked each of the 58 lines the list gives this slice against the
  code, strictly (a control function no view calls is not done): 48
  done, 9 in part, 1 not done. Six were closed the same day (the parity
  wave); three parts moved to the slice that owns what they need (a new
  node joining the open group and the Wormhole's missing tap pin to
  slice 5, the other table types' starting rows to slice 6); one is a
  look, decided (decision 11). The list's A6 has it. Slice 1's lines had
  been marked done from a report, and the same kind of check at this
  slice's start found 17 of them wrong; the check now closes every slice
  before the list says done.

Almost nothing a hand does at the window is under a gate. The fix wave
made a headless editor a test can click and press keys in, and two
gestures now run under it (Delete after the wires change; F). Every other
gesture below is one imnodes call decoded through the tested table into
one tested control call, and none has been made with a mouse: a wire made; a wire dragged
off; Delete; the create menu and where its node lands; a node's right-click
menu; the wheel; F; Shift with an arrow; a click's velocity; Ctrl+Z while
a text field has the keyboard. REVIEW R28 lists them for you.

## 5. The meter, against the base

`python tools/structure/check.py --delta bd4b20d`:

| Measure | Before | After |
|---|---|---|
| files | 514 | 561 |
| lines | 64,313 | 69,410 |
| files over the limit | 8 | 8 |
| functions over the limit | 50 | 50 |
| file-level mutable variables | 22 | 22 |
| duplicate blocks (reported, not gated) | 168 | 169 |
| include violations | 0 | 0 |

Nothing under `editor/` is in the baseline. `node_canvas.cpp` went from
202 lines to 110 and `properties_panel.cpp` from 206 to 144 while both
gained most of their function, because each was split by job before it
grew.

## 6. Decisions the session made

Where the code differs from the signed page, and what was decided inside
it. Each is reversible.

1. **`set_instrument` is not refused on a plain sound** (the page's
   comment said it was). No reader refuses an instrument block on any
   patch, and the page's own rule is that an edit refuses nothing a reader
   accepts. The window still offers no way to turn a plain sound into an
   instrument: `edit_instrument` refuses it there.
2. **The `dirty` flag is gone** (the page's snippet kept it beside the
   history). One fact kept twice drifts; `unsaved(open)` is the one
   question.
3. **Two functions the page did not list**: `fresh_face_label` ("Note1",
   "Note2"), and `fit_canvas` takes the canvas's size so Fit centres the
   graph as the old one did.
4. **`fresh_id` follows the old editor's code, not my summary of it**: it
   also gives "Note", "Channel" and "Mixer".
5. **A failed rebuild is silence, everywhere.** In slice 1 a setting edit
   the engine could not build left the old player sounding. Now the sound
   is the patch on the screen or nothing.
6. **A save ends the open drag** (found by three tests): otherwise an edit
   later in the same drag would not show as unsaved.
7. **Gathering**: new Notes are "Note1", "Note2" (the old loader's were
   "Note", "Note2"); a gathered node's saved position goes, because a
   Note's node is not on the canvas.
8. **The counts** are what the canvas draws: nodes without the Output
   node, wires with per-note pins and the output. The old count had the
   Output node in and per-note pins out.
9. **The create menu lists each type once**: the four favourites are not
   repeated in their families, as the old menu repeated them.
10. **Shift with an arrow pans 20 pixels a frame** (the old Shift speed);
    the old unshifted 5-pixel pan is gone with ruling 10.
11. **Looks**: title text dark, not white (white did not read on the pale
    gold and teal titles); a pin's number as `%.3g`; the octave stays 0 to
    8 and the minimap 20% (the old 0 to 20 and 15% not chased); a
    setting's field is 45% of the panel's width so its name fits; the
    panel's header shows the registered type's name ("SineSource"), not
    the old short one; a piano key two zones share shows its two computer
    keys stacked, where side by side they ran under the black keys.
12. **A Note's node that a file brings in unread is dropped when the file
    is opened**, like placement not an unsaved change (the old save
    dropped it; ruling 8). No measured patch has one.
13. **The open and the save dialog share one remembered folder** (the old
    editor kept two), in `%APPDATA%\MForce\folders.txt`.
14. **From step 1's check** (the parity list's A5): ten small things of
    slice 1's area mended here; Stop of live notes, the pre-clip level and
    showing a note that still sounds after its key is up go to slice 3,
    because the live player must report more than it does.

## 7. Debt, booked in CLEANUP

Rows 164 to 174: a `check()` line is cut at its first ": " to get its
sentence; `check()` accepts two records the build refuses (a per-note pin
inside a Multiplex template; an instrument with no output); a tap from a
Note's node is drawn from the Note's field pin and would lose its tap if
rewired there; nothing refuses a polyphony of 0 or of a million; a group
left with no member stays (slice 5 owns groups); `offer_setting_pin` asks
its question by trying a feed from a probe node on a copy; during the 5 ms
fade between two players their clipped outputs are summed unclipped;
`check()` does not require a Note's field to name a note node; no edit
removes a column of a table; the looks decided here and not chased to the
old editor's; a click on the canvas while a text field has the keyboard
applies the field and does not select.

## 8. The review

An Opus 5.5 agent that had seen neither the briefs' reasoning nor the
executor's reports read the range `bd4b20d..94a7c60` against eight claims,
built it, ran the gates and wrote a probe program.

**One Major, mended.** The canvas kept imnodes' wire selection across a
change to the patch, and imnodes keeps a selection as slots of its own
pool, not as wires. So: select a wire, delete a node from its menu (or
undo, or wire something), make a new wire, press Delete, and the new wire
went, though nobody had chosen it. The reviewer showed it with a headless
probe. The canvas now clears the wire selection whenever the wires it
draws differ from the last frame's, which covers every path, and the
probe's sequence is a test.

**One Minor, taken.** The rebuild proof's own comment said "through the
edits alone" and the test does a little more than that: it removes three
things a new node always has and a file may lack, copies comments and
groups, and one shape in the measured set cannot be made by the edits at
all (a Curve with no knots, because no edit removes a column of a table).
The comment now says exactly that, and section 4 above does. Removing a
column is for the slice that edits tables.

**Notes, two mended**: an undo of a move rebuilt the player and cut the
sounding notes (now only an undo that changes what the build reads does);
a setting's pin vanished when the node driving it was deleted, where the
old editor left it shown (now it stays on offer). **Two recorded**:
`set_instrument` on a plain sound (decision 1); `check()` does not require
a Note's field to name a note node (CLEANUP 171).

Found sound: every structural edit's refusals are `check()`'s or a "names
nothing" sentence; the cascade that removes a Note's unread node; renames
through every place a name sits; `savedAt` through undo past the saved
state, a new edit after it, the 200 cap and a save in the middle of a redo
chain; no view writes to the session or the patch except through
`ui_control`, and every `ui_control` function has a test; the engine
unchanged apart from the new edits. Not checked by the reviewer: sound at
a real device, the real window, the keyboard view, memory at 200 steps.

## 9. Lessons

### 9.1 Apply to a copy, check, then swap

The idiom behind every structural edit:

```cpp
Change connect(Patch& p, ...) {
  Patch copy = p;                 // a full, independent copy
  ...change `copy`...
  return adopt(p, copy, Change::Structure);
}

Change adopt(Patch& p, Patch& copy, Change kind) {
  const std::vector<std::string> lines = copy.check();
  if (!lines.empty()) throw std::runtime_error(check_sentence(lines.front()));
  p = std::move(copy);            // the copy becomes the patch
  return kind;
}
```

C++ people call the promise this keeps the *strong guarantee*: the call
either does all of its work or leaves everything as it was. It is the same
shape as a database transaction (work on the side, commit at the end), and
it is cheap to write in C++ for one reason: `Patch copy = p;` copies the
whole structure, vectors and maps included, because a struct of values is
a value. In C# `var copy = p;` copies a reference, so the same design
needs a deep clone or immutable records with `with`. `std::move(copy)` on
the last line hands the copy's storage to `p` without copying it again:
the C# analogue is assigning the reference, which is why a C# reader can
read `p = std::move(copy)` as "p is now that object".

The alternative is to validate first and mutate after. It is faster, and
it is how slice 1's six edits work, because they run sixty times a second
in a drag and test one thing. For the structural edits it would mean
writing each of `check()`'s rules a second time in the edit, which is
exactly the drift slice 1's review found.

The name `adopt`: the patch adopts the copy. `commit` is the database
word for it, and it already means something else in this repository every
day.

### 9.2 A function that takes a function

`step` runs any edit as one undo step:

```cpp
template <class Edit>
std::optional<Change> step(Session& s, const std::string& gesture, Edit edit);

step(s, "", [&](Patch& p) { return connect(p, node, pin, source, tap); });
```

The `[&](Patch& p) { ... }` is a lambda, the same thing as a C# lambda
`p => Connect(p, ...)`; `[&]` says it may use the caller's local variables
by reference (C# lambdas always can). `template <class Edit>` is how C++
says "any callable": the compiler writes a separate `step` for each lambda
it is called with, so the call costs nothing at run time and allocates
nothing. C# would declare the parameter `Func<Patch, Change>`, one
delegate type, resolved at run time. C++ has that too, `std::function<Change(Patch&)>`
(the table rule in `engine/patch/src/table_rule.cpp` takes one, because it
is declared in a header and defined in a `.cpp`, which a template cannot
be without more ceremony); and for the plainest case a bare function
pointer, which the
starting-shapes table in `edit_nodes.cpp` uses (`Columns (*columns)(const TypeDeclarations&)`,
the C# `Func<TypeDeclarations, Columns>` with no captured state).

`std::optional<Change>` is `Change?`: the step either happened, and here
is what kind it was, or nothing changed.

### 9.3 Why a table between the patch and the canvas

imnodes names everything by integer and reports gestures as integers ("a
link was made from pin 2051 to pin 4097"). The easy way to write a canvas
does that arithmetic inline in the drawing code and then again in the code
that handles a gesture, and nothing but care keeps the two the same (slice
1's canvas, which only drew, did it inline). Here both read one table that
a plain function makes from the patch, so "what does pin 4097 mean" has
one answer and a test.
C# and WPF people will recognise a view model: data shaped for the view,
made from the model, with no drawing in it. The difference from MVVM is
that nothing is bound or kept: the table is made afresh every frame and
thrown away, because the patch record is the one truth and a frame is
cheap.

# The editor, slice 5: arrange (report R34)

2026-11-24, refactor session XXVII. The page: `docs/architecture/editor-slice5.md`,
signed 2026-11-24 ("Go ahead."); its section 6 holds the thirty-five
rulings made while building. What it restores is row 5 of
`docs/architecture/editor-parity.md`. Base `7ef402b1`; commits `ba975cae`
(the record), `550b3216` (the control), `0bf3a0c4` (the views), `6a3bdab5`
(MIDI in and the build stamp), fix wave `843992d8`. Fable 5.2 wrote the
page and the briefs, ruled on what the first step found, read each report,
looked at the pictures and committed; two Opus 5.5 executors built it (the
first through the views, the second MIDI, the stamp and the fix wave); a
third Opus 5.5 agent, which had read the old editor for the page, reviewed
the whole range and then checked the parity list line by line.

## 1. What is on your desk

From the repository root:

```
build\editor\ui_app\Release\mforce_editor.exe patches\library\winds\oboe1.json
```

REVIEW R34 has the list of what to try. What slice 5 added:

- **Several selected**: a box, Ctrl and a click on a node or on one of
  its pins; a drag moves them all; Delete takes them all.
- **Groups**: a group is one purple node with a pin for each wire that
  crosses its edge. Double-click to go in; the breadcrumb goes back up.
  Group selection, Add to group, Remove from, Ungroup, a name changed in
  the panel.
- **Wormholes**: a right click on a wire runs it through a pair; the
  ghost span to the twin while one is hovered or selected; a double-click
  jumps to the twin.
- **Copy, cut and paste** through the system's clipboard, on every road:
  the Edit menu, Ctrl+C, Ctrl+X, Ctrl+V, the node's menu, the create
  menu. **Duplicate** and **Replace with**.
- **Listen to a group**, from its menu and from the breadcrumb's switch.
- **MIDI in**: the port box in the transport; keys hold notes; the wheel
  and channel pressure reach the instrument.
- **The build's time in the title**, and a banner when the program is
  older than its sources.

## 2. The shape

**The engine changed in one module, and nothing that renders changed.**
The `patch` module's record learned two rules (`check()` refuses a name
listed in two groups, and a group that holds itself) and gained the edits
that make and change groups, `paste`, `fragment_of`, `replace_node`, and a
fragment as one line of text. `mforce_cli` renders what it rendered: the
null, allocation and comp gates pass at every step.

**A group's pins are worked out from the wiring, never stored**
(`ui_model/groups.h`): the wires that cross its edge are its interface.
Going in, the canvas draws one level (`shown_at`); a wire's end is led to
whatever stands for it at that level; a group's pin holds the real pin
behind it, so a wire made on a group's pin is an ordinary wire.

**The record is the truth about what is selected.** The canvas's own
selection is taken into the record when the mouse button is released, and
given back to the canvas on every other frame. After every step, Undo,
Redo and change of level the record's list keeps only what the level
shows.

**One clipboard, handed to the views** as the file dialogs are: the
program's is the system's; a test's keeps a string. Copy and paste are
functions of `ui_control` that give and take a fragment's text; no editor
module reads JSON.

**MIDI is the sound card's pattern again**: the library hidden behind one
class in `ui_audio`, what a message does as plain data in `ui_control`
with tests that feed messages by hand, and one call a frame from the
program's loop and from the tests' frame.

## 3. The steps

| Step | What | Commit |
|---|---|---|
| 0 | The old editor read for this slice, with its numbers, words and reasons; the page written from it and signed | (docs) |
| 1 | The signed page checked against today's code, block by block; the measured patches' groups and wormholes counted. Nine contradictions and twenty-one questions, each ruled on | (read only) |
| 2 | The record: `check()`'s two rules, the group edits, `paste`, `replace_node`, a fragment's text | `ba975cae` |
| 3 | The control: the list of selected things, levels, grouping, the clipboard, duplicate, replace, the wire route | `550b3216` |
| 4 | The views: one level drawn, groups, wormholes, the breadcrumb, the menus, the panel | `0bf3a0c4` |
| 5 | MIDI in; the build stamp | `6a3bdab5` |
| 6 | Review, fix wave; the parity list's 38 lines checked one by one, its additions in the same wave | `843992d8` |

## 4. What was proven, and what no gate proves

Proven, by tests that run in the gates:

- **The record keeps its promises**: each new refusal of `check()` by its
  sentence, with every measured patch still passing; each group edit and
  each of its refusals; the last member leaving a group taking the group,
  up through nested groups; `fragment_of` then `paste` leaving exactly
  the patch expected, names by the rule ("Sine2" onto "Sine2" gives
  "Sine3"; "BP0" onto BP0 to BP3 gives "BP4"); a paste into another
  patch keeping the wires it can and counting the rest; the text of a
  fragment cut from three measured patches reading back to the same
  fragment; `replace_node` carrying by name and naming what it dropped.
- **A group is its wiring**: for every group of the seven measured
  patches that have them, the boundary, the one output and what each
  level shows; and at every level of all 131 patches, every wire of the
  record is drawn, or is a span between two Wormholes, or lies wholly
  inside one shown group, or feeds a pin that names its source.
- **Every gesture of the slice, from the mouse or the key**, found by
  name: a click, Ctrl and a click on a node and on a pin, a box, a drag
  of several; a right click that selects; Group selection, Add to,
  Remove from, Ungroup and Open from the menus; a double-click into a
  group and the breadcrumb back up; a group's rename in the panel with a
  refusal; a wire made onto a group's pin; Delete on several and on a
  group; copy, paste and cut by the keys, the Edit menu, the node's menu
  and the create menu; Duplicate and Replace with; a wire routed by a
  right click; two Wormholes paired by a dragged wire; the ghost on
  select and on hover, the tooltip, the jump with its twin centred;
  Listen from a group's menu and from the breadcrumb; the MIDI box and
  its "(none)"; the banner's Dismiss.
- **After each gesture, what must still hold**: every selected name
  exists and is shown at the level, the path names only groups each held
  by the one before, and nothing gone is drawn.
- **Each new handler was switched off once and its test failed**: the
  first executor did it for step 4's gestures in ten batches, the second
  for the wave's six.
- **One thing done is one Undo**: a group made, a paste, a cut, a
  duplicate, a replace, a wire routed, a delete of a group with all under
  it.
- **MIDI**, with messages fed by hand: a note held past its length with
  Live off and let go; all sixteen channels; the wheel and pressure
  reaching the player, and again after the player is rebuilt; eight kinds
  of message that do nothing; a MIDI note still sounding after the
  computer's keys are let go; Stop; a port closed with a note held.
- **The build stamp**: the words when not stale, stale, with no commit;
  the commit read from a loose ref, a packed ref, a detached head and a
  worktree, on folders the test makes.
- **The engine did not move**: the null, allocation and comp gates pass
  at each step. At the close `python tools/gates.py` prints ALL GATES
  PASS, 17,276 checks (16,547 at the base).

Not under any test, and yours to try from REVIEW R34:

- **A real MIDI port.** No test opens one. Choosing a port in the box is
  the one gesture of the slice with no test.
- **The system's clipboard**, and a paste from one editor into another.
- **The build stamp on a tree that is really stale.** The second executor
  showed it once by hand: a counted header touched, the banner shown, one
  build, the banner gone.
- **What the canvas looks like.** I looked at four pictures of the real
  window (the oboe at the top level and inside Drive, a wormhole pair
  with its ghost, and Drive again after the fix wave with the fed pin
  naming its source); the executors looked at six and two.

## 5. The meter, against slice 4's close

| Measure | Before | After |
|---|---|---|
| files | 623 | 679 |
| lines | 76,579 | 82,521 |
| files over the limit | 8 | 8 |
| functions over the limit | 50 | 50 |
| file-level mutable variables | 22 | 22 |
| duplicate blocks (reported, not gated) | 174 | 181 |
| include violations | 0 | 0 |

Nothing under `editor/` is in the baseline, and no engine file gained or
raised an entry. The editor is 11,225 lines of code and 6,621 of tests
(8,193 and 4,838 at slice 4's close); its largest file is 327 lines. The
engine gained 1,135 lines, all in `patch`. The full gates took between
274 and 501 seconds in this session, by how busy the machine was.

## 6. Decisions the session made

The thirty-five rulings are the page's section 6; the ones worth your eye:

1. **Cut on a group: your signed ruling 3 is amended.** It said Delete
   and Cut on a group both take everything in it. A group cannot be
   copied (the old editor refused it too), so a Cut that took it would
   destroy what never reached the clipboard. Cut now leaves the group and
   says so; Delete takes it with all under it, one Undo. R34 asks.
2. **The build stamp measures against the last build, not the program's
   link time** (amending ruling 14). Counting every header of a module
   the editor uses meant that a header no compiled file includes, once
   edited, would say STALE for ever, since nothing relinks: the false
   STALE the old code's own comment warns of. The build now touches a
   small file beside the program each time, and any successful build
   clears the banner.
3. **The selection holds only what the level shows.** The review's one
   Major: a node moved into a group stayed selected though unseen, and
   Delete then removed it from inside the group with no words.
4. **Inside a group, a pin fed from outside names its source** in grey
   ("source1 <- WhiteNoise"). The wire itself is not drawn at that level,
   as before; but the pin used to look unwired, and a wire dragged onto
   it replaced a feed nobody could see.
5. **An edit that takes the last member out of a group takes the group
   too**, kept in one place in the record so every edit honours it. This
   is the one case where two edits that existed (removing a node, removing
   a Note) do something they did not.
6. **A group has one out pin a source**, labelled "out" when there is
   exactly one, as before. The oboe's Delay group sends one signal to two
   readers: one pin, two wires.
7. **Replace with never refuses for something it carries.** What the new
   type has no place for is dropped and named in the status. An enum
   setting is carried by its label, so "seconds" cannot silently become
   "samples" between two types that number their choices differently.
8. **MIDI**: the first port is opened at start, as before (the page's
   wording had dropped it); the last wheel and pressure are given again
   to a player built after an edit; MIDI-held notes stay when the window
   loses the keyboard (their note offs still arrive); closing or
   switching the port lets its notes go.
9. **Old faults not restored**: Delete on a selected group doing nothing;
   an emptied group left drawn as "0 nodes"; the Output node addable to a
   group; Duplicate and paste landing outside the group you were in,
   unseen; a right click on a wire that already touched a Wormhole doing
   nothing at all, not even opening a menu; the jump's centring
   overwritten a frame later; a paste ending Listen; "n nodes" counting a
   nested group as one.
10. **Words where the old editor had none**, because it greyed the item
    out or did nothing: "Deleted N node(s)"; "the wire runs through a
    wormhole already: it is not routed again"; "a setting's wire is not
    routed through wormholes"; "group 'G' has no one output to listen
    to".
11. **Reversible looks and habits**: a new group is selected when made;
    F inside a group frames it at the top-left instead of centring it;
    the panel's node list also lists groups; a failed paste says "Paste
    failed:" and why.
12. **Signed, and still yours to turn round**: a copy carries its seed
    (a copied noise node plays the same noise as its original); a file
    with a dead group member is refused.

## 7. Debt, booked in CLEANUP

Rows 196 to 205: choosing a real MIDI port has no test, and opening the
port box under test starts the MIDI library and lists the machine's
devices (a stand-in port would mend both); `mforce_editor.exe` relinks on
every build even with nothing to compile (found at the fix wave, cause
not traced); the stamp's folder list is written when the build is
configured and holds this machine's paths; the stale banner sits over
part of the transport, at the old place; the one road on which a paste
is applied and taken back has no test; `select_nodes` accepts a name the
level does not show (only steps prune); "Add to group" lists every group
at every level, as before, so a node can be sent out of view; F inside a
group frames rather than centres; the duplicate-block count rose by 7;
and the looks and words decided here.

## 8. The review

An Opus 5.5 agent that had seen neither the briefs' reasoning nor the
executors' reports read the range, built a copy of it and ran its own
tests against it.

**One Major, acted on**, shown from the mouse: group two nodes,
right-click a third, "Add to group", press Delete. The third node, now
inside the group and no longer drawn, was deleted with no words. The
slice's own gesture tests did the same Add and passed, because their
check after each gesture asked only that every selected name still
existed. They now ask that it is shown.

**Four Minor**: a pin fed from outside the level drawn as unwired (acted
on); Cut on a group (ruled, section 6 item 1); the stamp's false STALE
(acted on, item 2); a replaced node's strip not carried to the new node
(not acted on: the render never held the new node, and the old editor did
not carry it either).

**Four Notes, all acted on**: the jump's test did not check the
centring; the ghost on hover and the tooltip had no test; a refused paste
said "(graph restored)" though nothing had been changed; Ctrl and a drag
out of a pin also toggled the selection.

**Then the parity list, line by line**: of the 38 lines that name slice
5, 32 were done, 4 done in part (each closed by a ruling already in the
wave), 1 struck (the theme file) and 1 gone with the second clipboard. It
listed five gestures with no test moving the mouse; each has one now.

**One fault the executor found in its own earlier work**: step 5's test
of "(none)" in the MIDI box clicked where the list had been laid out on
its first, unseen frame, so the box closed without anything chosen and
the test passed with its handler off. It now waits two frames, holds a
note, and fails when the handler is off.

Sound, by the review: nothing an offline render reaches changed; `paste`
and `replace_node` keep the record valid; no carried item that `check()`
would refuse; a paste of colliding names into a group, then Undo, Undo,
Redo, leaves the path and the selection valid; with the path three deep
and the outer group undone, the path is cut to the top and Listen ends;
every road to the clipboard goes through the stand-in under test; one key
press is one paste.

## 9. Lessons

### 9.1 A value that may not be there: `std::optional`

`paste_text` lands a paste either where the create menu was opened or by
the offset rule:

```cpp
void paste_text(Session& s, std::string_view text, std::optional<Vec2> at);

// canvas_menu.cpp, the create menu's Paste: at the click
if (paste) paste_text(s, clipboard_.text(), menuAt_);
// menu_bar.cpp, Ctrl+V: by the rule
if (shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) paste_text(s, clipboard_.text(), std::nullopt);
```

`std::optional<Vec2>` is C#'s `Vec2?` (`Nullable<T>`): a `Vec2`, or
nothing, with the difference written in the type. Inside, `if (at)` asks
whether there is one and `*at` reads it. The alternatives are worse in
plain ways: a pointer says "may be absent" but also "someone else owns
this, and it may be gone"; a magic value ("a negative x means no point")
is a rule every reader must remember; two functions double the surface.
The record uses the same type for a face's position and the canvas's
panning, which a file may or may not hold.

### 9.2 A header the build writes

The stale banner needs to know which folders the editor is built from.
The editor's CMake file knows that when the build is set up (it has just
told the compiler), so it writes it down as C++:

```cpp
// build/editor/ui_app/generated/build_sources.h, written by editor/ui_app/CMakeLists.txt
inline constexpr const char* kFolders[] = {
  R"(C:/@dev/repos/mforce/editor/ui_audio/include)",
  R"(C:/@dev/repos/mforce/editor/ui_audio/src)",
  ...
};
```

Two idioms. `R"( ... )"` is a raw string: nothing inside is an escape, as
in C#'s `@"..."`, which is what a Windows path wants. `inline constexpr`
makes the table a constant the compiler knows at compile time and lets
the header be included from more than one file without the linker seeing
two copies. The nearest C# idea is a source generator or a T4 template:
code written by the build, from what the build knows. The price is the
one named in the debt list: the table is as fresh as the last time the
build was set up, and it holds this machine's paths.

### 9.3 Where a rule lives

"A group never ends up empty" could have been written five times: in the
function that deletes, the one that removes a member, the one that moves
one, and so on, each remembering to tidy. It is written once, in the one
function every edit of the record already passes through on its way to
being accepted (`adopt`, which asks `check()` of the changed copy). The
first executor put it there; the brief had said only that every edit
must keep it. The same choice came up twice more in this slice and went
the same way: the selection is pruned in the one function every step,
Undo and Redo already calls, and the test helper that every gesture test
calls afterwards is where "what must still hold" is asked. A rule that
must hold after every operation belongs where every operation ends, not
in each operation. In C# the instinct is the same; the trap is the same
too: the review's Major was a rule ("the selection names only what is
shown") that nobody had written anywhere, so each function was right by
itself and the whole was wrong.

### 9.4 Mine, about the work

- **A check that asks too little passes too much.** The gesture tests
  ran "what must still hold" after every gesture, and the Major still
  got through, because "holds" asked whether each selected thing existed
  and not whether it was shown. When a brief says "assert the invariant",
  the invariant has to be written out in the brief.
- **A click on something that has just appeared needs a frame or two**
  before it lands. One test passed for a step with its handler dead
  because it clicked a list on the frame it was laid out. Switching the
  handler off is what found it, and that test had not been switched off
  when it was written, because nothing under test could show the
  difference then.
- **An executor's context runs out.** The first one reached about
  three quarters of its window by the end of the views; the second took
  MIDI, the stamp and the fix wave with what it needed written into a
  file. A slice of this size wants the hand-over planned, with the first
  step's findings saved to a file as they arrive.
- **Two signed rulings were amended in this slice** (Cut on a group; what
  the stamp measures against), each because the page's author had not
  followed his own sentence to its end. Both are put back to you in R34
  rather than left in a list.

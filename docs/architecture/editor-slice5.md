# The editor, slice 5: arrange

Status: **SIGNED by Matt 2026-11-24 ("Go ahead."), the seventeen rulings
of section 4 as written, the session's list named `selections` at his
word. BUILT the same day (report
`docs/autonomy/refactor/reports/2026-11-24-editor-slice5.md`, REVIEW R34).
Section 6 says what the build changed; where it differs from an earlier
section it is the decision. It amends rulings 3 and 14 of section 4.**
Parent: `editor.md` (signed 2026-11-23; its three rules, five modules and
limits hold here unchanged). What it must restore is row 5 of
`editor-parity.md`, A1: 38 lines of the list, about 1,720 lines of the old
editor. This page was written after the old code was read line by line for
each of them, with its numbers, its words and its own stated reasons
(`renders/scratch/ed5/old_read.md`, by the reviewer, read only). Where the
old editor did something odd or silently did nothing, section 4 says so
and rules on it.

## 1. What you can do when it is done

1. **Select several**: a box dragged on empty canvas, Ctrl and a click to
   add, Ctrl and a click on a selected node (or one of its pins) to take
   it out. Delete removes them all in one step.
2. **Groups.** "Group selection" on a node's menu makes a group of what is
   selected; it shows as one purple node with a pin for each wire that
   crosses its edge. Double-click it to go in; the breadcrumb above the
   canvas ("Patch > Group > ...") goes back up. "Add to group", "Remove
   from", "Ungroup", and a name you change in the panel.
3. **Wormholes.** Right-click a wire: it now runs through a pair of small
   violet nodes with nothing drawn between them. Hover or select one to
   see the span to its twin; double-click to jump to the twin.
4. **Copy, cut, paste** through the system's clipboard (so it carries
   between two editor windows): Edit menu, Ctrl+C, Ctrl+X, Ctrl+V, "Copy
   (n)" on a node's menu, "Paste" at the top of the create menu.
   **Duplicate** and **Replace with** on a node's menu.
5. **Listen to a group**: "Listen here" on a group's menu, and the
   "Listen: Patch | Group" switch on the breadcrumb while inside one.
6. **MIDI in**: a port chosen in the transport; keys hold notes, the
   wheel and channel pressure reach the instrument.
7. **The stale-build banner** and the build's time in the window's title.

## 2. The interfaces

Unlike slice 4, this slice changes the engine's `patch` module: the record
learns two rules about groups, and gains the edits that make, change and
paste them. Nothing that renders changes; the null, allocation and comp
gates stay as they are.

### 2.1 The record's rules for groups (`patch`: `check()`)

Today `check()` refuses a group member that names nothing and a name used
twice. It gains two refusals, each a shape the old editor's own code
could not survive (its walk up through groups would never end):

- a name listed in two groups, or twice in one;
- a group that holds itself, directly or through others.

No measured patch has either (7 of the 131 have groups: 27 groups, at most
two deep).

### 2.2 Group and fragment edits (`patch/edit.h`)

```cpp
std::string fresh_group_name(const Patch&);        // "Group", "Group2", ...: free among nodes, faces and groups
// A group of `members` (node ids, face labels, group names) at `pos`. Each
// member leaves the group it was in; the new group joins `parent` when
// that is not empty.
Change add_group   (Patch&, std::string_view name, const std::vector<std::string>& members,
                    Vec2 pos, std::string_view parent);
Change move_group  (Patch&, std::string_view name, Vec2 pos);
Change rename_group(Patch&, std::string_view name, std::string_view to);
Change remove_group(Patch&, std::string_view name);     // its members join the group that held it, or none
// `member` leaves its group and joins `group`; an empty name: no group.
Change regroup     (Patch&, std::string_view member, std::string_view group);

// The piece of the patch these nodes are: each as it stands, with its
// position. A feed that names a node outside the piece is kept as written.
Fragment fragment_of(const Patch&, const std::vector<std::string>& nodes);
// What a paste did: the ids the nodes were given, in the fragment's order,
// and how many feeds were dropped because they named nothing here.
struct Pasted { std::vector<std::string> ids; int dropped{0}; };
// The fragment's nodes join the patch at their positions plus `offset`.
// A node keeps its id when it is free, else takes its stem with the next
// free number. A feed that names a fragment node follows its new id; one
// that names a node of this patch is kept; any other is dropped and
// counted. Seeds are kept.
Pasted paste(Patch&, const Fragment&, Vec2 offset);

// What a replacement kept and lost.
struct Replaced { std::string id; int wiresKept{0}; std::vector<std::string> dropped; };
// `node` becomes a node of `type` under a fresh id: constants, settings,
// strings, arrays and dynamic pins carried by name, the seed kept, every
// wire that touched it moved to the pin of the same name; what the new
// type has no place for is named in `dropped`. Everything that named the
// old id (group members, the output) follows.
Replaced replace_node(Patch&, std::string_view node, std::string_view type);
```

The group edits are `Change::Layout`: the sound is untouched. Each edit
refuses a name that is not there in its own words and otherwise exactly
what `check()` refuses, as every structural edit does.

```cpp
// patch/fragment_text.h: a fragment as text, for the clipboard. No JSON
// type appears in it, so no editor module reads JSON (the design's 13.8).
std::string fragment_text(const Fragment&);              // one line
Fragment    fragment_from_text(std::string_view text);   // throws: "clipboard is not an MForce fragment"
```

### 2.3 What a group shows, worked out from the wiring (`ui_model/groups.h`)

A group's pins are never stored. As the old code put it: "boundary edges
ARE the interface, never authored".

```cpp
// A wire that crosses a group's edge.
struct Crossing { std::string node, pin; std::size_t feed{0}; std::string source; bool tap{false}; };
struct Boundary {
  std::vector<Crossing> in;    // a member reads something outside
  std::vector<Crossing> out;   // something outside (or the patch's output) reads a member
};
Boundary boundary(const Patch&, std::string_view group);
std::vector<std::string> nodes_under(const Patch&, std::string_view group);   // through nested groups and faces
std::string group_holding(const Patch&, std::string_view member);            // empty: none
// The one member whose signal leaves the group; with no wire leaving, the
// last member in signal order that has an output. Empty when there is
// none, or when more than one leaves: such a group has no "Listen here".
std::string output_node(const Patch&, std::string_view group);
// Why these cannot be one group, in the old words ("selection has 2
// outputs: a b"); empty when they can.
std::string group_refusal(const Patch&, const std::vector<std::string>& members);
// What the canvas draws at one level: the nodes, faces and groups whose
// holder is the last name of `path` (none: the top).
std::vector<std::string> shown_at(const Patch&, const std::vector<std::string>& path);
```

Two of the old rules are kept with their reasons: a span between two
Wormholes counts as no crossing ("the two-output rule was refusing exactly
the configuration wormholes exist for", Matt 09-06), and a note face's
field is never a group's output ("wiring any outside node to a member
face's field made a second boundary output and nulled the group's out
pin", Matt 2026-09-07).

### 2.4 The session: a selection of several, and where you are

```cpp
// before (ui_control/session.h)
std::string  selected;     // a node's id or a face's label (one namespace), or empty
// after
std::vector<std::string> selections;  // node ids, face labels, group names, in the order chosen
std::vector<std::string> groupPath;   // the groups gone into, outermost first; not saved

// before (ui_views/src/properties_panel.cpp, which thing the panel shows)
if (const Node* n = node_named(s.open.patch, s.selected)) {
// after
if (const Node* n = node_named(s.open.patch, selected_one(s))) {
```

`selected_one(session)` is the name when exactly one thing is selected and
empty otherwise. The list is `selections` (Matt 2026-11-24: "I would put
an S on it"), which is also how the record names its other lists
(`groups`, `faces`, `members`, `strips`, `popOuts`).

```cpp
// ui_control/selecting.h
void select_node (Session&, std::string_view name);              // one; empty: nothing (as today)
void select_nodes(Session&, std::vector<std::string> names);
void toggle_selected(Session&, std::string_view name);           // Ctrl and a click
const std::string& selected_one(const Session&);

// ui_control/grouping.h
void group_selection  (Session&);                                // "Group refused: ..." or "Grouped as '<name>' ..."
void add_to_group     (Session&, std::string_view group);        // the selection
void remove_from_group(Session&);                                // the selection, to the group above
void ungroup          (Session&, std::string_view group);
void open_group       (Session&, std::string_view group);        // go in
void go_up            (Session&, std::size_t depth);             // the breadcrumb: 0 is the patch; ends Listen
void listen_at_group  (Session&, std::string_view group);        // listen_at its output_node

// ui_control/clipboard.h: the text is the view's to put on the clipboard
std::string copy_text (Session&);                                // the selection as a fragment's text; empty, with words in status, when nothing can be copied
void paste_text       (Session&, std::string_view text, std::optional<Vec2> at);   // `at`: where the create menu was opened
std::string cut_text  (Session&);                                // copy_text, then the selection deleted: one step
void duplicate        (Session&, std::string_view node);
void replace_with     (Session&, std::string_view node, std::string_view type);
void route_wire       (Session&, const PinEnd& reader, std::size_t feed);          // through a new pair of Wormholes: one step
```

`rename` (it exists) learns a group's name, with the old refusals.
`delete_these` (it exists) learns a group: see ruling 3. Each call above is
one undo step.

### 2.5 MIDI in (`ui_audio/midi_in.h`, `ui_control/midi.h`)

```cpp
// ui_audio: RtMidi lives behind this, as RtAudio does behind the adapter.
struct MidiMessage { unsigned char status{0}, data1{0}, data2{0}; };
class MidiIn {
public:
  std::vector<std::string> ports();        // asked afresh each call: a keyboard plugged in later appears
  std::string open(int port);              // empty, or why not
  void close();
  std::string open_port() const;           // its name; empty: none
  int poll(std::span<MidiMessage> into);   // the messages since the last call; never blocks
};

// ui_control: what a message does, as plain data a test feeds by hand
void midi_message(Session&, MidiMessage);  // a note on holds its note; a note off lets it go;
                                           // controller 1 is the wheel; channel pressure is pressure
void pump_midi(Session&);                  // once a frame: poll, then midi_message for each
```

No test opens a port. The program's loop and the headless frame both call
`pump_midi`, as they both call `watch_audio`.

### 2.6 The build stamp (`ui_model/build_stamp.h`, `ui_app`)

```cpp
struct BuildStamp { std::string built; std::string commit; std::string newer; };   // "11-24 14:41"; "b441c05"; the file that is newer, or empty
bool        stale(const BuildStamp&);
std::string title_suffix(const BuildStamp&);    // "  [build 11-24 14:41 @b441c05]", then the STALE words when stale
```

`ui_app` fills one at start: the program's own time, the commit of the
checkout, and the newest source file among the modules the editor is
built from (the list is written into the program by its CMake file).

### 2.7 The views

- `NodeCanvas` draws one level (`shown_at`): nodes, faces, a group as one
  node with its boundary's pins, wires led to the pin that stands for
  each end. Box select, Ctrl and a click, double-click (a group: go in; a
  Wormhole: jump), a right click on a wire. The Wormhole's own face, the
  ghost span and the tooltip.
- `Breadcrumb` (new): the path and the Listen switch.
- The node's menu and the group's menu; `MenuBar`'s Cut, Copy, Paste;
  the create menu's Paste.
- `PropertiesPanel`: a group (its name, its members); "N selected" when
  several are.
- The transport's MIDI port box. The banner.
- A `Clipboard` the views are handed, as they are handed `FileDialogs`:
  the program gives the system's; a test gives a string, so no test ever
  writes your clipboard.
- `HeadlessEditor` learns a click with Ctrl held and a double-click. Every
  gesture above gets a test that moves the mouse or presses the key, found
  by name, and the handler is switched off once to see the test fail.

## 3. Steps

1. **Read and count** (read only): this page against today's code, block
   by block, and every point the read marked "inferred" tried where a
   scratch run can try it. What the page cannot do as written is ruled on
   before any brief.
2. **The record**: `check()`'s two rules, the group edits, `fragment_of`,
   `paste`, `replace_node`, the fragment's text, with tests.
3. **The control**: the selection of several, a level and its boundary,
   grouping, the clipboard, duplicate, replace, the wire route.
4. **The views**: the canvas at a level, groups and wormholes drawn, the
   breadcrumb, the menus, the panel. Pictures of oboe1 and the piano at
   the top level and inside a group.
5. **MIDI in.**
6. **The build stamp.**
7. **Review, fix wave, the parity check and its wave, report, on your
   desk.**

## 4. The rulings the signature accepts

The first four change what you see or do most.

1. **One clipboard, on every road.** The old editor had two: the Edit
   menu and Ctrl+X/C/V (with the canvas focused) used one inside the
   program; the node menu's Copy, the create menu's Paste and Ctrl+C/V
   (with the mouse over the canvas) used the system's. You kept the
   system's (the parity list, A2.1). So every road now leads to it, and
   **Cut is new on it**: Cut only ever existed on the one that is gone.
   It is a copy, then a delete, one step. Ctrl+X/C/V are the canvas's
   wherever the mouse is, unless a text field has the keyboard.
2. **A copy carries its seed.** Paste did before (as a side effect: it
   reloaded the whole patch), and the list's A2.2 calls Duplicate leaving
   the seed out a fault. So a copied noise node plays the same noise as
   its original. Say the word and copies get fresh seeds instead.
3. **Delete, and Cut, on a group take everything in it.** In the old
   editor both silently did nothing to a selected group. Here the group
   and all under it go in one step that Undo brings back, and the status
   line says how many nodes went. A group left with no members goes with
   its last one (the old kept it, drawn as "0 nodes").
4. **A dead group member refuses the file.** The old editor dropped a
   member that named nothing, with a line on stderr, and opened the file.
   Today's reader refuses the file and says which member. That is the
   same footing as an unknown node type (the list's A2.3): yours to
   overrule.
5. **The record holds the selection as a list.** The panel shows a node
   only when exactly one thing is selected, and "N selected" otherwise
   (the old panel went on editing the node selected before, without
   saying). A right click on a node outside the selection selects it
   alone first, so a menu's "(n)" is what you see selected.
6. **"One output" stays the editor's rule**, checked when a group is made
   or added to, in the old words ("Group refused: selection has 2
   outputs: a b"). It is not a rule of the record: a file is never
   refused for it. A wire made later that gives a group a second output
   is accepted, as before; the collapsed group then shows a pin for each
   (the old one showed none, and the wires vanished from that level).
   The Output node cannot be in a group at all: it is not a node of the
   record (the old "Add to group" could put it in one).
7. **Paste** lands at the copied places plus (60, 60), and each further
   paste of the same text another (60, 60); from the create menu it lands
   where you clicked (the old one ignored the click). A pasted node keeps
   its name when it is free, else takes the next free number ("Sine2"
   onto "Sine2" gives "Sine3", where the old gave "Sine22"). A wire to a
   node that was not copied is kept when this patch has a node of that
   name, else dropped and counted in the status, as before. Pasted nodes
   join the group you are in. Paste no longer ends Listen (the old reload
   did).
8. **Duplicate is a copy and a paste of that one node** without touching
   the clipboard: its constants, settings, strings, arrays, dynamic pins
   and seed (A2.2), the wires into it as a paste keeps them, at (30, 30)
   from the original, in the group you are in. The old one carried no
   wires and landed outside the group you were in, where you could not
   see it.
9. **Replace with** carries strings, arrays and the seed too (the old one
   dropped them without a word) and names in the status whatever the new
   type has no place for, as it named dropped wires. The new node takes a
   fresh name by its type, as before.
10. **Copy skips what the old one skipped**: a Note, a group, a Channel, a
    Mixer, with the old words when anything was skipped. Copying a group
    or a Note would be new, and is not added.
11. **Wormholes as before**: the small face with no title, no tap pin
    ("tapping pure glass == tapping its source"), the span never drawn,
    the ghost while hovered or selected, the tooltip, the words when two
    are wired into a pair. A right click on a wire still routes it at
    once; it now says so in the status and Undo takes it back. On a wire
    that already touches a Wormhole the old one did nothing, not even open
    a menu: here the status says why. The jump centres the twin (the old
    one was overwritten a frame later by the reframing of the level).
12. **Where you are is the session's**, not the file's. Going up a level
    ends Listen, as before ("Backing out returns the ears to the PATCH at
    any level", Matt 2026-09-07). The breadcrumb, its Listen switch and
    the camera on going in and out are as before. A group's "n nodes"
    counts the nodes under it (the old counted a nested group as one).
    Choosing a node in the panel's list that is inside a group goes to
    its level.
13. **MIDI as before**: every channel; a key always holds its note, Live
    checked or not ("a key struck on a MIDI keyboard is always intent to
    play"); controller 1 is the wheel; channel pressure is pressure.
    Pitch bend, the sustain pedal and poly aftertouch do nothing, as
    before; the list's line is corrected to say so. RtMidi goes to
    `ui_audio` (the design), and is polled once a frame.
14. **The build stamp is measured against today's tree.** The old one
    read build logs and folders that are gone. Stale now means: a source
    file of a module the editor is built from is newer than the program.
    The banner's words name `mforce_editor`; the title's suffix is as
    before. The commit is the checkout's, read at start, as before.
15. **The theme file is struck from the slice.** There never was one: the
    old `theme()` returned built-in colours, and its own comment put
    loading one off to "when the editor is rebuilt". Adding one would be
    a feature.
16. **The engine's `patch` module changes** (2.1, 2.2). Nothing that
    renders does.
17. **Not here**: copying groups and Notes; the reuse library the old
    comments promise for group names; pitch bend; tables, the shape
    editors, partials and formants, audition (slice 6).

## 5. For the C# reader

`Pasted paste(Patch&, const Fragment&, Vec2 offset)` returns a small
struct where C# would reach for `out` parameters or a tuple:

```cpp
struct Pasted { std::vector<std::string> ids; int dropped{0}; };
const Pasted done = paste(patch, fragment, offset);
if (done.dropped > 0) ...
```

A function in C++ returns one value, and a struct made on the spot is the
plain way to return two: named fields, no heap, and the compiler builds it
straight into the caller's variable (no copy). The alternatives are a
`std::pair` (fields named `first` and `second`, which say nothing), a
`std::tuple` (worse), or reference parameters the function fills in (the
`out` of C#, which hides at the call site which arguments are results).
The name was chosen as a past participle, like `Change` is a noun for
what an edit did: the type says what the call left behind.

## 6. What the build changed (2026-11-24)

A first step, read only, checked this page against today's code block by
block before any brief was written. It found nine places where the signed
text and the code disagreed and twenty-one questions the page did not
answer. Each was ruled on then (1 to 27), and eight more at the review
(28 to 35). Ruling 30 amends section 4's ruling 3 (Cut on a group) and
ruling 31 its ruling 14 (what the build stamp measures against). "C" and
"Q" numbers are that first step's contradictions and questions. Report:
`docs/autonomy/refactor/reports/2026-11-24-editor-slice5.md`.

### The record

1. **An edit that takes the last member out of a group takes the group
   too**, and so on up through any group that leaves empty in turn (C1,
   Q1). This is the record's: `remove_node`, `remove_face`, `regroup` and
   `remove_group` all keep it, so no function of the editor has to
   remember it. It changes what `remove_node` and `remove_face` do today
   in one case (they leave the empty group; a test builds one,
   `face_edit_tests.cpp:154`, and is changed with them). `check()` still
   accepts an empty group that a file brings. `add_group` with no members
   is refused.
2. **`check()`'s new sentences**, in the form of the ones beside them, at
   the member's path: `'m' is a member of 'A' and of 'B': a name is in one
   group at most`; `'m' is listed twice in group 'G'`; `'G' holds itself,
   through 'H'` (or `'G' holds itself` when it lists its own name).
3. **A pasted node's name** (Q4): its own id when it is free. Otherwise
   its stem, which is the id less its trailing digits (the whole id when
   that leaves nothing), with the first free number from 2: "Sine2" onto a
   patch that has "Sine2" gives "Sine3" (never the bare "Sine"); "BP0"
   onto BP0 to BP3 gives "BP4"; "Reverb" onto "Reverb" gives "Reverb2".
4. **`paste`'s `dropped`** (Q6) counts every feed dropped, a dynamic pin's
   drive included. A pin left with no feed takes its declared default, as
   it does when its feed is removed today.
5. **A fragment's text** (C4, Q5): the codec's fragment under one marker
   key, `{"mforceClip": 1, "nodes": [...], "positions": {...}}`, one line.
   Reading: a text that is not JSON, or has no `"mforceClip": 1`, throws
   "clipboard is not an MForce fragment"; one with no nodes throws
   "fragment had no nodes"; anything else the reader refuses throws the
   reader's own sentence. The old `mode` key is neither written nor
   accepted.
6. **`replace_node` never refuses for something it carries** (C5, Q7). It
   carries by name what the new type takes and names the rest in
   `dropped`:
   - a pin's constant, when the new type has a signal pin of that name
     that takes a number;
   - a setting, when the new type has a setting of that name and the
     value is inside its range; an enum by its LABEL, not its index;
   - a string, by name;
   - the arrays, when the new type declares exactly the columns the old
     node holds; otherwise none of them;
   - a dynamic pin, when the new type has a setting of that name that may
     be driven;
   - each wire into it, to the pin of the same name when that pin takes
     that source; each wire out of it, when its reader takes the new type;
     the patch's output, when the new type may be one;
   - the seed, the position, the group it is in.
   `dropped` names only what held something: a constant, setting or string
   that differed from the old type's default, the arrays when there were
   any, each dynamic pin, each wire (by its pin's name; "output" for the
   patch's). `wiresKept` counts the wires moved.
7. **A tap of a Wormhole** (Q20): `check()` does not change. The canvas
   offers no tap pin on one; a file that has such a tap keeps it, drawn
   from the Wormhole's one output pin.

### Groups on the canvas

8. **A group's out pins are one a source, not one a wire** (C2, Q2):
   `Boundary::out` holds each (member, plain or tap) that something outside
   reads, once; every wire from it leaves from that one pin. The Delay
   group of four measured patches has two wires from one source: one pin.
   With exactly one plain source the pin is labelled "out", as before;
   otherwise each is labelled by its member (and ".tap").
   `Boundary::in` stays one a wire (a member's pin and feed), labelled
   "member.pin" as before.
9. **A drive of a setting that crosses the edge is drawn like any wire**
   (to a gold pin on the group) **and is not counted by the two-output
   rule**, as the old rule did not count it (C3, Q3). A note face's field
   read from outside is drawn the same way and not counted either (Q10;
   the old reason, Matt 2026-09-07, stands).
10. **`output_node`'s "has an output" means a signal** (Q9): a part is
    never a group's output.
11. **`shown_at` leaves out the nodes a note face names** (C8): a face is
    shown, its nodes never are, as on today's canvas.
12. **A group's rename uses the old group words** (C9): "name is empty";
    "'.' not allowed"; "'__' prefix is reserved"; "name already in use by a
    node: X" (a node or a face); "name already in use: X" (another group).
    A node's and a face's rename keep today's words.
13. **The two selections** (step 1's C3, accepted as written): while the
    left button is down imnodes owns the selection; on the frame it is
    released over the canvas the record takes imnodes' (the Output node
    left out), before anything is pushed back; on every other frame with
    the button up imnodes is given the record's names that are shown at
    the level. Ctrl and a click on a pin toggles the pin's owner. A right
    click on a thing outside the selection selects it alone first (nothing
    in imnodes reads the right button: the old comment that it did was
    wrong).
14. **A change of level selects nothing; the jump selects the twin**
    (Q11), so its ghost shows where you came from. Listen ends whenever
    the path changes other than by going deeper, as before.
15. **The file keeps the top level's panning only** (Q12). A level's
    panning inside a group, and the panning remembered for each depth, are
    the session's and go at New and Open.
16. **The breadcrumb shows when the patch has a group or the path is not
    empty**, as before (Q13). The canvas moves down by the bar's height
    when the first group is made.
17. **The path follows** (step 1's B3): New and Open clear it; a rename of
    a group in it renames the entry; an ungroup, a delete, a regroup, or
    an undo or redo that leaves an entry that is not a group, or not held
    by the entry before it, cuts the path there (and Listen ends, as for
    any change that is not deeper).
18. **The ghost is drawn in the editor's own window so that it shows over
    the grid** (C7). A picture is the proof.
19. **`route_wire`** (Q19): the wire to the patch's Output can be routed,
    as the old one could. A setting's drive is not: the status says "a
    setting's wire is not routed through wormholes".

### The clipboard and its relatives

20. **Each further paste lands another (60, 60) on, with no memory of the
    last paste** (Q8): the offset is (60, 60) times the least k from 1 for
    which no node of the patch sits where the fragment's first node would
    land.
21. **"Replace with" is offered on a graph node that is not a Channel or a
    Mixer; "Duplicate" on the same, and neither on a Note** (Q7), which is
    the old list for Replace and Copy's skip list for Duplicate.
22. **No path from a test to the real clipboard** (Q15): the views take a
    `Clipboard`, and both headless contexts replace ImGui's own two
    clipboard functions with ones that keep a string, so a text field
    under test cannot reach it either.

### MIDI in

23. **The program opens the first port at start when there is one**, as
    before, and says nothing when there is none or it fails (C6, Q16).
    `main` asks; a session never opens one by itself, so no test does.
24. **The last wheel and pressure are given again to a player built
    after them** (the session keeps them). **MIDI-held notes are not let
    go when the window loses the keyboard** (their key-ups still arrive);
    Stop lets everything go. A MIDI note is the last note, as a held key's
    is. RtMidi's errors are caught inside `MidiIn` and returned as words.
25. **Key numbers** (Q18): `ui_control/keyboard.h` owns the two ranges
    that are not computer keys, the mouse's and MIDI's, and the view's own
    constant for the mouse goes.

### The build stamp

26. **What counts** (Q17): the `.h`, `.hpp` and `.cpp` files under the
    `include` and `src` folders of each module the editor is built from,
    and `editor/ui_app`'s own; not tests, not CMake files. The folders and
    the repository's `.git` are written into the program by its CMake file
    at configure time. The commit is read at start from `HEAD`, through a
    loose ref, `packed-refs` or a worktree's `gitdir:`; when none reads,
    the suffix has no commit.
27. **The stamp is the session's** (made by `main`, read by `title` and by
    the banner, dismissed for the session by its button). Local time, in
    the old form (month-day hour:minute).

### Ruled at the review (2026-11-24)

28. **The selection holds only what the level shows.** After every step,
    every Undo and Redo and every change of level, a name that
    `shown_at(patch, groupPath)` does not list leaves the selection (the
    review's Major: a node moved into a group stayed selected, unseen, and
    Delete took it).
29. **A pin fed from outside the level is drawn fed and names its
    source**, after its label, in grey. The wire itself is not drawn at
    that level, as before. (The review: such a pin looked unwired, and a
    wire dragged onto it replaced a feed nobody could see.)
30. **Cut never deletes what it did not copy.** This amends the page's
    ruling 3 for Cut: a group is not copied (ruling 10 of the page), so
    Cut leaves it and says so; Delete takes a group and all under it, as
    signed. Confirmed by Matt 2026-11-24: "Leave it".
31. **The stamp compares against the last build, not the program's link
    time**: the build touches a file beside the program each time it
    builds it or finds it built. This amends ruling 26, under which a
    header no compiled file includes, once edited, said STALE for ever:
    the false STALE the old code's own comment warned of.
32. **A replaced node's strip and pop-outs are not carried** to the new
    node: the take never rendered it. They are empty until the next
    Generate, as in the old editor.
33. **A paste the reader refuses says "Paste failed: <why>"**; nothing was
    changed, so nothing was "restored".
34. **Ctrl and a click on a pin toggles its owner; Ctrl and a drag out of
    a pin makes a wire and leaves the selection.**
35. **Closing or switching the MIDI port lets its held notes go.**

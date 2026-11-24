# The editor, slice 2: build

Status: **signed by Matt 2026-11-23 ("Signed, proceed"), all eleven rulings
of section 4 as written. BUILT that day (report R28); section 7 is what
the build changed.**
Parent: `editor.md` (signed 2026-11-23; its three rules, five modules and
limits hold here unchanged). What it must restore is row 2 of
`editor-parity.md`, A1: 41 capabilities of the old editor, about 1,450 of
its lines. Written in the shape of `editor.md`'s section 4.

## 1. What you can do when it is done

1. **Start a patch from nothing.** File > New, or starting the program with
   no argument: an instrument with a Sine and an Envelope, unwired, as the
   old one gave. It asks first when there are unsaved changes.
2. **Add nodes.** Right-click the canvas: Note; Curve, Envelope, Var,
   Range; then the registry's families (the main shelf, then the
   Experimental one). The node appears where you clicked, selected.
3. **Wire.** Drag from a node's output to a pin, or the other way. A pin
   that takes one feed swaps its old one; a list pin takes another. Drag
   from a node's tap pin for a tap. Drag to the Output node to make it
   the patch's output. A wire the engine would refuse is refused with its
   sentence in the status line: a part into a signal pin, the wrong part
   for a slot, a loop with no tap in it.
4. **Unwire and delete.** Drag a wire off its pin, or select it and press
   Delete. Delete or Backspace removes the selected wires, then the
   selected nodes, with everything that named them.
5. **The note as one node.** A Note shows its six fields (frequency,
   velocity, wheel, pressure, duration, onset) as output pins; a wire from
   one makes the file's node for that field behind the scenes, as before.
6. **Drive a setting once per note.** In the panel a mark beside each
   setting that can take one offers its pin on the node; wire a signal to
   it; drag it off or delete it; withdraw the offer.
7. **Rename** a node or a Note in the panel; everything that named it
   follows.
8. **The instrument's own settings**: polyphony, sustaining, the onset
   names, the glide time.
9. **Find your way**: F or Fit frames the whole graph; Shift with the
   arrows, or the status bar's buttons, pan; the status bar counts nodes
   and wires. File > Open Recent > Clear.
10. **Undo and redo** (Ctrl+Z, Ctrl+Y). New: the old editor had none.

## 2. The interfaces

### 2.1 The rest of the edits (`patch/edit.h`)

Slice 1's header has six edits. These join them. Each applies whole or
throws one sentence and leaves the patch as it was, and an accepted edit
keeps `check()` true, as before. All return `Change::Structure` unless
marked.

```cpp
// A node of `type` under `id` at `pos`, holding what a fresh one holds: every
// Param and setting at its type's declared default, as the reader completes a
// file's node; an Envelope the four stages of an ADSR (0.05, 0.1, 0.7, 0.2)
// and a Curve the two knots (0,0) and (1,1), as the old editor started them.
Change add_node(Patch&, std::string_view type, std::string_view id, std::uint32_t seed, Vec2 pos);
// An id nothing in the patch has, for a new node of `type`: the type's name
// without a trailing "Source" ("Sine"; "Curve" for a CurveNode), then Sine2,
// Sine3... Ids, group names and face labels share one namespace.
std::string fresh_id(const Patch&, std::string_view type);
// The node goes, and everything that named it: each feed that read it (a
// Param left with no feed takes its declared default), each dynamic pin it
// fed, its position, its place in groups, its field of a note face. A patch
// whose output it was has none.
Change remove_node(Patch&, std::string_view node);

// `source` feeds `pin` of `node`, through a tap when `tap`. A pin that takes
// one feed loses the one it had; a list takes one more. The pin's number goes.
Change connect(Patch&, std::string_view node, std::string_view pin, std::string_view source, bool tap);
// Feed number `feed` of the pin goes. A Param left with none takes its
// declared default.
Change disconnect(Patch&, std::string_view node, std::string_view pin, std::size_t feed);
// A setting driven once per note by `source`, and the drive removed.
Change feed_setting  (Patch&, std::string_view node, std::string_view setting, std::string_view source);
Change unfeed_setting(Patch&, std::string_view node, std::string_view setting);
// The node whose signal is the patch's output; empty for none.
Change set_output(Patch&, std::string_view node);

// A new name for a node, in every place that named it.
Change rename_node(Patch&, std::string_view node, std::string_view to);
// Columns of one table, all at once (a table's columns keep one length).
Change set_arrays(Patch&, std::string_view node, const std::map<std::string, std::vector<float>>& columns);
// The instrument's own settings. Refused on a plain sound, which has none.
Change set_instrument(Patch&, const InstrumentBlock& block);

// Note faces. A face is the one node the canvas shows for the per-field
// PerformNodes it names.
Change add_face   (Patch&, std::string_view label, Vec2 pos);          // Layout
Change move_face  (Patch&, std::string_view label, Vec2 pos);          // Layout
Change rename_face(Patch&, std::string_view label, std::string_view to);   // Layout
Change remove_face(Patch&, std::string_view label);                    // its PerformNodes go with it
// The id of the node that reads `field` for this face ("__perf_frequency",
// then 2, 3...), made now when the face has none: connect() takes it as a
// source. A face's node that nothing reads any more goes, with its field,
// when its last reader is disconnected or removed.
std::string face_field(Patch&, std::string_view label, std::string_view field);
```

- **What each refuses is what the reader and `check()` refuse, no more.**
  Slice 1's one Major was a refusal I wrote without reading the reader.
  So the step's brief names, for every refusal, the line in the codec,
  `check()` or the build that refuses the same thing, before any code. The
  known ones: an unregistered type; an id that is empty or taken; a feed
  that names no node; a part fed to a signal pin; a slot fed a type its
  door does not take, or through a tap; a feed that closes a cycle with
  no tap; a dynamic pin on a structural setting; a note field or a
  dynamic pin on a plain sound; a ragged table.
- **Rename follows `check()` only** (not empty, not taken). The old
  editor's further rules for a name a person types (no ".", no leading
  "__", which marks a face's nodes) are the editor's policy and live in
  `ui_control`, so the engine's edit refuses no id a file may hold.
- **The proof, over the 131 measured patches:** each is built again from
  an empty patch through these edits alone (every node added, every
  value, setting and table set, every wire and dynamic pin connected, the
  faces, the output, the instrument) and must equal the patch as read. So
  the edits can make everything the library holds. And: removing each
  node of each patch in turn leaves `check()` empty every time.

### 2.2 Undo (`ui_model/history.h`)

The design's 4.4, now concrete. The patch is a plain value, so undo is a
stack of copies.

```cpp
// The patches an edit replaced, so each can be taken back.
struct History {
  std::vector<Patch> past;      // oldest first, at most 200
  std::vector<Patch> future;    // what redo returns to, nearest last
  int savedAt{0};               // how many of `past` there were when the file was read or saved; -1 once that state is gone
};
void remember(History&, const Patch& before);   // an edit is about to replace `before`: keep it, drop the future
bool undo(History&, Patch& patch);              // false when there is nothing to undo
bool redo(History&, Patch& patch);
void mark_saved(History&);
bool unsaved(const History&);                   // the patch is not the one in the file
```

Before and after, where the open patch is declared and where an edit is
made:

```cpp
// before (slice 1)
struct OpenPatch { Patch patch; std::string path; bool dirty{false}; };
void edit_value(Session& s, ...) { ... set_value(s.open.patch, node, pin, v); s.open.dirty = true; ... }

// after
struct OpenPatch { Patch patch; std::string path; History history; bool dirty{false}; };   // dirty is what unsaved(history) says
void edit_value(Session& s, ...) { begin_edit(s, "value:" + node + "." + pin); set_value(...); ... }
```

- **A drag is one step.** `begin_edit(session, what)` keeps the patch only
  when `what` differs from the gesture that is open; the view calls
  `end_gesture(session)` when the mouse lets go or a field loses the
  keyboard. Sixty writes of one slider are one undo.
- **Undo to the saved state clears the star** (`savedAt`), which a plain
  flag could not do.
- Placing unplaced nodes at open, and panning, are not steps and not
  unsaved changes, as in slice 1.
- After an undo or redo the player is rebuilt (29 ms here) and the
  selection is kept when its node still exists.
- 200 steps of the largest library patch is a few megabytes.
- Alternative: a list of inverse operations. It needs an inverse written
  and tested for every edit, for memory nobody will miss.

### 2.3 What a view asks for (`ui_control`)

```cpp
// wiring.h: one end of a wire as the canvas names it.
struct PinEnd {
  enum class Kind { Output, Tap, NoteField, Input, SettingPin, PatchOutput };
  Kind        kind;
  std::string node;     // a node id, or a face label for a NoteField; empty for the PatchOutput
  std::string name;     // the pin, setting or field; empty for an Output or a Tap
};
// The two ends of a drag, in either order: one must give a signal (Output,
// Tap, NoteField) and one take it. A refusal goes to the status line.
void wire  (Session&, const PinEnd& a, const PinEnd& b);
void unwire(Session&, const PinEnd& reader, std::size_t feed);

// building.h
void request_new (Session&);                                   // through the unsaved question, as request_open
void create_node (Session&, std::string_view type, Vec2 pos);  // a fresh id and a random seed; selected
void create_note (Session&, Vec2 pos);
void delete_these(Session&, const std::vector<std::pair<PinEnd, std::size_t>>& wires,
                  const std::vector<std::string>& nodes);      // one undo step
void rename      (Session&, std::string_view nodeOrFace, std::string to);
void edit_instrument(Session&, const InstrumentBlock& block);
void offer_setting_pin   (Session&, std::string_view node, std::string_view setting);   // show its pin on the node
void withdraw_setting_pin(Session&, std::string_view node, std::string_view setting);   // and its feed, if it has one
void undo(Session&);
void redo(Session&);
void end_gesture(Session&);
void fit_canvas(Session&);                                     // pan to the nodes' corner
void clear_recent(Session&);
```

`Session` gains three plain things: the gesture that is open (a string),
the setting pins on offer (not saved: an offered pin with no feed has no
place in the file, as before), and a seed generator a test can set.

### 2.4 The views

- `NodeCanvas` gains: the create menu (from `SourceRegistry::types_in`,
  which `core` already has), a drag between pins (`wire`), a drag off a
  pin and the Delete key (`unwire`, `delete_these`), the note face drawn
  as one node, a pin row for each setting on offer or fed, each string
  setting's text on the node's body, the tap pin.
- `PropertiesPanel` gains: the name field, the offer mark beside each
  setting that is not structural, and, with nothing selected, the
  instrument's four fields in place of slice 1's read-only facts.
- `MenuBar` gains File > New, Edit > Undo and Redo, Open Recent > Clear.
- `StatusLine` gains the counts, the pan buttons and Fit.
- The headless test draws every library patch as before, now with its
  note faces as nodes, and still finds each patch unchanged by drawing.
- At open, PerformNodes that no face names are gathered into faces (one
  of each field per face, in file order), as the old loader did. Like
  placement it is not an unsaved change; it lives in `ui_model`.

## 3. Steps

Each ends at the gates, as slice 1's did.

1. **Slice 1's 22 lines.** Check each "unclear" line of the parity list
   against the code; tick it, mend it if it is small, or give it a slice.
2. **The edits** (`patch/edit.h`), with the two proofs of 2.1.
3. **History, gathering faces, and the control** (`ui_model`,
   `ui_control`), with tests and no window.
4. **The views.** What is drawn is checked headless and in pictures of
   the real window (a new patch; a library patch with its note faces as
   nodes). What is clicked and dragged, a picture cannot show: the create
   menu, a wire being made, Delete, undo. Those are yours to try, from a
   list in REVIEW.
5. **Review, fix wave, report, on your desk.**

## 4. The rulings the signature accepts

1. **The order of the slices that remain** is the parity list's A1:
   Build, Generate, See, Arrange, Shape. Generate moves before See
   because the old strips were drawn from a render.
2. **What is not coming back** is the parity list's A2: what you deleted
   on 2026-10-05, the faults, and files with an unknown node type being
   refused where they used to open inert.
3. **Undo and redo stay**, new as they are (A3). Strike this line and
   they come out.
4. **A new node is given a random seed**, by CLAUDE.md's rule ("random at
   creation time, stored in JSON"). The old editor gave none.
5. **A new node starts as the old editor started it**: declared defaults,
   an Envelope as an ADSR, a Curve with two knots; ids "Sine", "Sine2".
6. **A pin you unwire takes its declared default.** The old editor gave
   back the number the pin had before it was wired; the patch record
   holds a number or a feed, not both. Undo brings the number back.
7. **A setting can be driven by any signal**, which is the engine's rule.
   The old editor let only a Curve do it.
8. **A note field's node that nothing reads is removed**, as the old save
   dropped it.
9. **The instrument's settings are edited in the panel when nothing is
   selected.** The old editor showed them on the Output node; a click on
   the Output node selects nothing, so it comes to the same click.
10. **Each key has one job.** The arrows stay the octave and the note
    length (slice 1, your layout); Shift with an arrow pans the canvas.
    The old arrows did both at once.
11. **The default note a fresh patch is saved with moves to slice 3**,
    with the transport whose note it is.

## 5. Not in this page

- Groups, wormholes, copy and paste, duplicate, replace-with, selecting
  several nodes: slice 5. Tables and the shape editors: slice 6.
- The create menu's Output item (the Output node is always on the
  canvas), Channel and Mixer (they went with the mode flag).

## 6. For the C# reader

`History` is the memento pattern with nothing to hide: because `Patch` is
a plain value with `operator==` and a copy that copies everything (no
pointers into shared state), "the state before" is just `Patch before =
patch;`. In C# the same design needs a deep clone, or immutable records
(`with` expressions) so that a copy is safe to keep; in C++ a struct of
values copies deeply by default. That is the practical reason the engine
keeps the patch as data apart from the objects that make sound: the data
can be copied, compared and saved, and the sounding objects are rebuilt
from it.

## 7. What the build changed (2026-11-23)

Built the day it was signed; report
`docs/autonomy/refactor/reports/2026-11-23-editor-slice2.md`. Where this
section and an earlier one disagree, this one is what the code does.

1. **How an edit refuses** (2.1). Each structural edit finds what it
   names, applies itself to a copy of the patch and asks `check()` of the
   copy: it throws `check()`'s sentence or the copy becomes the patch. So
   "what each refuses is what the reader and `check()` refuse" is kept by
   construction, with two named exceptions (a number that is not finite; a
   note field that is not one of the six).
2. **`set_instrument` is not refused on a plain sound** (2.1's comment
   said it was): no reader refuses a block on any patch. The window still
   offers no way to do it (`edit_instrument` refuses it there).
3. **Added to `patch/edit.h`**: `fresh_face_label`. `fresh_id` also gives
   "Note", "Channel" and "Mixer", as the old editor's code did.
4. **A Note's node that nothing reads goes** only when the edit itself
   removed its last read; one already unread in a file is left alone.
5. **The proof, exactly** (2.1): each measured patch is made again from a
   patch holding its rate, length, score and comment, through the edits,
   and equals the file as read after three things a new node always has
   and a file may lack are removed (a seed, a position, a starting table)
   and two things no edit of this slice makes are copied (a node's
   comment, the groups). One shape cannot be made by the edits: a Curve
   with no knots, because no edit removes a column of a table.
6. **`OpenPatch` has no `dirty`** (2.2's snippet kept it): `unsaved(open)`
   asks the history. A save ends the open drag.
7. **`step`** (2.2's `begin_edit`): one function runs any edit as one undo
   step; a drag is one step; several engine edits in one step are all or
   nothing; an edit that changes nothing leaves no step.
8. **`sound_follows`**: after any change the build reads, a new player, or
   silence with the reason. This also changed slice 1's setting edits,
   which used to leave the old player sounding when a rebuild failed. An
   undo or redo that changes nothing the build reads does not rebuild.
9. **Added to the control** (2.3): `new_file`; `fit_canvas` takes the
   canvas's size and centres the graph; `onsets_from_text` and
   `onsets_text`; `toggle_computer_keys`, `key_down` with a velocity,
   `sounding_voices`. To the model: `fit_panning`, `centred_panning`,
   `canvas_counts`, `node_slot`, `face_slot`, `output_slot`, `forget` (a
   recent file).
10. **The canvas table** (2.4): `ui_views/canvas_table.h`, a plain
    function from the patch to what the canvas shows, with tests; the
    create menu's content likewise (`create_menu.h`). The canvas clears
    imnodes' wire selection whenever the wires it draws change.
11. **A deleted node's driven setting pins stay on offer**, as an unwired
    one does.
12. **Step 1's mends** (the parity list's A5), built here: the window's
    place on the screen, one click to type a number, the wheel, a bad
    position placed afresh, hints, the node's and the panel header's
    family colour, the wire's width, grey input-only pins, twelve recent
    files with names and tooltips, a save entering them, a missing one
    leaving; the PC Keyboard button, the piano's width, labels and note
    names, velocity from the click, the voice count and the level's
    colours; a node's right-click menu with Delete.

13. **The closing check and its wave.** After the build the reviewer
    checked each of the 58 lines the parity list gives this slice against
    the code (48 done, 9 in part, 1 not done; the list's A6). Closed the
    same day: Note greyed in the create menu on a plain sound; F from
    anywhere; a tooltip on a per-note pin; a Note's node that a file brings
    in unread dropped at open (ruling 8, which the edits apply only when an
    edit removes the last read); both computer keys on a shared piano key;
    the file dialogs remembering their folder (`ui_model/folder_memory.h`).

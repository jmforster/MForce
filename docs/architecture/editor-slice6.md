# The editor, slice 6: shape

Status: **PROPOSED 2026-11-24, for Matt's signature (REVIEW R35). No code
exists and none is written before the signature.**
Parent: `editor.md` (signed 2026-11-23; its three rules, five modules and
limits hold here unchanged). This is the last slice. What it must restore
is row 6 of `editor-parity.md`, A1: about 40 lines of the list, about
2,810 lines of the old editor. The page was written after the old code
was read line by line for each of them, with its numbers, its words and
its own stated reasons (`renders/scratch/ed6/old_read.md`, 1,104 lines, by
a reader that changed nothing). Where the old editor did something odd,
section 4 says so and rules on it.

## 1. What you can do when it is done

1. **Edit a node's tables in the panel**: a column of numbers, or several
   side by side with one row count; a row added, a row removed.
2. **The Envelope**: its stage table, the preset box with Apply, and a
   plot of its shape.
3. **The Curve**: the panel is its editor: points or expressions, the
   three interpolations, the table, the plot.
4. **The Segment and the Shaper**: their point rows and a preview of the
   shape each makes.
5. **The shape editor**: double-click a Curve's, a Segment's or a Shaper's
   preview for a window where you drag the points on a grid. The Shaper's
   curved segments, presets and morph (A and B); the Segment's pulse-train
   generator.
6. **The partials and formant strips** in the Waveforms tab, and the
   Scrub slider that shows the partials at a point of the render.
7. **Edit > Parameter mapping**: the table of what the note drives.
8. **File > Audition**: step through a folder of WAVs, save the patch
   behind one you like to a folder you choose.
9. **A crash log**, when the program dies.

## 2. The interfaces

### 2.1 A shape is points, in the record (`patch/edit.h`)

The old editor wrote a Shaper's curve as a flat list of numbers, so
removing one row turned every later x into a y. A Curve's knots, a
Shaper's curve and a Segment's (width, value) rows are all a list of
pairs, and the record gets one edit for them:

```cpp
struct Point { float x{0}, y{0}; };
// The node's shape as pairs: a CurveNode's knots, a Shaper's curve (A or
// B), a SegmentSource's (width, value) rows.
std::vector<Point> points_of(const Patch&, std::string_view node, std::string_view which);
// The whole shape at once. Knots and a Shaper's curve are kept in order
// of x (sorted here, as the old editor sorted on commit); a Shaper keeps
// at least two points; a Segment's time mode is pinned to what it was
// read as before its first width changes. Change::Structure.
Change set_points(Patch&, std::string_view node, std::string_view which, const std::vector<Point>& points);
```

`set_arrays` (it exists) stays for plain tables. `add_node` gives a new
node the rows its type really starts with (section 4, ruling 6).

### 2.2 A preview is the engine's own answer (one engine header)

The old previews called the engine's own curve and envelope code, and
where they did not (the Shaper's morph blend, the Segment's ghosts) they
drew something the render did not make. The editor's modules may not use
`source`, where that code lives. So the engine exports a few plain
functions that build the one node from the record, as the build does, and
ask it:

```cpp
// What a node's shape is, asked of the engine object the build would make of it.
std::vector<float> curve_values(const Node&, std::span<const float> xs);   // a CurveNode or a Shaper: y at each x
std::vector<float> envelope_shape(const Node&, int points);                // the stages, as the plot draws them
std::vector<float> segment_shape(const Node&, int points);                 // one pass of a SegmentSource
std::vector<float> partial_levels(const Node&, const std::map<std::string, float>& pins);   // a partial set's bars
std::vector<float> formant_gains(const Node&, std::span<const float> hz);  // a formant's gain at each frequency
```

They go in the engine module that already builds a node from the record
(step 1 says whether that is `patch` or `render`; either is within the
reach table as it stands). Nothing that renders changes.

### 2.3 Tables (`ui_control/tables.h`)

```cpp
// before: a table cannot be edited (the panel draws "<name>: N rows")
// after, from a cell being dragged:
edit_cell(session, node, "ampl1", row, value);   // each frame: the record changes and the plot follows
end_gesture(session);                             // at the release: one undo step, the player rebuilt once
```

```cpp
void edit_cell (Session&, std::string_view node, std::string_view column, std::size_t row, float v);
void add_row   (Session&, std::string_view node, std::string_view table);   // the old starting values for the new row
void remove_row(Session&, std::string_view node, std::string_view table, std::size_t row);
void apply_envelope_preset(Session&, std::string_view node, std::string_view preset);
void edit_points(Session&, std::string_view node, std::string_view which, const std::vector<Point>&);   // a row of a Curve, Segment or Shaper
```

### 2.4 The shape editor (`ui_control/shapes.h`)

The old one was a single function of about 930 lines with its state in
file-level variables. Here the state is plain data and each gesture a
function with a test:

```cpp
struct ShapeView {            // the window: which node, and how it is looked at
  std::string node, which;    // empty node: closed
  float x0{0}, x1{1}, y0{-1}, y1{1};   // what the canvas shows
  bool logX{false};
  int  held{-1};              // the point being dragged; -1: none
};
void open_shape   (Session&, std::string_view node);   // "Shape — <label>"; follows a rename; closes when its node goes
void close_shape  (Session&);
void insert_point (Session&, Point at);                // a left click on empty canvas
void move_point   (Session&, int point, Point to);     // a left drag; with Shift the tail slides with it
void delete_point (Session&, int point);               // a right click; never below the type's least
void zoom_shape   (Session&, float notches, bool vertical, Point about);
void pan_shape    (Session&, Vec2 pixels, Vec2 canvas);
void fit_shape    (Session&);
// The Shaper's own:
void bend_segment (Session&, int segment, float amount);   // a curved segment, and its reset
void clear_bends  (Session&);
void shaper_preset(Session&, std::string_view name);       // bow, reed1, reed2, lip, jet, hard, sine, saw, triangle
void clear_shaper (Session&);                              // to the identity
void add_morph(Session&);  void remove_morph(Session&);  void show_curve(Session&, std::string_view which);   // A | B
// The Segment's own:
struct PulseTrain { int pulses; float width, widthRamp, spacing, spacingRamp, peakEnd, varPct; bool bipolar; std::uint32_t seed; };
void make_pulse_train(Session&, const PulseTrain&);
```

One shape editor window, as before. Each drag is one undo step and the
sound follows at its release.

### 2.5 Partials, formants and the scrubber (`ui_control/waveforms.h`)

```cpp
// A strip that is bars or a gain curve, not samples: a partial set's
// levels or a formant's gains, from partial_levels and formant_gains.
struct PartStrip { std::string label, node; enum class Kind { Partials, Formant } kind; std::vector<float> values; };
std::vector<PartStrip> part_strips(const Session&);     // every partial set and formant of the patch, with or without a take
// The partial sets as they stood at `at` (0 to 1) of the take: each pin's
// value read from the take's strip of the node that feeds it, a pin that
// holds a number read as that number.
std::vector<PartStrip> part_strips_at(const Session&, float at);
```

`WaveView` gains `float scrub{0}`.

### 2.6 The mapping table, audition, the crash log

```cpp
// ui_control/mapping.h
struct MappingRow { std::string target, kind, drivenBy, shape; };   // "Target | Kind | Driven by | Shape"
std::vector<MappingRow> mapping(const Patch&);

// ui_control/audition.h
struct Audition {
  bool open{false};
  std::string source, target;            // folders; each remembered between runs
  std::vector<std::string> wavs;         // the source's, sorted
  int at{-1};                            // which is playing; -1: none
  bool repeat{false}, autoAdvance{false};
  std::vector<std::string> saved;        // the target's patches
};
void open_audition(Session&);  void close_audition(Session&);     // closing stops its sound
void set_audition_source(Session&, const std::string& folder);
void set_audition_target(Session&, const std::string& folder);
void play_variant(Session&, int which);  void next_variant(Session&);  void previous_variant(Session&);
void save_variant(Session&, const std::string& name, bool overwrite);   // copies <variant>.patch.json into the target
void retire_saved(Session&, const std::string& file);                    // section 4, ruling 4
void tick_audition(Session&);                                            // once a frame: repeat, or advance, when one ends

// ui_views/file_dialogs.h, added
virtual std::string pick_folder(FileKind) = 0;
```

A WAV is read into a `Take` and played on the Playback line through
`TakeSound`, which resamples it to the output's rate.

The crash log is `ui_app`'s (Windows code is allowed only there): a
handler for a fault and for an exception nobody caught, writing the kind
of fault, its address, the function and the stack to
`%APPDATA%\MForce\mforce_editor_crash.log`.

### 2.7 The views

- `PropertiesPanel`: a column, a table, the Envelope (preset, stages,
  plot), the Curve as its own editor, the Segment's and the Shaper's
  point rows and previews.
- `ShapeWindow` (new): the toolbar, the grid with its labels, the curve
  the engine makes over the grey skeleton being edited, the points, every
  gesture of 2.4, the confirm boxes.
- `WaveformsView`: the part strips and the Scrub slider.
- `MappingWindow` and `AuditionWindow` (new), opened from Edit and File.
- `HeadlessEditor` learns a drag with Shift held, a drag of the right
  button and of the middle button, and the wheel with Shift. A test's
  `FileDialogs` answers `pick_folder` with a folder the test made. Every
  gesture gets a test that moves the mouse or presses the key, found by
  name, checking what must still hold afterwards, with its handler
  switched off once to see the test fail.

## 3. Steps

1. **Read, probe and count** (read only): this page against today's
   code, block by block; two probes the read left open (what the build
   does with an Envelope's `sustainLevel` beside its stages; what it does
   with an ExplicitPartials' second columns while "evolve" is off); which
   measured patches, if any, hold a shape that breaks a rule of 2.1.
2. **The record and the previews** (engine): `points_of`, `set_points`,
   the starting rows, the five preview functions.
3. **The control**: tables, shapes, the part strips and the scrubber,
   the mapping, audition.
4. **The panel**: tables, the Envelope, the Curve, the Segment and Shaper
   rows and previews.
5. **The shape editor window.**
6. **The part strips and the scrubber; the mapping window; the audition
   window.**
7. **The crash log.**
8. **Review, fix wave, the parity check and its wave, report, on your
   desk.**

Two executors, the hand-over planned after step 4.

## 4. The rulings the signature accepts

The first five change what you see, hear or keep most.

1. **A table or a shape dragged is heard when you let go.** While you
   drag, the record changes and the plot follows your hand; at the
   release it is one undo step and the keys' player is rebuilt once
   (about 30 ms; a note sounding then is cut, as after any edit of the
   patch's structure today). The old editor wrote into its own copy every
   frame and you heard the change at the next note. Hearing it during the
   drag would need the engine to take a table while it plays, which is
   engine work this slice does not do.
2. **A preview shows what renders.** Every plot, preview and strip asks
   the engine object built from the node (2.2). That mends four old
   previews that did not: the Shaper's, which ignored its curved segments
   and its morph; the morph blend, whose arithmetic was not the engine's;
   the Curve's plot, which was not drawn at all when the first knot sat at
   x = 0 (six velocity curves in the three pianos, which therefore could
   not open the shape editor); and the Segment's ghosts, which read a
   stale number when their pin was wired. Where a pin is wired and its
   value cannot be known without a render, the ghost or blend is not
   drawn, and the window says so.
3. **The Shaper's curve is point rows in the panel**, like the Segment's.
   The old panel showed it as one flat list of numbers with a delete that
   removed one number, turning every later x into a y: the very fault its
   own Segment code had been mended for ("the generic per-float delete
   shifted the tail by one, turning widths into values").
4. **Audition never deletes a file.** The old Delete in the saved list
   removed the file from disk for good, after Enter. Your rule for the
   patches tree is that a rejected patch is moved to `patches/old/`,
   never deleted. So here Delete, then Enter, MOVES the file to
   `patches/old/` and the status says where it went. The old default
   target folder, `patches/curated`, no longer exists: there is no
   default, and Save waits until you have chosen a target (it is
   remembered).
5. **The crash log goes to `%APPDATA%\MForce\`**, not the folder the
   program was started in (which is the repository's root: the design's
   rule is that the editor writes nothing there). It holds the stack
   where the fault or the throw happened; the old one's stack for a C++
   exception was taken after the stack had unwound.
6. **A new node starts with the rows its type really has.** Today only
   the Envelope and the Curve do. A new ExplicitPartials shows "0 rows"
   while its build plays sixteen partials, and one "Add row" would
   replace the sixteen with one. `add_node` gives every type the rows its
   engine object starts with, as the old editor did. This is a change to
   the engine's `patch` module; it changes what a NEW node holds, never a
   file.
7. **The rules of a shape are kept by its edit, not by the file's
   reader.** `set_points` sorts knots, keeps a Shaper's two points and
   pins a Segment's time mode before its first width changes (the old
   panel lacked that guard and the shape editor had it: "an edit that
   pushed the first width under 1.0 ... silently flipped the whole shape
   to seconds — heard as silence"). `check()` does not change: a file
   that breaks a rule opens, as before, and step 1 says whether any
   measured file does.
8. **A typed cell is held to its column's declared range, as before**; a
   Segment's widths are left free, as before; a cell you do not touch is
   left as the file has it (91 formant gains in the cello and the viola
   sit above their declared 0 to 1, and stay there).
9. **In the shape editor a click selects a point without moving it**, and
   nothing is written until it moves. The old one jumped the point to the
   cursor and wrote on every frame the button was down.
10. **Revert goes; Undo does its work.** The old one-step Revert existed
    because, in its own words, the editor had no undo stack. The presets,
    Clear overrides and Clear to identity stay, with their confirm boxes.
11. **The mapping table keeps its old rule**, which lists every pin whose
    signal comes from a field of the note through anything, up to six
    nodes back. Run again on today's files by the reader, that rule gives
    508 rows over the measured patches, 19 on the oboe, about half of
    them through a node that is not a curve. Say if it should list only
    what a note's field drives through curves.
12. **Audition plays a WAV at its own rate** (the old one played every
    file at the sound card's), on the line Play uses: the transport's
    Stop or Play ends the audition (in the old one, as its code reads, it
    made the audition skip to the next file).
13. **The partials and formant strips are drawn with or without a
    render**, as before, from the node as it stands. Under the Scrub
    slider a pin that holds a number reads as that number (the old one
    read 0). The formant strip does not follow the scrubber (the old one
    captured its snapshots and never used them).
14. **The engine gains** the points edit, the starting rows and the five
    preview functions, and nothing that renders changes.
15. **For tests**: a folder picker beside the file dialogs; WAVs and
    patches in folders the test makes; no test moves or writes a file
    outside those. The crash log's writing is tested on a fault record
    made by hand; no test crashes the test program.

## 5. After this slice

Nothing of the old editor is left to restore. By your words of
2026-11-23 the refactor is then done, the pause for outside eyes on the
code follows, and features resume on your word.

## 6. For the C# reader

`struct Point { float x{0}, y{0}; };` and `std::vector<Point>` in place of
two parallel lists of numbers is the idea to notice. The record stores a
Curve's knots as two columns, `x` and `y`, because that is how the file
has them and how the engine reads them. An editor that edits the columns
directly can leave them out of step: delete a row from one and not the
other, sort one and not the other. The old Shaper panel did exactly
that. `points_of` and `set_points` are the only way the editor touches a
shape, and they take and give whole pairs, so a pair cannot be split. In
C# this is a `List<(float x, float y)>` over two `float[]`: the type
makes the mistake impossible to write, where a comment would only ask
you not to make it.

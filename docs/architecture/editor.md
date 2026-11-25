# The new editor

Status: **signed 2026-11-23 ("Signed, proceed"), with the corrections of
section 12. Slice 1 was built the same day (report R26,
`docs/autonomy/refactor/reports/2026-11-23-editor-slice1.md`); section 13
says where it differs from the signed text. The later slices are not
designed: each gets a page like section 4 for a signature first.**
Parent: `target-architecture.md` section 4 (the UI, decided 2026-10-05)
and the 2026-11-21 reset in `CLAUDE.md` ("written fresh on the `patch`
module ... its logic is operations on plain data with tests; the window
is a thin layer over them"). Written in the shape of Parts 5, 6 and 8.
The old editor is gone, so "before" below means what it did, from its
last version in git (`9f084c9^:tools/mforce_ui/main.cpp`, 13,506 lines).

## 1. What it stands on, from the code

Everything the first slice needs from the engine exists, less two small
additions (section 4):

| The editor needs | The engine has |
|---|---|
| A patch as plain data it can read, change, compare and copy | `Patch` (`patch/patch.h`): graph, instrument block, layout; `operator==`; `check()` |
| To read and write the file | `load_patch`, `save_patch`: the one codec |
| To know what a node type has, without a class per type | `type_declarations(type)`: params, inputs, settings (with enum labels, min, max), arrays |
| Where each node sits | `Layout`: positions, groups, note faces, saved in the file |
| To play it live | `LivePlayer(patch, voices)`: `note_on`, `note_off`, `fill()` for the audio thread; allocates nothing while playing |
| To render it offline with per-node strips | `play(patch)`, `NodeCapture` |

Measured today: reading and building all 131 library and baseline
patches takes 0.67 s, about 5 ms each, at one voice for most of them. A
live player builds sixteen voices, so a rebuild after an edit is tens of
milliseconds: affordable at the speed a person edits, too slow to do on
every frame of a drag. Section 4.2 follows from that, and slice 1
measures it on the largest library patches.

## 2. Why the old one lost its shape, and the rule that prevents it

The target architecture's diagnosis (4.1): imnodes only draws, nothing
supplied a model, so the model became file-level variables, more than a
hundred of them, and an edit was serialize, parse, rebuild every voice.

Before, a value edit (the old editor, in outline):

```cpp
static std::vector<GraphNode> s_nodes;            // one of more than a hundred file-level variables
...
if (ImGui::DragFloat(label, &pin.value))          // inside a draw function hundreds of lines long
    pin.constantSrc->set(pin.value);              // the widget reaches into the live DSP object
mark_graph_dirty();                               // 45 call sites; later: serialize to JSON text, parse, rebuild
```

After:

```cpp
// ui_views: the widget asks for an edit and knows nothing else
if (ImGui::DragFloat(label, &v, speed, pin.min, pin.max))
    edit_value(session, node.id, pin.name, v);

// ui_control: one place applies it
void edit_value(Session& s, std::string_view node, std::string_view pin, float v) {
    set_value(s.open.patch, node, pin, v);        // the engine's edit of the plain record; throws if refused
    s.open.dirty = true;
    s.live.value_changed(node, pin, v);           // the sounding voices hear it, no rebuild
}
```

The rule, in three parts, each enforced by the build and not by care:

1. **The patch record is the one truth.** Views draw from it and never
   hold a copy. There is no second graph structure.
2. **A view changes nothing itself.** It calls an edit function. Every
   edit function is plain code over plain data, with tests that need no
   window.
3. **No module can reach past its neighbour**: a view cannot open the
   audio driver's header, and the audio module cannot open ImGui's,
   because their targets are not given those folders (Part 8's
   mechanism).

## 3. The modules

The five modules of the target architecture (4.2), in a new top-level
folder, each made with Part 8's `mforce_module`:

```
editor/
  ui_model/    include/mforce/ui_model/   src/  tests/
  ui_audio/    ...
  ui_control/  ...
  ui_views/    ...
  ui_app/      main.cpp                   (the executable, mforce_editor)
```

| Module | Holds | May use | Libraries |
|---|---|---|---|
| `ui_model` | `OpenPatch`: the patch, its file, whether it has unsaved changes; later undo | patch | none |
| `ui_audio` | `AudioAdapter`: the output device, the callback, which player is sounding | patch, render, contract | RtAudio; later RtMidi |
| `ui_control` | `Session` (the editor's whole state, one object); the edit functions the views call; `LivePlay` (keys to notes, edits to sound) | `ui_model`, `ui_audio`, patch, render, contract | none |
| `ui_views` | One class per window; in the first slice the node canvas, the properties panel, the keyboard, the menu and the status line | `ui_model`, `ui_control`, patch, core | Dear ImGui, imnodes |
| `ui_app` | `main`: the window, the frame loop, settings, recent files | all of the above | GLFW, ImGui's backends |

- **A library is linked privately.** Only the module named in the last
  column can open that library's headers: RtAudio is included in
  `ui_audio`'s `.cpp` files and nowhere else, ImGui in `ui_views`' and
  `ui_app`'s. So no public header of the editor mentions a library's
  type, and replacing a library later (JUCE for the window or the
  device) touches one module. `mforce_module` gains one keyword for it,
  `LIBS`.
- **What the compiler cannot refuse:** `ui_views` uses `ui_control`,
  whose headers include `ui_audio`'s and, through them, render's and
  contract's. So the compiler would let a view include
  `audio_adapter.h` or `live_player.h` (never RtAudio itself). The
  structure gate refuses each, as it does play including voicing.
- **Names.** `OpenPatch`, `AudioAdapter` and `LivePlay` are the target
  architecture's. New here:
  - `Session`, the one object that holds the open patch, the audio
    adapter, the live play and the selection. Alternatives: `Workspace`,
    `EditorState`. ("Document" is ruled out since 2026-10-05.)
  - `mforce_editor`, the program. Alternatives: `mforce_ui` (the deleted
    program's name; scripts that drove its headless modes would mistake
    the new one for it), `mforce`.

## 4. The interfaces

### 4.1 Edits are the patch module's own functions (new: `patch/edit.h`)

The target architecture put the edits in the engine ("the edits
themselves are `Patch`'s own operations"). First slice:

```cpp
// What a host must do about an edit it has applied.
enum class Change { Value,       // a pin's constant: the sound can follow without a rebuild
                    Structure,   // anything the build reads: build again
                    Layout };    // the editor's own data: the sound is untouched

// Each edit applies whole, or throws std::runtime_error naming the node,
// the name and the reason, and leaves the patch as it was.
Change set_value  (Patch&, std::string_view node, std::string_view pin, float v);       // refused on a pin a wire feeds
Change set_setting(Patch&, std::string_view node, std::string_view name, float v);      // refused outside the declared range or labels
Change set_string (Patch&, std::string_view node, std::string_view name, std::string v);
Change move_node  (Patch&, std::string_view node, Vec2 pos);
```

- They are free functions over the plain record, like `check()`'s rules,
  and they read the same declarations table to decide what is allowed.
- Later slices add to the same header: `add_node`, `remove_node`,
  `connect`, `disconnect`, `rename_node`, `paste` (a `Fragment`),
  `make_instrument`, `make_plain_sound`.
- Alternative: methods on `Patch`. A free function keeps `Patch` a
  struct anyone can build in a test, and one header lists every edit
  there is.

### 4.2 A tweak reaches a sounding note (one addition to `LivePlayer`)

Part 5's design left this for here: "a message that changes a sounding
voice's setting (the editor's tweak-and-hear) is one more row in the
table".

```cpp
// render/live_player.h, control thread
// The constant of `pin` on `node`, in every voice: the voices that are
// sounding hear it within one buffer, the others when they next start.
// False when the player cannot follow it: the caller builds a new player.
bool set_value(std::string_view node, std::string_view pin, float v);
```

| Message | Direction | Carries | The audio thread does |
|---|---|---|---|
| Set (new) | control to audio | which constant, the value | writes it in every voice it holds |

The control thread keeps the values set since the player was built and
writes them into a voice when it starts it. So a voice gets a value from
the message if it is sounding and at its next start if it is not, and
the rule "a voice is touched by one thread at a time" is unchanged.

What each kind of edit does to the sound:

| `Change` | What `LivePlay` does | Heard |
|---|---|---|
| `Value` | `player.set_value(...)`; when that returns false, as `Structure` | while the note sounds, as the slider moves |
| `Structure` | builds a new `LivePlayer` from the patch and hands it to the adapter | from the next note; notes sounding at the swap fade over 5 ms |
| `Layout` | nothing | |

- A value that a node reads only when a note starts (an envelope's
  stage times, say) is heard on the next note. That is the engine's
  behaviour, not a rebuild.
- A build that fails (it can, once wiring exists) keeps the last good
  player sounding and shows the engine's sentence in the status line.
  The patch keeps the edit.
- Every setting edit is `Structure` in the first slice. If dragging a
  setting feels slow, a non-structural setting becomes a second message
  row; the first slice measures before deciding.
- The player cannot follow a constant inside the template of a
  `MultiplexSource`: that node builds its clones itself, out of the
  voice's reach. `set_value` returns false there and the player is
  rebuilt. No library or baseline patch has a `MultiplexSource` today.
  (A pin a wire feeds never gets this far: `patch/edit.h` refuses it.)
- A built voice holds a pin's constant in one of two forms: a param's
  own number, or a `ConstantSource` wired to an input. `set_value`
  writes whichever the pin has.

### 4.3 The audio adapter (`ui_audio`)

```cpp
class AudioAdapter {
public:
  AudioAdapter();                                  // no device yet: play() and the callback work, nothing is heard
  ~AudioAdapter();
  // The default output: stereo, 512 frames, at 48 kHz or the nearest rate
  // the device has. Throws nothing: when it fails, error() says why.
  void open();
  int  sample_rate() const;                        // the device's rate; 48000 before open()
  // Control thread. The player that sounds from the next buffer; the one
  // before it fades over 5 ms and is destroyed later on this thread,
  // never on the audio thread.
  void play(std::unique_ptr<LivePlayer> player);
  LivePlayer* player();                            // the current one, for note_on and set_value; null before the first
  float peak() const;                              // the output's recent peak, for the keyboard's meter
  std::string error() const;                       // why there is no sound, in words; empty when there is
private:
  struct Device;                                   // RtAudio lives behind this, in the .cpp
  std::unique_ptr<Device> device_;
};
```

- The callback is one function that the tests call directly with a
  buffer: no device is needed to test a swap, the fade, or that the
  callback allocates nothing (the counting allocator of
  `engine/testing`).
- `fill()` is mono; the adapter writes it to both channels.
- `main` calls `open()` once (`session.audio.open();`). No test does,
  so a `Session` made in a test never touches the sound card.
- **Live play is at the device's rate.** `LivePlay` builds each player
  from the patch with its `sampleRate` replaced by the adapter's. The
  file's `sampleRate` is the standalone render's and no edit changes
  it; when the two differ the status line says so. Of the 131 library
  and baseline patches 129 are at 48000; the other two
  (`double_advance_bug`, `double_advance_ok`) are at 22050.

### 4.4 The model and the session

```cpp
// ui_model/open_patch.h
struct OpenPatch {
  Patch       patch;
  std::string path;          // empty for a patch never saved
  bool        dirty{false};  // changed since it was read or saved
};
OpenPatch open_patch(const std::string& path);                 // load_patch; throws its message
void      save(OpenPatch& open);                               // save_patch; clears dirty
void      save_as(OpenPatch& open, const std::string& path);

// ui_control/session.h: the editor's whole state. main owns one.
struct Session {
  OpenPatch    open;
  AudioAdapter audio;
  LivePlay     live;         // keys to notes; edits to the player
  std::string  selected;     // a node id, or empty
  std::string  status;       // the last thing worth telling: a refusal, a build warning
};
```

- **Undo comes with slice 2** and is small because the patch is a plain
  value: a stack of `Patch` copies, one pushed per finished edit (a
  drag is one edit, pushed when the mouse lets go).
- `LivePlay` turns a key going down into a `PerformedNote` (held while
  the key is down on an instrument that sustains) and a key going up
  into `note_off`. Part 5 said the note-at-a-time performer "belongs to
  perform". Today it would decide nothing a three-line function cannot,
  so it stays in `LivePlay` until it has a decision to make (legato
  from overlapping keys, an onset by context) and moves then.

## 5. The window

- **One class per window**, each with one public function,
  `void draw(Session&)`. A view reads the session, draws, and calls edit
  functions. It keeps only what belongs to drawing (which tree node is
  open, the scroll position) as members.
- **Every node type is drawn by one generic node**, from
  `type_declarations`, as the old editor did. That part of it was
  right.
- **How ImGui code stays inside the limits.** An ImGui window function
  grows without bound unless it is cut: each view is its own file, a
  `draw` is a short list of calls to small private functions (one per
  section of the window), and the structure gate fails the build at 80
  lines a function and 600 a file. The editor's folders start with no
  baseline entries, so there is no allowance to grow into.
- **No file-level variable.** `main` creates the window, the ImGui and
  imnodes contexts and one `Session`, and passes them down.
- **Drawn without a window.** `ui_views` has one more function,
  `draw_once_headless(Session&)`: it makes an ImGui context with no
  backend, draws every view for one frame and destroys the context. A
  test calls it for every library patch, so "the editor draws this
  patch without crashing" is checked by the gates and not by you.
- **Never linked over the running program.** Windows lets a running
  `.exe` be renamed but not overwritten. The build renames an existing
  `mforce_editor.exe` aside before it links, so a build never fails
  because the editor is open, and you restart it when you choose.

## 6. The libraries

Before: five libraries cloned by hand into `engine/third_party/`, never
tracked; a fresh clone could not build the editor, or (until Part 8)
anything.

| Library | On your disk now | Used by |
|---|---|---|
| Dear ImGui | 1.92.7, docking branch, commit `f5f6ca07b` | `ui_views`, `ui_app` |
| imnodes | commit `b2ec254` | `ui_views` |
| GLFW | 3.5.0 development, commit `b00e6a8` | `ui_app` |
| RtAudio | 6.0.1 | `ui_audio` |
| RtMidi | 6.0.0 | `ui_audio`, from the MIDI slice |

After: git submodules under a top-level `third_party/`, each pinned to
the commit above (the ones the old editor ran on).

- A plain `git clone` gets the engine and builds it, as today. The
  editor is built when the submodules are there
  (`git submodule update --init`), and configure says in one line which
  it found.
- The libraries' own code is compiled without our warning rules and
  kept out of the structure gate; their headers are system headers to
  our code, as `json.hpp` is.
- Alternatives: download at configure (CMake `FetchContent`), which
  makes every first configure need the network; or copy the sources
  into the repository, which adds more than 100,000 lines of other
  people's code to a tree whose own code is 59,000.
- Adding the submodules writes five GitHub addresses into
  `.gitmodules` and fetches from them. I do that only on your word,
  which the signature gives.

## 7. Tests and proof

| Where | What is tested | Needs a window or device |
|---|---|---|
| `patch_tests` | every edit function: applies, refuses with its sentence, leaves the patch whole on refusal; an edited patch passes `check()` | no |
| `render_tests` | `set_value`: a note after it equals the same note on a player built from the edited patch, sample for sample; a sounding note changes within one buffer; nothing is allocated | no |
| `ui_model_tests` | open, save, save as, dirty | no |
| `ui_audio_tests` | the callback: a swap fades the old player and starts the new; no allocation | no |
| `ui_control_tests` | each edit reaches the patch, the dirty flag and the player; keys to notes | no |
| `ui_views_tests` | every library patch draws one frame headless | no |

Proof per slice is what the reset set: the build, `python tools/gates.py`
and a review of the slice. CTest finds a new module's tests. The
structure gate does not see a new top-level folder until it is told: it
scans `engine/` and `tools/`. So step 1 adds `editor/` to the folders it
scans and the five modules to `tools/structure/modules.json` (their
folders, what each may include, which module may open which library),
and a tool test holds that no file under `editor/` has a baseline
entry. The editor's limits fail from its first commit only because of
that step.

What only you can prove: that it feels right. Each slice ends with the
program on your desk and a short list of what to try.

## 8. The slices

**Slice 1: open, play, tweak, hear, save** (your words). When it is done
you can:

1. Start `mforce_editor`, open a patch (the menu, a recent file, or a
   path on the command line).
2. See its graph: every node at its saved position, with its wires.
   Groups and note faces are kept in the file and drawn in a later
   slice; until then their nodes are shown plainly.
3. Play it from the computer keyboard or an on-screen one, eight notes
   at once, with the output's peak shown.
4. Click a node and change its pins' constants and its settings (number,
   switch, choice). A constant changes the note that is sounding.
5. Save, save as. The title shows the file and whether it has unsaved
   changes; closing with unsaved changes asks first.

It plays instruments. A plain sound (no instrument block) opens, shows,
edits and saves in slice 1 and gets its sound with slice 4.

Steps, each ending at the gates: (1) the submodules, the `LIBS` keyword,
the five empty modules, the structure gate taught the new folder, and a
window that opens; (2) `patch/edit.h` and
its tests; (3) `LivePlayer::set_value` and its tests; (4) `ui_model`,
`ui_audio`, `ui_control` with their tests, no window; (5) the views and
the headless draw test; (6) on your desk.

**After it.** Superseded 2026-11-23 by `editor-parity.md`: the list of
everything the old editor did, which is what "done" is measured against
(Matt: done is "when all the capability that existed previously is
restored"). Its part A1 has the slices as now ordered and what each must
restore; the table below is the first guess, kept for the record. Slice
2's page is `editor-slice2.md`.

**The first guess at the order:**

| Slice | You can then |
|---|---|
| 2. Build | add and delete nodes from the registry's menu, wire and unwire (taps too), undo and redo, start a new patch, see the note face as one node |
| 3. See | watch each node's waveform and the output's, live; the spectrum |
| 4. Generate | render the patch's score, or a note, passage, chords or drums typed in the transport, through the engine; play it back; plain sounds sound |
| 5. Arrange | groups, copy, cut, paste, duplicate, replace-with, the theme file, MIDI in |
| 6. Shape | the shape editor for envelopes and tables, the partials and formant strips, audition, the mapping table, the crash log |

CLAUDE.md says the freeze lifts for sound work "once the editor's first
slices are usable". My read is after slice 3; that is yours to say.

## 9. The rulings the signature accepts

1. **The same libraries as before**: Dear ImGui with imnodes, GLFW,
   RtAudio, RtMidi (section 6). The old editor's fault was having no
   model, not what drew it.
2. **Git submodules, pinned**, under `third_party/`, and my adding them
   (section 6).
3. **Five modules under `editor/`**, with the reach table of section 3;
   libraries linked privately.
4. **`Session` and `mforce_editor`** as names (section 3).
5. **Edits are free functions in `patch/edit.h`** that return what kind
   of change they were (4.1).
6. **`LivePlayer::set_value`**: a constant follows the slider with no
   rebuild; every other edit, and a constant the player cannot follow,
   rebuilds the player and swaps it (4.2).
7. **No perform-module key performer yet** (4.4).
8. **The headless draw test** as the views' proof (section 5).
9. **Slice 1 as section 8 lists it**: instruments only for sound; groups
   and note faces drawn later; undo in slice 2.
10. **Live play at the device's rate**; the file's `sampleRate` stays
    the render's (4.3).

## 10. Not in this design

- The later slices' interfaces. Each gets a page like section 4 before
  its code, for your signature.
- Where settings and recent files live, the colour theme, key bindings:
  decided in the slice that needs them and recorded, not asked.
- Anything the old editor did that the target architecture deleted: the
  node-graph mode flag, the legacy parts of the mapping dialog, the
  second clipboard, its own streaming DSP graph, the headless
  comparison modes.

## 11. For the C# reader

ImGui is an *immediate-mode* interface, the opposite of WinForms or WPF.
There a button is an object that lives in a tree and raises an event.
Here there is no button object: every frame, sixty times a second, the
program calls `ImGui::Button("Save")`, which draws it and returns true
on the frame it was clicked. Nothing is kept between frames but what the
program itself keeps.

That is why the model matters more, not less. A retained toolkit gives
you a place to put state (the control), and data binding to keep it in
step with your model. An immediate-mode one gives you nowhere, so state
goes wherever the drawing code can reach, which in the old editor was
file-level variables. The cure is the one section 2 states: the
drawing code is handed one object, reads it, and asks for changes
through functions. The nearest C# pattern is Model-View-Update (Elm,
or a Redux store): the view is a function of the state, and every
change is a named function that takes the state and an input.

## 12. Corrected before the signature (2026-11-23)

Each interface block was run against the engine's headers and against
this design's own rules before asking for the signature. Five things
changed. The sections above carry the corrected text; this is what they
said before.

1. **The adapter's constructor opened the sound card** (4.3). `Session`
   holds an `AudioAdapter`, so every test that made a `Session` would
   have opened the device, against section 7's "needs a device: no".

   ```cpp
   // before
   AudioAdapter();          // opens the default output: 48 kHz, stereo, 512 frames
   Session session;         // main, and every ui_control test: the device opens

   // after
   AudioAdapter();          // no device yet: play() and the callback work, nothing is heard
   void open();             // the default output; on failure error() says why
   Session session;         // a test stops here
   session.audio.open();    // main only
   ```

   Alternative: `Session` holds a reference to an adapter `main` makes.
   Then every test builds two objects to get one, for no gain.

2. **A constant the player cannot reach** (4.2). The design said
   `set_value` is false only for a pin a wire feeds. A `MultiplexSource`
   builds the clones of its template inside itself, where the voice's
   node list does not reach, so a constant on a template node would
   have changed in the patch and not in the sound.

   ```cpp
   // before: false when the patch has no such constant (the pin is fed by a wire)
   // after:  false when the player cannot follow it: the caller builds a new player
   bool set_value(std::string_view node, std::string_view pin, float v);

   // LivePlay, on a Change::Value
   if (!player.set_value(node, pin, v)) rebuild();
   ```

   No library or baseline patch has a `MultiplexSource`, so nothing in
   the measured set takes the rebuild path today.

3. **The rate was not decided** (4.3, ruling 10). The adapter opened at
   48 kHz and a `LivePlayer` runs at its patch's `sampleRate`; a patch
   at another rate would have played at the wrong pitch. Now the player
   is built at the device's rate and the file's rate stays the render's.
   Two baseline patches are at 22050; the other 129 are at 48000.
   Alternative: reopen the device at each patch's rate, which depends
   on what the sound card will open.

4. **"The gates already cover a new module" was half true** (section 7,
   step 1). CTest finds a new module's tests. The structure gate scans
   `engine/` and `tools/` only (`SOURCE_GLOBS` in
   `tools/structure/check.py`) and knows a module only from
   `modules.json`, so the editor's limits would have measured nothing.
   Teaching it the folder is now part of step 1, with a test that the
   editor has no baseline entry.

5. **More than one pair the compiler cannot refuse** (section 3). A
   view is handed render's and contract's folders as well as
   `ui_audio`'s, through `ui_control`. The structure gate refuses all
   three; the sentence named only one.

## 13. Added while writing the steps (2026-11-23, after the signature)

Writing each step's brief against the code turned up things the signed
text did not have. None changes a ruling of section 9. Each is small and
reversible; say so if you want any another way.

1. **Most patches have no saved positions.** Of the 131 library and
   baseline patches, 114 have none and 16 lack some. Slice 1's "every
   node at its saved position" would have stacked them in one spot. So
   `ui_model` gets one more function, and opening a file calls it:

   ```cpp
   // A position for every node that has none: columns by distance from the
   // output. A node that has a position keeps it.
   int place_unplaced_nodes(Patch& patch);
   ```

   The positions go into the patch record (the record stays the one
   truth), are not an unsaved change by themselves, and are written by
   the next save.

2. **Two more layout edits** in `patch/edit.h` (4.1), because the layout
   also holds the canvas's Output node and its panning:

   ```cpp
   Change move_output(Patch&, Vec2 pos);
   Change set_panning(Patch&, Vec2 pos);
   ```

   Panning is kept in the patch and saved with it, and does not by
   itself mark the patch unsaved.

3. **The session holds three more plain things** (4.4):

   ```cpp
   struct Session {
     OpenPatch    open;
     AudioAdapter audio;
     LivePlay     live{audio};
     Keyboard     keyboard;     // new: the octave, the velocity, a struck note's length
     RecentFiles  recent;       // new: the files opened most recently
     std::string  selected;
     std::string  status;
     Unsaved      unsaved;      // new: a close or an open waiting for the answer about unsaved changes
   };
   ```

   Opening another file asks first when there are unsaved changes, as
   closing does.

4. **The computer-key table is yours from the old editor**, copied as
   data: the two zones (2026-09-13), Up and Down for the octave, Left
   and Right to halve and double a struck note's length.

5. **How the player finds a constant** (4.2): the build now lists where
   each pin's constant lives (`BuiltGraph::constants`), and leaves out
   the nodes inside a `MultiplexSource` template. That list is what
   makes `set_value` false there.

6. **The adapter's callback is public** (4.3), as `fill(float* stereo,
   int frames)`: the device calls it and so do the tests. `play()`
   refuses a player that is not at the adapter's rate.

7. **The platform's file dialogs sit behind an interface** the views are
   given (`FileDialogs`: `pick_open`, `pick_save`), implemented in
   `ui_app` with the Win32 dialogs. So no view contains Windows code and
   the headless test passes a stand-in.

8. **Where the editor keeps its own files**: `%APPDATA%\MForce\`
   (`recent_files.txt`, ImGui's `imgui.ini`). Nothing is written into
   the repository. The recent list is a plain text file, one path per
   line, so no editor module reads JSON.

9. **Two options on the program for proof, not for use**:
   `mforce_editor --frames N` draws N frames and exits (the real window
   and sound card come up), and `--shot FILE` saves the last frame as a
   picture, which is how the session that builds a slice looks at what
   it drew before it reaches your desk.

10. **RtMidi is pinned to the tag `6.0.0`**: the copy on your disk had no
    git history to take a commit from.

11. **An Envelope's `trigger` constant is not followed live** (4.2, found
    by step 3's census of every node type and pin that holds a constant
    in the library and baselines: 88 pairs, 87 followed sample for
    sample). A voice reads that one number when it is built, to decide
    which envelopes are on its trigger list (`voice.cpp`), so writing it
    later would not equal a rebuild. `set_value` is false for it and the
    player is rebuilt, as for a constant inside a Multiplex template.
    The other way, putting every Envelope on the trigger list, changes
    nothing a render plays but changes what the voice reports about
    itself; it is in CLEANUP as a suggestion for after the refactor.

12. **The measurement section 1 promised, and what follows from it**
    (4.2's last bullet). Rebuilding the live player for an edited patch
    takes 29 ms at the median over the 104 library and baseline
    instruments and 36 ms for the slowest (`sing_tenor_U`), sixteen
    voices, Release, on this machine. A rebuild also cuts the notes that
    are sounding. So a setting's drag field applies once, when the drag
    ends, and a checkbox or a choice at once; a pin's constant follows
    the drag on every frame with no rebuild, as designed. If a setting
    should be heard while it is dragged, that is the "second message
    row" of 4.2, for a later slice.

13. **What the pictures and the review added** (the fix wave).
    - **Arrange.** 14 of the 131 patches, `oboe1` and `piano_default`
      among them, were saved with groups or note faces (7 of them with
      groups; counted again for slice 5). Their members' positions were
      made for the old editor's collapsed groups and overlap when the
      nodes are shown plainly, as this slice shows them. Nothing is
      rearranged on open, so a save after a tweak keeps your layout.
      `View > Arrange nodes` lays every node out afresh when you ask:

      ```cpp
      int  arrange_nodes(Patch& patch);   // ui_model: every node and the Output node, by the placement rule
      void arrange(Session& s);           // ui_control: an unsaved change; the sound is untouched
      ```

    - **A node list at the top of the properties panel**, so a node
      hidden under another can be selected and tweaked without moving
      anything. And imnodes' minimap in a corner of the canvas.
    - **A number on a list pin is a legal edit.** The first version of
      `set_value` refused one. The engine reads, checks and plays it
      (the review's one Major), so the edit accepts it.
    - **Once a frame, `keep_up(Session&)`**: a value the player's queue
      could not take is sent, and a player the output has finished with
      is destroyed then and not at the next key press.
    - **`Session::opened`** counts the files opened, so a view drops the
      drawing state it kept for the patch before.

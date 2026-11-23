# The editor, slice 1: open, play, tweak, hear, save (report R26)

2026-11-23, refactor session XXVI. Design: `docs/architecture/editor.md`,
signed that day ("Signed, proceed"); its section 12 holds the five
corrections made before the signature and section 13 the thirteen things
added while building. Base `fcd73c2`; commits `947d021`, `dc3ebf3`,
`3414a43`, `737583a`, `e40ad75`, fix wave `98603b6`. Fable 5.2 wrote the
design and the briefs, read each report and diff, looked at the pictures
and committed; one Opus 5.5 executor built all five steps and the fix
wave; a second Opus 5.5 agent reviewed the whole range.

## 1. What is on your desk

From the repository root:

```
build\editor\ui_app\Release\mforce_editor.exe patches\library\winds\oboe1.json
```

(or with no argument, then File > Open). REVIEW R26 has the list of what
to try. What it does:

- **Open** a patch from the menu, the recent list or the command line.
- **See** its graph: every node drawn the same way from what its type
  declares, with its wires (a tap in orange, a per-note pin in green),
  the numbers on its pins, the Output node, a minimap.
- **Play** it from the computer keyboard (your two zones, arrows for
  octave and note length) or the on-screen piano, eight notes at once.
- **Tweak**: click a node, or pick it from the list at the top of the
  panel. Drag a pin's number and the sounding note follows. Change a
  setting and the next note has it.
- **Save**, save as. The title shows the file and a star when there are
  unsaved changes; closing or opening another file asks first.

What it does not do yet, by the design: add, delete or wire nodes; undo;
draw groups and note faces; show waveforms; play a plain sound (one with
no instrument block opens, shows, edits and saves, silently); edit a
table or an envelope's shape.

## 2. The shape

```
editor/
  ui_model/    the open patch, its file, unsaved or not; placement; recent files      315 lines
  ui_audio/    the sound card and the player that is sounding                         326
  ui_control/  Session (the whole state), what a view may ask for, keys to notes      618
  ui_views/    one class per view; each draws and calls ui_control                    983
  ui_app/      main, the window, the file dialogs                                     576
third_party/   Dear ImGui, imnodes, GLFW, RtAudio, RtMidi as pinned git submodules
```

2,818 lines in 56 files, and 1,193 lines of tests in 12. The largest file
is 206 lines (the properties panel) and the longest function outside
tests 34. No file under `editor/` has an entry in the structure gate's
baseline, and a test fails if one ever gets one.

A value edit, end to end (the design's section 2, as built):

```cpp
// ui_views/properties_panel.cpp: the widget asks and knows nothing else
if (ImGui::DragFloat(label, &v, speed, lo, hi)) edit_value(s, n.id, name, v);

// ui_control/edits.cpp: one place applies it
void edit_value(Session& s, std::string_view node, std::string_view pin, float v) {
  ...
  if (!applied(s, [&] { set_value(s.open.patch, node, pin, v); }) || same) return;   // the engine's edit; a refusal goes to the status line
  changed(s);                                                                         // unsaved; the status line cleared
  tell(s, s.live.value_changed(s.open.patch, node, pin, v));                          // the sounding voices hear it, or the player is rebuilt
}
```

In the engine, two additions and nothing changed:

- `patch/edit.h`: six edits as free functions over the patch record. Each
  applies whole or throws one sentence. They and `check()` read one set
  of rules (`patch/src/node_rules.h`).
- `LivePlayer::set_value`: the build lists where each pin's constant
  lives; the audio thread writes a new value into the voices it holds and
  the control thread into a voice as it starts it.

## 3. The steps

| Step | Commit | What | Checks at its close |
|---|---|---|---|
| base | `fcd73c2` | | 12,308 |
| 1 | `947d021` | the five submodules, `LIBS` on `mforce_module`, five modules, the structure gate taught `editor/`, a window | 12,330 |
| 2 | `dc3ebf3` | `patch/edit.h`; `check()` and the edits share their rules | 12,553 |
| 3 | `3414a43` | `LivePlayer::set_value` | 13,035 |
| 4 | `737583a` | the model, the audio adapter, the control; no window | 14,096 |
| 5 | `e40ad75` | the views and the program | 14,114 |
| fix wave | `98603b6` | the review's findings and the pictures' | 14,246 |

At every step: 0 warning lines, the null gate 77 of 77, the allocation
gate 0 of 131, the comp gate 21 the same.

## 4. What was proven, and what no gate proves

Proven by tests that need no window and no sound card:

- **The edits refuse nothing the engine plays.** Over the 131 library and
  baseline patches, every value (2,054), setting (5,024) and position
  (246) set again through the edit functions is accepted, and each patch
  comes out equal to itself.
- **A live write equals a rebuild.** For every node type and pin that
  holds a constant in an instrument of the measured set (88 pairs), a
  note played after `set_value` is compared, sample for sample, with the
  same note on a player built from the edited patch: 87 equal, and one
  not followed by rule (an Envelope's `trigger`, 13.11).
- **A view only draws.** All 131 patches are drawn with no window, with
  nothing selected and with each node selected in turn (1,164 draws);
  after each the patch is equal, not marked unsaved, and the status line
  is as it was.
- **Every patch opens.** The 104 instruments each get a player and sound
  at middle C; the 27 plain sounds open and say they have no instrument.
- **Nothing allocates on the audio thread**: `set_value` and the buffer
  that applies it, and the adapter's `fill` with and without a swap.

What no gate proves:

- **How it feels and looks.** I looked at ten pictures of the real
  window taken with `--shot`; nobody has pressed a key in it. The sound
  card path (`audio_device.cpp`), the Win32 dialogs and the message box
  have never run under a test.
- **27 of the 87 "equal" pairs were equal without the change being
  audible** in the census's 0.1 s note (envelope multipliers at 0, a
  noise source's phase and the like). They are printed by name; the
  equality proves less for them (CLEANUP 158).
- **39 more pairs appear only in plain sounds**, which the live player
  cannot play. `set_value` is unproven on them until plain sounds sound
  (slice 4; CLEANUP 159).
- **No player is destroyed on the audio thread, and none leaks**: shown
  by the code's shape (the audio thread holds no owning pointer) and by
  the review's reading, not by a test with two threads.

## 5. The meter, against the base

| Measure | `fcd73c2` | now |
|---|---|---|
| Files | 439 | 514 |
| Lines | 59,166 | 64,313 |
| Files over 600 lines | 8 | 8 |
| Functions over 80 lines | 50 | 50 |
| File-level mutable variables | 22 | 22 |
| `dynamic_cast` lines | 54 | 54 |
| Baseline entries | 98 | 98 |
| Include edges outside the map | 0 | 0 |
| Duplicated blocks (printed, not gated) | 140 | 168 |

Nothing the slice added is over a limit. The 28 new duplicated blocks
are the detector's count, which moves with the tree (CLEANUP 148); the
editor's five test `main.cpp` files and its small headers are what it
sees.

## 6. Decisions the session made

The design's section 13 has each with its reason. The ones you would
notice at the window:

1. Opening a file places the nodes that have no saved position (114 of
   the 131 patches have none) in columns by distance from the output.
   Nothing is rearranged on open for a patch that has positions.
2. `View > Arrange nodes` lays every node out afresh, on request. Your
   grouped patches (`oboe1`, `piano_default` and 12 more) open as an
   overlapping pile until groups are drawn, because their saved positions
   were made for collapsed groups; Arrange, or the node list in the
   panel, is how to work in them meanwhile.
3. A pin's constant follows the drag on every frame. A setting's drag
   applies when you let go, because a rebuild takes 29 ms at the median
   here (36 ms for `sing_tenor_U`) and cuts the sounding notes.
4. An Envelope's `trigger` number rebuilds the player and is not written
   live: a voice reads it once, when built, to decide its trigger list.
5. Live play is at the sound card's rate; the file keeps its own. The
   two 22050 Hz baselines play at 48000 and the status line says so.
6. The computer keys are your layout from the old editor, copied as
   data. No note key acts while Ctrl is down or a text field has the
   keyboard; a release always does.
7. The status line shows the last refusal until the next edit that
   changes something.
8. The editor keeps its own files in `%APPDATA%\MForce\` and writes
   nothing into the repository.
9. The editor's code is in namespace `mforce::ui`.
10. `mforce_editor --frames N --shot FILE` exists for proof, not for use.
11. The editor is built only when the submodules are there and CMake is
    3.25 or later; otherwise configure says why in one line and the
    engine, the tools and every gate build as before (shown in a second
    build folder).

## 7. Debt, booked in CLEANUP

Rows 155 to 163: JSON reaches `ui_model` through the codec's header; what
RtAudio's own CMake adds; your `build/` cache's old entries; the 27
unheard pairs; the 39 plain-sound pairs; the trigger list decided by a
value; the two edit refusals with no counterpart in `check()`; a number
past float's range read as infinity by the codec; the run-from-the-root
assumption.

## 8. The review

A second Opus 5.5 agent, over `fcd73c2..e40ad75`, attacking seven claims.

- **1 Major, from my brief.** `set_value` refused a number on a list
  pin. The codec reads one, `check()` has no rule against it, and the
  reviewer rendered a one-node `MultiSource` to show the engine plays
  it. I had written that refusal into step 2's brief without checking it
  against the reader. No measured patch has such a pin, so the census of
  2,054 values could not catch it. Fixed: accepted, and followed live.
- **5 Minor**, all fixed: a note key released under Ctrl or a text field
  left its note sounding; `edit_string` had no test; the live census
  counted unheard pairs as proven (they are now named); a string setting
  was cut at 255 bytes; the structure gate did not know `windows.h`.
- **4 Notes**, three fixed (`keep_up` once a frame, the canvas's integer
  ids, a wrong title on a late error) and one answered by a truer
  comment on `set_value`.
- **Found sound**: the live player's threads walked order by order (a
  voice handed back between a decision and its message; a Set before and
  after a Start; a Set that waited for room); the adapter's swap; the
  edit rules against `check()`; that no engine behaviour changed; that
  no header names a library's type; the limits.
- **Not checked by the reviewer**: the program at the window, the device
  path on real hardware, the Win32 dialogs.

## 9. Lessons

### 9.1 Hiding a library behind a pointer (the "pimpl")

`AppWindow` uses GLFW, OpenGL and ImGui, and its header names none of
them:

```cpp
// app_window.h
class AppWindow {
public:
  AppWindow(int width, int height, const std::string& title);
  ~AppWindow();
  void begin_frame();
  void end_frame();
private:
  struct Parts;                    // declared, not defined: GLFW and ImGui live in the .cpp
  std::unique_ptr<Parts> parts_;
};

// app_window.cpp
struct AppWindow::Parts { GlfwLibrary glfw; GlfwWindow window; GuiContexts gui; GuiBackends backends; };
AppWindow::~AppWindow() = default;   // here, where Parts is complete
```

In C# or Java a private field's type is invisible to callers anyway: the
compiler reads the class's metadata, not its source. In C++ a caller
compiles the header text, so a private member of type `GLFWwindow*` drags
`glfw3.h` into every file that includes the header. The idiom moves the
private members into a struct only the `.cpp` defines and keeps one
pointer to it. Its one oddity is the destructor: it must be written in
the `.cpp`, even as `= default`, because destroying the pointer needs the
full struct. (The structure meter read that line as a file-level variable
until step 2 taught it otherwise.) The alternative is an abstract
interface with a hidden implementation class, which is what `FileDialogs`
is; that fits where a test wants to substitute its own.

### 9.2 Handing an object to another thread without a lock

The audio thread must never wait, so `AudioAdapter` cannot guard its
player with a mutex (C#: `lock`). It uses one atomic word instead:

```cpp
std::atomic<std::uintptr_t> offered_;                    // nothing, silence, or a player's address

// control thread
const auto unseen = offered_.exchange(offer);            // put mine in, take what was there
if (unseen > kSilence) destroy(reinterpret_cast<LivePlayer*>(unseen));   // never taken: still mine

// audio thread, once a buffer
const auto offer = offered_.exchange(kNothing);          // take whatever is there
```

`exchange` swaps the value in one indivisible step (C#:
`Interlocked.Exchange`). Whichever thread took a pointer out is the only
one that has it, so no player is ever in two hands. The rule that makes
it safe is about ownership, not about the atomic: the control thread
owns every player (`unique_ptr`, C#'s nearest is a single owner that
calls `Dispose`), the audio thread only borrows, and it gives a borrowed
player back through another atomic slot when it is done. The audio
thread never deletes, because `delete` can take a lock inside the
allocator.

### 9.3 Why the name `Session`

It is the one object `main` owns and every view is handed: the open
patch, the sound, the keys, the selection, the status line. `Document`
was ruled out on 2026-10-05; `Workspace` suggested several open patches,
which there are not; `EditorState` would have been accurate and flat.
`Session` says "everything true while the program is running", which is
what a test builds one of.

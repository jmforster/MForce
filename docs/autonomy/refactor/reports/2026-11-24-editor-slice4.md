# The editor, slice 4: see (report R32)

2026-11-24, refactor session XXVII. The page: `docs/architecture/editor-slice4.md`,
signed 2026-11-24 ("Signed, proceed"); its section 7 holds the forty
rulings made while building. What it restores is row 4 of
`docs/architecture/editor-parity.md`. Base `dcc96e4`; commits `593f559`
(the control), `0439168` (the views), fix wave `50b5aa9`. Fable 5.2 wrote
the briefs, ruled on what step 1 found, read each report, looked at the
pictures and committed; one Opus 5.5 executor built every step and the
wave; a second Opus 5.5 agent reviewed the whole range and then checked
the parity list line by line.

The session began with your first hand on slices 1 to 3 (R30, now
resolved): a right click, selecting a node, the wheel and every drag on
the canvas were dead, from one fault no test could see because no test
moved a mouse. That set this slice's first rule: every gesture is run by a
test that moves the mouse or presses the key.

## 1. What is on your desk

From the repository root:

```
build\editor\ui_app\Release\mforce_editor.exe patches\library\winds\oboe1.json
```

REVIEW R32 has the list of what to try. What slice 4 added:

- **The bottom pane**, across the window under the canvas, with three
  tabs: Waveforms (shown first), Spectrum, Keyboard. The arrow at its
  top-right corner collapses it to its tab bar and restores it. The
  computer's keys play whichever tab is shown.
- **Waveforms.** After Generate: an Output strip, then one for each node
  in the node list's order, each drawn to its own peak with the peak
  written beside it; envelopes in yellow, hidden when Envelopes is
  unchecked; one to four columns.
- **Moving about in them**: the zoom arrows, Fit, the scroll slider; the
  wheel scrolls, Ctrl and the wheel zooms, a left drag scrolls; with the
  mouse over the strips, Left and Right (a page with Shift), Home, End,
  `=`, `-` and `0`.
- **Pop a strip out** into a window of its own, with its own zoom and
  scroll and the same gestures; it follows each new Generate.
- **Spectrum**: the render's output on a log axis from 20 Hz, in dB, with
  the frequency and level under the mouse.
- **Listen**: right-click a node, "Listen here": the keys play that
  node's signal in place of the patch's output, and the node wears the
  green mark.
- **The sound card looked after**: a watchdog reopens a stream whose
  callback has stopped; Edit > Restart audio and Ctrl+Shift+A do it by
  hand; the status line says what happened.

## 2. The shape

**The engine did not change.** No file under `engine/` is in the range.
The strips were captured at Generate since slice 3; listening is the
engine's own `set_output` on a copy of the patch.

**The strips are plain data** (`ui_control/waveforms.h`). `strips_of`
says which strips a take has. `extents` gives the least and greatest
sample of each pixel column, exactly, at any zoom. Every amount a gesture
moves the view by (a notch of the wheel, a drag of so many pixels, each
key) is a function there with a test. The views report what the hand did
and draw; they compute no amount.

**One function draws a strip**, for the pane and for a pop-out
(`ui_views/src/wave_strip.cpp`), and one reports the gestures over it.

**Listen is three lines of idea**: `heard_patch` is the open patch, or a
copy whose output is the tapped node at volume 1. The keys' player and a
running plain sound are built from it. Generate never is.

**The watchdog's decisions are plain data** (`ui_control/watchdog.h`):
`watch` (dead after a second without a beat, one attempt every two
seconds), `came_back` and `back_reason` are driven in tests by a clock of
numbers. The adapter (`ui_audio`) counts its buffers, restarts on the
editor's thread, and says whether it was ever asked to open, so no test
ever opens the sound card.

**A test finds what it clicks by name** (`ui_views/places.h`): each view
notes where it drew a tab, a menu item, a strip, an arrow, a pop-out's
close button, and the headless editor clicks there. The two menu clicks
slice 2's tests made by a guessed offset moved onto it.

## 3. The steps

| Step | What | Commit |
|---|---|---|
| 1 | The old editor's numbers and words for this slice read from its lines; today's seams read; the measured set counted. Thirteen places where the signed page and the code disagreed and sixteen open questions, each ruled on | (read only) |
| 2 | The control: strips, extents and their cache, the spectrum, Listen, the watchdog and the adapter's restart | `593f559` |
| 3 | The views: the pane, the strips and pop-outs, the spectrum, the canvas's Listen, the restart; the picture options | `0439168` |
| 4 | Review, fix wave; the parity list's 19 lines checked one by one, its additions in the same wave | `50b5aa9` |

## 4. What was proven, and what no gate proves

Proven, by tests that run in the gates:

- **Every gesture of the slice, from the mouse or the key**, through the
  headless editor: each tab; the chevron (the canvas grows and shrinks);
  both zoom arrows, Fit, the columns by arrow and typed, Envelopes, the
  slider; the wheel (a notch and half a notch), Ctrl and the wheel, a
  left drag of exactly 100 pixels; each of the strips' keys; a strip's
  arrow; in a pop-out its keys, drag, wheel, zoom arrows, slider, Fit and
  close; the spectrum's readout under the mouse; both Listen items and
  the mark; Restart audio by the menu and by the keys.
- **Seven of them were switched off to see the test fail**: the wheel (5
  checks failed), the left drag (1), the Right key (3), a strip's arrow
  (1), the Listen item (3), and the two calls that keep the scroll inside
  the take (4 and 1). Each passed again once put back.
- **One job per key**: over the strips, in the pane and in a pop-out,
  `=` and `0` play no note, Left and Right change no note length, and
  Shift with them does not pan the canvas; over the canvas the same keys
  are the keyboard's and the canvas's. A note already down is still
  released.
- **`extents` is exact**: against the plain loop at every zoom from 1 to
  4096 and at 1500 and 5000, with scrolls that are not multiples of 64,
  on a take of three million samples and one of 300.
- **The cache never serves a take that is gone**, also when the new take
  has the same length.
- **The scroll stays inside the take** after the zoom-out arrow, a change
  of columns, and a new shorter render, in the pane and in a pop-out.
- **The spectrum**: a full-scale sine reads 0 dB in its bin within 0.1 at
  48000 and at 22050.
- **Listen**: the keys sound the tapped node; Generate's take is the
  same, sample for sample, with and without it; each thing that ends it;
  a face, the Output node and a part refused in words with the keys still
  playing what they played.
- **The watchdog, on a clock of numbers**: dead at one second; an attempt
  every two seconds, fifteen over thirty; quiet again when a beat
  arrives; a restart by hand gives the stream its second; a failed
  attempt changes nothing and is said once; each reason for restarting on
  the return to the window, and no restart when something is sounding.
- **The engine did not move**: `mforce_cli.exe` has one hash through all
  three commits, and the null, allocation and comp gates pass at each. At
  the close `python tools/gates.py` prints ALL GATES PASS, 16,547 checks
  (15,994 at the base).

Not under any test, and yours to try from REVIEW R32:

- **Anything at the sound card.** No test opens it. A restart by hand,
  the watchdog reopening a dead stream, the return to the window after
  another program has taken the device: all are proven only as decisions
  on a clock of numbers.
- **What the strips and the spectrum look like.** I looked at five
  pictures of the real window (the oboe's strips, the piano in four
  columns, a pop-out, the spectrum, and one after the fix wave with a node
  listened to); the executor looked at ten.
- **The tooltips**, the peaks' text and the readout's dB figure.

## 5. The meter, against slice 3's close

| Measure | Before | After |
|---|---|---|
| files | 590 | 623 |
| lines | 73,153 | 76,579 |
| files over the limit | 8 | 8 |
| functions over the limit | 50 | 50 |
| file-level mutable variables | 22 | 22 |
| duplicate blocks (reported, not gated) | 154 | 174 |
| include violations | 0 | 0 |

Nothing under `editor/` is in the baseline. The editor is 8,193 lines of
code and 4,838 of tests (6,292 and 3,313 at slice 3's close); its largest
file is 253 lines. Among the new duplicate blocks are the test programs'
lists of tests, which the meter reads as one block repeated. The full
gates took between 216 and 382 seconds in this session, by how busy the
machine was.

## 6. Decisions the session made

The forty rulings are the page's section 7; the ones worth your eye:

1. **Coming back to the window: your signed ruling 3 is amended, because
   its premise was short.** The page said the old editor restarted the
   stream on every return "sound or not" and that the watchdog and the
   restart by hand would cover the rest. The old code's own note gives
   the reason, which the page left out: after another program
   reconfigures the device a stream can go on ticking while nothing is
   heard, and nothing inside the program can see it (it records the day a
   browser video killed the sound). The parity check found this by
   reading that note. So: on the return after two seconds away the stream
   restarts when the default device changed, or it is not running, or
   NOTHING IS SOUNDING. When something is sounding it is left alone, which
   is what ruling 3 promised you. That one case stays yours: R32 asks.
2. **The strips' keys are the old set**, fuller than the page named: Home
   and End, Shift with an arrow for a page, the keypad's plus and minus.
   Over the strips, Shift with Left or Right no longer pans the canvas.
3. **A strip is at least three lines of text high.** At the old 25 pixels
   its label and two peaks were drawn on top of each other. With the pane
   a fixed 28 percent of the window, about three rows show at once and
   the pane scrolls; columns and pop-outs show more. The old pane could
   be dragged taller. A draggable edge would be yours to ask for.
4. **Old faults not restored**: a scroll that could sit at 1 with nothing
   to scroll; a pop-out's Fit that used the width beside its buttons; a
   column that showed samples outside itself at zoom 64 and above; a
   watchdog that gave up after one failed reopen while saying
   "retrying"; a spectrum labelled on the sound card's axis.
5. **Fit has no upper limit**, as before (a long take must fit); the
   arrows keep 1 to 4096.
6. **A restart that fails changes nothing and is said once.** With no
   sound device at all the watchdog still tries every two seconds, but it
   stops nothing and leaves the status line alone after the first
   sentence. **A restart rebuilds the keys' player only when the device's
   rate changed.**
7. **The strips of the render before stay** until the new render replaces
   them (the old editor blanked them for the "Generating..." frame).
8. **A touchpad's partial notch scrolls** (the old code dropped it).
9. **While listening the keys sound even with nothing wired to the
   Output.** The item is on every node's menu; a node that gives no
   signal is refused in the status line in the engine's words.
10. **A node renamed after a Generate keeps its strip** and its pop-outs.
11. **A strip drag begins on the strips; new pop-outs open cascaded.**
12. **One exposure accepted as before**: if the sound card's thread is
    blocked for ever inside the audio library, a restart blocks the
    window with it, as it would have in the old editor. When that thread
    has ended by itself, the case the watchdog is for, nothing waits.

## 7. Debt, booked in CLEANUP

Rows 187 to 195: the restart's exposure above; the pane's fixed height;
a long node name runs past a strip's 45 pixel margin into its plot, as
before; an undo or a redo of a rename drops the node's strip until the
next Generate, as it drops the selection and Listen; the tooltips, the
peaks' text and the readout's dB figure are drawn and not checked; no
test reaches the sound card, so everything a restart does there rests on
your list; `mforce_editor` has six options that exist only to set up a
picture; the duplicate-block count rose by 20, test lists among them; and
the looks decided here.

## 8. The review

An Opus 5.5 agent that had seen neither the briefs' reasoning nor the
executor's reports read the range, built a copy of it and ran its own
tests against it.

**One Major, acted on.** The strips' scroll was not kept inside its
limits after the zoom-out arrow, a change of columns, or a new shorter
take. It showed it from the mouse: End on a 115,200-sample take, then
Generate a 0.1 second note, and every strip is blank. The old editor
clamped the scroll every frame; the new one now does (`keep_inside`), in
the pane and in each pop-out.

**Two Minor, acted on.** A restart by hand was not told to the watchdog,
which could then close the stream just opened; and the pop-outs' gestures
were thinly tested (the reviewer drove them in a scratch test and none
was dead; the tests are in the tree now), with one test that checked
less than its comment said.

**Five Notes**: four acted on (a failed restart that stopped playback and
rewrote the status every two seconds; a fade that could briefly pass 1;
Listen on a plain sound with nothing running; a comment that claimed more
than was shown). One is the accepted exposure of section 6.

**Then the parity list, line by line**: of the 19 lines that name slice
4, 13 were done, 4 done in part and 2 belong elsewhere (one never
possible from the window, one shared with slice 5). The four parts were
the scroll clamp again, the pop-outs' untested gestures, a renamed node
losing its strip, and the restart on returning to the window, whose
reason the page had left out (section 6, item 1). All four were closed in
the same wave. It also found two differences no ruling covered, kept and
ruled (item 11).

Sound, by the review: no engine file in the range; no view writes to the
session directly; no view of the samples outlives its frame; `extents`
exact; the adapter's restart joins the old thread before anything the
callback reads is written.

## 9. Lessons

### 9.1 A count as identity, not an address

The old strips kept a table of least-and-greatest values per buffer, and
found a buffer's table by the buffer's address:

```cpp
// before (the old editor, inside the function that drew a strip)
static std::unordered_map<const float*, WaveMip> s_waveMips;   // which buffer? the one at this address
const float f0 = buf[0], f1 = buf[sampleCount / 2], f2 = buf[sampleCount - 1];
WaveMip& mip = s_waveMips[buf];
if (mip.count != sampleCount || mip.fp[0] != f0 || mip.fp[1] != f1 || mip.fp[2] != f2) { /* rebuild */ }
```

An address says where something is, not which thing it is. Free a buffer
and allocate another of the same size, and the allocator may hand back
the same address: the table then describes samples that are gone. The old
code guarded against that by comparing the length and three sample
values, which is a guess. The new one counts:

```cpp
// after
int takes{0};                                        // Session: goes up each time the take is replaced
const Table& table(int take, const std::string& node, std::span<const float> samples);
```

Every place that replaces the take goes through one function that adds
one. The cache remembers the count it was built for and drops everything
when it differs. In C# the trap is rarer, because an object's reference
stays unique while anything holds it; the same idea appears there as a
version number on a collection (the one that makes `foreach` throw when
the list changed under it). The alternative here would be a shared
pointer to the take, compared by identity; a count is smaller and cannot
keep a dead take alive.

### 9.2 The destructor is the stop

`AudioAdapter::restart()` (`editor/ui_audio/src/audio_device.cpp`):

```cpp
std::string AudioAdapter::restart() {
  if (!asked_) return "no sound device was ever opened";
  device_.reset();                                 // the stream closed: no callback runs from here
  start_device();
  return openError_;
}
```

`device_` is a `std::unique_ptr<Device>`. `reset()` destroys the device
object, and its destructor stops the stream and waits for the sound
card's thread to end. So "no callback is running" is not something the
code hopes or checks: it is true on the next line because the destructor
has returned. That is RAII again (the primer's section on it): the object
owns the stream, and ending the object ends the stream. In C# this is
`Dispose()` called by hand or by `using`; the difference is that C++ runs
it at a point you can name in the source, which is what lets the next
line write the fade lengths the callback reads, with no lock.

### 9.3 One number both threads touch

The heartbeat is one line in the audio thread and one in the editor's:

```cpp
beats_.fetch_add(1, std::memory_order_relaxed);     // audio thread, each buffer
return beats_.load(std::memory_order_relaxed);      // editor's thread, each frame
```

`std::atomic<std::uint64_t>` is C#'s `Interlocked.Increment` and
`Volatile.Read` in one type. "Relaxed" asks for the least: each read sees
some whole value the counter really had, with no promise about ordering
against other variables. That is enough for a question like "has it
moved since I last looked", and it never blocks, which the audio thread
must not.

### 9.4 Mine, about the work

- **A ruling that drops something the old code did must quote the old
  code's own reason first.** The page's ruling 3 was written from a
  one-line summary ("restarts on every return, sound or not"). The reason
  was in a comment ten lines above the code. The parity check read it;
  the page's author had not.
- **Switching a gesture off to watch its test fail** costs a rebuild and
  is the only proof that the test can fail. It goes into every view
  brief from here.
- **Clicking by name** removed the two tests that guessed where a menu's
  first item is; the same change is what made a pop-out's close button
  testable.

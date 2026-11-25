# The editor, slice 4: see

Status: **SIGNED by Matt 2026-11-24 ("Signed, proceed"), the thirteen rulings
of section 4 as written. BUILT the same day (report
`docs/autonomy/refactor/reports/2026-11-24-editor-slice4.md`, REVIEW R32).
Section 7 says what the build changed; where it differs from an earlier
section it is the decision. It amends ruling 3 of section 4.**
Parent: `editor.md` (signed 2026-11-23; its three rules, five modules and
limits hold here unchanged). What it must restore is row 4 of
`editor-parity.md`, A1: 15 capabilities of the old editor, about 790 of its
lines. Unlike slice 3's page, this one was written after the old code was
read line by line for each of them (by the reviewer, read only); where the
old editor did something odd, section 4 says so and rules on it.

## 1. What you can do when it is done

1. **The bottom pane, with its three tabs as before**: Waveforms (shown
   first), Spectrum, Keyboard. A chevron at its top-right corner collapses
   it to its tab bar and restores it.
2. **Waveforms.** After Generate: an Output strip, then one strip for each
   node, in the node list's order, each drawn to its own peak with the
   peak written beside it. Envelopes in their own colour, hidden when
   "Envelopes" is unchecked. One to four columns. Chords and Drums show
   the Output strip alone, as before.
3. **Moving about in them**: the zoom arrows and readout, Fit, the scroll
   slider; the wheel scrolls, Ctrl and the wheel zooms, a left drag
   scrolls; with the mouse over the strips, `-`, `=`, `0` and the arrows
   zoom, fit and scroll. Zoom and scroll are kept from one render to the
   next; the first render fits.
4. **Pop a strip out** into a window of its own, with its own zoom and
   scroll; it follows each new Generate.
5. **Spectrum**: the output's, on a log axis from 20 Hz, in dB, with the
   frequency and level under the mouse.
6. **Listen**: right-click a node, "Listen here": the keys now play that
   node's signal in place of the patch's output, and the node wears the
   green mark. "Stop listening here" ends it.
7. **The sound card looked after**: when its callback stops for a second
   the stream is reopened, and again every two seconds until it comes
   back; Edit > Restart audio and Ctrl+Shift+A do it by hand; the status
   line says what happened and why.

## 2. The interfaces

Nothing in the engine changes. The strips were captured at Generate in
slice 3 (`Take::strips`); listening is the engine's own `set_output` on a
copy of the patch.

### 2.1 What the strips show, as plain data (`ui_control/waveforms.h`)

```cpp
// How the strips are looked at. Not part of the patch; kept between renders.
struct WaveView {
  int  zoom{1};          // samples per pixel, 1 to 4096
  int  scroll{0};        // the first sample shown
  int  columns{1};       // 1 to 4
  bool envelopes{true};  // envelope strips shown
  bool fitted{false};    // the first render has been fitted
};
// One strip to draw: its words, whose signal, and where its samples are.
struct Strip {
  std::string label;                 // "Output", or the node's id
  std::string node;                  // empty for the Output
  bool envelope{false};
  std::span<const float> samples;    // a view into the take
};
// The strips of a take, in the order drawn: the Output (the take as one
// line, the mean of its two channels), then each captured node that is
// not a Note, in the patch's order.
std::vector<Strip> strips_of(const Session&);

// One pixel column of a strip.
struct Extent { float lo{0}, hi{0}; };
// `width` columns of `samples` from `scroll`, `zoom` samples each: the
// least and the greatest in each. A cache makes a long take cheap.
void extents(std::span<const float> samples, int zoom, int scroll, std::span<Extent> into, ExtentCache&);

void zoom_in(WaveView&);  void zoom_out(WaveView&);          // halve, double
void fit(WaveView&, int samples, int stripWidth);             // the whole take in one strip's width
void scroll_by(WaveView&, int samples, int total, int stripWidth);   // kept inside the take
```

- `Session` gains `WaveView waves;` and the pop-outs
  (`std::vector<PopOut>`: a strip's node and a `WaveView` of its own).
- The old cache was a file-level table keyed by a buffer's address. Here
  it is an object the view owns, keyed by the take.
- Tests with no window: `extents` against the plain loop over every zoom;
  fit and the scroll's limits at one column and at four; `strips_of` on a
  Note take, a Chords take and no take.

### 2.2 The spectrum (`ui_control/spectrum.h`)

```cpp
struct Spectrum { std::vector<float> dB; int sampleRate{0}; int size{0}; };   // one level per bin; empty before a render
// The take's output: the middle of it (up to 8192 samples, a power of
// two, at least 256), a Hann window, one transform; a full-scale sine
// reads about 0 dB; nothing below -120.
Spectrum spectrum_of(const Take&);
```

It uses the engine's `util/fft.h`, so `ui_control` gains `util` in the
reach table. Test: a sine at a known frequency peaks in its bin at about
0 dB, at the take's own rate.

### 2.3 Listen (`ui_control/listening.h`)

```cpp
void listen_at(Session&, std::string_view node);   // refused, with a sentence, where a node gives no signal
void stop_listening(Session&);
// The patch the keys play: the open one, or, while listening, a copy whose
// output is the tapped node and whose instrument volume is 1.
Patch heard_patch(const Session&);
```

`Session` gains `std::string listening;` (a node's id; not saved, not an
undo step). `sound_follows` builds the keys' player from `heard_patch`.
Deleting the node, an undo that removes it, New and Open end it; a rename
follows it. Before and after, at the one call site:

```cpp
// before (playing.cpp, sound_follows)
why = s.live.rebuild(s.open.patch);
// after
why = s.live.rebuild(heard_patch(s));
```

### 2.4 The watchdog (`ui_audio`, `ui_control/watchdog.h`)

```cpp
// ui_audio/audio_adapter.h, added
std::uint64_t heartbeat() const;   // counts the device's callbacks
// Closes the stream and opens the default device again. Empty, or why not.
// The rate may be another: the caller rebuilds what it plays.
std::string restart();
bool default_device_changed() const;

// ui_control/watchdog.h: when to restart, as plain data a test can drive
struct Watchdog { std::uint64_t lastBeat{0}; double lastChange{0}, lastAttempt{-1e9}; int attempts{0}; };
enum class Watch { Nothing, Restart };
Watch watch(Watchdog&, std::uint64_t beat, double now);   // dead after 1 s without a beat; one attempt every 2 s, for as long as it takes
void restart_audio(Session&, std::string_view why);       // "by hand", "the callback stopped", "back at the window"
```

After a restart the keys' player is rebuilt at the device's rate and what
was playing has stopped. The program's loop calls the watch once a frame
with its own clock; the test calls it with a clock of numbers.

### 2.5 The views

- `BottomPane` (new): the tab bar, the chevron, the three tabs. The
  keyboard strip you have now becomes its Keyboard tab.
- `WaveformsView`: the toolbar (zoom arrows and readout, Fit, Columns,
  Envelopes, the scroll slider), the strips in their columns, the pop-out
  arrow on each, the gestures of section 1. `StripWindow`: a pop-out. One
  drawing function serves the pane and the pop-outs.
- `SpectrumView`: the plot, its grid and labels, the readout under the
  mouse.
- `NodeCanvas`: "Listen here" and "Stop listening here" in a node's menu;
  the green mark after the title.
- `MenuBar`: Edit > Restart audio (Ctrl+Shift+A).
- `HeadlessEditor` learns to click a tab of the pane and a strip's arrow,
  so the pane's tabs, a pop-out and the collapse run under a test.

## 3. Steps

1. **The old numbers.** The read is done; the executor takes each number
   the briefs call "as before" from the old lines (colours, margins, the
   10% wheel step, the 28% pane) and cites it.
2. **The control**: the strips, extents and their cache, the spectrum,
   Listen, the watchdog and the adapter's restart, with tests.
3. **The views**: the pane, the two views, pop-outs, the canvas's Listen,
   the menu. Pictures: each tab after a Generate on oboe1 and the piano;
   four columns; a pop-out; the pane collapsed; a node being listened to.
4. **Review, fix wave, the parity check and its wave, report, on your
   desk.**

## 4. The rulings the signature accepts

The first three change what you see or hear most.

1. **The bottom pane is three tabs, as before**: Waveforms, Spectrum,
   Keyboard, so the piano is behind a tab while you look at strips (the
   computer's keys still play). Slices 1 to 3 showed the keyboard all the
   time under the canvas; with a strips pane as well, the canvas would
   have about 350 pixels of a 900-pixel window. Say the word and the
   keyboard stays in sight under the pane.
2. **Listen is for the keys** (and for a plain sound that is running). A
   render is always of the patch's own output, so Play, the strips, the
   spectrum and Save WAV never depend on a tap. The old editor was the
   same for Note and Passage, and its Chords render listened to the tap,
   which is not restored. Listening plays at volume 1, as before. It is
   not offered on a Note (the old one offered it and silently did
   nothing).
3. **Coming back to the window does not cut the sound.** The old editor
   restarted the stream every time the window regained focus after two
   seconds away, sound or not. Here it restarts then only when the
   default device changed or the stream is not running. The watchdog and
   the restart by hand cover the rest. Say if you want the old
   unconditional restart.
4. **The Output strip is the take as one line**, the mean of its two
   channels. The old one was the instrument's own output before the
   mixer; for an instrument, panned to the centre, the two are the same
   shape.
5. **The spectrum is of the render at the render's rate**, is made again
   after every Generate (Chords and Drums too) and is cleared with the
   strips. The old one labelled a 22050 Hz render on the sound card's
   axis and went stale after Chords and Drums: faults, mended. The 0 dB
   line is drawn (the old grid stepped over it).
6. **With the mouse over the strips, `-`, `=`, `0` and the arrows are the
   strips'** and play no note and move no octave; anywhere else they are
   the keyboard's. One job per key by where the mouse is (the parity
   list's A2).
7. **Fit and the scroll's limit use the width a strip really has**; the
   first render fits the real width. The old Fit ignored the columns and
   the first fit assumed 800 pixels.
8. **A pop-out has the pane's gestures** and keeps its window when
   another is closed (the old ones were numbered by position, so closing
   one moved the rest).
9. **No playhead**: the old editor drew none.
10. **The watchdog keeps trying.** The old one stopped after one failed
    reopen while still saying "retrying". The status words name what
    asked for the restart.
11. **A restart stops what is playing** and rebuilds the keys' player at
    the device's rate, which may have changed.
12. **`ui_control` may use `util`** (the engine's FFT).
13. **Not here**: the Scrub slider and the Partials and Formant strips
    (slice 6); strips for Note nodes (the old one hid them); strips drawn
    from live play (the old ones never were).

## 5. Not in this page

- Groups, wormholes, the clipboard, several nodes selected, MIDI in, the
  build-stamp banner: slice 5.
- Tables, the shape editors, partials and formants, audition: slice 6.

## 6. For the C# reader

`std::span<const float> samples` in `Strip` is the idea to notice. A strip
does not own or copy its samples: it is a view (a pointer and a length)
into the take's buffer, C#'s `ReadOnlySpan<float>`. Drawing forty strips
of a ten-second render copies nothing. The price is the one a C# span
has too: the view must not outlive what it looks at, so `strips_of` is
called each frame and its result is never kept; the take is replaced only
by Generate, on the same thread. Where C# enforces that with `ref struct`
rules, C++ leaves it to the design, which is why the page states it.

## 7. What the build changed (2026-11-24)

Step 1 took the old editor's numbers and words from its lines and checked
this page against today's code before any brief was written. It found
thirteen places where the signed text and the code disagreed and sixteen
questions the page did not answer. Each was ruled on then (1 to 30), and
ten more while building, at the review and at the parity check (31 to
40). Ruling 34 amends section 4's ruling 3. "A" numbers are step 1's
report sections. Report:
`docs/autonomy/refactor/reports/2026-11-24-editor-slice4.md`.

### The strips

1. **The Output's line is the take's.** `Take` gains the take as one line
   (the mean of its two channels), made where the take is made, for every
   kind of take. The Output's `Strip::samples` is a view of it. (The page
   had it as a view into the interleaved stereo, which cannot be.)
2. **A plain sound's take, like Chords and Drums, shows the Output alone.**
3. **The zoom.** The arrows keep 1 to 4096 as before. Fit is the take's
   samples divided by the plot's width, at least 1, with NO upper limit, as
   before (a long take must fit). From above 4096 the zoom-out arrow does
   nothing and the zoom-in arrow halves.
4. **"The width a strip really has" is its plot**: the strip less its 45
   pixel margin. Fit, the first fit, the scroll's limit, the wheel's step
   and the keys' step and page all use it, in the pane (one column's) and
   in a pop-out (its own; the old pop-out's Fit used the width left beside
   its buttons: a fault, not restored).
5. **The scroll stays in 0 to max(0, samples - plot width x zoom).** The
   old clamp allowed 1 with nothing to scroll: not restored.
6. **`extents` is exact**: the least and greatest of exactly the column's
   samples at every zoom and scroll. The 64-sample table serves the whole
   buckets inside a column and the plain loop its two ends. (The old table
   read whole buckets, so a column showed samples outside itself.)
7. **The cache is keyed by the session's count of takes and the strip's
   node.** `Session` counts its takes; every place the take is replaced or
   cleared goes through one function that counts. `extents` takes what it
   needs to name the strip (the page's signature had a bare span).
8. **The wheel**: a notch scrolls a tenth of the plot's samples, wheel up
   to the left, as before; a part of a notch scrolls its part (the old
   code dropped it, so a touchpad's wheel did nothing). Ctrl and the
   wheel: up halves the zoom, down doubles it.
9. **The strips' keys, while the mouse is over the strips** (the pane's or
   a pop-out's; hovered, not merely focused): Left and Right scroll a step
   (a tenth of a page) and with Shift a page, repeating while held; Home
   and End go to the ends; `=` and keypad + halve the zoom, `-` and keypad
   - double it, repeating; `0` fits. This is the old set (A5); the page's
   ruling 6 named only part of it. While the strips have the keys: `=` and
   `0` play no note, Left and Right change no note length, and Shift with
   Left or Right does not pan the canvas. Up and Down are the keyboard's
   octave everywhere (the strips never used them). A key that went down
   as a note still releases its note. "Over the strips" is the answer of
   the frame before (the canvas and the keyboard read keys before the pane
   and the pop-outs are drawn).
10. **The first fit is once in the session**, as before, with the real
    plot width (the page's ruling 7). A pop-out's view starts as the
    pane's.
11. **The strips of the render before stay on screen until the new render
    replaces them.** The old editor cleared them for the "Generating..."
    frame: not restored.
12. **Pop-outs.** Each has a number never reused in the session, and its
    window is named by it. The same strip may be popped out twice, the
    Output too (its node name is empty). A pop-out of an envelope shows
    whether or not Envelopes is checked. A node that is gone: the empty
    strip, as before. New and Open close them all.
13. **Peaks as before**: the upper is the greatest sample or 0, the lower
    the least or 0, "%.3f".

### The spectrum

14. Made by the view when the session's count of takes changes, and kept by
    the view until it changes again. `Spectrum::size` is the transform's
    size N; `dB` holds N/2 levels.
15. The grid as the old code has it (A8: the decades from 10 to 100,000
    times 1, 2 and 5, kept inside 20 Hz to Nyquist; lines every 12 dB from
    +6 down), plus a line at 0 dB in the major colour (the page's ruling
    5). The readout's formats are the old ones ("%.2f kHz   %.1f dB" from
    1000 Hz up, "%.1f Hz   %.1f dB" below).

### Listen

16. **Three call sites, not one**: the keys' player (`sound_follows`) and a
    plain sound started or followed (`play_plain`, `plain_follows`) are
    built from `heard_patch`. Generate never is.
17. **What it refuses** is the reader's own sentence, got by asking the
    reader: `check()` of the copy, and for an instrument what the voice
    build refuses (a StereoMixer passes `check()` and the voice build
    refuses it). No list of types is kept in the editor. A refusal goes to
    status and nothing changes: the keys go on playing what they played.
    A note face and the Output node are refused by name.
18. **The menu's item** is on every graph node's menu, one item worded by
    state ("Listen here" / "Stop listening here"), after Delete and a
    separator, as before. Not on a face's menu. Choosing it on a part gives
    the refusal in status.
19. **No status words on success**, as before; the mark shows it.
20. **An undo or a redo that renames the node listened to ends
    listening**, as it ends the selection (a step back knows only what is
    gone). A rename made by hand follows.

### The watchdog and the restarts

21. **The adapter says whether it was ever asked to open.** `watch_audio`
    and a restart do nothing to one that never was (every test's), and a
    restart then says so in status. No test opens the sound card.
22. **One call a frame**: `watch_audio(Session&, double now, bool focused)`,
    from the program's loop (with the window's clock and focus) and from
    `HeadlessEditor::frames` (with a clock of its own, focused). It runs
    the watch, and it notices the focus going and coming back: after two
    seconds or more away, a restart only when the default device changed or
    the stream is not running. The away-time bookkeeping is plain data
    beside the watchdog's, under a test with a clock of numbers.
23. **The sentences** (the old ones' shape; `why` is "by hand" or "back at
    the window"):
    - "Audio stream restarted (<why>)"
    - "Audio restart FAILED (<why>) — <reason> — watchdog will retry"
    - the watchdog's own: "Audio stream reopened (attempt N) on '<device>'",
      with " — died: <last error>" when there is one; "Audio reopen FAILED
      (attempt N) — <reason> — retrying..."; and when a beat arrives after
      an attempt, "Audio callback ALIVE again (post-reopen)".
    The attempt count starts again when a beat arrives (the old never
    reset it). The adapter gives the device's name.
24. **A restart**: asks for what `open()` asks for; clears the error of an
    open that failed before (today a second open would keep it); sets the
    lines' fades from the new rate while no callback runs.
25. **A stream that will not stop** (a device thread blocked for ever
    inside the library) would block the restart, and the window with it,
    as it would have blocked the old editor's. Accepted as before: when
    the device's thread has ended by itself, which is the case the watchdog
    is for, closing does not wait. It goes into the report as a known
    exposure.
26. **The default device's change** is asked of the system only when the
    focus comes back after two seconds away, never each frame; the adapter
    keeps the id it opened.
27. **Ctrl+Shift+A** is routed like the menu's other shortcuts and does
    nothing while a text field has the keyboard, as before.

### The pane

28. **The pane spans the window's width** under the canvas and the
    properties panel, where the keyboard strip is now, and takes 0.28 of
    the window's height under the menu bar (the old pane's share).
    Collapsed it is the tab bar's height (a frame's height plus 10, as
    before). There is no dragging its edge, so restoring gives the share
    back.
29. **The chevron** is the old arrow button at the tab bar's right end
    (down while open, up while collapsed), with the old tooltips. How it
    is placed there is the executor's.
30. **Which tab is shown and whether the pane is collapsed are the
    session's**, changed through functions of `ui_control`, as the
    transport's tab is.

### Read errors in the read of the old code (for the record)

The grid's decades run to 100,000 times 5, and 10 Hz is never drawn; the
kHz readout has two decimals; the pop-out's Fit used the width beside its
buttons; Fit has no upper limit; the scroll clamp allowed 1; a separator
line stands at the margin's edge; the upper peak is never below 0; the
64-sample table read whole buckets.

### Ruled while building and at the review (2026-11-24)

31. **The scroll is kept inside its limits every frame**, in the pane and
    in each pop-out, as the old editor kept it: after the zoom-out arrow,
    a change of columns, a new shorter take or a narrower plot (the
    review's Major: a shorter take drew blank strips).
32. **After any restart that opened a stream the watch counts from that
    moment**: the stream has its second before it is called dead and the
    next attempt is two seconds off (the review: a restart by hand could
    be followed at once by the watchdog's own).
33. **A restart that fails changes nothing and is said once**: nothing
    stops, nothing is rebuilt, and its sentence is written on the first
    failure and again only when the reason differs. With no sound device
    at all the watchdog still tries every two seconds, quietly.
34. **Coming back to the window after two seconds away restarts the
    stream when the default device changed, or the stream is not running,
    or nothing is sounding.** This amends the page's ruling 3, whose
    premise was short: the old editor restarted on every return because a
    stream can go on ticking while nothing is heard after another program
    reconfigures the device (old:3298-3303; its own note records the day a
    browser video killed the sound), and neither the watchdog nor the two
    conditions of ruling 3 can see that. When something IS sounding the
    return leaves it alone, as ruling 3 promised; the restart by hand
    covers that case.
35. **A restart rebuilds the keys' player only when the device's rate
    changed** (the page's ruling 11 rebuilt it always); what was playing
    still stops.
36. **Listen on a plain sound asks its reader whether or not one is
    running.**
37. **A node renamed by hand keeps its strip and its pop-outs** (the take's
    strip id follows the rename, as the selection does). An undo or a redo
    of a rename is not followed: the strip comes back with the next
    Generate.
38. **A strip is at least three lines of text high** (its label and its
    two peaks; at the old 25 pixels they were drawn on top of each other),
    and the pane scrolls, its scrollbar always shown so the plot's width
    does not change under a fit.
39. **A strip drag begins on the strips** (the old one scrolled when any
    drag crossed them). **New pop-outs open cascaded** so one does not
    hide another's close button.
40. **While listening the keys sound even with nothing wired to the
    Output**, as the old tap did.

# The editor, slice 4: see

Status: **SIGNED by Matt 2026-11-24 ("Signed, proceed"), the thirteen rulings
of section 4 as written. No code exists yet: the build starts in a new
session.**
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

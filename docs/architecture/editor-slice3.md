# The editor, slice 3: generate

Status: **signed by Matt 2026-11-23 ("Signed, proceed"), all eleven rulings
of section 4 as written. BUILT 2026-11-24 (report R30); section 7 is what
the build changed, and it wins over the sections before it.**
Parent: `editor.md` (signed 2026-11-23; its three rules, five modules and
limits hold here unchanged). What it must restore is row 3 of
`editor-parity.md`, A1: 26 capabilities of the old editor, about 1,240 of
its lines, plus the parts the two checks moved here (Stop of live notes,
the pre-clip level, a note shown as sounding after its key is up). Written
in the shape of `editor-slice2.md`.

## 1. What you can do when it is done

1. **The transport**, a strip across the top with four tabs, as before.
   **Note**: pitch and octave, velocity, duration, Live. **Passage**: the
   two-line text, octave, BPM, velocity, Onsets (auto, all tongue, all
   slur, all breath), Save Passage and Load Passage. **Chords**: text,
   Group, Figure, octave, BPM, Delay, Save and Load.
   **Drums**: the pattern, the drum map (`KK=path;SN=path`), BPM.
2. **Generate** renders the open patch, as it is in the window, saved or
   not, playing what the tab says: one note; the passage, with its `|`
   phrase marks stamping breath, tongue and slur on a patch marked
   sustaining; the chords through the chord performer and its figures; the
   drum pattern on one patch per drum. The button reads "Generating..."
   while it works. What the render's nodes reported (a clamped frequency)
   shows in the status line.
3. **Play** plays the last render; **Stop** stops it, and every sounding
   note with it. Space does both. Note mode renders again first when the
   note, velocity or duration changed; Passage plays the last render as
   it is (render, tweak, Play is the A/B); Chords and Drums render every
   time.
4. **Save WAV**: the last render, 16-bit stereo.
5. **Live**, on the Note tab, on at start. On: a key holds its note until
   you let go, on any patch (as many keys as there are voices; on a patch
   marked sustaining, the one legato line, which now slurs back to a key
   still held and cuts the release before it in 3 ms). Off: every key
   strikes a note of the tab's duration.
6. **A plain sound sounds.** Play runs it until Stop; Generate renders its
   length.
7. **What is sounding, shown**: the piano highlights every sounding note,
   not only the keys held down; the level shows the peak before the clip
   as well as after; "Last note: 60 C5 261.6Hz vel 0.80" at the
   transport's right.
8. A patch's embedded note fills the Note tab when the file is opened, and
   a new patch is saved with the default one, as before.

## 2. The interfaces

### 2.1 The live player holds more than one note (`render/live_player.h`)

The one change to something that makes sound. Four additions, no change
to what a note already played does.

```cpp
// before
NoteId note_on(const PerformedNote& note);   // `hold` joins the one open line
void   note_off(NoteId id);                  // releases the line when `id` is its current note

// after
// A note marked `hold`: on an instrument that sustains it joins the one
// legato line, as before. On one that does not, it holds its own voice until
// note_off(id): as many held notes as there are voices. A voice that cannot
// hold (no envelope to gate) plays the note's length, as before.
NoteId note_on(const PerformedNote& note);
void   note_off(NoteId id);                  // releases the note `id` began, line or not
// Every sounding voice fades out over the fade a stolen voice gets, and
// comes back. Stop, and panic.
void   stop();
// The notes of the voices handed over and not yet given back, lowest first.
int    sounding_notes(std::span<int> into);  // how many it wrote; allocates nothing
// The largest sample of the mix before the output stage since the last call.
float  take_peak_before_clip();
// What the nodes of the voices this thread holds reported, since the last call.
void   collect_faults(std::vector<FaultReport>& out);
```

- Each is the control thread's, by the rule the player already has: a
  voice is touched by one thread at a time. The sounding notes come from
  the control thread's own record of what it handed over; the peak is one
  atomic the audio thread writes; faults are read from a voice only after
  it has come back.
- The line's two refinements (the 3 ms cut of a release still ringing when
  a new line starts; the slur back to a key still held) are the old
  editor's lines 4359 to 4462, matched in step 1.
- Proof: the tests the player has (a note started after equals the same
  note on a fresh player; nothing allocated in `fill`) extended to held
  notes on a patch not marked sustaining, eight at once, released in any
  order; the allocation gate; the null gate untouched, because no offline
  render goes through the live player.

### 2.2 Text to a performance (`perform`, new header `text_performance.h`)

The transport's three texts become notes in the engine, not in the
editor: the parsers are already in `music` (`parse_passage`,
`parse_chord_string`, `parse_drum_pattern`), and the embedded score
already has this shape (`perform_embedded_score`).

```cpp
enum class Onsets { Auto, AllTongue, AllSlur, AllBreath };

// What a text became: the notes or hits, where the last one ends, and what
// is worth telling ("the patch is not marked sustaining: phrase marks ignored").
struct TextPerformance {
  Performance performance;
  double endSeconds{0.0};
  std::vector<std::string> notes;
};

TextPerformance perform_note   (float noteNumber, float velocity, float seconds, bool hold);
TextPerformance perform_passage(std::string_view text, int octave, float bpm, float velocity,
                                Onsets onsets, const InstrumentClass& cls);
TextPerformance perform_chords (std::string_view text, int octave, float bpm, std::string_view group,
                                std::string_view figure, float delayMs, const InstrumentClass& cls);
TextPerformance perform_drums  (std::string_view pattern, float bpm);
```

Each throws the parser's sentence on a text it cannot read. The phrase
stamping (`|`, breath, tongue, slur, hold) is the old editor's
`stamp_passage`, moved here whole.

### 2.3 A patch played offline (`play/play.h`)

```cpp
// before
PlayResult play(const Patch& patch);                 // its embedded score, or its plain sound

// after: the same, and
// How loud a render comes out: as the instrument's own peak guard leaves it
// (any note or passage), or with the guard off and the whole scaled so its
// peak is 0.95 (chords and drums, whose notes pile up).
enum class Level { Guarded, Normalised };
// The patch's instrument playing `performance`, with the voice tail; each
// node of `capture` recorded as a strip, by id.
PlayResult play(const Patch& patch, const Performance& performance, double endSeconds,
                Level level, const std::vector<std::string>& capture = {});
// A drum kit, one patch per drum number, playing `hits`, normalised.
PlayResult play_drums(const std::vector<std::pair<int, Patch>>& drums, const Performance& hits,
                      double endSeconds);

struct PlayResult { ...as today...; std::vector<std::vector<float>> strips; };   // one per captured id
```

- `Level::Normalised` is what the old editor did for chords and drums, so
  a loud chord is turned down and not clipped.
- Proof that ties the new path to the proven one: a note, and a passage
  with no phrase marks, played through `play(patch, performance, ...)`
  give sample for sample what `play(patch)` gives for the same patch with
  those notes as its embedded score, over the measured instruments. The
  embedded-score path is the one the null gate holds.

### 2.4 The output plays sounds, two at once (`ui_audio`)

Today the adapter plays one `LivePlayer`. It has to play three kinds of
thing: the keys' instrument, a render being played back, a plain sound
running. An interface with three small implementations is the plain way
to say that (C#: `interface ISound`).

```cpp
// before (ui_audio/audio_adapter.h)
void play(std::unique_ptr<LivePlayer> player);
LivePlayer* player();
// call site (ui_control/live_play.cpp)
audio_.play(std::make_unique<LivePlayer>(live, kNotesAtOnce));

// after
// Something the output plays, on the audio thread.
class Sound {
public:
  virtual ~Sound() = default;
  // `frames` stereo frames ADDED to `stereo`. Never blocks, allocates, frees or throws.
  virtual void fill(float* stereo, int frames) noexcept = 0;
  virtual bool over() const noexcept { return false; }   // nothing more to play
};

class AudioAdapter {
  // Two sounds at once, summed: what the keys play, and what Play plays.
  enum class Line { Keys, Playback };
  void   play(Line line, std::unique_ptr<Sound> sound);   // null: silence on that line
  Sound* playing(Line line);                              // the current one; null when none or over
  ...open, sample_rate, peak, error, fill as today...
};
// call sites (ui_control)
audio_.play(Line::Keys,     std::make_unique<KeysSound>(std::move(player)));
audio_.play(Line::Playback, std::make_unique<TakeSound>(session.take, audio_.sample_rate()));
```

- The hand-over is the one slice 1 built and reviewed (one atomic word, the
  old sound faded over 5 ms and destroyed on the control thread), used
  once per line.
- Two lines and not one, because the old editor mixed the keys with the
  playing render and you could play along with a passage.
- `ui_audio` stops knowing the engine's live player at all: it plays
  `Sound`s. The three sounds live in `ui_control`: `KeysSound` (a
  `LivePlayer`, mono to both channels), `TakeSound` (a render played once),
  `RunningSound` (a plain sound's mixer, until stopped).
- Names, with their alternatives: `Sound` (or `Playable`, `AudioSource`);
  `Line` (or `Channel`, which the engine already uses for a mixer's
  channel, or `Slot`); `TakeSound` follows `Take` below.

### 2.5 The transport and the take (`ui_model`)

```cpp
// transport.h: what the transport's fields hold. Not part of the patch.
// Each field starts where the old transport started it (step 1 reads them).
struct Transport {
  enum class Mode { Note, Passage, Chords, Drums };
  Mode mode{Mode::Note};
  std::string note;              // "C#4" or a number; the octave, velocity and duration are the keyboard's
  bool live{true};
  std::string passage;  float bpm;  Onsets onsets{Onsets::Auto};
  std::string chords, chordGroup, figure;  float delayMs{15.0f};
  std::string drumPattern, drumMap;
};

// take.h: the last render, kept until the next one.
struct Take {
  std::vector<float> stereo;  int sampleRate{0};  int frames{0};
  std::vector<std::string> stripIds;  std::vector<std::vector<float>> strips;   // slice 4 draws them
  std::string faults;            // what the nodes reported, in the engine's words
  Transport madeFrom;  float velocity{0}, seconds{0};   // so Play knows when a Note take is stale
};
```

One octave, one velocity, one note length: the Note tab shows the
keyboard's (`Session::keyboard`), so the tab, the arrows and the keyboard
strip's buttons move the same three numbers, as the old transport's did.

### 2.6 What a view asks for (`ui_control/generating.h`)

```cpp
void set_transport(Session&, const Transport&);   // a field changed; not an undo step
void generate(Session&);        // renders the open patch as the tab says, into session.take
void play(Session&);            // by the tab's rule (section 1, item 3); a plain sound: runs it
void stop(Session&);            // the playback, and every sounding note
bool playing(const Session&);
void save_wav(Session&, const std::string& path);
void save_text(Session&, const std::string& path);   // the tab's passage or chords
void load_text(Session&, const std::string& path);
std::string last_note(const Session&);               // "60 C5 261.6Hz vel 0.80"
```

`Session` gains `Transport transport;` and `Take take;`. `key_down` reads
`transport.live`. The reach table of `editor.md` section 3 gains two
entries: `ui_control` may use `play` and `perform`.

### 2.7 The views

- `TransportView` (new), the strip at the top: the tabs and their fields,
  Generate, Play or Stop, Save WAV, the last note.
- `MenuBar` gains Play and Stop; Space plays or stops when no text field
  has the keyboard.
- `KeyboardView`: the piano highlights `sounding_notes`; the level shows
  two peaks.
- `HeadlessEditor` learns to press Space and click a button, so Generate,
  Play and Stop run under a test.

## 3. Steps

1. **Read and count.** The old Live path and the line's refinements (4319
   to 4471), the chord and drum renders (7301 to 7487), what the embedded
   score seeded (4746 to 4773), every way the old transport refused. And a
   census over the measured instruments: which can hold a voice, which
   fall back to a struck note.
2. **The live player** (2.1), with its tests and the allocation gate.
3. **Text to a performance and a patch played offline** (2.2, 2.3), with
   the sample-for-sample proof.
4. **The output's two lines and the three sounds; the transport, the take
   and the control** (2.4 to 2.6), with tests and no window.
5. **The views.** Pictures of the transport's four tabs; Generate, Play,
   Stop and Space under the headless editor.
6. **Review, fix wave, the parity check and its wave, report, on your
   desk.**

## 4. The rulings the signature accepts

1. **A render is of the patch in the window**, saved or not. The old
   Chords tab refused a patch that had never been saved, though it rendered
   the one in memory: a fault, not restored. Nor are its Inversion and
   Spread spinners, which nothing read (the parity list's A2, "controls
   that did nothing").
2. **A render is at the file's rate**, so Save WAV is what the command
   line renders. When the sound card runs at another rate, Play resamples
   it plainly and the status line says so. (Two of the 131 measured
   patches are at 22050 Hz.) Live play stays at the sound card's rate.
3. **Generate holds the window while it renders**, as before, with the
   button saying so first. A long render is not made cancellable here.
4. **Keys sound over a playing render**; Stop stops both.
5. **Live is on at start**, as before. That changes slice 1's keyboard in
   one way: on a patch not marked sustaining a key now holds its note,
   where slice 1 struck a note of fixed length. Unchecking Live gives the
   struck note back.
6. **One octave, one velocity, one note length**, shared by the Note tab
   and the keyboard.
7. **The transport's fields are not saved with the patch**, as before: the
   file's embedded note fills them at open and is written back as it was
   read. A new patch is saved with the default note (60, 0.8, 2 s).
8. **Passage and chord texts, and WAVs, each remember their folder**
   (`.psg`, `.chd`, `.txt`), in the folder memory slice 2 made.
9. **The strips are captured at Generate and drawn in slice 4.** Until
   then they are a tested field nobody sees.
10. **A plain sound**: Play runs it until Stop, at the sound card's rate;
    Generate renders its `seconds`; the keys do nothing on it.
11. **`ui_control` may use `play` and `perform`** (the reach table).

## 5. Not in this page

- The waveform strips, the spectrum, the Listen tap, the device's
  watchdog: slice 4.
- MIDI in: slice 5. Audition: slice 6.
- The embedded score leaving the patch file (BACKLOG row 10): after the
  refactor, on your word.

## 6. For the C# reader

`Sound` is an abstract base class with one pure virtual function, which
is exactly a C# interface with one method; `KeysSound : Sound` is a class
implementing it, and `std::unique_ptr<Sound>` is a reference to "some
sound" whose owner is known. This is the first place in the editor where
inheritance is the right tool: three unrelated things must be driven by
one caller (the audio thread) that should not know which it has. The
alternative, a `std::variant<KeysSound, TakeSound, RunningSound>` (a closed
list the compiler checks, like a C# discriminated union if it had one),
would put the engine's types back into `ui_audio`'s header, which is the
thing the interface removes. The cost of the virtual call is one indirect
call per buffer of 512 frames, which is nothing.

## 7. What the build changed (2026-11-24)

Step 1 read the old editor's code and the engine's seams before any
brief was written and found sixteen places where the signed text and
the code disagreed. Each was ruled on then, and ten more as the build
went. Report: `docs/autonomy/refactor/reports/2026-11-24-editor-slice3.md`.

1. `Transport` and `Take` live in `ui_control` (beside `Keyboard`), not
   `ui_model`: `Transport` holds `Onsets`, which is `perform`'s, and
   `ui_model` may use `patch` only.
2. `Level::Normalised` renders the instrument directly (the mixer clips
   every sample, so the guard cannot be turned off through it): volume 1,
   guard off, scaled by 0.95/peak only when the peak passes 0.95, as the
   old chords did. `Level::Guarded` goes through the mixer as `play(patch)`
   does. DRUMS ARE GUARDED, not normalised: the old drums' normalise never
   ran (its kit kept the guard on).
3. A render's length is the last note's end plus the ring-out's tail
   (0.4 s), the old Generate's rule. The proof against `play(patch)` is
   sample for sample over the frames both have.
4. `play_drums`: each drum's patch is built as one voice's graph with its
   note never set (as the old kit did), by drum number; `DrumKit` gains a
   gap (a number with no drum) and ignores a hit on one instead of
   crashing. It renders at the rate the caller gives (the open patch's).
   The drum map's text is parsed in `perform` (`parse_drum_map` existed
   only in the old editor). The default map names library percussion
   patches that exist (the old default named files that no longer do).
5. A plain sound running until stopped needs engine work the page did not
   list: a mixer that renders block after block without starting again or
   allocating, and the envelopes held (absolute time, a hold stage), as
   the old stream did. Built in step 3 as `build_running_sound`.
6. `LivePlay` keeps the `LivePlayer*` of the `KeysSound` it handed over and
   drops it at each swap; every `audio.player()` call becomes
   `live.player()`.
7. `Sound::over()` is backed by an atomic. The adapter no longer checks a
   rate: a sound is made for the adapter's rate by its maker.
8. The adapter soft-clips the SUM of its two lines, once, as the old
   callback did, and reports the peak before and after. So the keys'
   player is built with its own guard off (`LivePlayer`'s new constructor
   argument) and `take_peak_before_clip` is not built.
9. Defaults, the old code's: note length 2.0 s (the keyboard's `seconds`
   becomes 2.0); note "C4" (48; note 60 is "C5" here); duration field 0.1
   to 30 by 0.1, arrows halve and double within 0.05 to 30; "Last note"
   with two spaces between fields. DECIDED DIFFERENT: the Delay field
   starts at 15 and means what it shows (old: 0, meaning 15); PC Keyboard
   on at start (old: off; slice 2's choice); octave 0 to 8 (old arrows 0
   to 20; slice 2's choice).
10. `perform` gains "the first score note as written" (the seed reads the
    score's first entry raw, not as performed) and the default embedded
    score's text; `new_patch` gets `seconds` 3.0 and that score.
11. `FileDialogs` takes a kind (patch, wav, passage, chords): filters,
    default names and remembered folders by kind.
12. Old faults not restored: the Chords refusal and every Drums error were
    overwritten by the success line; Chords errors went to stderr; drums
    were rendered, and every WAV labelled, at the device's rate. New: an
    error shows and stays; a WAV is at the render's rate.
13. Play follows the old order exactly: it stops everything that sounds
    (keys too), then plays; keys pressed after that sound over it.
    `playing()` is true while the playback line plays OR any voice sounds,
    so the button and Space say Stop while a key's note or its release
    rings (that is the panic).
14. A carried note takes "slur" (in the player, step 2). The slur back to
    a key still held is `LivePlay`'s (its record of the keys down). The
    3 ms cut is the player's.
15. One measured instrument sustains (`phrase_smoke_phrased`): the line's
    refinements are proven on it alone.
16. `perform_chords` takes no instrument class (nothing read it). Chord
    velocity is 1.0 and the patch's volume is ignored, as before.

Also from section A:
- Capture set at Generate: every signal node of the graph by id (Note
  nodes included); Chords and Drums capture nothing.
- The seed (at every open of a file that has a score): note as "%g" text,
  velocity, duration clamped 0.05 to 30, octave = note / 12 clamped 0 to
  8. A file with no score leaves the transport as it was.
- "Last note" is set by a struck note, a held note, a carry, and Note
  Generate; not by Passage, Chords, Drums or the slur back.
- Status words are the old ones where the page says "as before" (step 1,
  A4 to A7 list them).

Added while writing step 4's brief:
17. `Sound::fill` WRITES its frames (the page said "added"): the adapter
    owns the mixing and the 5 ms fade of the sound before.
18. `ui_control` may use `music` as well as `play` and `perform` (the page's
    ruling 11 named two): the note and drum-map parsers are `music`'s, and
    a forwarding function in `perform` to dodge the rule would be worse.
19. Opening or starting another patch drops the take and stops the playback.
20. `LivePlayer::collect_faults` reads every voice's fault byte (an atomic
    the audio thread sets): the one thing of a sounding voice the control
    thread touches.

From the build and the review:
21. `ui_control` may use `core` too, for `note_to_freq` in the last note's
    text (one function, not a third copy of the formula).
22. The drum status is "Generated N hits", the hits performed. The old
    line's "x R repeats" is not restored (the parser expands repeats, so
    the old line printed 1).
23. While a plain sound runs it follows the patch: a change the build
    reads, or the end of a drag of a number, replaces it with one built
    from the patch as it now is (the old stream read the live objects).
24. The proof that ties the new offline path to the gated one runs over a
    spread of the measured instruments (every eighth, plus the one that
    sustains, the two at 22050 Hz and one with no score), not all 104: it
    ran once over all of them, 3.5 million frames with none different,
    and the code path is the same for each.
25. `Transport` holds no octave, velocity or duration of its own: they are
    the keyboard's (`Session::keyboard`), which the Note tab, the arrows
    and the keyboard strip all move.
26. A Note take is stale also when the last take was not a Note's: Note,
    Play after a Passage, Chords or Drums Generate renders the note (the
    old one replayed whatever buffer there was).

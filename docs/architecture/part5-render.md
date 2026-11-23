# Part 5: render, the contract, play

Status: **signed by Matt, 2026-11-22 ("Signed, and proceed with all
steps without waiting for my ok"), as written: the seven rulings of
section 9 stand as proposed, the live player included. Built the same
day; section 13 says where the code differs from this text.** Parent:
`target-architecture.md`, sections 3.4, 3.7, 3.9, 3.11 and section 8
item 4. Written under the 2026-11-21 reset (`CLAUDE.md`, "Scope right
now"): short, the interfaces, then the build order. The blocks still
read *proposed*; the signature decided each.

## 1. Today, from the code

- `render/instrument.h` is 796 lines and `PitchedInstrument::play_note`
  131, both over the limits. `PitchedInstrument` is all public and holds
  the voice pool, note delivery, the held line, the sample loop, the
  ring-out, capture and the live calls.
- The ring-out is written twice: `play_note`'s classic path and
  `finish_line` (the meter lists it as a 19-line duplicate).
- The performers in `music/conductor.h` call the instrument at 12 places
  (`play_note` 11, `play_hit` 1). The Conductor picks pitched or drums
  with two `dynamic_cast`s.
- `PerformedNote::curve` is a pointer to a local that lives for one call.
- A Conductor is hand-built six times in `tools/mforce_cli/main.cpp`
  (lines 106, 181, 482, 588, 714, 881) and once in `tools/test_figures`.
  Each copy has its own length rule, mixing rule and statistics.
  `engine/src/embedded_score.cpp` is an eighth player.
- **The replay bug.** The composer stamps a part's events in whole-piece
  beats. `Conductor::perform(Piece)` plays every part once per section,
  adding the section's offset each time. With one section it is right.
  `scores/baselines/template_binary.json` has two.
- Five of the meter's 12 include violations are render including music;
  a sixth is `music/pitch_bend.h` including the Envelope.
- The live calls (`acquire_voice`, `prepare_voice`, `prepare_voice_at`,
  `continue_voice_live`, `StreamingVoice`) lost their host with the old
  editor. The allocation gate, the explorer and `engine_tests` call them,
  each with its own hand-written sample loop.
- Two things have no user at all: `capture_begin` has no caller in the
  tree, and no code builds a `DrumKit` (one test makes one by hand; no
  patch form describes one). `Tone` and `DrumHit` in `music/basics.h`
  are an earlier attempt at the contract and are referenced nowhere.

## 2. The modules after this part (proposed)

| Module | Holds | May include |
|---|---|---|
| contract (new) | what crosses from performance to realization | nothing |
| patch | as today | core, source, contract |
| render | instruments, voices, mixing, the live player, WAV | core, source, patch, contract |
| music | as today, the performers included | core, contract |
| play (new) | the function that runs perform and render end to end | everything above |

Music stops including render and render stops including music. The six
violations named above go; the six that remain are music including JSON
(Part 6). Targets per module are Part 8; until then the meter's map is
the check.

The performers stay under `music/` in this part. Making `perform` its
own module is Part 6's split. Two files move now because the layering
needs it: `pitch_curve.h` goes to contract, and `compile_pitch_curve`
(curve to Envelope) goes to render. What is left of `pitch_bend.h` is
three performer functions; the file is renamed `pitch_gestures.h`.

## 3. The contract (proposed)

`engine/include/mforce/contract/`, header-only, standard library only.

```cpp
// pitch_curve.h: PitchCurve, moved from music/ unchanged.

// performance.h
struct PerformedNote {
  float start{0.0f};       // seconds from the start of the performance
  float duration{1.0f};    // seconds
  float noteNumber{60.0f};
  float velocity{0.8f};
  int   onset{0};          // InstrumentClass::onset_id; 0 = none
  bool  hold{false};       // the excitation continues past the note's end
  std::optional<PitchCurve> curve;   // owned
};

struct PerformedHit {
  float start{0.0f};
  float duration{0.1f};    // seconds
  int   drumNumber{0};
  float velocity{1.0f};
};

// What a performer made of one part, in the order it is to be played.
struct Performance {
  std::vector<PerformedNote> notes;
  std::vector<PerformedHit>  hits;
};

// instrument_class.h: what a performer may know about an instrument.
struct InstrumentClass {
  bool sustaining{false};
  std::vector<std::string> onsets;            // ordered
  int onset_id(std::string_view name) const;  // position + 1; 0 if absent
};
```

- **`start` is in the note.** Today it is a second argument to
  `play_note`. A performer that returns data has to put it somewhere.
- **The curve is owned.** That removes the dangling pointer by
  construction.
- **Order is the performer's, not time order.** Voices are handed out in
  delivery order and a held line's notes must be consecutive, so an
  instrument plays `notes` in the order given and never sorts. Humanize
  can put two starts slightly out of order; that is today's behaviour
  and it stays.
- **One `Performance` holds both lists.** A part's events are notes,
  chords, hits and rests; a pitched instrument plays the notes and a kit
  the hits, as today. The alternatives were a list of
  `variant<PerformedNote, PerformedHit>` (every reader visits a variant
  to find it holds the one kind it wanted) and a hit expressed as a note
  whose number is the drum (MIDI's way; it overloads `noteNumber`).
- **`onset` is an `int`.** It is an index into a list. The graph reads
  it as a float; the instrument converts at delivery, exactly.
- `InstrumentClass` is filled from the patch's instrument block at build
  and replaces `PitchedInstrument::onsetNames`, `sustaining` and
  `onset_id`. The "name is not in this instrument's vocabulary" warning
  becomes the caller's: `onset_id` is a pure lookup.

## 4. The instrument's entry point (proposed)

```cpp
struct Instrument : MonoSource {
  // Pre-renders one part's performance. Called once per part; several
  // parts may share an instrument. render() mixes everything played.
  virtual void play(const Performance& part) = 0;
  virtual const InstrumentClass& instrument_class() const = 0;
  void clear();            // forget what was played
  float volume{1.0f};
  bool  peakGuard{true};
};
```

`PitchedInstrument::play` delivers each note (continue the open line,
open a line, or the classic path: the three cases stay as they are) and
ends an open line when the list ends. `DrumKit::play` plays the hits.
`play_note(pn, startTime)`, `play_hit` and `finish_open_lines` stop
being public, and the Conductor's two `dynamic_cast`s go.

## 5. The performers return data (proposed)

```cpp
// music/time_map.h: beats to seconds across a piece's sections.
struct TimeMap {
  explicit TimeMap(const std::vector<Section>& sections);
  explicit TimeMap(float bpm);              // one tempo
  float seconds_at(float beat) const;
  float bpm_at(float beat) const;
};

struct Conductor {
  ChordPerformer chordPerformer;
  NotePerformer  notePerformer;
  DrumPerformer  drumPerformer;
  Performance perform(const Part& part, const TimeMap& time);
};

// A patch's embedded score: its phrased notes copied across, the rest
// through NotePerformer, as today.
struct EmbeddedScore {
  Performance performance;
  double endSeconds{0.0};
  std::vector<std::string> warnings;
};
EmbeddedScore perform_embedded_score(const std::string& scoreJson,
                                     const InstrumentClass& cls);
```

- The Conductor keeps its name and its job (read the score, direct the
  performers). Its instrument registry and its loop over sections move
  to play.
- `Conductor::perform` takes no `InstrumentClass`: no rule of its
  performers reads one today, and a parameter nothing reads is
  speculation. The embedded score's reader takes it because it uses it
  (`hold` is dropped on a non-sustaining instrument; onset names are
  looked up).
- The embedded score's reader moves from render to music, since it needs
  the music JSON and `NotePerformer`. `music::Note` still gains nothing.
- `NotePerformer::humanize` has had no caller since 4.1 (CLEANUP 51): the
  field, `jitter()` and its generator go. It is 0 on every path, so no
  render moves.
- `Tone` and `DrumHit` are deleted.

## 6. play (proposed)

```cpp
// play/play.h
struct Ensemble {          // which instrument plays each part; not owned
  std::unordered_map<std::string, Instrument*> byPart;  // Part::instrumentType
};

struct PlayResult {
  std::vector<float> stereo;       // interleaved
  int   sampleRate{0};
  int   frames{0};
  float peak{0.0f};
  double rms{0.0};
  std::vector<FaultReport> faults;
  std::vector<std::string> warnings;
};

// A piece on its instruments.
PlayResult play(const Piece& piece, const Ensemble& ensemble,
                Conductor& conductor, float tailSeconds = 2.0f);
// A patch by itself: its embedded score on its own instrument, or its
// plain sound.
PlayResult play(const Patch& patch);
```

One set of rules, which are `--compose`'s today:

- **Once per part.** Each part is performed once over the whole piece
  through the `TimeMap`. This is the replay fix.
- **Length:** the piece's seconds by the `TimeMap`, plus the tail.
- **Mix:** each instrument renders mono; the renders are summed; a sum
  of more than one instrument that passes 0.98 is scaled to 0.98; mono
  goes to both channels at unity.
- **A patch** is built, its score performed and played on its
  instrument, and rendered through its mixer exactly as `build_sound`
  does today, so the null gate's 77 hold their bytes.

`build_sound` stays in render for callers that want the built object
(the allocation gate, the tests) and no longer plays a score.
`render/embedded_score.{h,cpp}`, the six loops in the tool with
`render_piece_to_wav` and `render_and_write`, and the loop in
`test_figures` are deleted. Each tool mode becomes: build the piece and
the ensemble, call `play`, write the WAV, print.

**What moves at this step, and only here:**

- `template_binary` in the comp gate, and any piece of more than one
  section: played once where it was played once per section.
- `--josie` gains the scale-to-0.98 rule where its sum clipped at the
  writer before.
- `--melody` and `--play` took the length's tempo from the last section
  and now use the map: a length change for multi-section pieces only.
- Nothing else. `--chords` passes its 1-second tail; `--dun` keeps its
  pre-roll in the tool and stays silent until Part 6 makes hand-built
  pieces audible.

## 7. The instrument split (proposed)

| Class | File | Job |
|---|---|---|
| `Voice` | `render/voice.{h,cpp}` | one clone of the graph, playable a note at a time |
| `VoicePool` | `render/voice_pool.h` | an instrument's voices and the rotation |
| `RingOut` | `render/ring_out.h` | the one rule for when a voice is over |
| `NodeCapture` | `render/node_capture.h` | per-node strips during an offline render |
| `PitchedInstrument` | `render/pitched_instrument.{h,cpp}` | plays a `Performance` on the pool; the held line |
| `DrumKit` | `render/drum_kit.h` | plays hits |
| `Instrument` | `render/instrument.h` | the base and the mix of what was played |

```cpp
class Voice {
public:
  struct Note { float freq, velocity; int samples; float seconds;
                int onset; const PitchCurve* curve; };
  void  prepare(const RenderContext& ctx);
  void  start(const Note& n, bool gated);    // Setup: the note, the dynamic
                                             // pins, start, the triggers
  void  carry_on(const Note& n, float fromFreq, float glideSec);
                                             // the next note of a held line
  int   release(int lastNoteSamples);        // the gate closes; samples of
                                             // release still to play
  float next();                              // one sample: the clocks, the
                                             // root pull, the tap-only tails
  bool  can_hold() const;
};

class RingOut {
public:
  RingOut(int sampleRate, int bodySamples);
  void  prime(std::span<const float> played);  // a held line's history
  bool  more() const;        // false once the fixed tail has passed and the
                             // level is under the floor, or at the cap
  float shape(float sample); // follows the level; fades the cap's last 80 ms
};
```

- `Voice::next()` is the one sample loop body. Today it is written in
  `render_chunk`, the explorer, the allocation gate and several tests.
- `RingOut` is a stepper so that the offline loop and the live player
  drive the same rule one sample at a time. The arithmetic and its order
  are today's, so the offline bytes hold.
- Members become private; bodies move to `.cpp`.
- `Voice::can_hold()` stops counting a Multiplex template's own envelope
  (CLEANUP 65), so a held line on such a voice is delivered as ordinary
  notes, which is what the comment beside today's test says it does. No
  file in library or baselines has the shape.
- Capture is kept, as `NodeCapture` with one test, though nothing calls
  it until the editor draws strips. Its hook is inside the sample loop
  this step is shaping; it is about 40 lines.

## 8. The live player (proposed)

```cpp
// core/spsc_queue.h: one producer thread, one consumer thread. Fixed
// capacity. Never blocks, never allocates.
template <class T, std::size_t N> class SpscQueue {
public:
  bool push(const T& item);   // false when full
  bool pop(T& item);          // false when empty
};

// render/live_player.h
class LivePlayer {
public:
  LivePlayer(const Patch& patch, int voices);  // builds 2 x voices
  using NoteId = std::uint32_t;
  // control thread
  NoteId note_on(const PerformedNote& note);   // 0 = not sent
  void   note_off(NoteId id);
  void   set_wheel(float v);
  void   set_pressure(float v);
  // audio thread
  void   fill(float* mono, int frames) noexcept;
};
```

**The rule (target 3.4): a voice is touched by one thread at a time, and
ownership passes with a message.**

| Message | Direction | Carries | The audio thread does |
|---|---|---|---|
| Start | control to audio | a prepared voice, its id, gain, body length, held or not | adds it to the sounding set |
| Carry | control to audio | the id and the next note's numbers | `Voice::carry_on` |
| Release | control to audio | the id | `Voice::release`, then the ring-out |
| Fade | control to audio | the id | ramps it to zero over 5 ms |
| Done | audio to control | the voice | (the control side reuses it) |

- `note_on` makes the same three-way choice `PitchedInstrument::play`
  makes (continue the held line, open one, classic), through one shared
  function, so offline and live cannot drift.
- **Nothing the audio thread does allocates or frees.** A voice owns its
  glide Envelope and re-aims it in place (CLEANUP 78); a compiled bend
  travels with its voice and comes back with it.
- **Pool size:** `voices` is the larger of the patch's polyphony and
  what the host asks for (most library patches say 1, written for their
  monophonic test scores). The player builds twice that: `voices` to
  sound and as many spare. Idle voices cost memory and build time, no
  CPU.
- **Steal policy:** when no voice is idle, the victim is the voice
  released longest ago, or failing that the one started longest ago. It
  gets Fade; the new note starts on a spare in the same buffer. A burst
  that outruns the spares drops the excess notes: `note_on` returns 0
  and never waits.
- A note starts at the next buffer boundary, as today.
- `fill` is mono: one instrument, its volume and peak guard applied by
  the same code `Instrument::render` uses. Stereo and the device are the
  host's.
- Deleted: `StreamingVoice`, `prepare_voice`, `prepare_voice_at`,
  `acquire_voice`, `release_voice`, `release_all_voices`,
  `continue_voice_live`.
- Users in this part: the allocation gate plays each patch through
  `fill`, so it measures the audio thread's real path; the explorer's
  one-note render becomes a one-note `Performance` through
  `Instrument::play`, and its `dynamic_cast` goes.
- Tests: a note through `fill` equals the same note through `play`,
  sample for sample; a steal fades in 5 ms and the new note is whole;
  `fill` under the allocation probe allocates nothing; the queue under
  two real threads loses and repeats nothing.

Designed for, not built: a message that changes a sounding voice's
setting (the editor's tweak-and-hear) is one more row in the table, and
the note-at-a-time performer that turns key events into `PerformedNote`s
belongs to perform. Both come with the editor's design.

## 9. The rulings the signature accepts

1. **The live player is built in this part, last.** My read: yes. The
   editor's first slice (open, play, tweak, hear, save) needs it; the
   allocation gate becomes a measurement of the real audio path; and
   `Voice` is then shaped against both of its users at once. If no, step
   5 waits for the editor and today's live calls stay until then.
2. **Pool and steal** as section 8: twice the voices; released-first,
   then oldest; a 5 ms fade; a burst past the spares is dropped.
3. **One `Performance` per part with two lists** (section 3).
4. **`play`'s one mixing rule and one length rule for every mode**, and
   the list of what moves (section 6).
5. **`DrumKit` and capture are kept** though nothing builds the one or
   calls the other. The target keeps both; deleting either is yours.
6. **The performers stay in `music/` until Part 6** (section 2).
7. **`onset` is an `int` in the contract** (section 3).

## 10. Build order and proof

Each step is one commit or a few, and each ends with the build,
`engine_tests` and the null gate at 77 of 77.

1. **The contract.** The module; `PerformedNote` owns its curve and its
   start; `InstrumentClass`; `compile_pitch_curve` into render;
   `pitch_gestures.h`.
2. **Data across the boundary.** The performers return `Performance`;
   `Instrument::play`; the two casts go. The Conductor's piece loop
   stays for this step, replay included, so nothing moves.
3. **play.** The module and both `play` functions; the eight players
   deleted; the replay fix. The only step where a render may move
   (section 6's list).
4. **The split** (section 7).
5. **The live player** (section 8), if ruling 1 is yes.

The null gate's patches never reach the Conductor's piece loop or the
chord performer, so steps 2, 3 and 4 also run the existing comp gate
(`tools/comp_template_null_gate.py`, the baselines through `--compose`):
all the same at 2 and 4; at 3, all the same but `template_binary`.

Expected on the meter at the close: no render file over 600 lines and no
render function over 80; include violations 12 to 6; `dynamic_cast` down
by three; the ring-out duplicate gone.

One independent review and one short report at the end of the part. The
report offers the fixed `template_binary` render for Matt's ears.

## 11. Not in this part

- Composed pieces gaining onsets and holds: a performer rule, Part 6 or
  after the freeze.
- Whether an ordinary note re-anchors its random draws (BACKLOG 11): it
  moves every multi-note render; after the freeze.
- A truncated section's effect on later start times, and DUN pieces
  rendering silence: Part 6. `play` and the composer share one function
  for a section's start beat, so they cannot disagree meanwhile.
- The mixer's clock ticking once per channel (CLEANUP 94): after Part 8.
- A library target per module: Part 8.

## 12. For the C# reader

**A value that lives inside its owner.** `const PitchCurve* curve` was a
C++ pointer to a local variable in the performer; it was valid only
until that function returned, which was fine while the performer called
the instrument directly and is a dangling pointer the moment the
performer returns a list. `std::optional<PitchCurve>` is C#'s
`PitchCurve?` on a struct (`Nullable<T>`): the curve's storage is part
of the note, copied with it, destroyed with it. The alternatives were
`std::shared_ptr<const PitchCurve>` (a C# reference: one heap object,
shared lifetime, which nothing here needs) and `std::unique_ptr` (which
would make a note impossible to copy).

**An abstract method in place of a type test.**
`virtual void play(const Performance&) = 0;` is C#'s
`public abstract void Play(Performance p);`. The Conductor's
`dynamic_cast<PitchedInstrument*>` was C#'s `inst as PitchedInstrument`:
the caller had to know every subclass, so a third kind of instrument
meant editing the Conductor. With the abstract method the caller knows
only `Instrument`.

**A queue instead of a lock.** A mutex on the audio thread means the
callback can wait for a UI thread the operating system has paused, and
the buffer underruns. `SpscQueue` is a ring of fixed size with two
`std::atomic` indexes, one written by each side; neither side ever
waits. The C# analogue is a bounded `System.Threading.Channels` channel
created with `SingleReader` and `SingleWriter`, or the Disruptor's ring
buffer. It works only because there is exactly one producer and one
consumer, which is why there is one queue in each direction.

## 13. As built (2026-11-22)

Built the day it was signed, in the five steps of section 10 and one fix
wave: `ad463d8`, `eae609b`, `b517173`, `47d87db`, `a9e5677`, `fa085c3`.
The report is
`docs/autonomy/refactor/reports/2026-11-22-part5-render.md`. Where the code
differs from the text above, the code is right and this section says how.

- **Section 5, the embedded score's order.** The reader emits notes in
  score order. The old one delivered an unphrased note late when a phrased
  note followed it (its slide-run buffer held it), so a score that mixes
  the two renders differently now. No score in library or baselines mixes
  them and the null gate held; the review built one that does and measured
  the difference. This belongs on section 6's list of what moves and was
  not on it.
- **Section 6, `--dun`.** Its length now comes from the map, which counts
  a section's full beats, where the tool subtracted a truncated tail. The
  output is silent either way until Part 6.
- **Section 6, `PlayResult`** also carries `mixPeak`, `scale`, `buildMs`
  and `renderMs` (the tool prints all four). `play` throws on a piece with
  no section and on a piece none of whose parts has an instrument, where
  the tool wrote silence.
- **Section 7.** `Voice` has no accessor to its internals but `nodes()`
  and `signals()`; a read-only `description()` names, by node id, the
  output, the tails, the envelopes a line gates, the trigger envelopes and
  the dynamic pins. `PitchedInstrument` is constructed with its pool and
  `pool()` is const; `LivePlayer` is its `friend`. `RingOut` takes its body
  length at construction or later (`set_body`). The three-way choice is
  `choose_delivery` in `render/delivery.h`. `render/mixer.h`'s four types
  have a file each and render's sources sit under `engine/src/render/`.
- **Section 8.**
  - `SpscQueue` has a third call, `space()`, for the producer.
  - `LivePlayer` also has `set_volume`, `sample_rate` and `sounding()`.
  - A fresh note first checks that a voice is idle and that the queue has
    room for everything it will send, and only then steals, so a steal is
    never spent on a note that cannot start.
  - A key-up the queue cannot take is kept and sent first by the next
    control call.
  - An idle voice is chosen in rotation.
  - While a line is open every `note_on` carries, as offline, so a held
    line makes the instrument monophonic live. With one line at a time,
    "released longest ago" and "started longest ago" name the same voice.
  - The player does not read `InstrumentClass::sustaining`: like the
    offline instrument it trusts the note's `hold`, and the caller applies
    the piano rule.
  - Queue capacity is 256 each way.

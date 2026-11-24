# The editor, slice 3: generate (report R30)

2026-11-24, refactor session XXVI. The page: `docs/architecture/editor-slice3.md`,
signed 2026-11-23 ("Signed, proceed"); its section 7 holds the twenty
rulings made while building. What it restores is row 3 of
`docs/architecture/editor-parity.md`. Base `cbf8626`; commits `b341e96`
(the live player), `99e5100` (text to a performance, offline play, a
running sound), `15c115f` (the output's two lines, the transport,
Generate, Play, Stop), `5faeedd` (the views), fix wave `e0a108f`, parity wave `9875f8a`. Fable 5.2 wrote the
page and the briefs, ruled on what step 1 found, read each report, looked
at the pictures and committed; one Opus 5.5 executor built every step and
the waves; a second Opus 5.5 agent reviewed the whole range and then
checked the parity list line by line.

## 1. What is on your desk

From the repository root:

```
build\editor\ui_app\Release\mforce_editor.exe patches\library\keys\acoustic_piano\piano_default.json
```

REVIEW R30 has the list of what to try. What slice 3 added:

- **The transport**, a strip across the top with the four tabs: Note,
  Passage, Chords, Drums, each with the fields it had.
- **Generate** renders the patch in the window, saved or not, playing
  what the tab says; **Play** plays the last render and **Stop** stops
  it and every sounding note; **Space** does both. **Save WAV** writes
  the last render.
- **Live**, on at start: a key holds its note until you let go, on any
  patch, as many keys as there are voices. On a patch marked sustaining
  it is the one legato line, which now slurs back to a key still held.
  Live off: every key strikes a note of the tab's duration.
- **A plain sound sounds**: Play runs it until Stop.
- **What is sounding, shown**: the piano highlights every sounding note,
  a release still ringing included; the level shows the peak before the
  clip and after; "Last note" at the transport's right.
- **Passage and chord texts** save and load as files; a patch's embedded
  note fills the Note tab when the file is opened.

## 2. The shape

**The engine change is in one class.** `LivePlayer` (`engine/render`)
learned that a held note on an instrument that does not sustain is its
own voice, released by its own id; `stop()`; and to report what is
sounding and what its nodes complained of. Nothing offline goes through
it, so no render could move, and none did.

**The transport's texts became engine functions, not editor code**
(`engine/perform/text_performance.h`). The old editor parsed a passage,
scheduled it and stamped breath, tongue and slur on it inside its window
code. That is now four plain functions that return a performance, with
the old rule as a table of ten cases in their test. A command-line tool
could call them tomorrow.

**A patch played offline with any performance** (`engine/play`), with its
nodes captured as strips. It is tied to the path the null gate holds: a
note played this way equals the embedded-score render sample for sample.

**The output plays sounds, two at once** (`editor/ui_audio`). `Sound` is
an interface of one function. The adapter holds two lines, the keys and
the playback, each through the one lock-free hand-over slice 1 built
(now a class, `SoundLine`, used twice), sums them and clips the sum once.
It no longer knows what a live player is. The three sounds are small
classes in `ui_control`: the keys' instrument, a render played back, a
plain sound running.

**Generate, Play and Stop are plain functions with tests** (`ui_control/generating.h`),
and the window's transport is a thin layer over them. The editor is now
6,292 lines of code and 3,313 of tests, its largest file 233 lines
(`generating.cpp`); the engine gained 1,845 lines, tests included.

## 3. The steps

| Step | What | Commit |
|---|---|---|
| 1 | The old Live, transport, chord and drum code read; the engine's seams read; the measured set counted. Sixteen places where the signed page and the code disagreed, each ruled on | (read only) |
| 2 | The live player | `b341e96` |
| 3 | Text to a performance; a patch played offline; a sound that runs | `99e5100` |
| 4 | The output's two lines; the three sounds; the transport, the take, Generate, Play, Stop | `15c115f` |
| 5 | The views | `5faeedd` |
| 6 | Review and fix wave | `e0a108f` |
| 7 | The parity list's 32 lines checked one by one; its wave of tests | `9875f8a` |

## 4. What was proven, and what no gate proves

Proven, by tests that run in the gates:

- **A held key on any instrument.** Eight held at once on the piano,
  released in a scrambled order, each release taking its own note and no
  other. A held note is the struck note, sample for sample, until it is
  let go. The three measured instruments that cannot hold fall back to a
  struck note.
- **No stuck note.** A seeded scramble (20261123) of about 8,000 random
  key-downs, key-ups, stops and fills, half on an instrument that
  sustains and half on one that does not, ends with nothing sounding; the
  same with the queue filled past its capacity.
- **The audio thread still allocates nothing**, through held notes,
  releases, the 3 ms cut and stop; and in the adapter through a swap.
- **The old passage rule, as a table**: ten cases (marks or none,
  sustaining or not, each onset mode, a rest inside a phrase, a repeated
  pitch), each note's onset and hold as the old code gave them.
- **The new offline path equals the gated one**: a note on a spread of
  16 of the 104 measured instruments, and a passage on 3, sample for
  sample over 706,890 frames. (It ran once over all 104, 3.5 million
  frames, 0 different, before it was thinned to keep the gates quick.)
- **A plain sound runs**: for each of the 27 measured ones, rendering
  block after block equals one render; its envelopes hold; nothing
  allocated.
- **The output's hand-over, twice**: 2,000 seeded swaps on both lines
  with nothing leaked, nothing destroyed on the audio thread and nothing
  played after it was handed back; a sum past 1 clipped once.
- **Generate, Play, Stop**: each tab on oboe1 and the piano; every
  refusal in its words; Play's rule per tab; a render at 22050 Hz played
  at 48000 at the right pitch and length; the WAV read back.
- **In the window, headless**: a click on Generate renders after the
  frame that says "Generating..."; Space plays and Space stops.
- **The engine did not move**: the null, allocation and comp gates pass
  at every step. At the close `python tools/gates.py` prints ALL GATES
  PASS (15,946 checks after the fix wave; 15,958 in the fast set after
  the last wave, which added tests only).

Almost nothing a hand or an ear does is under a gate. Under the headless
editor: a click on a tab, on Generate, on Play and Stop; Space; Delete; F.
Not under any test, and yours to try from REVIEW R30: what a render sounds
like; held keys at the sound card; the four file dialogs; the piano's
highlight while a release rings; the two levels on a loud chord; the
"Generating..." button seen with your own eyes while a long render blocks.

## 5. The meter, against the base

Against slice 2's close (`python tools/structure/check.py`):

| Measure | Before | After |
|---|---|---|
| files | 561 | 590 |
| lines | 69,410 | 73,153 |
| files over the limit | 8 | 8 |
| functions over the limit | 50 | 50 |
| file-level mutable variables | 22 | 22 |
| duplicate blocks (reported, not gated) | 169 | 154 |
| include violations | 0 | 0 |

Nothing under `editor/` is in the baseline. The gates cost more than they
did: `render_tests` went from 41 s to about 45 s (the live player's new
tests) and the fast set from about 70 s to about 95 s; the two tests that
had pushed it to three minutes were thinned to a spread in step 4.

## 6. Decisions the session made

The twenty rulings are the page's section 7; the ones worth your eye:

1. **Play stops everything first, keys too, and "playing" counts a
   ringing key** (the old order, which I had not read when I wrote the
   page's ruling 4). Keys pressed after Play sound over the render. So
   Space while a note rings is Stop: that is the panic.
2. **One clip, at the output.** The adapter clips the sum of its two
   lines, as the old callback did, so the keys' player is built with its
   own guard off.
3. **Drums are not normalised.** The old code meant to, and never did
   (its kit kept the guard on, so the peak never passed 1).
4. **Old faults not restored**: the Chords and Drums errors that the
   success line overwrote now stay on screen; a WAV is written at the
   render's rate, where the old one labelled every WAV with the sound
   card's.
5. **The Delay field starts at 15 and means what it shows** (old: 0,
   which meant 15). **PC Keyboard is on at start** (old: off). **The
   drum map starts with two library patches that exist** (the old
   default named files that are gone).
6. **`ui_control` may use `music` and `core`** beyond the two modules
   the page named, for the note parser and note-to-frequency: one
   function each, rather than a copy.
7. **A plain sound's envelopes are held on the patch record**
   (`held_patch`), before the build, not on built objects as the old
   stream did it.

8. **A plain sound that is running follows the patch** (a change the
   build reads, or the end of a drag of a number, restarts it from the
   patch as it now is). The review found that an edit was not heard until
   the next Play; the old stream read the live objects.
9. **A Note Play after another tab's Generate renders the note.** The old
   one replayed whatever buffer there was; the parity check found the
   difference and it is kept.
10. **The drum status counts the hits performed** ("Generated 4 hits");
    the old line's "x R repeats" always printed 1 and is not restored.

## 7. Debt, booked in CLEANUP

Rows 175 to 186: the structure meter reads a `= {}` default argument as
a file-level variable (worked round with an overload); the mixer ticks its
clock once per sample per channel, so block rendering equals one render
only while no gain or pan is wired to a moving signal (true of all 27
measured plain sounds); a running sound ends after two hours, and an
envelope with no sustain holds at its last stage, both as before; a patch
that sustains but cannot hold a voice can retrigger on key-up (none in the
measured set); a carry changes the note's velocity and not the voice's
gain; a note is dropped while a release waits for room in the queue; the
headless editor reaches the transport's tabs and buttons but not its
fields; Drums need the open patch to be an instrument, for the rate;
`perform_drums`' quarter-beat default cannot be reached from text; the
Note tab leaves room under its buttons because the strip keeps one
height; a view's fields are tested only as "drawing changes nothing"; and
the looks decided here.

## 8. The review

An Opus 5.5 agent that had seen neither the briefs' reasoning nor the
executor's reports read the range `cbf8626..5faeedd` against eight claims,
built it, ran every gate and wrote a demonstration program. It walked the
two threads of the live player and of the output's hand-over through the
orders it could construct (a key-up for a voice stolen, given back or
never started; a stop with a start still queued; a full queue; a sound
swapped while the one before fades) and found no race and no crash.

**No Major. Six Minor, all acted on:**
- A slur back the player refused (its queue full) left a note sounding
  with no key down and no way to release it but Stop. Shown with a
  program. Mended: a refused carry releases the line, and the sequence is
  a test.
- Play added the "render is at X Hz" sentence to the status again on
  every press. Mended.
- `core` had joined the control's reach with no ruling. Recorded (ruling
  21).
- A source file held raw NUL bytes, so git called it binary and no diff
  of it could be read. Mended.
- The drum status words were not the old ones. Recorded (ruling 22).
- The adapter's scramble test never silenced the keys' line. Mended.

**Notes**: a check that could not fail and a comment that claimed more
than its test, both mended; a plain sound that did not follow edits while
it ran, mended (ruling 23); a patch that sustains but cannot hold, booked
(CLEANUP 178).

**Then the parity list, line by line**: every one of the 32 lines the
list gives this slice is restored in the code (19 done; 12 done with a
part deliberately different, each traced to a ruling; 1 that no longer
applies). One difference had no ruling and got one (26). Seven lines of
the list had misdescribed the old editor and are reworded. What the check
found missing was proof: four things with no test under the editor, three
of which now have one (faults reaching the status line; a new patch's
default note through a save; the Note tab's composing). The fourth, a
click on a field under the headless editor, is booked (CLEANUP 181).

Not checked by the reviewer: anything at a real sound card; the envelope
hold against the old lines; three of the new test files read in full.

## 9. Lessons

### 9.1 An interface, in C++

```cpp
class Sound {
public:
  virtual ~Sound() = default;
  virtual void fill(float* stereo, int frames) noexcept = 0;
  virtual bool over() const noexcept { return false; }
};

class TakeSound final : public Sound {
  void fill(float* stereo, int frames) noexcept override;
  bool over() const noexcept override;
};
```

C++ has no `interface` keyword. A class whose functions are `virtual`
and `= 0` ("pure": no body here, a subclass must give one) is one. Word
by word against C#:

- `virtual ... = 0` is an interface method. `over()` has a body, so it is
  a C# default interface method: a subclass may leave it alone.
- `override` is C#'s `override`, and as there it is checked: misspell the
  name or change a parameter and the compiler refuses.
- `final` is `sealed`.
- `virtual ~Sound() = default;` has no C# analogue and is not optional.
  The adapter destroys a sound through a `Sound` pointer; without a
  virtual destructor that would run `Sound`'s destructor only and leave
  the `TakeSound`'s buffer behind. Rule of thumb: a class with a virtual
  function gets a virtual destructor.
- `noexcept` is a promise that the function throws nothing, which the
  audio thread needs: there is nobody there to catch.

The alternative was `std::variant<KeysSound, TakeSound, PlainSound>`, a
closed list the compiler checks at every use (a discriminated union).
It would be right if the set were fixed and the adapter needed to know
which it had. Here the adapter must NOT know: knowing would put the
engine's live player back into `ui_audio`'s header, which is what the
interface removed. This is the first interface in the editor, and the
test for the next one is the same: several unrelated things, one caller
that should not tell them apart.

### 9.2 `std::span`: lending an array

```cpp
int sounding_notes(std::span<int> into);   // how many it wrote

int notes[16];
int n = player.sounding_notes(notes);
```

A `std::span<int>` is a pointer and a length: a view of somebody else's
array, C#'s `Span<int>` exactly. The function writes into the caller's
storage and returns the count. The obvious signature,
`std::vector<int> sounding_notes()`, allocates a vector on every call,
and this one is called every frame on the thread that also feeds the
audio queue; the engine's rule is that the paths near the audio do not
allocate. The caller's `int notes[16]` lives on the stack and costs
nothing. The name `into` says which way the data flows.

### 9.3 A pointer that keeps something else alive

A drum is the output node of a patch's graph, and the graph is owned by
a `Voice`. The kit wants a `shared_ptr<ValueSource>` to the node; if the
`Voice` died the node would die with it. The standard library has a
constructor for exactly this:

```cpp
std::shared_ptr<Voice> voice = ...;
ValueSource* node = /* the voice's output node */;
std::shared_ptr<ValueSource> drum(voice, node);   // points at node, keeps voice alive
```

It is called the aliasing constructor: the new pointer shares the first
argument's ownership (the reference count of `voice`) and points at the
second. C# needs nothing like it, because the garbage collector keeps an
object alive as long as anything reaches it. In C++ lifetime is stated,
and this states "the node lives while the voice lives, and the voice
lives while the kit holds the drum" in one line, with no cast and no
second owner.

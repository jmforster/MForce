# Part 5 — render, the contract, play (report R22)

2026-11-22, refactor session XXIV. Design:
`docs/architecture/part5-render.md`, signed that day; its section 13 says
where the code differs from the signed text. Base `a2899cb`; commits
`ad463d8`, `eae609b`, `b517173`, `47d87db`, `a9e5677`, fix wave `fa085c3`.
Fable 5.2 wrote the design and the briefs, read each report and diff, ran
the meter and committed; one Opus 5.5 executor built all five steps and the
fix wave; a second Opus 5.5 agent reviewed the whole range.

## 1. What changed for you

- **One render moved, on purpose.** `scores/baselines/template_binary.json`
  has two sections, and every part of a piece was being played once per
  section. It is played once now. Before and after are in
  `renders/comp/audition/part5_replay_fix/` (peak 0.434 before, 0.315
  after; same length). Nothing else in a gate moved: the null gate's 77
  patches and the other 20 comp baselines are byte-identical at every
  step.
- **The command-line tool works as before.** Every mode goes through one
  `play` function. `tools/mforce_cli/main.cpp` went from 1,193 lines to
  951.
- **The engine can be played live again**, with nothing to play it yet:
  `LivePlayer` is the engine half of the editor's first slice. Its users
  today are the tests and the allocation gate, which now measures the real
  audio-thread path.
- **Nothing in `patches/` or `scores/` was written.**

## 2. The shape

| Module | Holds | May include |
|---|---|---|
| contract (new) | `PerformedNote`, `PerformedHit`, `Performance`, `PitchCurve`, `InstrumentClass` | nothing |
| render | instruments, voices, mixing, the live player, WAV | core, source, patch, contract |
| music | as before, with the performers, the `TimeMap`, the embedded score's reader | core, contract |
| play (new) | `play(piece, ensemble, conductor)`, `play(patch)` | everything above |

Render includes nothing of music and music nothing of render. A performer
hands a `Performance` (data) to an instrument through one method,
`Instrument::play`.

`render/instrument.h` was one all-public struct of about 800 lines. It is
now:

| Class | Job | Lines (header + source) |
|---|---|---|
| `Instrument` | the base: `play`, the mix of what was played | 61 + 42 |
| `Voice` | one clone of the graph, playable a note at a time | 138 + 223 |
| `VoicePool` | an instrument's voices and the rotation | 36 |
| `RingOut` | the one rule for when a voice is over | 77 |
| `NodeCapture` | per-node strips during an offline render | 42 + 16 |
| `PitchedInstrument` | plays a `Performance` on the pool; the held line | 80 + 167 |
| `DrumKit` | plays hits | 38 + 51 |
| `LivePlayer` | one instrument live behind `fill`; the hand-off | 148 + 256 |

`core/spsc_queue.h` (55 lines) is the lock-free queue the live
player's two directions use.

## 3. The steps

Every step ended with the build, `engine_tests`, `test_figures`, the tool
tests, the null gate, the allocation gate and the comp gate, run once by
the executor through one script (`renders/scratch/ab5/gates.py`).

| Step | Commit | `engine_tests` | Null gate | Comp gate |
|---|---|---|---|---|
| base | `a2899cb` | 11,820 checks | 77/77 | 21 same |
| 1 the contract | `ad463d8` | 11,820 | 77/77 | 21 same |
| 2 data across the boundary | `eae609b` | 11,872 | 77/77 | 21 same |
| 3 play, the replay fix | `b517173` | 11,900 | 77/77 | 20 same, `template_binary` moved |
| 4 the instrument split | `47d87db` | 11,925 | 77/77 | 21 same |
| 5 the live player | `a9e5677` | 11,967 | 77/77 | 21 same |
| the fix wave | `fa085c3` | 11,977 | 77/77 | 21 same |

The allocation gate was 0 of 131 at every step. From step 5 it plays each
instrument patch through `LivePlayer::fill`: its score's notes, then a held
line with a carried note and a release, then a steal.

## 4. What moved, and what no gate hears

- `template_binary`, as above.
- **The embedded score's order.** A patch's test score is now read in
  score order. The old reader delivered an unphrased note late when a
  phrased note (one with `hold` or an `onset`) followed it. No score in
  library or baselines mixes the two, so nothing in scope moved; a score
  that does would render differently. I decided this at step 2 and the
  design's list of what moves did not name it; the reviewer built such a
  score and measured the difference. It is recorded in the design's
  section 13.
- **Tool modes no gate runs.** `--chords` and `--play` were run by hand on
  `piano_default`. `--melody` and `--dun` are silent, as they were before
  this part (hand-built pieces have no events until Part 6). `--dun`'s
  output is longer by a truncated section's tail. `--josie` fails before
  it plays ("Unknown PitchDef offset: -8"), and did so before this part (a
  stored 4.6 binary gives the same line); its four patches are not in the
  tree.
- **Live against offline** is tested for one note and for one held line
  (with a carried note), sample for sample. A sequence of several fresh
  notes on a patch with more than one voice is not expected to equal
  `play` of the same notes: live takes an idle voice, offline rotates
  blind, and a voice's history is part of its sound.

## 5. The meter, against the base

| Measure | Before | After |
|---|---|---|
| Files over 600 lines | 16 | 13 |
| Functions over 80 lines | 62 | 56 |
| Include edges outside the map | 12 | 6 |
| `dynamic_cast` lines | 56 | 55 |
| Changelog comment lines | 99 | 78 |
| Duplicated blocks | 129 | 124 |
| Files | 246 | 270 |
| Lines | 54,235 | 55,612 |

No render, contract or play file is over 600 lines and none of their
functions is over 80. The six include edges left are music including JSON
(Part 6). Engine `dynamic_cast` lines fell by three (the Conductor's two,
the explorer's one); the tests gained two.

## 6. Decisions the session made

1. The embedded score's reader emits in score order (section 4).
2. `play` throws on a piece with no section, and on one none of whose
   parts has an instrument; the tool wrote silence.
3. `PlayResult` carries `mixPeak`, `scale`, `buildMs`, `renderMs` beyond
   the signed fields, so the tool's printed lines keep their text.
4. `TimeMap` does not read a section's truncated tail; the composer and
   the map share one function for a section's start beat. Truncation's
   effect on time is Part 6's.
5. A voice counts as released, for a steal, when no key holds it: a
   classic note from its start, a line from its key-up.
6. The live player checks for an idle voice and for queue room before it
   steals; keeps a key-up it cannot queue; picks idle voices in rotation.
7. `Voice` shows what its build made as a description by node id, not as
   pointers; `LivePlayer` is a `friend` of `PitchedInstrument`.
8. The meter's known-answer test pinned the ring-out duplicate this part
   removed. It now finds the same shape in a fixture.
9. The tool's score warnings print with the `  warning: ` prefix the build
   warnings have; the refusal of an articulation on a phrased note no
   longer names a spec section.

## 7. Debt, booked in CLEANUP

- A held line makes a live instrument monophonic (the engine has one line
  per instrument). The editor's design decides what a held key on a
  polyphonic patch does.
- The live player trusts a note's `hold`; the note-at-a-time performer
  that applies the piano rule comes with the editor.
- `--josie` is dead at the base; `--melody` and `--dun` are silent until
  Part 6.
- `Mixer::render` allocates its scratch per call: fine offline, in the
  way when a live player sits under a mixer.
- The tests' note literals repeat as long lines.

## 8. The review

One independent read of `a2899cb..a9e5677` by an agent that wrote none of
it: 0 Critical, 1 Major, 8 Minor, 7 suspicions.

- **Major:** the embedded score's order (section 4). Recorded, not changed.
- **Minor, fixed in `fa085c3`:** the steal chose by start time where the
  design says release time; a steal could be spent on a note the queue
  then refused; a key-up the queue refused was lost; `Voice`,
  `PitchedInstrument` and `DrumKit` exposed internals only tests read; a
  header comment claimed more than the code; no test ran the player on two
  threads; history in two rewritten files' comments.
- **Minor, recorded:** `--dun`'s length.
- **Suspicions acted on:** idle voices now rotate; a note id is never 0;
  an unreachable branch is gone; `play.h` says a second `play` on the same
  instruments continues their voices' state.
- **Found sound:** the queue's ordering and arithmetic; the hand-over (a
  voice is started before it is sent and reused only after it comes back);
  no allocation or last-reference drop on the audio thread; no mutable
  state shared between voices in any node; `RingOut` equal to the two old
  loops; the `TimeMap` at one tempo; the three module boundaries.

What the review could not prove: the queue's memory ordering cannot be
shown wrong or right by a test on x86, where stores keep their order. The
claim rests on the code as read by two readers and on one two-thread test
that exercises the hand-over.

## 9. C++ lessons

### 9.1 `friend`: `internal`, for one named class

`PitchedInstrument` keeps its voices private and shows the world a
read-only `pool()`. `LivePlayer` must start and release those voices. C#
would mark the member `internal` and let the whole assembly in. C++ has no
assembly visibility; it has `friend class LivePlayer;`, written inside
`PitchedInstrument`, which lets exactly that class reach the private
members. The alternative was a public, mutable `pool()`, which let any
holder of an instrument start a voice in the middle of a render. `friend`
is the narrower door, and the class that grants it names who comes
through.

### 9.2 Designated initializers: C#'s object initializer, in order

```cpp
PerformedNote n{.start = 0.4f, .duration = 0.5f, .noteNumber = 60.0f, .hold = true};
```

is C#'s `new PerformedNote { Start = 0.4f, Duration = 0.5f, ... }`. Two
differences. The names must appear in the order the struct declares them,
and a compiler rejects any other order. And it works only on an
*aggregate*: a struct with no constructor of its own. That is why
`PerformedNote` has default values on its members and no constructor:
adding one would turn every such line into an error. Members left out
take their defaults, so a member added to the note later needs no visit to
any call site, where a positional `{60.0f, 0.8f, 0.4f, 0.0f, true}` would
silently shift.

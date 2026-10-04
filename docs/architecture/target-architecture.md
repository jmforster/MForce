# MForce target architecture

Status: **Part 1 (overall shape), draft for Matt's review, 2026-10-04.**
Nothing here is built. Parts 2 onward detail each area and are written
from the code itself; three of them are decided at the highest effort
setting (marked below).

Sources for Part 1: the 2026-10-02 review, measurements of the tree taken
on 2026-10-04, the audit's outline of the UI file, and the structure of
the legacy Unity project (`../mforce-unity`). I have not yet re-read most
engine code first-hand for this document; each area's part will be.

## 1. What this document is for

It is the destination for the surgery phase: the modules the code should
end up in, what each is for, what each may depend on, and what gets
deleted. Matt signs it before cutting starts. The route is free; this is
what "arrived" means.

## 2. Principles the layout follows

1. **One job per module, one job per class.** A module that draws does
   not open audio devices. A class that holds a patch does not know JSON.
2. **Dependencies point one way.** Lower layers never know the layers
   above them.
3. **One of each thing.** One patch file codec. One pan law. One ring-out
   rule. One place a DSP primitive is written.
4. **The engine is the product; tools are thin.** Anything two tools both
   need lives in the engine.
5. **Boundaries the build enforces.** Each module is its own library
   target where that is practical, so a forbidden include is a compile
   error, not a review finding.
6. **Structure and behaviour never change in the same checkpoint.** A
   move must render byte-identical. A bug fix lists the renders it is
   expected to change.

## 3. The layers

```
                      tools:  mforce_cli   mforce_ui   tests
                                  │            │          │
        ┌─────────────────────────┴────────────┴──────────┘
        ▼
  perform   (Conductor: turns a composed Piece into played notes)
        │                         │
        ▼                         ▼
  compose  (Composer,          render  (instruments, voices, mixer,
   strategies, builders)          offline and live rendering)
        │                         │
        ▼                         ▼
  music model (pitch, scale,   patch   (the patch document, its one
   chord, figures, Piece)         file codec, the graph builder)
        │                         │
        │                         ▼
        │                      nodes   (every ValueSource: oscillators,
        │                         noise, additive, physical, filters)
        │                         │
        └────────────┬────────────┘
                     ▼
                   core   (the ValueSource contract, shared primitives)
```

This is your own three-tier idea made into layers: **Composition**
(music model, compose) → **Performance** (perform) → **Realization**
(render, patch, nodes).

One edge the diagram does not draw: render uses a few basic value types
from the music model (a pitch, an articulation). The model is plain data
and depends on nothing above core, so that edge is allowed.

### 3.1 core

- **Purpose:** the ValueSource contract and the primitives every node
  shares.
- **Holds:** `ValueSource`, descriptors, `RenderContext`, `Envelope`,
  curves, the random source, fast math, the node registry.
- **New here:** one home for the DSP primitives that are currently
  re-typed per node (band-limited step, tuning allpass, DC blocker, bow
  friction, biquad coefficient formulas).
- **Depends on:** nothing.
- **Changes from today:** the contract itself is revised (Part 2). The
  registry stops including JSON; per-type JSON quirks move to the patch
  module.

### 3.2 nodes (today's `source` and `filter`)

- **Purpose:** every concrete ValueSource.
- **Holds:** oscillators, noise, wavetable and its evolutions, additive
  (partials, formants), physical models, filters, combiners, modulators,
  grouped in folders by family.
- **Depends on:** core, and other nodes it is built from.
- **Changes from today:** `source` and `filter` become one layer, since
  an FM oscillator already uses a filter and vibrato already uses a noise
  source. Oversized files split by family (`wave_evolution.h` is 1,552
  lines; `partials.h` is 1,486). The thirteen copy-pasted evolution
  holders become one. Real-time violations are removed.

### 3.3 patch (new)

- **Purpose:** what a patch *is*, independent of how it is stored, drawn
  or played.
- **Holds three things:**
  - `PatchDocument`: the nodes, their settings, the connections between
    them, groups, and the instrument and score blocks. Plain data. No
    JSON, no drawing, no audio.
  - `PatchCodec`: the only code that reads or writes the patch file
    format. Legacy forms are converted here at load, or removed
    (section 5).
  - `GraphBuilder`: turns a `PatchDocument` into a live graph of
    ValueSources through the registry, wiring by descriptor.
- **Depends on:** nodes, core.
- **Changes from today:** this replaces the engine loader's two entry
  points and 17-branch type chain, the UI's loader, the UI's two savers
  and its clipboard schema. The command-line tool, the UI and the tests
  all go through it. **Decided at maximum effort (Part 4).**
- **Names:** "Document" because it is what Open and Save act on; "Codec"
  because it codes and decodes one format.

### 3.4 render

- **Purpose:** play a built graph as notes, offline or live.
- **Holds:** the instrument and its voices, the mixer, the limiter, the
  performance inputs (wheel, pressure, `PerformedNote`, pitch bend, pitch
  curve), WAV reading and writing, and one "render this to a buffer and
  report its statistics" service that every tool uses.
- **Depends on:** patch, nodes, core, and the music model's basic value
  types.
- **Changes from today:** `PitchedInstrument` (620 lines, all public, ten
  responsibilities) is split into the voice pool, the voice renderer, the
  ring-out rule (one function instead of three copies) and capture. The
  live path gains a hand-off that lets a voice be prepared off the audio
  thread and published without a lock. **Decided at maximum effort
  (Part 5).**

### 3.5 music model

- **Purpose:** the value types of music.
- **Holds:** pitch, scale, chord, meter, figures, the `Element` variant,
  the Piece / Section / Part / Passage / Phrase tree, the harmony
  timeline, templates as plain data.
- **Depends on:** core, for the random source only. Measured: 20 of the
  music headers' 22 includes of core are the random source; the other two
  belong to pitch bend, which moves to render.
- **Changes from today:** harmony context is stored once per section, not
  twice. Strategy configuration becomes a closed variant instead of four
  "only one is filled in" optional fields. JSON moves out to its own
  files, as with patch.

### 3.6 compose

- **Purpose:** produce a Piece from templates.
- **Holds:** `Composer`, the strategies, the figure builders, voicing,
  walkers, and their registries.
- **Depends on:** music model.
- **Changes from today:** `composer.h` (2,081 lines, more than half of it
  the bodies of strategies declared elsewhere) is split so each strategy
  lives in its own file. `compose()` stops writing to the template it
  promised not to change. The realize step that turns a hand-built Piece
  into events becomes public, which is what makes DUN scores audible
  again.

### 3.7 perform

- **Purpose:** turn a composed Piece into played notes on instruments.
- **Holds:** `Conductor` and the performers.
- **Depends on:** music model, render.
- **Changes from today:** this removes the two-way dependency between
  music and render. Today `render/instrument.h` includes two music
  headers (pitch bend and pitch curve) while `music/conductor.h` includes
  the instrument. Pitch bend is itself a ValueSource, and pitch curve
  depends on nothing, so both move down into render beside
  `PerformedNote`, which the instrument already consumes. The Conductor
  moves up here.

### 3.8 codecs and text formats

JSON for music types (`music_json`, `templates_json`, style tables) and
the DUN / DURN text parsers sit beside the modules they serve as separate
files, and are the only music files allowed to include JSON.

## 4. The UI

### 4.1 Why it lost its shape

Your Unity version had the shape for free: the xNode framework supplied
the graph, node and port classes and a separate editor class, and you
added a controller and an audio adapter. The C++ port used imnodes, which
only draws. Nothing supplied a model, so the model became file-level
variables.

One thing the port did improve: your Unity project needed a node class
per node type, and for many a wrapper class as well (dozens of files).
The C++ version drives every node type from the engine's descriptors
with one generic node. That stays.

### 4.2 Target modules

| Module | Purpose | Unity equivalent | May use |
|---|---|---|---|
| `ui_model` | `EditorDocument`: a `PatchDocument` plus what the editor adds (positions, the group being viewed, selection, dirty state). Every edit goes through its operations: add, delete, rename, connect, group, replace, duplicate, paste. | `MNodeGraph`, `MNode` | engine patch module |
| `ui_audio` | `AudioAdapter`: the audio device, the callback, live voices, MIDI input. | `AudioAdapter` | engine render module, RtAudio, RtMidi |
| `ui_control` | Four small classes, not one controller: `Generator` (turn the transport's note, passage, chords or drums text into a render request and run it), `Playback` (play and stop the rendered buffer), `LivePlay` (keyboard and MIDI notes to live voices), `Audition` (walk a folder of renders). | `SoundController`, split | `ui_model`, `ui_audio`, engine |
| `ui_views` | One class per window: node editor, properties, shape editor, transport, keyboard, waveforms, spectrum, partials and formant strips, audition, menus. | `MNodeEditor`, `UI/V*` | `ui_model`, `ui_control`, ImGui |
| `ui_app` | `main`: window and ImGui start-up, dock layout, the frame loop, settings, crash logging. | Unity itself | all of the above, GLFW |

- **Names:** `AudioAdapter` is your name for the same role, kept on
  purpose.
- **`SoundController` is not copied.** Matt's note, 2026-10-04: it was
  something of a god class itself. Its 822 lines mix generating (note,
  passage, chords, drums, piece), playback, recording, instrument
  selection, live notes, display zoom and scroll, duration parsing and an
  FFT. In the target those are separate: the four classes above, with
  duration and passage parsing in the engine's text formats, the FFT in
  the engine's analysis helpers, and zoom and scroll as state of the
  waveform view.
- **Separation of concerns, enforced:** each module is a library target.
  `ui_views` cannot include the audio driver and `ui_audio` cannot
  include ImGui, because the build does not give them those headers.
- **One renderer.** Today the UI builds and streams its own DSP graph for
  Stream and Listen while Play and Generate use the engine's. In the
  target every sound the UI makes comes from an engine-built graph, so
  "the UI sounds different from the command line" cannot happen.
- **Edits stop round-tripping through text.** Today a parameter edit is
  serialize, parse, rebuild every voice. In the target the UI changes the
  document and the engine applies the change.

### 4.3 The headless checks

`mforce_ui` has twelve command-line modes (roundtrip, generate check,
gate check, several dumps). Most exist to detect the UI and the engine
disagreeing. With one codec and one renderer most of them have nothing
left to detect and are deleted; what remains becomes ordinary engine
tests of `PatchCodec`. Until the UI is cut over, they stay, because they
are the UI's only behaviour gate.

## 5. What gets deleted

Your veto applies to every line. Items marked **(ask)** I cannot judge
without you.

**Engine**

- `BasicAdditiveSource`: fully covered by `AdditiveSource` with
  `FullPartials`.
- `AdditiveSource2`: a half-finished port with a pitch bug; one baseline
  patch uses it and would be migrated.
- The first-wired-consumer advancing rule, `RefSource` wrapping,
  starved-reference promotion and advance lists, if the per-tick memo is
  adopted (Part 2).
- Two of the three legacy paramMap implementations. **(ask)** If every
  patch you care about is converted on disk once, the third goes too and
  the file format has one form.
- The older string-typed way of attaching an evolution to a wavetable;
  the node form stays.
- Dead composition code: the `IComposer` / `Genre` facade, the unused
  `SectionStrategy`, the realization-strategy registry that is parsed but
  never read, unused `Composer` members.
- The duplicated `WaveSourceMono` / `ValueSourceMono` pair (one stays).

**UI**

- One of the two clipboards (the fragment format stays).
- The second render path for Chords and Drums.
- The UI's own streaming DSP graph and its hand copies of the mixer's
  pan law.
- The legacy paramMap editor and the half of the Mappings dialog that
  still creates paramMap entries. **(ask)**
- Node-graph mode and the conversion between it and patch mode.
  **(ask: do you still use node-graph mode?)**
- The stale-build stamp guard and its test tool, replaced by a build
  rule that never links over the running program.

**Tools**

- The MusicXML path in `durn_converter`, which is dead: every file
  throws. **(ask: are the ABC, kern and MIDI converters still wanted?)**
- `test_figures` as a separate program; its tests move into the engine
  test suite.
- **(ask)** `ppl_to_json` and the pattern-library strategy, given the
  comp ground rules shelved every generator except the Markov figure
  one. Shelved code could move out of the engine instead of being
  deleted.

**Everywhere**

- Comments that carry dates, backlog ids or REVIEW numbers (about 276
  lines).

**Not deleted, although the review flagged them:** anything whose removal
changes how a patch sounds, such as the hidden high-note loudness boost.
Those are listening decisions, not surgery.

## 6. Bugs fixed along the way

These change output, so each gets its own checkpoint with the affected
renders listed:

- Conductor replays every part once per section.
- Hand-built (DUN) pieces render silence.
- Chord voicing spins to integer overflow on dictionary voicings.
- The UI's pan law differs from the engine's by 3 dB at centre.
- An oscillator throws on a non-positive frequency, which ends the
  process if it happens on the audio thread.
- Three noise types have different defaults from the command line and
  from the UI.
- The remaining confirmed critical and high findings in the review.

## 7. Decisions that get their own part

| Part | Decision | Effort |
|---|---|---|
| 2 | **The ValueSource contract.** Your per-tick memo (`next(tick)`, each node remembers its last tick and value); splitting "reset for a new note" from "here is the note length" in `prepare`; one source of truth for sample rate; no throw on a render path; a wire that is rejected says so. | maximum |
| 3 | nodes: family layout, shared primitives, the evolution holders, the additive classes. | extra-high |
| 4 | **The patch document, the codec and the graph builder**, including how the UI's editor document sits on top of it. | maximum |
| 5 | **Render:** the instrument split and the lock-free live hand-off. | maximum |
| 6 | Music: the model / compose / perform split, harmony stored once, strategy configuration, the spine fixes. | extra-high |
| 7 | UI: modules, the behaviour inventory, cut-over order. | extra-high |
| 8 | Build and tests: targets per module, warnings as errors, third-party sources, test layout, the meter. | extra-high |

## 8. Order of surgery

Areas overlap; this is the intended order, not a rule.

1. The meter, CTest registration, and one command that runs every
   behaviour gate.
2. core contract and nodes (Parts 2 and 3). Everything else stands on
   them.
3. patch (Part 4), with the command-line tool moved onto it first.
4. render (Part 5).
5. The UI rebuilt on patch and render (Part 7).
6. Music (Part 6). It is nearly independent of 2 to 5 and can interleave.
7. Build, tests and the comment purge (Part 8), then phase 2 gates on.

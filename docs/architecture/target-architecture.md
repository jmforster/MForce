# MForce target architecture

Status: **Part 1 (overall shape) decided with Matt, 2026-10-05.** He
annotated the 2026-10-04 draft with seventeen notes; each was debated and
decided one at a time, and every decision is written in place, dated.
Nothing here is built. Parts 2 onward detail each area and are written
from the code itself; three of them are decided at the highest effort
setting (section 7).

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

**Decided 2026-10-05: the tiers are joined by data, not by calls.**
Composition produces a Piece. Performance turns a Piece into performed
notes. Realization turns performed notes into audio. Performance knows
nothing about an instrument except the class it declares (section 3.9).
Render knows nothing about music.

| Module | Does | May use |
|---|---|---|
| play | Runs perform and render end to end, as one function | everything below |
| compose | Produces a Piece from templates | voicing, music model |
| perform | Turns a Piece into performed notes | voicing, music model, contract |
| render | Turns performed notes into audio | contract, patch, nodes, core |
| patch | The patch, its one file codec, the graph builder | contract, nodes, core |
| voicing | Turns a chord symbol into pitches | music model |
| music model | The value types of music | core |
| contract | The types that cross from perform to render | nothing |
| nodes | Every ValueSource | core |
| core | The ValueSource contract and shared primitives | nothing |

This is your three-tier idea made into layers: **Composition** (music
model, voicing, compose), **Performance** (perform), **Realization**
(render, patch, nodes).

**Voicing may be decided in either Composition or Performance** (Matt,
2026-10-05). Today only Composition does it. `Chord` already carries both
the symbol (root, definition, inversion, spread) and the voiced pitches,
so a performer that prefers its own voicings needs no new data. The only
consequence now is placement: voicing is its own module beneath both
tiers (section 3.10). Whether Composition's output is a score or a lead
sheet stays parked in IDEAS.md.

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

**RenderContext today** (guide, 2026-10-05)

- It is a struct with one field: the sample rate.
- It is handed down the graph when a note is prepared:
  `prepare(context, frames)`. Each node passes it on to its inputs.
- It is the C++ form of `MConfig.VALUES_PER_SECOND`. The C# `Prepare(count)`
  became `prepare(context, frames)`: the same note length, plus an object
  carrying the rate in place of a global constant.
- It once also carried the note-off position. That was removed on
  2026-08-13, when release moved inside the note.
- Nothing owns it. Forty places build one on the spot from whatever
  sample rate they have: the instrument (6 places, once per note), the
  command-line tool (9), the UI (5), the tests (19), and one node.
- Almost nothing reads it. Five nodes take the rate from it: the additive
  partials, the full additive source, the 2D mesh, and the bowed-string
  and brass evolutions. Every other node was given the rate when it was
  constructed, as the C# `Rate` field worked: 44 constructors in 29 files
  take a sample rate.
- So the sample rate has two sources of truth. They agree only because
  every caller passes the same number to both.
- `frames`, the second argument, is the note's length. It is not part of
  the context but always travels with it.

Part 2 decides its fate: either it becomes the only source of the sample
rate (what a plugin host needs, since the host can change the rate), or
the five readers take the rate at construction like every other node and
the type is deleted.

Two unrelated types share the word: `PerformContext` (in the patch
loader: a voice's note inputs as wireable sources) and `KeyContext`
(music: harmony).

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

**Source versus Filter (decided 2026-10-05)**

- "Filter" has never been a type, in C# or in C++. It was a folder. Since
  the port the folder no longer says what a node does: nodes that process
  another node's signal live in 19 files, 6 under `filter`, 11 under
  `source` and 2 under `core`.
- Three hand-kept classifications disagree today: the folder, the
  category each type reports (used only for node colour and to recognise
  envelopes), and the UI's hand-written create menu of about 75 entries.
- **One layer.** `source` and `filter` stop being a dependency boundary.
- **One classification.** Each type declares its family once, where it is
  registered. Folder, colour and the create menu all follow from it; the
  menu is generated from the registry.
- **"Filter" survives as a family name.**
- The family list and its membership are Matt's and are settled in Part
  3, starting from today's menu families (Generators, Noise, Wavetable,
  Additive, Envelopes, Filters, Combiners, Loop).
- Held for Part 3: whether nodes that take a signal input share a base
  class. Matt, 2026-10-05: such a base would own the input source and
  nothing else; there is no shared processing step to put in it. (Unlike
  oscillators, which share a phase accumulator, these nodes share only
  the fact of having an input: many have several inputs or modulated
  pins, and a delay line in a feedback loop depends on evaluation order.)
- For Part 2, because it is true of every pin on every node and not only
  of inputs: each pin is declared by hand in five places (descriptor
  table, set, get, prepare, advance). Solving that once in the
  ValueSource contract may leave an input-owning base with nothing to do.

### 3.3 patch (new)

**Names decided 2026-10-05.** Two vocabularies that never mix:

| | Description (plain data) | Live (makes sound) |
|---|---|---|
| One node | `Node` | `ValueSource` |
| The wiring | `Graph` | a voice |
| The whole thing | `Patch` | `Instrument` |

Building a Patch gives an Instrument. Today the words are crossed: in
code `Patch` and `InstrumentPatch` are both built things, and the UI's
`GraphNode` is description and live object at once.

- **Purpose:** what a patch *is*, independent of how it is stored, drawn
  or played.
- **Holds:**
  - `Patch`: a `Graph` (the `Node`s, their settings and connections, and
    which node is the output), plus the instrument block, the optional
    score, and the layout (positions, groups). Plain data; no JSON, no
    drawing, no audio. The file format already has this shape: all 134
    tracked patches have a `graph` block and 107 also have an
    `instrument` block.
  - The edit operations, as methods of `Patch`: add, delete, rename,
    connect, group, replace, duplicate, paste. `Patch` keeps itself
    valid. Today these exist only inside the UI file, which is why the UI
    has headless modes named rename, replace and paste so tests can reach
    them.
  - `patch_json.h`: the only code that reads or writes the patch file
    format. A `to_json` / `from_json` pair of functions, as in
    `music_json.h`, `templates_json.h` and `envelope_json.h`, plus
    `load_patch(path)` and `save_patch(path, patch)`. Not a class: it has
    no state and one implementation.
  - `build_instrument(patch)`: turns a `Patch` into a live `Instrument`
    through the registry, wiring by descriptor. A function, for the same
    reason.
- **Depends on:** contract, nodes, core.
- **Changes from today:** this replaces the engine loader's two entry
  points and 17-branch type chain, the UI's loader, the UI's two savers
  and its clipboard schema. The command-line tool, the UI and the tests
  all go through it. The two built-result structs named `Patch` and
  `InstrumentPatch` go away. **Decided at maximum effort (Part 4).**

### 3.4 render

- **Purpose:** turn performed notes into audio, offline or live.
- **Holds:** the instrument and its voices, the mixer, the limiter, the
  live performance inputs (wheel, pressure), the function that compiles a
  `PitchCurve` into an Envelope, WAV reading and writing, and one "render
  these performed notes to a buffer and report its statistics" service.
- **Depends on:** contract, patch, nodes, core. Nothing from music.
- **Changes from today:** `PitchedInstrument` (620 lines, all public, ten
  responsibilities) is split into the voice pool, the voice renderer, the
  ring-out rule (one function instead of three copies) and capture. The
  live path gains a hand-off that lets a voice be prepared off the audio
  thread and published without a lock. **Decided at maximum effort
  (Part 5).** `Instrument` gains one entry point that accepts a part's
  performed notes. Pitched instruments and drum kits each implement it,
  which removes the type test the Conductor does today.

**Capture** is not live playing. It records what individual nodes put
out during an offline Generate, so the UI can draw the per-node waveform
strips. The live path never touches it.

**The live hand-off (agreed in principle 2026-10-05; details in Part 5)**

- Today the audio callback takes a mutex for its whole buffer, and the
  UI thread holds the same mutex while it picks a voice, walks its graph,
  resets every node and writes it into the table the callback reads. The
  lock exists because a voice's graph is shared between the two threads.
- **Rule: a voice is touched by exactly one thread at a time. Ownership
  is passed, never shared.**
- Two queues, one each way. The audio thread only ever does a
  non-blocking receive, at the top of each callback.
- Starting a note: the control side prepares an idle voice it owns, with
  no lock and allocation allowed, then sends "start this voice".
- Changing a sounding voice (release, slur to a new pitch, fade out) is a
  message with the numbers already worked out. Nothing the audio thread
  does in response allocates.
- A finished voice is sent back for reuse or disposal. Stealing is "fade
  out and return"; the pool keeps spare voices so a new note never waits.
- Buffer playback and streams use the same messages.
- A live-playing class in render owns the queues and the mixing loop and
  exposes one function, "fill this buffer". The ring-out, cap fade, pan
  law and soft clip then exist once. The UI's `AudioAdapter` opens the
  device and calls that function; a plugin host can call the same one.
- The engine's in-place calls (`acquire_voice`, `prepare_voice_at`,
  `continue_voice_live`) are replaced by check out, prepare, send.
- Timing is unchanged: a note starts at the next buffer boundary.
- Part 5 pins down the message set, the pool size and the steal policy.

### 3.5 music model

- **Purpose:** the value types of music: what Composition produces and
  Performance reads.
- **Holds:** pitch, scale, chord, meter, figures, the `Element` variant,
  the Piece / Section / Part / Passage / Phrase tree, the harmony
  timeline.
- **Does not hold the templates** (decided 2026-10-05). Templates are
  instructions to the composer, not music; they move to compose.
- **Depends on:** core, for the random source only. Measured: 20 of the
  music headers' 22 includes of core are the random source; the other two
  are in `pitch_bend.h`, which is split between perform and render
  (section 3.7).
- **Changes from today:** harmony context is stored once per section, not
  twice. JSON for these types stays in its own file (`music_json.h`).

### 3.6 compose

- **Purpose:** produce a Piece from templates.
- **Holds:** the templates (`PieceTemplate`, `PassageTemplate`,
  `PhraseTemplate`, `FigureTemplate`, the motif pool) and their JSON;
  `Composer`; the strategies and their settings; the figure builders;
  walkers; and their registries.
- **Depends on:** voicing, music model.
- **Why the templates are here:** only the composer, the strategies and
  their helpers use them (19 headers), plus the tools. The Conductor and
  everything a performer reads never touch them.
- **Changes from today:**
  - `composer.h` (2,081 lines, more than half of it the bodies of
    strategies declared elsewhere) is split so each strategy lives in its
    own file.
  - The realize step that turns a hand-built Piece into events becomes
    public, which is what makes DUN scores audible again.
  - **Strategy settings.** Today a template names a strategy by a string
    and carries a slot for every known strategy's settings:
    `PhraseTemplate` has four optional settings structs of which "only
    one is populated at a time", `PassageTemplate` has six more, and the
    voicing selector's settings are a raw JSON blob that each selector
    parses itself. The name and the filled slot can disagree unchecked,
    and adding a strategy means editing the central struct and its JSON
    code. Direction (form decided in Part 6, after reading the strategy
    headers): strategies are an open set, so each strategy owns its own
    settings type and the template holds "this strategy and its settings"
    as one unit. Adding a strategy then touches no central file.
  - **What the composer writes back.** Today `compose()` takes its
    template as read-only and writes to it anyway: the realized motifs go
    into the template's motif pool; each strategy's planned passage is
    stored back "for regeneration / lock semantics"; and walker-generated
    harmony is written into the Piece's Section through a read-only
    pointer, into both copies of that section's harmony. The template is
    serving as both the request and the record of what was decided,
    which is the mechanism for generate, review, lock, regenerate. Part 6
    chooses between declaring the template as in-and-out, and returning
    the record separately so "what was asked for" and "what was decided"
    are distinct objects.

### 3.7 perform

- **Purpose:** turn a composed Piece into performed notes.
- **Holds:** `Conductor` and the performers (note, chord, drum). Between
  them: articulations, ornaments, slide runs, chord strum figures,
  humanize, beats to seconds, and the onset and hold rules.
- **Produces:** per part, a time-ordered list of performed notes or drum
  hits, in seconds.
- **Knows about an instrument:** only its declared class (section 3.9).
- **Depends on:** voicing, music model, contract. Not render.
- **Changes from today:**
  - Performers stop calling the instrument; they return data.
  - One performer replaces four partial ones: `conductor.h`, the patch
    loader's embedded-score path, the UI's `stamp_passage`, and the UI's
    live-key rule. Composed pieces gain onsets and holds as a result.
  - `Conductor` keeps its role of reading the score and directing the
    performers. Its instrument registry and its loop over sections move
    to play (section 3.11).
  - `pitch_bend.h` is split and the name retires. It is not a type. It
    holds three performer functions (bend, bend-mordent and slide run,
    each to a `PitchCurve`), which stay here, and one realization
    function (curve to Envelope), which moves to render.
  - Live keys go through a small note-at-a-time performer that emits the
    same output type.

### 3.8 codecs and text formats

JSON for music types (`music_json`, `templates_json`, style tables) and
the DUN / DURN text parsers sit beside the modules they serve as separate
files, and are the only music files allowed to include JSON.

### 3.9 contract

- **Purpose:** the types that cross from Performance to Realization.
  Perform and render both include this module; neither includes the
  other.
- **Holds:**
  - `PerformedNote` (exists today in the instrument header): note number,
    velocity, seconds, onset id, hold, optional pitch curve. The curve
    becomes owned data; today it is a pointer to a variable that lives
    only for the length of one call.
  - `PitchCurve` (exists today under music): the one representation of a
    pitch gesture. Semitone offsets over fractions of a note's length.
  - A performed drum hit.
  - `InstrumentClass`: what a performer is allowed to know about an
    instrument. **Decided 2026-10-05:** a real type now, in seed form. It
    holds the two facts a patch declares for performers today,
    `sustaining` and the ordered onset vocabulary, plus the lookup from
    an onset name to its id (position in the list; 0 for unknown). It is
    filled from the patch's own instrument block. Nothing else is added
    until a performer rule needs it.
- **Depends on:** nothing.

Not built until the feature freeze lifts (backlog 78): named classes,
Parts naming a class, and checking an instrument against its Part's
class. Until then a performance is valid for the instrument it was made
for, because the class comes from that instrument's own patch.

### 3.10 voicing

- **Purpose:** turn a chord symbol into pitches.
- **Holds:** the voicing selectors and profiles (eight headers today).
- **Depends on:** music model.
- **Changes from today:** it moves out from under compose so that perform
  can use it too. Nothing new is built.

### 3.11 play

- **Purpose:** one function that runs the tiers end to end: perform each
  part against its instrument's class, hand each part's performed notes
  to its instrument, and mix.
- **Holds:** the mapping from parts to instruments, and the loop over
  sections and parts.
- **Depends on:** everything below it.
- **Changes from today:** it replaces seven hand-written copies in the
  command-line tool, the UI's chord path, and the note player inside the
  patch loader. A patch's embedded score becomes a small score handed to
  this function, so the patch module no longer includes the Conductor.
  The Conductor's replay-once-per-section bug is fixed here.

## 4. The UI

### 4.1 Why it lost its shape

Your Unity version had the shape for free: the xNode framework supplied
the graph, node and port classes and a separate editor class, and you
added a controller and an audio adapter. The C++ port used imnodes, which
only draws. Nothing supplied a model, so the model became file-level
variables.

One thing the port did improve: your Unity project needed a class per
node type, twice over. The wrapper classes were the first generation
(patches built in the Unity inspector) and the node classes the second.
The C++ version drives every node type from the engine's descriptors
with one generic node. That stays.

### 4.2 Target modules

| Module | Purpose | Unity equivalent | May use |
|---|---|---|---|
| `ui_model` | `OpenPatch`: the `Patch` open in the editor, the file it came from, whether it has unsaved changes, and later undo. The edits themselves are `Patch`'s own operations in the engine. | `MNodeGraph`, `MNode` | engine patch module |
| `ui_audio` | `AudioAdapter`: the audio device, the callback, live voices, MIDI input. | `AudioAdapter` | engine render module, RtAudio, RtMidi |
| `ui_control` | Three small classes, not one controller: `Transport` (turn the transport's note, passage, chords or drums text into a render request, run it, play and stop the result), `LivePlay` (keyboard and MIDI notes to live voices), `Audition` (walk a folder of renders). | `SoundController`, split | `ui_model`, `ui_audio`, engine |
| `ui_views` | One class per window: node editor, properties, shape editor, transport, keyboard, waveforms, spectrum, partials and formant strips, audition, menus. | `MNodeEditor`, `UI/V*` | `ui_model`, `ui_control`, ImGui |
| `ui_app` | `main`: window and ImGui start-up, dock layout, the frame loop, settings, crash logging. | Unity itself | all of the above, GLFW |

- **Names:** `AudioAdapter` is your name for the same role, kept on
  purpose.
- **`SoundController` is not copied.** Matt's note, 2026-10-04: it was
  something of a god class itself. Its 822 lines mix generating (note,
  passage, chords, drums, piece), playback, recording, instrument
  selection, live notes, display zoom and scroll, duration parsing and an
  FFT. In the target those are separate: the three classes above, with
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
  `Patch` and the engine applies the change.
- **Where editor state lives:** positions and groups are saved in the
  file, so they are part of `Patch`. Selection and which group is being
  viewed belong to the node editor view.
- **Names decided 2026-10-05:** no class is called "Document".
  `Generator` was dropped because it collides with the engine's own term
  (a Generator is a ValueSource that produces a signal).

**Theme and constants (decided 2026-10-05)**

- One `Theme` object holds every colour and other look-and-feel value,
  with built-in defaults, loadable from JSON. Node colours are keyed by
  family (section 3.2). Today the UI file has 115 lines of colour
  literals and 62 distinct colours.
- Constants live per module, beside what they configure, not in one
  global file. A single constants header is included by everything, so
  every file recompiles when any constant changes.
- The default sample rate gets one home. Today it is a bare 48000 in 25
  places across 12 engine files, and the UI has two differently named
  constants for it.

### 4.3 The headless checks

`mforce_ui` has twelve command-line modes (roundtrip, generate check,
gate check, several dumps). Most exist to detect the UI and the engine
disagreeing. With one codec and one renderer most of them have nothing
left to detect and are deleted; what remains becomes ordinary engine
tests of `patch_json`. Until the UI is cut over, they stay, because they
are the UI's only behaviour gate.

## 5. What gets deleted

Every item below was decided with Matt on 2026-10-05; the entries marked
"decided" record the discussion. His veto still applies to every line
when the cut is made.

**Engine**

- `BasicAdditiveSource`: fully covered by `AdditiveSource` with
  `FullPartials`.
- `AdditiveSource2`: a half-finished port with a pitch bug; one baseline
  patch uses it and would be migrated.
- The first-wired-consumer advancing rule, `RefSource` wrapping,
  starved-reference promotion and advance lists, if the per-tick memo is
  adopted (Part 2).
- **paramMap, entirely (decided 2026-10-05).** All three implementations
  go: the engine's load-time conversion, the UI's own JSON converter, and
  the UI's stash and editor. The file format then has one form.
  - 528 of the 3,137 patch files still carry a paramMap; the list is in
    `docs/matt/parammap_patches.md`.
  - `tools/parammap_to_wiring.py` converts them and stays as a standalone
    tool, so an old patch can be converted later on demand. The existing
    conversion gate reads 80 of 80 manifest patches byte-identical after
    conversion (run 2026-10-05).
  - Converted on disk, each checked to render byte-identical before and
    after: library (67), baselines (15), audition (111), and pending (9).
    Matt authorised converting the nine pending patches in place; that is
    a one-time exception to the rule that runs never write there.
  - Left as they are, converted only if revived: sweep (103), old (223).
  - Afterwards a patch that still has a paramMap fails to load, with a
    message naming the tool.

- **The string-typed evolution (decided 2026-10-05).** A wavetable can
  name its evolution with a string ("pluck", "averaging", "target") that
  the loader builds, or have an evolution node wired to its `evolution`
  input. The string form goes. 70 wavetable nodes already use the node
  form; ten use the string form, of which two are live (the baseline
  `delay_test` and the pending patch `harsh_pluck`) and get converted.
- **`TargetEvolution` is kept and becomes a node.** It morphs a plucked
  string's table toward a target waveform and is reachable today only
  through the string form.
- **`IComposer`, `Genre`, `ClassicalComposer`: deleted.** A wrapper left
  from an earlier migration that nothing uses. Genre lives today as data,
  in the style profile.
- **`SectionStrategy`: deleted** until a section level is designed. It is
  a ten-line base class that nothing derives from, registers or calls.
- **Three unused `Composer` members: deleted.** `compose_figure` and
  `compose_phrase` are wrappers nothing calls (the comments saying
  strategies call them are stale); `registry_get_for_phase2` has no
  caller.
- **The duplicated `WaveSourceMono` / `ValueSourceMono` pair:** one is
  deleted. Their bodies are identical.

**Not deleted: the realization strategies.** An earlier draft listed them
as dead. They are half-finished: a base class, a registry and two
strategies (block chords, rhythm pattern) exist, but the composer always
asks for "block" and handles rhythm patterns inline, and the template's
`realizationStrategy` key is read and saved but never consulted. Part 6
finishes it so the key does what the template documents, and removes the
inline copy.

**UI**

- One of the two clipboards (the fragment format stays).
- The second render path for Chords and Drums.
- The UI's own streaming DSP graph and its hand copies of the mixer's
  pan law.
- **The legacy parts of the Mappings dialog (decided 2026-10-05).** The
  dialog has three parts: a read-only table of what the note drives,
  derived from the graph's wires; a table of legacy paramMap entries with
  the old curve editors; and an Add row that still creates a paramMap
  entry instead of a wire. The second and third go. The menu item and the
  dialog stay, reduced to the read-only table. Adding a mapping is done
  by wiring a Note output to a pin.

- **Node-graph *mode*, not the node-graph *form* (decided 2026-10-05).**
  A patch with no instrument block, ending in a Channel and a Mixer and
  playing for a set number of seconds, stays: it is 1,048 of the 3,137
  patch files, and the form every sweep and validation cell is written
  in. What goes is the mode: the editor's flag (tested in 32 places), the
  second saver, the UI's own stereo streaming code with its copy of the
  pan law, and the convert-back stash. One kind of thing, a `Patch`: with
  an instrument block it is an instrument that notes play in many
  voices; without one it is a plain sound that runs for its `seconds` or
  until stopped. The editor asks the patch which it is. Convert becomes
  two ordinary `Patch` operations, make-instrument and make-plain-sound;
  the guessing rule (the first node with an unconnected `frequency` gets
  the note pitch) lives there and replaces `tools/add_instrument_block.py`.
  Continuous streaming stays for both kinds through the engine's live
  class.

- The stale-build stamp guard and its test tool, replaced by a build
  rule that never links over the running program.

**Tools**

- `test_figures` as a separate program; its tests move into the engine
  test suite.
- **Decided 2026-10-05: the converters stay.** Corpus work is stalled,
  not dead. ABC, kern and MIDI keep working and get one shared
  intermediate form instead of four private ones. The MusicXML path is
  broken today (every file throws); whether it is repaired or dropped is
  settled when the converters are redesigned.

- **The pattern library moves out of the engine (decided 2026-10-05),**
  through a mechanism that fits every shelved generator in
  `docs/autonomy/comp/GENERATORS.md`:
  - a separate, optional library target, `mforce_compose_shelved`, with
    its source under `engine/shelved/`, off by default and built with one
    CMake option;
  - registration is explicit: a shelved strategy registers only through a
    `register_shelved()` function that exists only when that library is
    linked, and only when the caller asks. Shelving means not registering;
  - the template null gate runs with the option on, so the keeper
    templates keep covering shelved paths;
  - recall is moving a file back.
  For the pattern library: `pattern_library.h`, `library_passage_strategy.h`
  and the registration move to the shelved target; `tools/ppl_to_json`
  moves to a shelved tools folder; the `.ppl` data stays. Which of the
  other nine shelved entries move is Matt's call per entry; Part 6 asks.

**Everywhere**

- Comments that carry dates, backlog ids or REVIEW numbers (about 276
  lines).

**The hidden loudness boost: deleted (decided 2026-10-05).**
`PitchedInstrument::hiBoost` scales a voice's gain by pitch (at 0.3: 1.3
times at 1 kHz, 1.6 times at 10 kHz) and lives in no patch file. The UI
never applies it; the command-line tool's composition modes hard-code
0.3; its single-patch mode defaults to 0. The scalar, the six constants
and the `--hiboost` flag go, and nothing is pre-added to any patch. A
patch that needs a keytracked loudness gets a Curve in its gain path when
Matt's ear says so (his own direction, backlog 19). Every composition
render changes in that checkpoint, with the treble a little quieter
(about 1.5 to 2.5 dB for the oboe between 500 Hz and 1 kHz); the
melody-to-accompaniment balance that follows is a listening decision.

**Not deleted, although the review flagged them:** anything else whose
removal changes how a patch sounds. Those are listening decisions.

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
- The hidden loudness boost is removed (section 5), which changes every
  composition render.
- The remaining confirmed critical and high findings in the review.

## 7. Decisions that get their own part

| Part | Decision | Effort |
|---|---|---|
| 2 | **The ValueSource contract.** Your per-tick memo (`next(tick)`, each node remembers its last tick and value); splitting "reset for a new note" from "here is the note length" in `prepare`; one source of truth for sample rate and the fate of `RenderContext`; no throw on a render path; a wire that is rejected says so; pins declared once instead of in five places. | maximum |
| 3 | nodes: the family list and its membership (starting from today's menu), shared primitives, the evolution holders, a node for `TargetEvolution`, the additive classes, whether input-taking nodes share a base. | extra-high |
| 4 | **`Patch`, its JSON functions and `build_instrument`**, the edit operations including make-instrument and make-plain-sound, and how the UI's `OpenPatch` sits on top. | maximum |
| 5 | **Render:** the instrument split, the entry point that takes a part's performed notes, and the lock-free live hand-off (message set, pool size, steal policy). | maximum |
| 6 | Music: the model / voicing / compose / perform split, the one performer returning data, harmony stored once, the form of per-strategy settings, template as in-and-out or a separate record, finishing the realization strategies, which shelved generators move out, the spine fixes. | extra-high |
| 7 | UI: modules, the behaviour inventory, `Theme`, cut-over order. | extra-high |
| 8 | Build and tests: targets per module, warnings as errors, third-party sources, test layout, the shelved target, the meter. | extra-high |

## 8. Order of surgery

Areas overlap; this is the intended order, not a rule.

1. The meter, CTest registration, and one command that runs every
   behaviour gate.
2. core contract and nodes (Parts 2 and 3). Everything else stands on
   them.
3. patch (Part 4), with the command-line tool moved onto it first; the
   paramMap and string-evolution conversions on disk.
4. render (Part 5), together with the contract module, the one performer
   and the play function, since the instrument's new entry point and the
   performer's new output are two ends of one change.
5. The UI rebuilt on patch and render (Part 7).
6. Music (Part 6). It is nearly independent of 2 to 5 and can interleave.
7. Build, tests and the comment purge (Part 8), then phase 2 gates on.

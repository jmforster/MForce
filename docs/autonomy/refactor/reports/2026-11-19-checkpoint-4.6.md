# Checkpoint 4.6 — the module, beside the loader (report R21)

**Landed 2026-11-19 on main, `c74ba17`..`37f7ba8` with the review's fix wave
`a994e18`** (base before 4.6 `40d036d`, the commit that carries this
checkpoint's plan; plan
`docs/superpowers/plans/2026-11-18-part4-checkpoint-4.6.md`, its Ledger the
record of every ruling and every task). Part 4 section 8.1's sixth row. The
go-ahead reached the executing session as its start: session XX wrote the
plan and started session XXI with the prompt the plan names, and no
amendment came with it, so the plan's italic rulings stand as written
(Ledger, execution notes).

Before this checkpoint one program part read a patch file: the loader, 1,383
lines that parse JSON and build live nodes in the same breath. Now a second
reader stands beside it. `engine/include/mforce/patch/` holds a patch **as
data** — records with no pointer into the engine and no JSON type — a table
of what every registered type declares, a codec that reads a file into the
records and writes them back, a check of what a patch must keep true, and a
builder that turns the records into wired nodes. `render` builds an
instrument or a plain sound over it. Nothing you run uses it yet: the
command-line tool, the editor, the allocation gate and the tests' loads
still go through the loader, and they move at 4.7. What 4.6 had to prove is
that the two readers agree, and the instrument for that is new: **the twin
gate**, which renders every patch through both in one process and compares
the floats. Over the 131 files of library and baselines it says `SAME` 131
times, offline and on the live path, and says it again when each file is
first saved by the new writer, when every value that equals its default is
taken out, and when the nodes are built in reverse order.

No file on disk changed. No render moved. Nothing here is for your ears.

Reading guide: twenty-five minutes. Section 1 is what changed for you, which
is little, and what will change at 4.7, which this checkpoint measured in
advance. Section 2 is the shape: the module drawn, and one patch — `oboe1` —
followed from its file through the codec into the record and through the
builder into a voice. Section 4 is how two readers were shown to agree.
Section 6 is what the session decided. Section 8 is the independent review.
Section 10 is the C++ lessons.

## 1. What changed for you

- **Nothing is audible.** Under the command-line tool, which still reads
  through the loader: the 131 in-scope files, 131 identical to the base's
  renders and 0 different; the nine files with no score, 9 of 9 on the live
  path; 4.4's 52 synthetic files, and 89 of 4.5's 91 with the other two
  refused as they are meant to be; this checkpoint's own, 103 of 103 with
  one moved exactly as predicted (section 4.1); the 21 composition
  templates. The allocation gate: 131 patches, none allocates while
  playing.
- **Nothing changed on disk.** No patch, score or template was rewritten.
  `git diff 40d036d..HEAD -- patches scores` is empty; audition and pending
  were read and never written.
- **In the editor, one row.** A Combiner's `gainAdj` now accepts −100 to 100
  (it was −1 to 10). Its plus and minus buttons step by one hundredth of the
  range, so the step went from 0.11 to 2; a typed value is as before. Why:
  the trombone family uses `gainAdj` as a signed scale. `compute()`
  multiplies the second operand by `1 + gainAdj`, so −2 subtracts it and
  −18.088 scales it by −17.088. Six of your files hold such values
  (`audition/articulation1/trombone_tongue.json`,
  `audition/trombone1/trombone_attempt1` to `4`,
  `pending/brass/Trombone_att1.json`: thirty settings at −2, −3 and
  −18.088). They play today because the loader checks no range. The new
  reader refuses a setting outside its declared range, and the rule decided
  on 11-08 is that the declaration widens, never the file.
- **Four wavetable evolutions no longer clamp five int settings in the
  node** (`CellularAutomatonEvolution.rule` and `.stepEvery`,
  `ReactionDiffusionEvolution.subSteps`, `HistogramEqualizeEvolution.bins`,
  `SortErosionEvolution.swapsPerSample`). The editor still clamps what you
  type. No file of yours holds a value outside those ranges. Until 4.7 a
  hand-written file with such a value is used as written (rule 300 behaves
  as rule 44: the low eight bits are the table); from 4.7 it is refused by
  name.
- **One new command, which plays nothing:**
  `mforce_cli --check <files or folders>` reads each file through the new
  reader, builds it, and prints each refusal with the place in the file.
  The 883 files of your four folders take under twenty seconds. Today it
  says:
  library and baselines, 131 files, 0 refused; audition, 708 files, 0
  refused; pending, 44 files, 4 refused —

      REFUSED patches/pending/gs_chaotic.json
        graph.nodes[0].type: node 'gs' (GrayScottSource): Unknown node type: GrayScottSource
      REFUSED patches/pending/slow_attack_horn.json
        graph.nodes[7].params.formants: node 'fseq' (FormantSequence): unknown key 'formants'

  (`gs_spots` and `gs_waves` as `gs_chaotic`). Those four do not load today
  either: the same two reasons, in the loader's words. Nothing was done to
  them; pending is yours.
- **What will stop loading at 4.7, and what you would see.** None of it is
  in a file of your four folders (the census and `--check` both say so). The
  list is what the new reader refuses that today's loader plays, each
  measured on a file written for it and rendered under the base first, so
  the "today" column is a hash and not a reading of the code:

  | In the file | Today | From 4.7 |
  |---|---|---|
  | a setting outside its declared range | clamped where the node has a hook (`KSString.brightness` 5 plays as 1), used as written where it has none (`Biquad.b0` 12) | `'brightness' is 5, outside its range 0.05 to 1` |
  | a bool given as 2 | true | `'normalize' takes true or false, not 2` (0 and 1 still read) |
  | a string on a number's setting; a number where an array takes a list | ignored | `'b0' takes a number, not "1.5"`; `'values' takes a list of numbers, not 0.5` |
  | an enum word that is no label (`"Bandstop"`) | the default kept, a line on stderr | `'mode' holds "Bandstop", which is not one of its labels (Lowpass, Highpass, …) or an index into them` |
  | an enum label in another case (`"highpass"`) | read as the label | the same sentence; `python tools/convert_patch.py` writes the label |
  | an int with a fraction (`sections` 2.5) | 2 | `'sections' takes a whole number, not 2.5` |
  | a number inside a list of feeds | a constant feed | `'source' holds 0.25, where a list holds {"ref": id} or {"tap": id}` |
  | `null` under `dynamicPins` (the editor writes it for a promoted, unwired setting) | ignored | `'stage_accuracy' in dynamicPins takes {"ref": id}, not null` — 4.7's translation layer must stop writing it |
  | a dynamic pin in a plain sound | ignored | `a plain sound has no note, so nothing drives a dynamic pin` |
  | a negative or fractional seed, or one of 2^32 or more | wrapped; truncated; wrapped | `'seed' takes a whole number from 0 to 4294967295, not -5` |
  | a top-level `frames`; `instrument.release`; any key outside the record at the top, on a node, in the layout | `frames` is the render's length; the rest ignored | `frames: unknown key 'frames'` |
  | a key its type does not declare (`frequency` on a noise source); a preset knob beside an Envelope's columns | ignored | `unknown key 'frequency'` |
  | a dynamic pin on a node inside a Multiplex template | silently inert | refused at the build, naming the node and the Multiplex |
| *found by the independent review, which came at the list from the loader's side (section 8, M1):* | | |
| `true` or `false` where a setting takes a number, or as a seed, a `volume`, a `glideMs` | read as 1 and 0 | `'b0' takes a number, not true` |
| `true` where a setting takes an enum word | the second label | `'mode' holds true, which is not one of its labels (Raw, Resonance) or an index into them` |
| `null`, a list or a feed on a scalar setting | ignored | `'b0' takes a number, not null` |
| `true` inside an array | 1 | `'x' takes a list of numbers, not [true,16000.0]` |
| two nodes under one id | the first is heard, the second ignored | `'saw' names two things: a node, a node; ids, group names and face labels share one namespace` |
| an empty id | plays | `an id is empty` |
| a cycle of reads that closes through a dynamic pin | plays | `a cycle with no tap in it: ks -> t60c -> ks` |
| a `comment` that is a list, on a node or at the top | ignored | `'comment' takes a string, not ["a"]` |
| in the layout: a position or a group member naming no node; a group named as a node | the layout is not read by the engine | `'gone' names no node`; `'saw' names two things: a node, a group; …` |
| `"dynamicPins": null` on a node | read as none | `'dynamicPins' takes an object, not null` |
| `polyphony` 1.5; `sampleRate` 48000.5 | the fraction dropped | `'polyphony' takes a whole number, not 1.5` |

  Three things go the other way. A file whose nodes are in any order loads
  (today a node must come after what it reads). An Envelope with no
  `params` at all loads as the type's default Envelope (today:
  `Envelope requires params`). And a `NameGate` inside a Multiplex template
  fires, where today's clone is given no onset vocabulary and the gate
  never does — the one shape that plays differently, and no file of the
  four folders holds a Multiplex at all.
- **What no command exercised.** The editor's interactive paths (no desktop
  control from the session): the `gainAdj` row's new range and step were
  read in the code, not clicked.

## 2. The shape

### 2.1 The module, drawn

    tools (cli, editor, tests, gates)
        |                     today: load_patch_file / load_instrument_patch (the loader)
        |                     from 4.7: load_patch, build_sound, build_instrument
        v
    render   build_sound(const Patch&)       -> BuiltSound { rate, frames, mixer, warnings }
             build_instrument(const Patch&)  -> PitchedInstrument (the voice pool)
        |
        v
    patch    the records      Node, Graph, InstrumentBlock, Layout, Patch, Fragment
             the table        type_declarations(type), prototype(type), declared_types()
             the codec        from_json / to_json, load_patch, save_patch
             the check        Patch::check()
             the builder      build_graph(graph, note, block) -> BuiltGraph
        |
        v
    source (every node type)  ->  core (Component, the pins, the registry)

Arrows are includes, and they point one way. Nothing under `patch/` includes
`render/` or `music/`; the structure meter checks it on every run. JSON is
included by four files of the module and no other. The module prints
nothing: what a build has to say comes back in `BuiltGraph::warnings`, and
the host prints it.

Fourteen headers and seven source files under `patch`, with four small
internal headers beside the sources: 2,334 lines. In `render`, three
headers and two sources: 307.

| What | Files | Lines |
|---|---|---|
| the records | `node.h`, `graph.h`, `instrument_block.h`, `layout.h`, `fragment.h`, `patch.h` | 36, 16, 18, 43, 20, 28 |
| the table | `type_declarations.h`, `type_declarations.cpp` | 50, 128 |
| the check | `patch.cpp`, `graph_cycles.h` | 321, 56 |
| the codec | `patch_json.h`, `node_json.cpp`, `patch_json.cpp`, `codec_path.h`, `sentence.h`, `float_text.h` | 25, 366, 355, 27, 27, 28 |
| the builder | `note_signals.h`, `built_graph.h`, `build_graph.h`, `build_graph.cpp` | 29, 22, 19, 426 |
| asked by both readers, moved in | `envelope_preset.h`, `retired_spellings.h` and `.cpp`, `table_rule.h` and `.cpp` | 157, 30, 46, 19, 42 |
| `render` | `built_sound.h`, `build_sound.h`, `build_instrument.h`, `build_sound.cpp`, `build_instrument.cpp` | 17, 11, 16, 180, 83 |

The loader beside it is 1,307 lines (1,383 at the base).

### 2.2 A node as data

One record shape for every node, whatever its type
(`patch/node.h`):

```cpp
struct Wire { std::string source; bool tap{false}; };

struct Node {
  std::string id;
  std::string type;
  std::optional<uint32_t> seed;
  std::map<std::string, float>              values;      // a pin's constant, while nothing feeds it
  std::map<std::string, std::vector<Wire>>  wires;       // a pin's feeds, in order
  std::map<std::string, float>              settings;    // a bool is 0 or 1, an enum its index
  std::map<std::string, std::string>        strings;
  std::map<std::string, std::vector<float>> arrays;      // an array, or one column of a table
  std::map<std::string, std::string>        dynamicPins; // a setting and the node that feeds it once per note
  std::string comment;
  bool operator==(const Node&) const = default;
};
```

There is no `Pin` type and no per-type struct. A pin is a name the type
declares; what the record holds for it is a number, feeds, or nothing. Which
map a name sits in is decided by its declaration's kind, and by nothing
else.

### 2.3 The table: what a type declares

A node's declarations live on an instance — each pin registers itself with
its owner in its constructor — so "what does `SVFSource` declare" is
answered by making one and asking it. `type_declarations("SVFSource")` does
that once, copies every descriptor into plain values (`TypeDeclarations`),
keeps the instance as the type's prototype, and for each slot records which
registered types its door accepts, found by offering one of each to a
scratch instance. The codec sorts a file's keys by this table, `check()`
judges a record by it, the descriptor dump is printed from it. In the test
binary the table reads all 84 types (the registry's 78 and six test probes)
in 3.6 ms, slot acceptance included; 448 declared defaults were compared
with a fresh instance of each type and none differs.

### 2.4 The codec: one reader, one writer

`from_json` reads a node's `params` bag against its type's declarations,
key by key: a key that names a Param takes a number (into `values`) or a
feed (into `wires`); a slot takes `{"ref": id}`; a setting takes what its
type takes; an array a list; `seed` goes to `Node::seed`; anything else is
`unknown key`. Three decisions shape it.

**The model is complete in pins and settings.** After the bag is read,
every declared Param the file left out gets its declared default in
`values`, and every setting likewise. So a `Patch` never depends on an
engine default that could later move, and a sparse hand-written file
becomes full on its first save. Arrays are not filled: an
`ExplicitPartials`' rows follow its `maxPartials`, so a default for them is
the node's own to make.

**The Envelope's preset form is read, never written.** 363 Envelopes in the
four folders say `{"preset": "adsr", …}`. The codec lays the stages out
through the same table of six presets the loader calls, keeps the eight
columns, and writes columns back.

**A refusal is a path and a sentence.**
`graph.nodes[14].params.cutoffFreq: node 'Damp_lpf' (SVFSource): …`. Each
level that fails rethrows with its own step in front, so the path composes
without any level knowing where it sits. Where the loader had a sentence for
the same refusal, the codec keeps it word for word.

### 2.5 What a patch keeps true

`Patch::check()` returns every violation, each with its path, and an empty
list when the patch holds: ids unique and non-empty, in one namespace with
group names and face labels; the output a signal or the mixer; every feed,
dynamic pin, position, member and face field naming a node; a pin a number
or feeds, one feed unless it is a list; every name declared for its kind; a
slot's part of a type its door takes; a setting inside its range; a table's
columns at one length; no cycle without a tap in it; no note read in a
plain sound. The codec ends every read by asking it. 4.8's edit operations
will ask it after every edit.

### 2.6 The builder

`build_graph(graph, note, block)` in five steps, each a function:

1. **Sort.** At each step the first node, in the file's order, whose feeds
   are built. A file whose feeds come first — every file of yours but one —
   is built in file order, as today.
2. **Create and fill**, in the loader's order: inputs, params, settings,
   arrays. A Param's number becomes the pin's own constant with nothing
   wired; an Input's number, a wired constant, because an Input has none of
   its own.
3. **Bind taps**, after the last node, so a tap may point forward.
4. **Dynamic pins**, in the file's node order.
5. **Finish**: each node's own `validate()`, the tails only taps read, the
   output, the signals by id.

Three types are known to it by name, one per idea no declaration can
express: a `PerformNode` is not built at all (its id becomes the note's
signal its `field` names); a `NameGate` resolves its name against the
block's onsets; a `MultiplexSource` is handed a closure that builds one
clone of its template.

### 2.7 `oboe1`, followed through

`patches/library/winds/oboe1.json`: 21 nodes, the output `Reverb`, an
instrument block of two keys, an embedded score, a layout. Four stops.

**The file to the record.** One node as the file has it:

```json
{"id": "Damp_lpf", "type": "SVFSource",
 "params": {"source": {"ref": "DC_Block_hpf"}, "cutoffFreq": {"ref": "Damp_cutoff_curve"},
            "resonance": 0.800000011920929, "mode": "Lowpass", "normalize": false}}
```

The codec asks the table what an `SVFSource` declares — one Input
(`source`), two Params (`cutoffFreq`, `resonance`), an enum setting (`mode`,
five labels) and a bool (`normalize`) — and sorts the five keys by it:

    Node  id "Damp_lpf"   type "SVFSource"
      wires     source     -> [DC_Block_hpf]
                cutoffFreq -> [Damp_cutoff_curve]
      values    resonance  =  0.8
      settings  mode       =  0        (the index of "Lowpass")
                normalize  =  0

`cutoffFreq` and `resonance` are the same kind of pin; one is fed and one
holds a number, so they sit in different maps. The long number is how a
double prints the float 0.8; the record holds the float, and a save writes
`0.8`, the shortest text that reads back to that float (section 10).

**The model made complete.** `Ampl_env` in the file says nothing of its
`trigger` pin; the record holds `values["trigger"] = 0`, the declared
default, and a save writes it. Likewise the block: the file gives
`polyphony` and `volume`, the record has five fields, and a save writes
`"glideMs": 15.0, "onsets": [], "polyphony": 1, "sustaining": false,
"volume": 0.5`. From its first save through the module a file no longer
leans on a default the engine could later move. (One oddity is booked, not
fixed: the Envelope's `sustainLevel` is filled as 0.7 beside columns that
say otherwise. It is a view of the columns and no render reads it; the
review found two more settings of the kind, section 8, M3.)

**The record to a voice.** `build_instrument` makes one voice (polyphony 1)
and for it the note's six signals — frequency, velocity, wheel, pressure,
duration, onset — over the voice's own performance source. Then
`build_graph`:

- The file's three `PerformNode`s (`__perf_freq`, `__perf_frequency`,
  `__perf_frequency2`, each `field: frequency`) are not built. Each id
  becomes a name for the one frequency signal: three ids, one object.
- Every other node is made by the registry and filled, in the file's order,
  because in this file every node already follows what it reads. `Wormhole`
  reads `Delay_line` through a tap, nine nodes ahead of it; a tap is no
  read, so the order stands and `Wormhole`'s source is left open.
- `Damp_lpf` is filled as the loader fills it: the input, then the Params
  (`cutoffFreq` wired to the curve; `resonance` set as the pin's own
  constant, where the loader wires a constant node that holds 0.8), then the
  two settings.
- After the last node the tap is bound: `Wormhole` now holds a handle on
  `Delay_line` and reads the value it had one sample ago. That closes the
  oboe's loop: `Delay_line` to `Wormhole` to `Wormhole2` to `Drive_inputs`,
  through the junction and the two filters, back to `Delay_line`.
- The voice takes the output (`Reverb`), the two Envelopes in the file's
  order (`Drive_env`, `Ampl_env`), no tail to tick (both `Formant1` and
  `Combined` read `Delay_line` directly) and no dynamic pin.

**The verdict.** `SAME patches/library/winds/oboe1.json`: the offline
render equal to the loader's as bits, sample rate and frame count included;
on the live path every sample of every note of the embedded score, each
note's gain, and the number of allocations. `SAME` again when the module's
own save of the file is read by either reader, and when `Damp_lpf` loses
`mode` and `normalize` (they equal their defaults) and `Ampl_env` its
`minValue`, `timeMode`, `timeScale` and two accuracies, which do too.

One number that is not a gate: in the close's two timing runs, which
alternate the reader that goes first, the module took 0.77 and 0.81 of the
loader's time to load and render this file (1,599 ms against 2,066, and
1,895 against 2,342, three processes sharing four cores). The likely cause
is the Param's own constant, which is read without a call where the
loader's constant node is pulled every sample; it was not isolated.

### 2.8 What exists twice until 4.7

Two readers means two of several things for exactly one checkpoint: the
voice loop (the loader's and `build_instrument`'s), the plain-sound path,
the mono adapter. The twin gate needs both arms to exist; the loader's
half is deleted at the flip. The meter was expected to show those pairs
as duplicate blocks and does not: no duplicate block names
`build_graph.cpp`, `build_instrument.cpp` or `build_sound.cpp` (section 5).

## 3. The tasks, commit by commit

Six tasks and the close, one executor each (Opus 5.5, from a brief file),
one tree, one build. The coordinator read the diff of Tasks 1 to 4 whole, compared the
command-line binary by sha256, ran the null gate and made each commit. A
docs commit after each task carries the plan's Ledger and nothing else.

| Commit | Task | What it holds | Tests after it |
|---|---|---|---|
| `40d036d` | 0 | the plan and STATUS; the base's renders heard once; the census; 74 synthetic files, each with the verdict expected of it | 9,889 |
| `c74ba17` | 1 | **every edit to code the loader runs**: `gainAdj`'s range, the four clamp hooks, the trigger's test, the Multiplex's builder, three exports and the preset table moved under `patch/`, `BuiltSound`, the mixer's list of signals | 9,923 |
| `249d7b5` | 2 | the records, the declarations table, `Patch::check()` | 10,480 |
| `cb33594` | 3 | the codec, `mforce_cli --check`, the descriptor dump printed from the table | 11,279 |
| `3c4b362` | 4 | `build_graph`, `build_instrument`, `build_sound`; the tests' inline patches through both readers; the generated reader twin | 12,014 |
| `37f7ba8` | 5 | the twin gate | 12,014 |
| `a994e18` | 6 | the review's and the mutation run's fix wave | 12,184 |

The order is the proof's order. Task 1 is the only task that touches a line
the loader, a node or a voice runs, so it is the only one after which the
whole render gate set had to be run again; from Task 2 on the command-line
tool's render path is the same code, and each task's question is whether the
new code agrees with it. Task 6 was three agents — the mutation run, one
reviewer beside it, then one fix wave — and one edit by the coordinator
after it (section 6, ruling 62).

## 4. The proofs

The claim is that nothing a file plays has changed and that the new reader
builds what the old one builds. Seven instruments, each for a different way
that could be false.

### 4.1 One task holds every edit the loader runs

Task 1 (`c74ba17`) is the only commit that changes a line the loader, a
node or a voice executes. After it the whole render gate set ran: the 131
in-scope files against the base's hashes, the score-less nine on the live
path, three sets of synthetic files, the composition gate, the allocation
gate, the null gate. One synthetic moved, and it was predicted before the
run: `h15`, a cellular automaton with `rule` 300, which the deleted clamp
used to play as rule 255 and which now plays as rule 44, the low eight bits
(Ledger 37). From Task 2 on the command-line binary's render path is the
same code, and its sha256 was compared at every commit instead of rendering
again.

### 4.2 The twin gate

`tools/twin_gate/main.cpp` reads each patch file with both readers in one
process and compares what they build, twice over:

- **offline**, as the command-line tool renders: the sample rate, the frame
  count, every float of the stereo buffer as bits, and the fault report;
- **live**, as the editor and MIDI play: every sample of every note of the
  file's embedded score (a C4 where there is none), each note's gain, and
  the number of heap allocations while playing.

| Run | What is compared with the loader's render of the file | Result on the 131 |
|---|---|---|
| the control | the loader again | 131 same |
| **the twin** | **the module** | **131 same** |
| resaved | the file as the module saves it, read by the loader, and read by the module | 131 same, both arms |
| sparse | the record with every value at its declared default removed (5,958 keys), saved and read by the loader; read by the module; built by the module with nothing filled back | 131 same, all three arms |
| order, tails kept | the module building the nodes in reverse file order, the tap-only tails kept in theirs | 131 same |
| order, reversed | the same with the tails reversed too | 131 same |

What each run is for. The **control** shows the gate can be trusted on a
file: if the loader differed from itself (an unseeded random source would),
a difference between readers would mean nothing. None does. **Resaved** is
where the writer is heard, and where an Envelope the file gives as a preset
and the module writes as eight columns is shown to be the same Envelope to
the old reader. **Sparse** proves two things the complete model rests on:
that a declared default really is what the node holds when the file is
silent, and that setting it again changes nothing. The two **order** runs
answer a question the design left open (section 6, rulings 18 and 26): does
the order of nodes in a file mean anything? On your files, no. Three hold
tails that only a tap reads and six hold dynamic pins, and every one renders
the same with its nodes reversed.

Since the fix wave each run's summary also says what its arms did, and a
run whose arm did nothing fails (section 4.5): 176 notes compared on the
live path in the twin run (352 over the resaved run's two arms, 528 over
the sparse run's three); 5,958 keys removed by the sparse arm; 131 saved
texts that are not their file's bytes, which is every file, the writer
writing the complete model. No score went unplayed.

Order can be heard in one shape, and no file of yours has it: two dynamic
pins fed by one stateful chain. Each push steps the chain, so which node is
pushed first decides which value it gets. A synthetic pair shows it (`c3`,
`c3r`: both order runs report a difference, the two files' values
exchanged). The builder keeps dynamic pins in the file's node order, so such
a file plays as it does today.

Outside the 131: the score-less nine, 9 same; 4.4's synthetics, 52 same;
4.5's, 89 same and two refused by both readers in the same sentence; this
checkpoint's own set, 108 files by the close (75 before the review, 33
added by it and the fix wave), against the verdict written for each
beforehand — 59 same, one that differs by design (a `NameGate` inside a
Multiplex template), three the loader refuses and the module reads (two
files with their nodes in reverse order, and an Envelope with no
`params`), one both refuse, 44 the module refuses, each with the sentence
expected of it — and none departing from its row.

One row was wrong, and the gate is what found it. The fixture written to
show the `NameGate` difference (`b2`) could not show it: its notes are held
as one line, and a clone's Envelope inside a Multiplex is on no voice's
list, so the notes that carry the onset are silent under both readers. The
row was corrected and a variant without the held line (`b2n`) carries the
difference, the module's sample exactly twice the loader's at the first
onset (Ledger 55).

### 4.3 The generated reader twin: every type, not every file

Twenty of the 78 registered types appear in no in-scope file, so no run
over files hears them. For each of the 84 types in the test binary (78 and
six test probes) `engine_tests` writes three small patches from the
declarations table and plays a quarter-second C4 through both readers:

| Text | Built by both | Sound | Readers differ |
|---|---|---|---|
| sparse: the node, its inputs fed, nothing else said | 83 | 76 | 0 |
| rows: every array given three rows, and every Param a number off its default | 83 | 76 | 0 |
| settings: every scalar setting off its default | 82 | 72 | 0 |

The types not built are probes that refuse by design, by both readers in
the same words. In every text the record also survives a write and a read
unchanged.

The second text's Param numbers came with the fix wave (section 8, M2).
Without them the twin already heard a builder that set a *wrong* number on
a Param, because the complete model gives every Param its default and the
builder sets it: doubling every number fails 69 types. What it could not
hear was a builder that *dropped* the number, a dropped default being the
node's own default. With a number off the default on every Param, that
fault fails 48 types.

### 4.4 Every inline patch of the tests is a twin case

`engine_tests` holds some eighty patches as text. They now load through one
helper that builds each under both readers and compares the samples before
handing the test its instrument. 66 call sites; a table names the loads
where the readers are known to part: 18 rows, every one a load the module
refuses, every one an enum word that is not exactly a label (`"FREQUENCY"`,
`"multiply"`, `"cubic"`). A load that differs and is not in the table fails
the run; so does a row that no longer differs, and so does a refusal whose
sentence lacks the words its row holds.

### 4.5 The mutation run

A test that passes proves nothing until it has been seen to fail. So, before
the close, 115 deliberate faults were put into the code this checkpoint
wrote, one at a time: the fault applied, the tests run, the file restored.
Each asks one question: does anything notice?

| Where | Faults | Caught by a standing test | Not caught |
|---|---|---|---|
| The ground (the four edits the loader's path runs) | 4 | 3 | 1 |
| The declarations table | 8 | 7 | 1 |
| `Patch::check()`, each rule skipped in turn | 35 | 35 | 0 |
| The codec, `--check` and the dump | 23 | 20 | 3 |
| The builder | 23 | 21 | 2 |
| `build_instrument` and `build_sound` | 15 | 13 | 2 |
| The twin gate itself | 7 | (see below) | |

99 of the 108 faults in the module and the tool were caught by
`engine_tests`. Nine were not, and every one became a test, or a deletion,
in the fix wave (`a994e18`):

| Fault | Why nothing noticed | What was done |
|---|---|---|
| A Multiplex given a new template was not marked as needing a rebuild | Every Multiplex in a file or a test is given its template once, before it first plays | A test that gives a second template and hears it |
| The table's slot probe run on the type's own prototype, which then kept the last part it accepted | Nothing reads a prototype's slot afterwards | A test that every prototype's slots are empty once the table is built |
| `save_patch` without its final newline | No test read the last byte | One does |
| `--check` exiting 0 with files refused | No standing test ran the built tool at all | `tools/tests/test_cli.py`: the tool is run on a file that reads, one that does not, and one that reads and will not build |
| The descriptor dump printing an enum's labels reversed | The same; only a scratch hash noticed | The same file pins one enum's labels in order. The converter reads this dump |
| The tails advanced in the order they were built, not the file's | No test built two tails out of file order | One does |
| The builder's own check of a table's columns removed | `check()` refuses the same table first on every path a test took | A test builds a record that was never checked |
| A guard against one Envelope being listed twice, removed | It cannot happen: the builder has already refused two nodes under one id | The guard was dead code and is deleted |
| `build_instrument` preparing at 48000 whatever the file says | Only the twin gate heard it, on the two 22050 Hz baselines; every test patch is at 48000 | A test at 22050 that needs no loader |

**A fact for 4.7.** Nineteen of the 99 were caught by one detector alone:
the tests' helper that builds every inline patch under both readers
(section 4.4). That comparison leaves with the loader. Before the loader is
deleted those nineteen faults have to be run again against the tests as
they will stand; the list and each fault's exact text are kept
(`renders/scratch/ab46/t6a/mutations.py`, `summary.tsv`), and STATUS
carries it.

**The gate, mutated.** Seven faults were put into the twin gate itself. Two
were noticed by its own runs. Five were not: the live path skipped, the
allocation counts not compared, `--expect` passing a wrong verdict, the
sparse arm removing nothing, the resaved arm reading the original file. The
gate as committed has none of these faults — its 558 lines were read whole
at Task 5 — but a run could not have told. What shows each comparison
alive, and what the fix wave added:

| Comparison | Shown to fire by | Added |
|---|---|---|
| samples late in a render | `b2n`: the first difference at sample 96,002 | — |
| the live path | the 48000 fault above, on the unmutated gate: "live first at sample 1" | the summary counts live notes; none compared fails the run |
| a verdict against its row | twice: Task 5's `b2` row, and the mutation that stopped the comparison at sample 1,000 | — |
| the resaved arm hears the writer | the `%g` fault in the writer: 15 of the 131 differ | the summary counts saved texts that differ from their file; none fails the run |
| the sparse arm | — | the summary counts keys removed; none fails the run |
| the allocation counts | once, by hand: one allocation added to the module's arm gave `DIFFER patches/baselines/BaselineSIN.json: live allocations: 0 1` | nothing standing; the allocation gate, a process of its own, is the detector of record |

One fault could not be run as written. A builder closure that holds a
reference to its caller's note signals is undefined behaviour once the
caller is gone, and undefined behaviour cannot be a test. The executor
modelled it instead (the closure reads, at clone time, whatever was last
built at that address), and the tests' helper heard it: every voice's
clones were reading the last voice's signals.

### 4.6 The close's measurements

Once, on the tree as committed (`a994e18`; the command-line binary
`fe86ad64…94ce0577`, which the close's build left untouched).

| Gate | Result |
|---|---|
| the 131 in-scope files against the base's renders | 131 identical, 0 differ |
| the score-less nine, on the live path | 9 of 9 |
| 4.4's 52 and 4.5's 91 synthetic files | 52 of 52; 89 of 89, and two refused as they should be |
| this checkpoint's synthetic files, under the loader | 103 of 103, with `h15` on rule 44's hash as section 4.1 says; the first run of this arm is the paragraph below |
| the composition templates against 4.5's close | 21 same |
| the allocation gate | 131 patches, 0 allocate |
| the null gate | 77 of 77 |
| the roundtrip gate (the editor's save and load) | 0 id changes, 0 new differences, the 6 known |
| `engine_tests` | 12,184 checks, all pass, 85 s |
| the tool tests; ctest | 353; 5 of 5 |
| the twin gate, its ten runs | as section 4.2, every count as the fix wave left it |
| `mforce_cli --check` | library and baselines: 131 files, 0 refused, 0.9 s. Audition and pending: 752 files, 4 refused, 17.7 s. The synthetic set: 108 files, 45 refused, exactly the rows that say so |
| `convert_patch.py --check`, the four folders | 0 pending, 0 refused |
| the descriptor dump | byte-equal to Task 1's |

**One gate line read FAILED, and the cause was a list.** The whole-set
script reported this checkpoint's synthetic arm as failed: 102 identical
of 103, and one file not rendered. The list that arm renders from had lost
one name, `b2n`, when the fix wave rewrote it; the file was there. The
executor rendered `b2n` alone and its hash is the base's. The coordinator
put the name back and ran that one arm again: 103 of 103. The engine was
not involved. It is told here because a close that printed FAILED anywhere
should say where and why.

The full `tools/gates.py` still ends `FAILED: roundtrip smoke`: one shape
of six, `wiring`, shows a render difference through the editor's save. It
is the same line as at the base and at 4.5's close, it is older than
Part 4, and every other gate of that script passes.

**Timing.** The command-line tool against the binary from before the
checkpoint, the 131 files, three rounds, the best of each arm, the whole
thing twice: totals 0.990 and 1.005 (new over old), medians 1.010 and
1.004. Flat, as it should be: the tool still renders through the loader.
The five slowest ratios are different patches in the two runs, all short
renders, so they are noise. The twin gate's own timing, loader against
module, alternating which goes first: over the 131 the module takes 0.960
and 0.973 of the loader's time (per file, medians of 0.976 and 0.982). The
new reader is not slower than the old one.

**Four shapes, measured for the record.** Two rows of the cleanup list
(98, 104) said "4.6 decides" and nothing had measured them.

| Shape | Under the loader | Under the module |
|---|---|---|
| a channel whose source is a tap of a node nothing else reads | silence, and a warning on stderr | the same silence; the warning as data |
| a number as a channel's source | that constant | the same |
| a tap from one channel's chain into another's, or onto another channel's volume | renders | the same samples |
| a Multiplex template that taps a node outside it | refused: `Unresolved tap target: X` | refused: `inside the template of 'mux' only a tap reads it, and nothing advances a clone's tail` |

The first three load under both readers and render alike, and `check()`
refuses none of them. The last is refused by both; the module's sentence
is right about the template and says nothing of the reader outside it.

### 4.7 What none of it can hear

- **The editor.** Nothing in the session can click. The `gainAdj` row was
  read, not used.
- **A silent pass.** In the generated twin a type whose patch renders
  silence passes by building alike in both readers and no more:
  `SegmentSource`, `CurveNode` and `NameGate` in all three texts, and
  `FullPartials`, `SequencePartials`, `RangeSource` and `PerformNode` with
  every setting at its far end. All
  but one of those are in in-scope files, where the twin gate hears them.
  The one is `NameGate`: no in-scope file holds it and its generated patch
  is silent, so it rests on four synthetic files (`b1`, `b1c`, `b2`, `b2n`)
  and the builder's own tests. The other nineteen types that no in-scope
  file holds all sound in all three texts.
- **Held lines and onsets on the live path.** The twin gate plays notes one
  after another there, as the allocation gate does. Lines and onsets are
  heard offline.
- **Which reader runs first.** The loader always runs first and the module
  second, in one process. A difference that depended on running second
  would not show as one; the control run is the evidence that nothing
  carries over. (The timing run alternates the two since the fix wave.)
- **The layout's writer.** Neither reader renders a layout, so the resaved
  run cannot hear it. Its proof is the identity `from_json(to_json(p)) ==
  p`, which a fault symmetric between reader and writer would pass. The
  first consumer that reads a saved layout back is the editor, at 4.7.
- **An allocation a type makes on first use.** The twin gate counts
  allocations on the live path after both offline renders have run in the
  process, so such an allocation is in neither arm's count. The allocation
  gate, a process of its own, is where it is heard.
- **A score the player would refuse.** `--check` builds and does not play
  (section 6, ruling 62).
- **Sizes nobody uses.** An `ExpandRule` at count 16 and recurse 2 rendered
  through the loader in 52 minutes and then failed to allocate through the
  module. It was not chased (Ledger 53; the reviewer was asked to read for
  it, section 8). An `ExpandRule`'s declared ranges multiply to sizes no
  machine holds; that is CLEANUP 106, and older than this checkpoint.

## 5. The meter (`--delta` from the base `40d036d`)

| Measure | Base | Now | |
|---|---|---|---|
| files | 226 | 252 | +26 |
| lines | 64,773 | 70,202 | +5,429 |
| files over 600 lines | 18 | 18 | |
| functions over 80 lines | 96 | 96 | |
| file-level mutable variables | 120 | 120 | |
| `const_cast` | 10 | 10 | |
| `dynamic_cast` | 63 | 73 | +10 |
| changelog comments | 213 | 211 | −2 |
| duplicate blocks | 156 | 162 | +6 |
| include violations | 12 | 12 | |
| files no module owns | 0 | 0 | |

Twenty-six files and 5,400 lines: about 2,300 are the module, 2,600 are
tests, 600 are the twin gate. Two numbers rose.

**`dynamic_cast`, +10.** Four in the builder, one in `build_instrument`,
one in the declarations table, four in the tests. The builder's are the
questions the design keeps: is this built object a signal, the mixer part,
a Multiplex, an Envelope. Ruling 49 is why they are asked of the object
and not read off the record's type name (section 10.7). The loader's four
leave with it at 4.7.

**Duplicate blocks, +6.** None is in the module. New in the tests' text:
runs of JSON that the seven inline Multiplex patches share, and a
three-line refusal lambda now in seven places where it was in five. The
rest is the duplicate finder dividing files nobody edited differently
than before. The two pairs expected for exactly one checkpoint — the
loader's voice loop against `build_instrument`'s, its plain-sound path
against `build_sound`'s — did not appear.

No function in the module, the two build files or the twin gate is over
the 80-line limit; the longest are `judge_file` in the gate (39),
`retired_key_refusal` (38), `assemble_mixer` (32) and `template_of` (28).
The loader keeps its four (208, 140, 100 and 82 lines). The twin gate's
one source file stands at exactly the 600-line limit and is deleted at
4.7. The snapshot (`tools/structure/metrics.json`) is retaken with this
report.

## 6. Decisions the session made (2026-11-02 standing rule)

Two kinds. The first were written into the plan in italics as rulings that
were yours to overturn; the go-ahead came with no amendment, so each stands,
and each is reversible in the place named. The second were made while
executing. The Ledger has every one at full length under its number.

**From the plan, standing as written**

| # | Decision | To reverse it |
|---|---|---|
| 2 | `CombinedSource.gainAdj` takes −100 to 100 | one line, `combined_source.h`; a narrower range refuses the trombone files |
| 3 | four evolution hooks that clamped five int settings are gone; the other thirteen clamps stay | the four hooks are in `c74ba17`'s diff |
| 4 | an Envelope restarts when its trigger's **value** is not zero, where the test was "is something wired" | one comparison, `instrument.h`; under the loader both tests agree on every file |
| 6 | a Multiplex template that holds a tail only a tap reads is refused by name | making it work is a per-clone advance list inside the Multiplex's `compute()`: new behaviour, so not this refactor's |
| 9 | a bool reads as `true`, `false`, 0 or 1; any other number is refused | strict would need `algev_test_sort.json` rewritten by hand |
| 17 | the module map gains `patch` now (the design gave it to 4.7) | none needed: the meter fails on a file no module owns |
| 18, 19 | nodes are built in an order the builder derives; the envelope lists follow the file's node order | measured by the twin's two order arms, section 4.2 |
| 20 | a build's warnings come back as data (`BuiltSound::warnings`; a third parameter of `build_instrument`) | an interface call, raised in REVIEW R21 |
| 23 | `--dump-descriptors` is printed from the declarations table now | byte-equal to the loader-era dump, so nothing to reverse |
| 24 | the tests' one helper lands now, with both readers behind it | it is 4.7's test work, done a checkpoint early |
| 27 | the list of what the new reader refuses that the loader plays (section 1's table) | each row is one rule of the codec or `check()` |

**Made in execution**

| # | Decision | Why |
|---|---|---|
| 37 | an unclamped `rule` of 300 behaves as 44, and nothing faults | read in the four headers before the edit; the gate's prediction held |
| 45 | a float is written as the shortest text that reads back; two floats of 4.28 billion take a longer text | the identity `from_json(to_json(p)) == p` outranks "shortest" |
| 46, 50 | four small internal headers under `engine/src/patch/` the plan did not list (`float_text.h`, `codec_path.h`, `sentence.h`, `graph_cycles.h`) | each is one thing two or three files of the module need; the third copy of a three-line function is where the consolidation rule bites |
| 47, 51 | the codec's and the builder's shapes where the plan left a choice, and the sentences it did not give | listed in the Ledger; each is a wording or an order, none an interface |
| 48 | a preset knob of the wrong shape is refused in the JSON library's words | booked, CLEANUP 105 |
| 49 | **the builder asks the built object what it is** (`dynamic_cast`), where the executor's first pass read the record's type name and used `static_cast` to keep the meter's cast count flat | a record with two nodes under one id made that undefined behaviour, shown as a crash; the design keeps exactly these questions. The meter's count went from 63 to 73 over the checkpoint (section 5) |
| 53 | the generated reader twin writes three texts per type; five settings take a chosen value in place of their maximum, because their maximum is a size | `ExpandRule` at its maximum is 39 million partials for each one its host has |
| 60 | the twin gate's summary counts what its arms did, and a run whose arm did nothing fails | five faults put into the gate itself passed as `SAME` (section 4.5) |
| 61 | a cycle of reads that closes through a dynamic pin stays refused, and is now on section 1's list | the design's own rule (Part 4 §5.2: dynamic-pin feeds are in the sort alike); what the review found missing was the row, not a reason against the rule |
| 62 | **`mforce_cli --check` builds each file and plays nothing** | the fix wave built with `build_sound`, which plays an instrument's score: audition and pending took five minutes and printed 923 diagnostic lines around four verdicts. Built without playing: 14 s, nothing but the verdicts. What it leaves unfound is a score the player would refuse (CLEANUP 120) |
| 63 | `tools/tests/test_cli.py`: a standing test runs the built tool, with the binary ctest names | two faults put into the tool passed every test there was |
| 64 | a guard in `build_instrument` that could not fire is deleted | the mutation run found nothing could reach it: the builder refuses two nodes under one id first |

Nothing in either table changes a file or a render.

## 7. Debt and future cleanup

What this checkpoint leaves, in the order it should be met. Every item is a
row of `docs/autonomy/refactor/CLEANUP.md`, which has the full text.

| Row | What | When |
|---|---|---|
| 107 | **A read past the end of a vector, older than this checkpoint and in both readers.** `ExplicitPartials::on_array_changed` rebuilds its layout as each column arrives, sized by the first, and reads the other columns to that length while they still have the old one. It happens on any file whose rows outnumber the node's birth size when `mult1` arrives first. The state after the last column is right, which is why no render shows it. Found by reading, in the review (m3); not run | 4.7's first task: it is an edit to code the loader runs |
| 110 | Three settings are views of something else the node holds, and the record keeps the file's number beside the rows that overrule it (section 8, M3) | REVIEW R21's question; 4.7's editor layer must set settings before arrays |
| 113 | Nineteen of the mutation run's faults were caught only by the tests' two-reader helper, whose second reader leaves at 4.7 (section 4.5) | 4.7: rerun the nineteen before the loader is deleted |
| 111 | The editor's preset swap would drop an Envelope's nonzero trigger constant once it goes through the module (m7) | 4.7 |
| 84 | The Shaper's `morph` advisory, which has waited since 4.4 for a Param to have a constant of its own. It has one now | 4.7 |
| 109 | `engine_tests` takes 85 s where it took about 30, and has no filter, so a mutation run drives the whole of it for every fault | half goes with the loader at 4.7; the filter is open |
| 112 | A number that is not finite in a record: the writer writes `null`, the reader refuses it, and the record is not equal to itself (m6) | 4.8, the operations |
| 106, 108, 116, 117, 118 | Sizes and pushes nothing checks in one place: `ExpandRule`'s ranges multiply past any machine; `Setting<int>::from_float` on a NaN is undefined behaviour; thirteen clamp hooks a dynamic pin's push can still reach; a Multiplex clone's nodes are on no list by id, so their faults and warnings are not reported under their own names; one knob list serves six Envelope presets | open |
| 105, 114, 120, 121 | A preset knob of the wrong shape is refused in the JSON library's words; a `/W4` warning in `music/basics.h`; `--check` does not play a score; no render hears the layout's writer | open |
| 115 | The twin gate's `--expect` keys a row by file name and checks no difference's place; its `UNPLAYED` line cannot be reached from a file | goes with the gate at 4.7 |
| 119 | `float_text.h` uses `std::from_chars` on floats, which an older libc++ lacks | Part 8 |

Rows this checkpoint closed or moved: 19 and 55 are done (`c74ba17`); 47,
48, 79, 85, 88, 89, 90 and 99 are done in the module, the loader's half of
each going with the loader at 4.7; 56 and 96 are refused by name; 98 and 104
were measured at the close (section 4.6). What 4.7's plan inherits that is
not debt (the tests' table of eighteen known differences, the editor's
`null` dynamic pin, two converter rules, the twin gate's rerun at the flip)
is in STATUS.

## 8. The review (R21)

An Opus 5.5 reviewer that had written none of the code and seen none of the
session's reasoning, read-only at the commits, briefed to refute: the plan's
ten Review Focus items, the coordinator's own claims numbered as things to
break, the surfaces no gate looks at, and every sentence of the record. It
wrote 36 patch files of its own and ran them through the base binary, the
new `--check` and the twin gate. Beside it, the mutation run (section 4.5).
One review, then one fix wave (`a994e18`), as the four verification rules of
11-14 have it.

No Critical finding: no render of an in-scope file can move, no data is
lost, no gate lies. Three Major and seven Minor.

| # | Finding | What was done |
|---|---|---|
| M1 | **The list of what stops loading at 4.7 was incomplete.** Eleven more kinds of thing the loader plays and the module refuses, each shown on a file: `true` or `false` where a setting takes a number or an enum word (the loader reads them as 1 and 0); `null`, a list or a feed on a scalar setting (ignored); a bool inside an array; a seed of 2^32 or more (wrapped); two nodes under one id (the first is used); an empty id; a cycle that closes through a dynamic pin; a comment given as a list; a layout entry that names no node, or a group named as a node; `"dynamicPins": null`; `polyphony` 1.5, `volume: true`. And one the other way: an Envelope with no `params`, which the loader refuses and the module reads. | Every refusal is one the design asks for, so no code changed. 29 of the 36 files joined the synthetic set with a row each (two were files the set already had; five showed no difference: `seconds` as a bool and a bool on a Param are refused by both readers, two other shapes render the same). Section 1's table gained the rows, and each sentence is pinned whole by a test, ten of them newly. `--check` over your four folders already said none of these is in a file of yours. |
| M2 | **A proof was claimed that could not fail.** The Ledger named the generated reader twin as one of three proofs that a Param's number renders the same under both readers. Its three texts put no number on any Param. | The second text now gives every Param a number off its default. Making the new check fail first showed the finding was narrower than stated: the complete model already put every Param's default through the builder, so a *wrong* number was heard; a *dropped* one was not. Now it is, on 48 types (section 4.3). No difference between the readers came of it. |
| M3 | **"The one setting that is a view" was three.** Besides the Envelope's `sustainLevel`, an `ExplicitPartials` overwrites its `maxPartials` with the number of rows, and a `LayeredRedNoiseSource` its `count`; a `Biquad` in Resonance mode writes `a1` and `a2` as it renders. The record keeps the file's number beside rows that say otherwise. No render hears it, because the builder sets settings before arrays. | Recorded as a fact 4.7 inherits: an editor layer that pushed the record's `maxPartials` onto a node after its rows would reset the node to default rows. Whether a view should be declared as one is REVIEW R21's question. |
| m1 | A row of the tests' table of differences passed on any refusal at all, not on its own. | Each row now carries the words of its sentence, and was seen to fail on the wrong ones. |
| m2 | The writer silently keeps one of two entries when a record holds a name in two maps. Such a record fails `check()`. | The comment now says exactly that. |
| m3 | The Ledger said a ragged table "is refused and not read past its end". The node's own hook runs on each column as it arrives, before the rule. | The sentence is corrected. Reading the hook found something older: section 7, the first row. |
| m4 | Three things about the twin gate: a score event with no `note` was said to end the process; its timing always ran the loader first; `--expect` keys a row by file name and never checks where a difference falls. | The first did not hold when run: the loader's own render refuses such a file first, so the line is `BOTH REFUSE`. A guard and a count were added all the same. The timing alternates. The third stands; the gate is deleted with the loader (CLEANUP 115). |
| m5 | `--check` said "read" of a file that would still not build. | `--check` builds too (section 6, ruling 62). |
| m6 | A record that holds a NaN is not equal to itself. | Said in section 10.1; booked for 4.8's operations (CLEANUP 112). |
| m7 | At 4.7, an editor preset swap would drop an Envelope's nonzero trigger constant. | A fact for 4.7's plan (CLEANUP 111). |

Also from the review and booked: `Setting<int>::from_float` on a NaN is
undefined behaviour in both readers (CLEANUP 108), and no render hears the
layout's writer (section 4.7). Four comments it named as saying more than
their code were rewritten.

The ten Review Focus items: nine held, one broken in part (the fifth: every
new refusal was to be "one the base also gives or a listed difference", and
the list was short — M1). Of the coordinator's ten numbered claims: seven
held and three were broken in whole or in part (the list; "the one case";
the generated twin as a proof).

On the unexplained failure of section 4.7 (an oversized `ExpandRule` that
rendered through the loader and then could not be allocated through the
module), the reviewer read both builds for that text: the same calls in the
same order, the same final state, the loader's instrument freed before the
module's begins, and an earlier run at a larger size where both readers
failed the same way. Its reading is a process near its memory limit, not a
difference between readers. It was argued from the code, not run.

What the reviewer did not look at, in its words: the bodies of five clamp
hooks (`KSString`, `AllpassResonator`, `HammerBank`, `SVFSource`, `FM`); the
codec's and builder's test families beyond the float, the breaks and the
generated twin; the twin gate's Python wrapper and the meter; the sparse,
order and timing arms, none of which it ran; sections 2, 6 and 10 of this
report.

## 9. Reading guide

The module in the order that makes each file need only the ones before it.
Line counts at HEAD.

| Read | Lines | What to look for |
|---|---|---|
| `engine/include/mforce/patch/node.h` | 36 | the one record every node is; six maps and what each holds |
| `patch/graph.h`, `instrument_block.h`, `layout.h`, `fragment.h`, `patch.h` | 16, 18, 43, 20, 28 | the rest of the records; `Patch::check()` declared on the last |
| `patch/type_declarations.h` | 50 | what the table says of a type: plain values, no pointer into a node |
| `engine/src/patch/type_declarations.cpp` | 128 | one instance made per type and asked; a slot's door offered one of each type |
| `engine/src/patch/patch.cpp` | 321 | `check()`: one small function per invariant, each adding lines to one list |
| `patch/patch_json.h` | 25 | the codec's whole face: two functions per record, `load_patch`, `save_patch` |
| `engine/src/patch/node_json.cpp` | 366 | a node's bag read by kind; the preset form; the model made complete; the writer |
| `engine/src/patch/patch_json.cpp` | 355 | the document, the block, the layout, the fragment; a save through a temporary file |
| `engine/src/patch/codec_path.h`, `sentence.h`, `float_text.h`, `graph_cycles.h` | 27, 27, 28, 56 | four small internal headers; `float_text.h` is section 10.6 |
| `patch/note_signals.h`, `built_graph.h`, `build_graph.h` | 29, 22, 19 | what a build takes and what it gives back |
| `engine/src/patch/build_graph.cpp` | 426 | read `build_graph` at the bottom first, then upward: sort, create and fill, taps, dynamic pins, finish |
| `engine/include/mforce/render/built_sound.h`, `build_sound.h`, `build_instrument.h` | 17, 11, 16 | the two entry points a host will call from 4.7 |
| `engine/src/build_instrument.cpp` | 83 | a voice made from a built graph; six `static_assert`s that tie the note's fields to their labels |
| `engine/src/build_sound.cpp` | 180 | a plain sound: the mixer form and the bare form |
| `tools/twin_gate/main.cpp` | 600 | the instrument: both readers, one process, floats compared |

What not to read: `tools/engine_tests/main.cpp`'s 2,600 new lines are
tests; the families' names are in the Ledger under each task. The loader
(`engine/src/patch_loader.cpp`) lost 76 lines to the moves and is otherwise
as it was; it is deleted at 4.7.

## 10. C++ lessons

### 10.1 `operator==(const Node&) const = default`: a plain value

A C# `record` gives you value equality for free: two records are equal when
their members are. A C++ `struct` gives you nothing — `a == b` does not
compile — until you declare the operator, and since C++20 you can declare it
and let the compiler write it: `bool operator==(const Node&) const =
default;` compares every member in declaration order, and each member's own
`==` does the work (a `std::map` compares its pairs, a `std::vector` its
elements, a `std::optional` its presence and then its value). Every record
of the module has that one line, and it is what lets a test say
`from_json(to_json(p)) == p`.

Two things it cannot see, both shown in the tests: a float `-0.0` equals
`0.0` (so two records that would be written differently compare equal), and
a NaN does not equal itself (so a record holding one is unequal to its own
copy). The first is harmless here; the second is why a non-finite number in
a record is booked as something the codec should refuse.

The alternative is writing the operator by hand, which is where a member
added later is forgotten. "Plain value" also means what the record does
*not* hold: no pointer into the engine, no JSON type, nothing that needs a
destructor written. Copying a `Patch` copies a patch.

### 10.2 `std::map` where order is part of the meaning

`Node::values` is a `std::map<std::string, float>`, a sorted tree, not a
hash table. C#'s `Dictionary` is the hash table (`unordered_map` here);
C#'s `SortedDictionary` is `std::map`. The choice is not about speed. A
`std::map` walks its keys in order, the same order on every compiler and
every run, and three things rest on that: a saved file's keys are sorted, so
two saves of one patch are byte-equal; `==` between two maps is
well-defined; and a node's dynamic pins are pushed in setting-name order,
which a render can hear when two pins share one stateful chain (Task 0's
synthetic c3 measured it).

The loader shows the other side. It collected a voice's Envelope lists by
walking an `unordered_map`, whose order is whatever the hash gives. No
render could hear that order (the three loops over those lists are a
maximum and two flag writes), which is the only reason it was safe. The
builder collects them in the file's node order instead: an order that
means something beats an order that happens not to matter.

### 10.3 An anonymous namespace: a file's `private`

C# has `private` and `internal`. A C++ source file has neither for free
functions: a function defined in `patch.cpp` is visible to the linker under
its name, and a second file defining the same name is a silent clash.
`namespace { … }` — a namespace with no name — makes everything inside it
local to that one file. `patch.cpp` is 321 lines of which the last ten are
`Patch::check()`; everything above sits in an anonymous namespace
(`check_names`, `check_output`, `check_node`, …) and exists nowhere else.
The older spelling is `static` on each function, which the loader uses; the
namespace covers types too, which `static` cannot.

When two files of the module need the same helper, it goes in one internal
header beside them (`engine/src/patch/sentence.h`: a name in quotes, a comma
list) — not in the public include folder, and not copied. This checkpoint
caught itself at a third copy of a one-line function and moved it.

### 10.4 A function-local `static`: `Lazy<T>` without the type

The declarations table is built on first use and kept. In C# that is a
`static readonly Lazy<T>`. In C++ it is a `static` variable *inside* a
function:

```cpp
Table& table() {
  static Table t;      // constructed the first time control passes here, once, thread-safely
  return t;
}
```

The language guarantees the initialisation runs once even if two threads
arrive together. A `static` at file level would also be built once, but at
an unspecified time before `main`, in an unspecified order relative to other
files' statics — the "static initialisation order fiasco" — and it counts
as a file-level mutable variable, which the project's budget for new code
forbids. The same idiom runs `register_all_sources()` once:
`static const bool registered = (register_all_sources(), true);`.

The table itself is read a type at a time rather than whole, for a reason
that only showed in the tests: `engine_tests` registers six probe types as
it runs, and a table read whole at the first ask would never learn of them.

### 10.5 `to_json` / `from_json`: found by the argument's type

There is no `PatchSerializer` class. There are free functions,
`void to_json(json&, const Node&)` and `void from_json(const json&, Node&)`,
declared in the same namespace as `Node`, and the JSON library finds them:
`json j = node;` looks for a `to_json` whose second parameter is a `Node`,
searching the namespaces of the argument's type. The rule is called
argument-dependent lookup (ADL). The C# analogue is nearest to an extension
method the serializer discovers by convention, or a `JsonConverter<Node>`
registered globally — except nothing is registered: declaring the function
is the registration.

What it buys: `json(patch)` composes, because `to_json(Patch)` builds its
graph with `json(patch.graph)`, which finds `to_json(Graph)`, which finds
`to_json(Node)`. What it costs: the lookup is by overload resolution, so a
missing function is a long compiler error, and a name that collides with a
standard one is found too — a local helper called `quoted` collided with
`std::quoted` in this checkpoint, through the same lookup, and was renamed.

### 10.6 `std::to_chars`: a float has one shortest text, and two floats spoil it

The editor writes `0.6000000238418579` where you typed 0.6, because it
widens the float to a double and prints the double. `std::to_chars(first,
last, value)` with no format argument prints the *shortest* decimal text
that reads back to the same float: `0.6`. Every float has exactly one such
text. The codec writes that.

Then the measurement the plan asked for as a sample was run whole, over all
4,278,190,080 finite floats, and two of them do not come back:
±7.038531e-26. The reason is in how the file is read, not how it is
written. The JSON library reads a number as a `double` and the codec
narrows it to `float`: two roundings. For those two values the double
nearest the shortest text sits a hair on the wrong side of the midpoint
between two floats, and the second rounding lands on the neighbour. One
rounding would have been right; two are not. So the writer checks: if the
double nearest the shortest text narrows back to the float, it writes that
text; otherwise it writes the float widened exactly, seventeen digits. Two
floats in four billion get the long form, and `from_json(to_json(p)) == p`
holds for every finite float there is.

The general lesson is the one in 8.7 of Part 4 turned around: byte-identity
makes "nothing changed" a fact, and an exhaustive run makes "always" a fact
where a hundred thousand samples made it a likelihood. It took nine
minutes.

### 10.7 What the session's own mistakes teach

Five, each with the C++ or the method in it.

**A cast that keeps a number flat.** The meter counts `dynamic_cast`, and the
plan asked the builder to add none. The executor's first builder met the
number by another road: it read the record's `type` text and, where the text
said `"MultiplexSource"`, wrote `static_cast<MultiplexSource&>(node)`.
`static_cast` down a hierarchy is a promise the compiler takes on trust: it
adjusts the pointer and checks nothing. A record with two nodes under one id
broke the promise (the text was one node's, the object the other's) and the
test crashed. `dynamic_cast` asks the object itself and answers null when it
is not one. In C# terms: `obj as T` is `dynamic_cast<T*>` (null when it is
not one) and `(T)obj` is `dynamic_cast<T&>` (it throws); both are checked
at run time. `static_cast` is nearer `Unsafe.As<T>`. The builder now asks the object, the count rose by five
until the loader goes, and the builder refuses two nodes under one id before
it builds anything. The lesson past the language: a meter number held by a
means that defeats what the number is for is worse than the number rising.
The number rose and the report says why.

**"Nothing else names it."** The plan said only `patch_loader.cpp` named a
type it was about to change. Wrong twice: the loader never used the name,
and three tests did. A sentence of the form "nothing else" is a search
result and should be written from a search made that hour (Ledger 39).

**"The one case."** The Ledger called the Envelope's `sustainLevel` the one
setting a node rewrites from its rows. The reviewer found two more, and a
fourth that rewrites while rendering (section 8, M3). Same shape as the
last: "the one", "the only", "nothing but" each claim a search of every
type, and what had been done was a reading of the types that came to mind.

**A list of differences cannot be thought up.** The list of what the new
reader refuses and the old one plays was built from the rules: for each
rule of the codec and of `check()`, a file that breaks it. The reviewer came
at it from the other side, with files the *old* reader is lenient about —
`true` where a number goes, a seed too large, two nodes with one name — and
found eleven shapes the list lacked (M1). Two readers differ wherever
either is lenient, and the lenient one's leniencies are found by throwing
shapes at it, not by reading the strict one's rules.

**A proof that could not fail.** The Ledger named the generated twin as
proof that a number on a Param renders alike under both readers. Its
patches put no number on any Param (M2). And a fixture written to show a
difference (`b2`) could not show it, because its expectation of the *new*
reader could not be run until the new reader existed, and by then the row
was written (Ledger 55). The mutation run is the general answer to both: a
check counts as a proof once something has been seen to make it fail. It
found five ways the twin gate itself could go blind and say "same".

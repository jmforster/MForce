# Part 4: patch

Status: **complete and decided, 2026-11-09.** Section 1 written
2026-11-08; sections 2 to 6 decided with Matt on 2026-11-08 and sections
7 to 9 on 2026-11-09, each as proposed, with seven amendments to earlier
sections recorded where they apply (3.1, 3.2, 3.4, 3.8, 4.1, 4.8, 7.4)
and three corrections of fact annotated in place (1.3, 5.3, 7.3). The
part lands as the eight checkpoints of section 8; the first thing
written next is checkpoint 4.1's plan.
Written at maximum effort, one section at a time, each decided with Matt
before the next is written, as Part 2 was. Nothing here is
built. Parent: `target-architecture.md`, sections 3.3, 5, 7 and 8.
Builds on Part 2 (the contract, the bases, the pins) and Part 3 (the
nodes, the registry's one classification).

**Decided before this document** (`target-architecture.md`, 2026-10-05,
Matt's shape; restated here so this document stands alone):

- Two vocabularies that never mix: `Node` / `Graph` / `Patch` describe,
  `ValueSource` / voice / `Instrument` make sound. Building a `Patch`
  gives an `Instrument`. The two built-result structs named `Patch` and
  `InstrumentPatch` go away.
- One codec, `patch_json.h`: a `to_json` / `from_json` pair plus
  `load_patch(path)` and `save_patch(path, patch)`. Functions, not a
  class. One builder, `build_instrument(patch)`, wiring by descriptor
  through the registry. The registry stops including JSON; per-type
  JSON quirks move to the patch module.
- The edit operations are methods of `Patch` (add, delete, rename,
  connect, group, replace, duplicate, paste), and `Patch` keeps itself
  valid. Positions and groups are saved in the file, so they are part
  of `Patch`. The UI's `OpenPatch` sits on top (Part 7).
- The paramMap goes entirely — the engine's load-time conversion, the
  UI's converter, the UI's stash and editor — with the conversion plan
  of section 5 (library, baselines, audition and the nine pending
  patches converted on disk after a byte-identical check; sweep and
  old left as they are; afterwards a patch that still carries one
  fails to load naming `tools/parammap_to_wiring.py`).
- The node-graph *mode* goes, not the *form*: the editor's flag, the
  second saver, the UI's own streaming graph with its pan-law copy, and
  the convert-back stash. Convert becomes two `Patch` operations,
  make-instrument and make-plain-sound, carrying the guessing rule that
  `tools/add_instrument_block.py` holds today.
- The Mappings dialog loses its legacy table and its Add row; the
  hidden loudness boost (`hiBoost`) is deleted (every composition render
  moves — a listening decision); the stale-build stamp guard is
  replaced by a build rule that never links over the running program.
- The order of surgery puts the command-line tool onto the new module
  first, then the conversions on disk. The embedded score becomes a
  small score handed to the play function (Part 5), so this module no
  longer includes the Conductor.
- Matt's standing rules that bear on it: no patch back-compat inside
  the engine (change the form, no shims or toggles; the standalone
  converters are the way back for `old/` and `sweep/`); seeds are
  random at creation and stored in the file; explicit registries, no
  reflection-like designs.

Sections (each decided with Matt before the next was written):

1. Today, from the code
2. The module and the vocabulary: where `patch` sits, what it may
   include, the names (decided 2026-11-08)
3. `Patch` and `Graph` as data: a node record, wires by kind, ids and
   order, the instrument block, the score (kept for now, Matt
   2026-11-08), the layout, unknown keys (decided 2026-11-08)
4. The codec: one reader, one writer, what is strict and what is
   refused by name; the forms on disk and the two conversions
   (decided 2026-11-08)
5. The builder: `build_instrument` and the plain-sound build; the
   registry without JSON; where the eleven branches and twelve
   configurators go; Multiplex's template; the `mix` node (decided
   2026-11-08)
6. The edit operations, make-instrument and make-plain-sound, and the
   invariants `Patch` keeps (decided 2026-11-08)
7. The hosts: how the command-line tool, the tests and `OpenPatch` sit
   on top; edits that stop round-tripping through text (decided
   2026-11-09)
8. Byte-identity plan: the checkpoints of Part 4 and what each may
   change (decided 2026-11-09)
9. Deletions, in order, and what each takes with it (decided
   2026-11-09)

## 1. Today, from the code

Measured 2026-11-08 on `b30a237` (checkpoint 3.9 landed). The scope is
`patches/library` (80 files) and `patches/baselines` (52): 132 files,
957 nodes. Whole-tree numbers (3,146 JSON files under `patches/`,
36,174 nodes) are given where the difference matters.

### 1.1 The engine loader

- **Two entry points, two built results.**
  [patch_loader.h](../../engine/include/mforce/render/patch_loader.h)
  declares `load_patch_file(path)` → `Patch { sampleRate, frames,
  unique_ptr<StereoMixer> }` and `load_instrument_patch(path,
  minPolyphony)` / `load_instrument_patch_json(text, minPolyphony)` →
  `InstrumentPatch { unique_ptr<PitchedInstrument>, sampleRate }`. Both
  structs are built things; neither describes a patch. `load_patch_file`
  takes a path only: the explorer writes every variant to a temp file to
  load it (`explore.cpp:334`), and the UI writes `mforce_playback.json`
  on every play while the graph is dirty.
- **The file.** [patch_loader.cpp](../../engine/src/patch_loader.cpp)
  is 1,702 lines in module `render`. `build_graph` is 348 lines (the
  meter's budget is 80). 41 `throw`s; 11 `fprintf(stderr)` paths that
  warn and continue (an enum string matching no label keeps the default;
  a paramMap name other than `frequency` is ignored; `instrument.release`
  is ignored; a bend aimed at a node that reads frequency once; a NameGate
  name outside the vocabulary; Shaper morph under hysteresis; a tap-only
  tail in a plain-sound patch; `hold` on a non-sustaining instrument).
  Fourteen `dynamic_cast` / `dynamic_pointer_cast` sites. One file-level
  mutable, the `thread_local` tap collector. It includes `nlohmann/json`
  and five `music` headers (`basics`, `conductor`, `music_json`,
  `pitch_bend`, `pitch_curve`): five of the module map's seven
  render→music violations are here, the other two in `instrument.h`.
- **The instrument voice loop is written twice** — once in
  `load_patch_file` (lines 1403–1464) and once in
  `load_instrument_patch_json` (1646–1691): the meter's largest duplicate
  block at 62 lines, with the node-indexing pair (1385–1396, 1633–1643)
  beside it. The 2.5 review listed the pair for Part 4.
- **The type chain.** Eleven hand-written branches run before the
  generic registry path (seventeen at the 2026-10-05 review): `VarSource`
  and `RangeSource` (constructor arguments), `Envelope` (the stage-list
  form read by hand, the preset form through `envelope_json.h`, then the
  generic pass), `CurveNode` (knots, expression knots, interp, mode),
  `PerformNode` (resolves to the voice's `PerformOut` adapter; fails
  outside an instrument), `NameGate` (resolves a name against the
  instrument's onset vocabulary), `SegmentSource` (values, oneShot and
  the seed as constructor arguments), `Vibrato` (seven settings set by
  hand, then the generic pass), the three Butterworth filters as one
  branch (`sections` as a constructor argument; `cutoff` as an alias of
  `cutoffFreq`), `HybridKSSource` (`targetPartials`), and
  `MultiplexSource` (below). `SoundChannel` and `StereoMixer` are not
  nodes: the caller handles them. In scope, 378 of the 957 nodes take a
  hand-written branch: Envelope 174, CurveNode 75, PerformNode 73,
  Butterworth 17, RangeSource 12, Vibrato 11, SegmentSource 8, VarSource
  6, HybridKS 1, Multiplex 1, NameGate 0.
- **Twelve JSON configurators in the registry.**
  `source_registrations.cpp` registers 75 types; twelve registrations
  carry a `JsonConfigurator` lambda that reads the node's JSON itself:
  `FormantPeaks`, `FormantSequence`, `BWLowpassFilter`,
  `BWHighpassFilter`, `SVFSource` (the `cutoff` alias again),
  `PhasedValueSource` (`overlap`, a `stages` list with `source`,
  `percent`, `min`, `max`, `gain`), `RepeatingSource`, `CombinedSource`,
  `CrossfadeSource`, `MultiSource`, `StaticVarSource`,
  `StaticRangeSource`. The keys they read: `cutoff`, `operation`,
  `overlap`, `gainAdj`, `bias`, `varPct`, `stages`, `source`, `ratio`,
  `min`, `max`, `gap`, `gapVarPct`, `formants`, `duration`, `durVarPct`,
  `baseValue`. `source_registry.h` includes `nlohmann/json.hpp` for the
  `JsonConfigurator` and `ResolveParamFn` signatures, so every file that
  registers a type sees JSON.
- **Wiring by descriptor.** `wire_params_generic` walks the node's four
  descriptor sets: inputs (a slot refuses a signal; a multi input takes an
  array of refs; a string in a pin position is refused by name, naming
  `tools/string_evolution_to_node.py` for `evolution`), params, settings
  (bool, number, or an enum label matched case-insensitively), arrays.
  `reject_wire` names the node, the pin, the offered type and what a slot
  takes. Two type-specific checks live inside the generic function: the
  Shaper's morph and hysteresis invariants (`values2`/`segs2` lengths and
  segment types) and the `expandRule` struct (inline fields or a ref to an
  `ExpandRuleNode`, through `dynamic_cast<Partials*>`; an unresolved or
  wrong-kind ref is ignored — 3.1 §7).
- **Unknown keys.** `reject_unknown_keys` accepts a params key if it is
  in a global allowlist of 120 names (`kBranchKeys`) or in any of the
  node's descriptor sets. The list is global by design (2.5 I5): the
  commonest pin names (`frequency`, `source`, `gain`, `cutoff`, `curve`,
  `mode`, `delay`, `ratio`, `volume`, `a`, `b`, `x`) pass on every node,
  a `gap` on a `RepeatingSource` passes and is ignored (2.6 §7), and
  `formants`, `spectra`, `gains` stay listed though they are descriptors
  now (3.1 §7). The meter's structure test checks that the list covers
  every key `tools/lint_patches.py` scrapes from the loader sources.
  Node-level keys read are `id`, `type`, `params`, `inputs`,
  `dynamicPins`; any other node-level key and any top-level key passes
  silently (`ui`, `groups`, `_comment`, `_provenance`, `skeleton`,
  `variants` all exist on disk).
- **Three wire forms, one of them legacy.** `{"ref": id}` must name an
  earlier node (0 forward refs in the whole tree). `{"tap": id}` is bound
  in a second pass and may point forward; a node consumed only by taps
  goes on the voice's advance list (a plain-sound patch warns that it will
  not advance). `dynamicPins` is a node-level sibling of `params`: the
  settings this patch drives once per note, each a `{"ref"}`; a
  structural setting is refused. `instrument.paramMap` is the legacy
  form, converted at load by `build_bindings`: only the name `frequency`
  is consumed; a target is `node.pin` or a bare node id; a setting target
  becomes a push binding, a voice whose output is a Multiplex pushes every
  entry, any other pin is wired once to a chain rooted at the voice's
  frequency adapter, through a `CurveNode` (LogX or LogLog) for `curve`
  and a `CombinedSource` multiply by a linear `CurveNode` of velocity for
  `vcurve`. The chain's nodes have no ids and are in no table (2.5 I6b).
- **The instrument form.** `polyphony` (default 4) voices, each a full
  `build_graph` over the same JSON; the six `PerformOut` adapters are made
  before the build so a `PerformNode` can resolve to them. Trigger
  envelopes and the envelope population are collected by
  `dynamic_cast<Envelope*>` over every node; the Multiplex at the output
  by a cast on the root. Instrument keys read: `polyphony`, `volume`,
  `onsets`, `glideMs` (default 15), `sustaining`, `release` (retired,
  warned), `paramMap`. **The embedded score is performed at load**:
  `perform_score_json` drives a `NotePerformer` at 60 bpm (v2 keys `hold`
  and `onset`; articulation and ornament through `music_json.h`), so
  loading an instrument-and-score patch renders its audio into the
  instrument's buffers — the command-line tool times load separately for
  exactly this reason. `frames` is the score's end, or `seconds` when
  larger. **The `mix` node:** an instrument-form patch whose node list has
  a node with id `mix` supplies the mixer's `gainL`/`gainR`; the loader
  resolves them by building the whole graph a second time with a
  throwaway voice. Three library patches and seven files tree-wide carry
  one.
- **The plain-sound form.** One `build_graph`; `graph.output` is a
  `StereoMixer` whose `inputs.channels` name `SoundChannel` nodes, each
  with `inputs.source` and `volume`/`pan` params; every channel source is
  wrapped in a `MonoSource` adapter that owns a clock (`WaveSourceMono`
  and `ValueSourceMono`, identical bodies; section 5 of the target
  deletes one). A bare mono output is wrapped at unity volume and centre
  pan. `seconds` gives the length (the `frames` key is read but no file
  on disk uses it).
- **What the loader never does:** write a file, or hold a description of
  the patch after the build. Nothing in the engine serializes a patch.

### 1.2 The file on disk

- **Blocks.** In scope: `graph` 132, `sampleRate` 132 (48000 in 130,
  22050 in two baselines), `instrument` 105 (78 library, 27 baselines),
  `score` 96, `seconds` 61, `ui` 20, `groups` 7, `_comment` 10,
  `_provenance` 3. Tree-wide: `instrument` 2,090 of 3,139, `score`
  2,057, `ui` 977, `groups` 329. The two forms: 105 instrument-form
  patches in scope and 27 plain-sound (25 of them ending in a
  `StereoMixer`; 1,054 and 1,049 tree-wide).
- **The instrument block** carries `polyphony` on all 105, `paramMap` on
  82, `volume` on 19, `sustaining` on 1; tree-wide also `release` (54,
  retired) and `onsets` (5). No file carries `glideMs`.
- **The paramMap** survives on 82 in-scope patches (67 library, 15
  baselines; the voice library is 56 of the 67), 85 entries, every one a
  bare `"node.pin"` string under `frequency`. Tree-wide 528 files, 1,345
  entries: 606 bare, 739 with a `curve` (142 of them naming `interp`),
  70 with a `vcurve`. `docs/matt/parammap_patches.md` lists them by
  folder as of 2026-10-05.
- **The wiring format** is small on disk: 7 nodes carry `dynamicPins` in
  6 files (the four pianos, `viola_default`, `perform/wiring_setting`;
  179 nodes tree-wide); 28 tap edges in 13 files (2,038 in 1,453). Node
  ids beginning `__` — the converter's `__perf_`/`__curve_`/`__mul_` and
  the editor's Note faces — number 113 in scope, 3,565 tree-wide.
- **Strings in param positions**, all of them forms the loader or a
  configurator reads by hand: `Envelope.preset` 110, `CurveNode.interp`
  74, `PerformNode.field` 73, `Envelope.timeMode` 34, `CurveNode.mode`
  30, `CombinedSource.operation` 12, `SVFSource.mode` 1 (an enum label).
  Tree-wide the archived `WavetableSource.evolution` string (8, all in
  `old/`), `NameGate.name` (5) and `AdditiveSource2.partialMode` (5,
  `old/`) join them. Object-valued params without a `ref` are taps only
  in scope; tree-wide 73 inline `expandRule` objects.
- **The score** is a list of events with `note`, `time`, `duration`,
  `velocity` on every one of the 168 in-scope events; three baseline
  events carry `articulation` and `ornament`, one carries `hold`. Tree-wide 6,526 events, 23
  with `articulation`, 6 with `ornament`, 1 with `hold`, none with
  `onset`. `scores/` (42 files) is a different format — `keyName`,
  `scaleName`, `bpm`, `sections`, `parts` — and is the comp lane's.
- **The layout** the editor writes and the engine ignores: `ui.positions`
  (a map id → `[x, y]`, plus `__output`; 18 files in scope),
  `ui.panning` (16), `ui.noteFaces` (which per-field `PerformNode` file
  nodes belong to which editor face; 14), and a top-level `groups` list
  of `{name, members, pos}` (27 groups in 7 files; `oboe1` has three,
  `piano_default` five).
- **Identity and order.** Ids are free strings; the editor makes the
  label the id, replaces `.` with `_` and strips a leading `__` unless the
  id arrived synthesized. Six library ids fall outside
  `[A-Za-z_][A-Za-z0-9_]*`: `Hammer+hf` and `Hammer+hf+body` in each of
  the three pianos. No file has a duplicate id. Nodes are an
  ordered list and refs resolve against earlier entries, which the
  converter and the editor both honour by prepending synthesized nodes.
  The output node is last in 128 of 132 files.

### 1.3 The editor's twin

[main.cpp](../../tools/mforce_ui/main.cpp) is 14,058 lines; `main` is
1,738; 88 file-level mutable variables; 21 `dynamic_cast`s; 123 dated
comments. Everything below is in that one file.

- **`GraphNode` is description and live object at once** (lines
  211–571): id, type, label, the `Pin` lists (name, kind, default,
  `inputOnly`, `multi`, `isTap`, hint, and a `ConstantSource` holding the
  editable value), **and** `dspSource` — an engine node created from the
  registry in the constructor, with five construction special cases of
  its own (Parameter, PerformNode, Envelope, VarSource, RangeSource).
  Beside them: `settingValues`, `arrayValues`, the instrument
  declarations on the Output node (`polyphony`, `sustaining`,
  `onsetsBuf`, `glideMs`), `jsonSeed`, `jsonExtras` (every params key the
  load did not consume, carried verbatim and written back; an edit of a
  same-named pin erases the entry — "edit wins"), `waveformData`,
  `gridPos`, `synthesizedId`, `perfFieldIds`, the CurveNode's knots and
  interp, a `dynamicPins` JSON object with its imnodes ids. `Link` is two
  pin ids. `NodeGroup` is a name, member labels, a position and its
  boundary pins. Four UI-only types: `PatchOutput`, `Parameter`,
  `SoundChannel`, `StereoMixer`; `PerformNode` is one Note face with six
  output pins over one file node per field. `GraphMode` is tested at 32
  sites.
- **A second loader**, `load_graph_from_path` (730 lines; corrected
  2026-11-09 from "about 790"). It runs
  `convert_parammap_to_wiring` first — the third paramMap implementation,
  which synthesizes `__perf_`, `__curve_` and `__mul_` nodes and keeps the
  unconvertible entries in the stash `s_loadedParamMap` (41 references
  remain; 3.9 folded the last unconvertible class, so the stash holds
  only a non-`frequency` name, a target with no node, or an entry with no
  target — CLEANUP 44). It stashes the score, `seconds` and the
  instrument block's unmodelled keys verbatim, consumes the `PerformNode`
  file nodes into faces, makes an unknown type an inert node with a
  status line and the retired-type hint, reads params into pins, settings
  and arrays with branches for `NameGate`, `CurveNode` and `Envelope`,
  then links from refs and taps, `inputs.source`/`inputs.channels`, the
  groups, the positions and the panning.
- **Two savers.** `serialize_patch_graph` (about 434 lines) writes
  `sampleRate`, the nodes (ids from labels; per-field `PerformNode` nodes
  prepended; a param as ref, tap or constant; Envelope `stages` and
  `timeMode`; settings except `sustainLevel`; arrays; the seed; the
  extras over the top), the instrument block (extras passed through,
  `polyphony`, `sustaining`, `onsets`, `glideMs` when set, the paramMap
  stash), the score and `seconds` verbatim — or a default one-note score
  injected for a graph that never had one — the groups, `ui.positions`,
  `ui.panning`, `ui.noteFaces`. `save_node_graph` (about 165 lines) is
  the plain-sound saver: `seconds` fixed at 5, `inputs` for channel and
  mixer, groups and layout. Their two param emissions are the meter's
  second-largest duplicate block (47 lines). The six roundtrip
  KNOWN_DIFFS fail because this codec cannot carry what the file says —
  the two wiring-format baselines (`perform/wiring_setting`,
  `wiring_smoke`) and the four whose render depends on tap and advance
  order — "Part 4's codec" in every report since 2.4.
- **Two clipboards.** An internal one with a third node schema
  (`clip_node_state`: type, pins, `chPins`, settings, arrays, stages,
  `timeMode`, seed, the Output's declarations) and a text fragment
  (`mforceClip: 1`, `mode`, `nodes`, `positions`) that is produced by
  saving the whole graph to a temp file and reading it back.
- **The mode conversion pair**, `convert_patch_to_node_graph` and back,
  with a convert-back stash of paramMap, score, `seconds` and polyphony;
  the node→patch direction guesses the main source (the first channel's
  source, else the mixer's first channel, else the topologically last
  node).
- **Every engine sound goes through text.** Six sites call
  `load_instrument_patch_json(root.dump())` or `load_instrument_patch
  (path)` — Generate, the live instrument cache, the chords render, the
  audition folder, `--gatecheck`, `--dump-playback` — and
  `get_playback_patch_path` re-serializes to a temp file on every play
  while dirty. Beside that the editor streams its own graph
  (`g_stream*`), with its own copy of the equal-power pan law and
  `soft_clip` at lines 3702–3728.
- **Fourteen headless modes** (the target counted twelve):
  `--dump-curve-opts`, `--stamp`, `--gatecheck`, `--gencheck`,
  `--roundtrip`, `--rename`, `--apply-preset`, `--replace`,
  `--dump-boundary`, `--paste-frag`, `--convert-roundtrip`,
  `--dump-playback`, `--dump-layout`, `--dump-stream`. The roundtrip,
  rename, replace, paste and convert modes are the editor's only
  behaviour gate for its codec.

### 1.4 The command-line tool

[main.cpp](../../tools/mforce_cli/main.cpp) (1,268 lines) and
`explore.cpp` (604). Twelve modes: the default `run_patch` (load, render
the mixer, write the WAV, print the statistics and the faults),
`--chords`, `--melody`, `--josie`, `--compose`, `--play`, `--dun` (each
`load_instrument_patch` and a `Conductor`), `--explore` and
`--explore-filter` (mutate a patch by dot-path and render each variant
through a temp file), `--dump-descriptors` (the registry dump the lint
consumes), `--lint-template`, `--test-ornaments`. `hiBoost` is set to
0.3 by hand at five sites; `--hiboost` is a flag on the chords mode. The
target's deletion lands here.

### 1.5 Readers and writers elsewhere

- **`engine_tests`** (8,808 lines) builds 47 inline JSON patches and
  calls `load_instrument_patch_json` 39 times and `load_patch_file`
  once. The loader's behaviour is pinned by: wire rejection and unknown
  keys, the string pin, the envelope forms, the wiring door, the
  `FormantPeaks` load, the review gaps (slot hint, deleted type, alias
  key), the paramMap on an unwired pin and on a Multiplex, the tap
  guard, cycle, reading and live tests, the perform source and the note
  reseed. `tools/alloc_gate` loads both forms separately;
  `tools/test_figures` loads once.
- **The gates** (`tools/gates.py`): fast — ctest, `mforce_ui --stamp`,
  `rt_smoke.py` (one patch per shape), `alloc_gate.py`; slow — the null
  gate (a frozen manifest of 78 renders over library and baselines), the
  roundtrip over the whole scope (editor load and save, then two
  command-line renders; the six KNOWN_DIFFS listed so a seventh fails
  the run), and the wiring null gate (paramMap conversion against frozen
  hashes).
- **Python that reads or rewrites patch files**: `lint_patches.py`
  (scrapes the loader's *sources* for the hand-read keys; the meter's
  test ties `kBranchKeys` to its scrape), `parammap_to_wiring.py` with
  `null_gate_wiring.py` (mirrors `build_bindings` in JSON space),
  `add_instrument_block.py`, `rewire_instrument_output.py` and
  `fix_over_rewire.py` (the guessing rule, three times),
  `migrate_deleted_types.py` (its `RULES` and `retired_type_hint` are one
  list in two languages — CLEANUP 27), `string_evolution_to_node.py`,
  `test_stable_roundtrip.py`, `rt_smoke.py`, `bisect_roundtrip_diff.py`,
  `migrate_fullAdditive.py`, `migrate_legacy_additive_params.py`,
  `add_default_score.py`. Beside them 83 `gen_*.py` generators and 13
  `_run_*`/`_score_*` drivers emit patch JSON by hand; nine generators
  still emit deleted formant types (CLEANUP 42).

### 1.6 Where it sits in the module map

`tools/structure/modules.json` places `patch_loader.cpp` in `render`
(core, source allowed) and has no `patch` module; the loader's five
music includes are listed violations. JSON is allowed in five engine
files and `tools/`: `envelope_json.h`, `source_registry.h`,
`music_json.h`, `templates_json.h`, `patch_loader.cpp`. The budgets the
meter reports against are 600 lines per file and 80 per function.

### 1.7 Inputs booked to this part by earlier checkpoints

- The global key allowlist becoming per-type, and what a key some branch
  reads should mean on a node that has no such pin (2.5 I5; 2.6 §7's
  `gap`; 3.1 §7's `formants`/`spectra`/`gains`; 3.5 ruling 17 — the lint's
  selftest passed nothing for weeks because `frequency` was in the
  scrape).
- Whether the loader clamps every setting to its declared range, so
  the four `on_setting_changed` clamp hooks go (3.4 ruling 4; CLEANUP 19).
- Ids for the nodes made at bind time, so their faults are collected
  (2.5 I6b); the loader voice-loop pair (2.5 §7).
- The plain-sound (mixer) form's clocks: cross-loop sharing of a node
  between a channel and the mixer's gain loop is unsupported, and the
  mixer clock ticks once per channel per sample (2.3 rulings 8, D1, D2).
- `RangeSource.normalized`: the registry factory sets `true`, the
  descriptor default is `false`, the UI's new-node default differs — three
  disagreeing defaults (2.1 §7; IDEAS' universal min/max would retire the
  node).
- Whether Inputs are valid paramMap targets (2.1) — moot once the
  paramMap goes, listed for completeness.
- The `expandRule` branch's silent ignore of an unresolved or wrong-kind
  ref (3.1 ruling 11).
- The editor's `jsonExtras` carry mechanism retires with the codec (3.4
  §7); the editor's paramMap residue (CLEANUP 44); the Envelope `stages`
  branch reading the two accuracies before the generic pass reads them
  again (CLEANUP 29); the two curve dialects, ints in the preset form and
  strings in the stages form (CLEANUP 25); the `adsr` release default
  (CLEANUP 23); the editor's new-Envelope default against the registry's
  factory (CLEANUP 26); a per-form key check in the lint (CLEANUP 24);
  the one-list-in-two-languages of retired names (CLEANUP 27); a new
  `FormantPeaks` from the menu having no rows (CLEANUP 45, Part 7's).
- The six roundtrip KNOWN_DIFFS, which the target's one codec is meant
  to close (checkpoint 0 report; 2.6 §7).
- From Matt's GOALS (uncommitted, 2026-11): a **Patch node** (drop a
  node that *is* another patch file; its output pin is that patch's
  output — for reusing structures like the loop and for variants without
  copying), a **patch audit** (disable nodes or sections and compare the
  render to find what the found sound no longer needs), and **node
  bypass**. All three are feature work after the freeze; the data model
  this part decides should not make them hard.

## 2. The module and the vocabulary

Status: **decided with Matt, 2026-11-08.** The direction (2.1) in
session; the names, the export list and the files (2.2–2.4) as proposed
("Names stand").

### 2.1 Where `patch` sits (decided)

- **A module between `nodes` and `render`**, at
  `engine/include/mforce/patch/` and `engine/src/patch/`. It may include
  `core`, `nodes` and, once Part 5 creates it, `contract`. It includes
  nothing from `render` or `music`, and JSON only in its codec file. The
  module map gains the row; the meter reports the arrows.
- **It builds graphs, not instruments.** Its one build call takes a
  `Graph` and stand-ins for the note's six fields and returns the nodes by
  id, wired and checked, with the output signal named. It never sees a
  voice, a mixer or a performance adapter.
- **`render` builds instruments and plain sounds over it.**
  `build_instrument(patch)` creates the `PitchedInstrument` and, for each
  voice, makes the voice's `PerformSource` and six `PerformOut` adapters,
  calls the graph build with those as the stand-ins, and wraps the result
  in the voice. The plain-sound build — a `StereoMixer` over the file's
  channels, or one channel around a bare output — is `render`'s too. The
  command-line tool, the editor and the tests call those two functions
  and never the graph build directly.
- **The direction is the one the target drew** (`render` depends on
  `patch`; `patch` on `contract`, `nodes`, `core`), and the seam is the
  one the loader already has: today `build_graph` takes a
  `PerformContext` of six `shared_ptr<ValueSource>` made by
  `make_perform_context`, and a `MultiplexSource` asks for its clones
  through a closure typed over `Component` and `ValueSource`, both
  `core`'s. Nothing in the direction is new; what changes is that one
  file becomes two modules and two functions.

Why this way and not the other, for the reader: `patch` is the editor's
model library. With the audio types out of it, the editor links it to
load, edit and save a patch and computes nothing; a tool that lints,
diffs or converts patches links it alone. The other direction would make
every such program carry voices, the mixer, the limiter and WAV I/O, and
would flip an arrow in the target's table for no gain.

### 2.2 The names (proposed)

The target fixed the three pairs — `Node` / `ValueSource`, `Graph` / a
voice, `Patch` / `Instrument`. The parts and the seam still need words.

| Description side | Live side | Note |
|---|---|---|
| `Patch` — the whole file as data: a `Graph`, the optional instrument block, the optional score (kept for now, 2.5), the layout | `Instrument` (`PitchedInstrument`, `DrumKit`) | today's built struct named `Patch` in `patch_loader.h` goes in the first checkpoint, so the name is free when the new type arrives |
| `Graph` — the ordered nodes and the output node's id | a voice (`VoiceGraph` today) | |
| `Node` — one node's record: id, type, its pin values (section 3) | `ValueSource` | no `Node` exists in the engine today; the editor's `GraphNode` goes in Part 7 |
| `InstrumentBlock` — polyphony, volume, sustaining, onsets, glideMs | | the source `InstrumentClass` (contract, Part 5) is filled from |
| `Layout` — positions, groups, note faces | | editor data the engine never reads, kept in `Patch` because the file holds it |
| `Score` — the embedded note list | | kept for now (2.5); whether its type is a small local record or the music model's is section 3's |
| `NoteSignals` — the six stand-ins the graph build takes | `PerformOut` × 6 | replaces the loader's `PerformContext`: "Context" is the word `KeyContext` already uses (target 3.1), and these are signals |
| `BuiltGraph` — what the graph build returns: the nodes by id and the output signal | | today's `GraphResult`; the voice adds the clock, the perform source and the bindings |

**Namespace: `mforce`, flat**, like `nodes` and `render`. Part 3's `Part`
collided with the music model's `Part` and became `DrivenPart`; `Node`,
`Graph`, `Patch` and `Layout` collide with nothing in the engine today,
and they are the words the project chose. A `mforce::patch` namespace
would protect the generic words at the price of `patch::Patch` in every
host; not proposed.

Alternatives weighed: keeping `PerformContext` (the loader's name, one
rename fewer; set aside for the word alone); `GraphInstance` for
`BuiltGraph` (the same thing said less directly); `InstrumentSpec` for
`InstrumentBlock` ("instrument" is what the file calls the block, and
"spec" is the comp lane's word for a passage specification).

### 2.3 What the module exports (proposed)

Four things, and nothing else:

1. **The data model**: `Patch`, `Graph`, `Node`, `InstrumentBlock`,
   `Layout`, and `Score` while it stays. Plain values — copyable,
   comparable, no pointer into the engine.
2. **The codec**, `patch_json.h`: `to_json(json&, const Patch&)`,
   `from_json(const json&, Patch&)`, `load_patch(path)`,
   `save_patch(path, patch)`. The only file in the module that includes
   JSON (section 4).
3. **The graph build**, `build_graph(const Graph&, const NoteSignals*,
   const InstrumentBlock*) → BuiltGraph`: the registry create, the wiring
   by descriptor, the per-type forms, and the checks that fail a build by
   name (section 5). A null `NoteSignals` is the plain-sound case, where a
   `PerformNode` is an error, as today; the block supplies the onset
   vocabulary a `NameGate` resolves against.
4. **The edit operations**, as methods of `Patch` (section 6).

What `render` adds over them: `build_instrument(const Patch&, int
minPolyphony) → std::unique_ptr<PitchedInstrument>` and a plain-sound
build returning the mixer and its frame count (its result's name is
section 5's; today that result is the struct called `Patch`). `DrumKit`
is assembled by the hosts by hand today and stays so until Part 5.

### 2.4 Files (proposed)

One type per file, Part 3's rule: `patch/node.h`, `patch/graph.h`,
`patch/patch.h` (the edit operations declared there),
`patch/instrument_block.h`, `patch/layout.h`, `patch/note_signals.h`,
`patch/built_graph.h`, `patch/build_graph.h`, `patch/patch_json.h`;
their `.cpp` under `engine/src/patch/`. `render/build_instrument.h`
beside `instrument.h`. `patch_loader.h` and `patch_loader.cpp` are
deleted when the last host moves (section 8).

### 2.5 Decided in session, 2026-11-08

- **The direction** (2.1): `patch` builds graphs; `render` builds
  instruments and plain sounds over it; the target's arrows stand. Matt:
  "I agree with your preference."
- **The embedded score stays in the file for now.** Its removal is a
  follow-on, decided together with how patches are tested going forward
  (BACKLOG row 10). Matt: "it can be a follow-on, in context of decisions
  about how we want to test patches going forward. So keep for now." The
  10-05 sentence stands — the block becomes a small score handed to the
  play function — and until Part 5 builds that function, `render`'s
  `build_instrument` keeps today's performance of it as a function of its
  own, so no render moves (section 8).

## 3. `Patch` and `Graph` as data

Status: **decided with Matt, 2026-11-08, as proposed** ("Section 3
stands"): the six questions of 3.10 all resolved the way they are
written.

### 3.1 The records (proposed)

One record shape for every node, whatever its type. A node is its id,
its type, and six maps keyed by the names the type declares:

```cpp
struct Wire { std::string source; bool tap{false}; };   // a feed: the node it reads, and whether through a tap

struct Node {
  std::string id;
  std::string type;
  std::optional<uint32_t> seed;                          // random at creation, stored for reproducibility
  std::map<std::string, float>              values;      // a param pin's constant, while it is unwired
  std::map<std::string, std::vector<Wire>>  wires;       // a pin's feeds: one for a pin, several for a multi input
  std::map<std::string, float>              settings;    // a scalar setting as the engine stores it: a bool is 0/1, an enum its index
  std::map<std::string, std::string>        strings;     // a string setting (a new kind, 3.3): NameGate's name
  std::map<std::string, std::vector<float>> arrays;      // an array, or one column of a table
  std::map<std::string, std::string>        dynamicPins; // a setting and the node that feeds it once per note (the pin model's word; 4.9 Q2)
  std::string comment;
};

struct Graph { std::vector<Node> nodes; std::string output; };

struct InstrumentBlock {
  int polyphony{4};
  float volume{1.0f};
  bool sustaining{false};
  std::vector<std::string> onsets;                       // ordered: an onset's id is its position, 0 is none
  float glideMs{15.0f};
};

struct Vec2 { float x{0}, y{0}; };
struct Group    { std::string name; std::vector<std::string> members; Vec2 pos; };
struct NoteFace { std::string label; std::map<std::string, std::string> fields; };   // field -> the file node it reads
struct Layout {
  std::map<std::string, Vec2> positions;                 // by node id
  std::optional<Vec2> outputPos;                         // the editor's Output node ("__output" today)
  std::optional<Vec2> panning;
  std::vector<Group> groups;
  std::vector<NoteFace> faces;
};

struct Patch {
  Graph graph;
  std::optional<InstrumentBlock> instrument;             // present: an instrument; absent: a plain sound
  Layout layout;
  int sampleRate{48000};
  std::optional<double> seconds;
  std::string score;                                     // the embedded score as text this module never reads (2.5; 3.5)
  std::string comment;
};
```

Plain values: copyable, comparable, no pointer into the engine, no JSON
type. The maps are ordered by name, which is the order the file has today
(the JSON library writes objects sorted). A `std::map` rather than a
vector of pairs because every lookup the codec, the builder and the
editor make is by name.

Two things a reader will look for and not find. There is no `Pin` type
and no per-type form type: a pin is a name the type declares, and what
the file holds for it is a constant, a feed, or nothing. There is no
`Instrument` or `ValueSource` anywhere in these records.

### 3.2 Wires by kind (proposed)

- **A constant**: `values[pin]`. A param pin's number while nothing feeds
  it, which the builder makes the pin's own constant; or an input pin's
  number, which the builder makes a `ConstantSource` wired in, since an
  input has no constant of its own (amended under 5.12 Q1, 2026-11-08:
  the 33 in-scope cases are all a `CombinedSource` operand).
- **A signal wire**: `wires[pin] = {{source}}`. The builder wires the
  source node into the pin and the type's by-name door accepts or refuses
  it (a part into a signal pin, a signal into a slot).
- **A tap wire**: `wires[pin] = {{source, tap = true}}`. Read through a
  `TapSource` after every other node is built, so it may close a cycle; a
  node fed only by taps goes on the voice's advance list, as today.
- **Several feeds**: a multi input or a slot list — `MultiSource.source`,
  `CompositePartials`' parts, `FormantSequence.spectra`, the mixer's
  channels — holds a vector in file order, which is semantic for a
  sequence and for any parallel array.
- **A dynamic pin**: `dynamicPins[setting] = source`. Today's
  `dynamicPins`: the setting is evaluated from the source once per note and pushed, never
  pulled; a structural setting is refused. The setting keeps its own
  value in `settings` while pushed, which is the editor's stowed scalar
  (pin model, settled 2026-08-19).
- **A pin has a constant or feeds, never both.** The one place today's
  file can say both is refused.
- **No attribute rides on a wire.** `MultiSource`'s per-source `weight`
  (three library electric pianos) and the per-source delay its header
  offers become arrays on the node, parallel to its `source` feeds (3.3).
  The channel and mixer nodes' `inputs.source` and `inputs.channels` are
  wires like any other; the spelling is the codec's (section 4).

### 3.3 Forms: what the uniform record cannot hold today, and how it will (proposed)

The rule: **a form the record cannot hold is a missing kind, not a case
for a branch.** The inventory of section 1 reduces to two new kinds and
a list of nodes declaring what they already read. The node changes are
section 5's to order; the kinds are decided here.

| Today's form | Where it lands | What changes |
|---|---|---|
| `Envelope` preset form (`preset` plus knob keys; 110 in scope, 2,078 tree-wide) | read-time form: the codec lays the stages out through `envelope_json.h`'s table and stores stages; written back as stages (3.10 Q4) | nothing in the node; the table stays the one list of layouts, which the editor's Apply already uses |
| `Envelope` `stages` (a list of eight-field stages) | a table: eight arrays in one group — `startVal`, `endVal`, `power`, `type`, `percent`, `minSec`, `maxSec`, `nominal`; `timeMode` an enum setting (fraction, seconds) | the Envelope declares its stage columns as array descriptors; `type` is an **enum column** — the new kind: `ArrayDescriptor` gains `enum_labels` (Linear, Expo, InverseExpo, Sine, Hold) so the file keeps its words |
| `CurveNode` `knots`, `exprKnots`, `interp`, `mode` | `knots` a table of `x`, `y`; `exprKnots` a table of `x`, `form` (enum column: linear, power), `a`, `b`; `interp` an enum setting (linear, logx, loglog); `mode` an enum setting (points, expressions) | the node declares them; its public members become pins |
| `expandRule` inline object (0 in scope, 73 tree-wide) | gone: the `ExpandRule` node wired into the `expandRule` slot is the one form | the branch goes; the standalone converter rewrites the 73 |
| `PhasedValueSource` `stages` (each a source ref with four numbers), `overlap` | a `sources` multi input and a table of `percent`, `min`, `max`, `gain`; `overlap` a setting | node change; one baseline |
| `MultiSource` per-source `weight`, delay | arrays `weights`, `delays`, parallel to `source` | node change; three library patches, four tree-wide |
| `BWLowpassFilter`/`BWHighpassFilter`/`BWBandpassFilter` `sections` (a constructor argument) | a structural int setting | node change; one baseline carries it |
| `cutoff` as an alias of `cutoffFreq` (one in-scope file; three configurators and the loader's branch) | one name, the descriptor's `cutoffFreq` | the alias goes; the converter renames |
| `HybridKSSource` `targetPartials` | an array | node change; one baseline |
| `VarSource` `absolute`, `RangeSource` `normalized` (constructor bools; `normalized` has three disagreeing defaults, 2.1) | bool settings with one default each | node change |
| `SegmentSource` `values`, `oneShot`, seed (constructor arguments) | an array, a bool setting, the seed | node change |
| `Vibrato`'s seven keys set by hand with the branch's defaults | settings, as they already are | the branch goes once its defaults are shown equal to the declared ones (section 5) |
| `NameGate` `name` | a **string setting** — the second new kind: `SettingType::String`, held in `Node::strings` | descriptors gain the kind; the node declares `name` |
| `PerformNode` `field` | an enum setting (frequency, velocity, wheel, pressure, duration, onset) | `PerformNode` becomes a registered type that the builder resolves to a stand-in (section 5) |
| `MultiplexSource`'s template (a JSON subtree re-parsed at prepare) | nothing stored: derived from the `Graph` at build | section 5 |
| `FormantPeaks` equal-length check, `FormantSequence`'s refusal of `formants` | the generic rule: a table's arrays have one length; an unknown key is refused by name | the two configurators go |
| `CombinedSource`, `CrossfadeSource`, `StaticVarSource`, `StaticRangeSource`, `RepeatingSource` configurators (`operation`, `gainAdj`, `bias`, `ratio`, `min`, `max`, `baseValue`, `varPct`, `duration`, `durVarPct`, `gap`, `gapVarPct`) | settings or pins the nodes declare | section 5 reads each and says which |

Two kinds, then: a string setting, and an enum column on a table. The
tables themselves are not new — grouped arrays exist since the
`ExplicitPartials` columns, and 3.9 made the formant peaks four `Array`
rows. The Envelope's stages and the Curve's knots take the same shape.

### 3.4 Ids and order (proposed)

- **An id** is any non-empty string, unique within its graph. The model
  reserves no prefix: `__` is the converter's and the editor's convention
  for synthesized nodes, and the codec does not care. The six library ids
  with a `+` stay valid.
- **Order carries no meaning.** `Graph::nodes` is the file's order and the
  writer keeps it for stable diffs; the builder sorts topologically over
  signal wires, taps excluded, and refuses a cycle with no tap in it by
  naming its nodes. A forward reference becomes legal, so a hand-authored
  file cannot fail on order. Today's loader builds in list order and
  requires feeds to come first, and 0 of 3,146 files break the rule, so no
  file changes. No render can depend on build order: the loader already
  collects a voice's envelopes by walking an unordered map, every seed is
  the node's own, and faults and capture go by id.
- **The output** is a node id; the builder requires it to name a signal
  or, in the plain-sound form, the mixer part (5.8) — any other part is
  refused, as today (amended under 5.12 Q4, 2026-11-08).

### 3.5 The instrument block, `sampleRate`, `seconds`, and the score (proposed)

- `InstrumentBlock` holds exactly what the loader reads: `polyphony`,
  `volume`, `sustaining`, `onsets`, `glideMs`. Its presence makes the
  patch an instrument; its absence a plain sound (the target's one kind
  of thing, two forms). `release` (retired; 54 files, all old) and
  `paramMap` are refused by name, the paramMap naming
  `tools/parammap_to_wiring.py` (the target's rule).
- `sampleRate` and `seconds` stay fields of `Patch`. For the plain-sound
  form, `seconds` is part of what the form is (it "plays for a set number
  of seconds"); for the instrument form the two are the standalone
  render's rate and length. They belong to the same question as the
  embedded score — how a patch is tested on its own — and move together
  with it if BACKLOG row 10 moves it (two baselines are at 22050 Hz,
  `double_advance_bug` and `_ok`).
- **The score rides as text.** `Patch::score` holds the block's JSON text
  verbatim; this module never parses it. The performer that plays it
  (`render`'s `build_instrument` until Part 5, the play function after)
  parses it with `music_json`, as the loader does today. Why not a typed
  record: three baselines' events carry `articulation` and `ornament`,
  which are music-model types, and `patch` includes nothing from `music`;
  the alternative that keeps a typed record would put a Composition-tier
  type into this module or into `contract`, where it does not belong.
  Text is the honest shape for a passenger that is leaving (2.5), and when
  it leaves the change is one field (3.10 Q5).

### 3.6 The layout (proposed)

`Layout` is the editor's data the engine never reads, kept in `Patch`
because the file holds it (the target, 2026-10-05): a position per node
id, the Output node's position, the canvas panning, the groups (a name,
member node ids, a position), and the note faces (which per-field
`PerformNode` file nodes an editor face shows as one node; Part 7 may
replace the device and this record with it). The codec round-trips it
exactly; the edit operations keep it consistent — a deleted node leaves
its position, its group and its face; a renamed node is re-keyed. A
patch a tool writes has no layout, and the editor lays out what has
none, as today.

### 3.7 Unknown keys and comments (proposed)

- **Every key the codec does not know is refused by name with its path**
  — `graph.nodes[3].params.frequncy`, `instrument.release`,
  `graph.nodes[7].extras` — at every level: the top, the node, the
  node's params. Today only the params level is checked, against a global
  list; this closes Part 2 §6's open question (reject, not preserve) and
  the per-type half of 2.5 I5, because the known keys are the type's own
  declarations plus the record's fixed keys, so `frequency` on a
  `WhiteNoiseSource` is refused.
- **One comment, two places.** `Patch::comment` and `Node::comment`,
  free text, written only when non-empty. The ten baselines' `_comment`
  becomes `comment`; the three voice patches' `_provenance` objects
  (source file, note, why, run) become comment text. Nothing else that is
  not the model may appear in a file; the sweep and old folders' spec
  files that mix `skeleton`, `variants`, `master_seed` into patch JSON
  fail to load until converted, like everything else there.

### 3.8 What `Patch` keeps true (proposed; section 6 has the operations)

Without the registry: ids unique and non-empty; `output` names a node,
or is empty while a patch is being assembled — the builder refuses an
empty one by name (amended under 6.12 Q1, 2026-11-08); every wire and
every push names a node; a pin has a constant or feeds,
not both; every layout entry names a node; no cycle without a tap. With
the registry (checked by the codec on read and by every edit): the type
is registered; every pin, setting, string, array and push names a
declaration of the type; a multi input is the only pin with several
feeds; a slot's source is a part of the declared kind; a table's arrays
have one length; a push names a non-structural setting. Whether a
setting outside its declared range is clamped or refused is section 4's
(CLEANUP 19).

### 3.9 Designed for, not built

Matt's GOALS name three things this record should not make hard. A
**bypass** is one more field on `Node` and a builder rule per family. A
**Patch node** is a type whose string setting names a file, which the
string kind of 3.3 makes expressible; the builder's work is section 5's
to leave room for. A **patch audit** copies a `Patch`, deletes a node or
a group, builds and compares — copyable records and the edit operations
are all it needs.

### 3.10 The six questions (decided 2026-11-08, each as written)

1. **The uniform record** (3.1–3.3): six maps over the type's declared
   names, the forms made generic by nodes declaring what they already
   read, and exactly two new kinds — a string setting and an enum column
   on a table — rather than typed per-type form structs in the model.
2. **Order carries no meaning** (3.4): the builder sorts; a tap-less cycle
   is refused by name. The alternative keeps today's rule that a feed
   comes first, as an invariant the edit operations maintain.
3. **Enum spelling**: the reader takes an enum setting as its index or
   its label; the writer always writes the label. Files converge on save.
   This closes CLEANUP 25 (the two curve dialects). The strict alternative
   refuses the index and converts the tree once.
4. **The Envelope preset form** stays a read-time form defined by the
   engine's own table, written back as stages; or the converter rewrites
   the 2,078 nodes and the codec reads stages alone.
5. **The score as text** this module never reads, versus a typed record
   somewhere a music type may live.
6. **`comment` at the patch and the node**, with `_comment` and
   `_provenance` converted into it.

## 4. The codec

Status: **decided with Matt, 2026-11-08, as proposed** ("Section 4
stands"): the seven questions of 4.9 all resolved the way they are
written; 3.1's `pushes` renamed `dynamicPins` under Q2.

### 4.1 One reader, one writer (proposed)

- `patch/patch_json.h` holds `to_json(json&, const Patch&)` and
  `from_json(const json&, Patch&)`, the same pair for `Graph`, `Node`,
  `InstrumentBlock` and `Layout` (ADL pairs, as `music_json.h` does), and
  `load_patch(path) → Patch`, `save_patch(path, const Patch&)`. The only
  file in the module that includes JSON. A fragment the editor copies is
  a `Fragment` — its own record, since its feeds may name nodes it does
  not hold (6.5; amended under 6.12 Q2, 2026-11-08) — with one more pair
  here (the target keeps the fragment format; Part 7 cuts the editor
  over).
- **A read either succeeds completely or fails naming the place**: file,
  path, node id, type and reason, as one `std::runtime_error` — load is
  control-side work, so Part 2 §6's no-throw rule, which is for render
  paths, does not bind here. The editor shows the message in its status
  line; the command-line tool prints it.
- Who calls it: the command-line tool, the editor, the tests, and the
  converter's check mode. The builder never does — it takes a `Patch`.
  Python tools that read patch files keep reading JSON themselves; the
  lint's job becomes a load through the codec (section 7).

### 4.2 Declarations: what the codec knows about a type (proposed)

- **`type_declarations(type)`**: one prototype per registered type, made
  once through the registry and cached, read into a plain table — the
  param pins with default and range, the inputs with multi, slot and
  hint, the settings with type, range, labels and the structural flag,
  the arrays with group and labels, the string settings. The codec
  validates every name and value shape against it; the editor builds its
  pins from it (today's `build_pins` makes the same prototype); the
  builder wires by it (section 5). `mforce_cli --dump-descriptors`, which
  the lint reads today, becomes this table printed.
- **The two terminal types**, `SoundChannel` and `StereoMixer`, declare
  pins like any type — `source`, `volume`, `pan`; `channels` (multi),
  `gainL`, `gainR` — as registered parts whose owner is the plain-sound
  build (section 5). The codec has no special case for them.
- `PerformNode` is a registered type with the `field` enum setting;
  `NameGate` declares `name` as a string setting (3.3). Nothing the codec
  reads is outside a declaration or the record's own fixed keys.

### 4.3 The file's shape (proposed)

The file keeps the shape it has, with the shadows removed:

```
{
  "sampleRate": 48000,
  "seconds": 3.0,                      // when present
  "comment": "…",                      // when non-empty
  "instrument": { "polyphony": 1, "volume": 0.5, "sustaining": true,
                  "onsets": ["tongue", "breath"], "glideMs": 15 },   // when the patch is an instrument
  "score": [ … ],                      // the passenger (3.5), re-emitted from its text
  "graph": {
    "output": "Reverb",
    "nodes": [
      { "id": "Breath_curve", "type": "CurveNode", "seed": 7,
        "params": {
          "source": {"ref": "__perf_freq"},              // a feed
          "x": [100, 1000], "y": [0.2, 0.9],             // a table, column-major
          "interp": "logx", "mode": "points"             // enum settings, as labels
        },
        "dynamicPins": { "sustainLevel": {"ref": "Sus_curve"} },
        "comment": "…" },
      { "id": "Damp_lpf", "type": "BWLowpassFilter",
        "params": { "cutoffFreq": {"tap": "Damp_cutoff_curve"},   // a tap feed
                    "inputSource": {"ref": "Junction"},
                    "sections": 2 } }                              // a setting
    ]
  },
  "layout": { "positions": {"Breath_curve": [-1108.2, -265.7], …},
              "output": [1716.0, 352.0], "panning": [1716.0, 352.0],
              "groups": [{"name": "Breath", "members": […], "pos": [x, y]}],
              "faces": [{"label": "Note1", "fields": {"frequency": "__perf_frequency"}}] }
}
```

- **One `params` bag**, as today. For a type, a name's kind is its
  declaration, so a bag loses nothing, and a human reads a node as one
  list. What the codec checks is the value's shape against the kind: a
  pin takes a number, a feed object or a list of feeds; a setting a
  number, a bool or a label; a string setting a string; an array a list of
  numbers or labels. A `seed` stays in `params` where every seeded node
  has it.
- **`dynamicPins` keeps its name.** "A promoted setting is a dynamic
  pin" is the vocabulary settled 2026-08-19 (pin model §8–9). The record's
  map carries the same word, `Node::dynamicPins` (4.9 Q2; 3.1 amended).
- **`inputs` folds into `params`.** The channel's `source` and the
  mixer's `channels` are feeds like any other; the writer writes one
  spelling.
- **One `layout` block** replaces `ui` and the top-level `groups`:
  `positions`, `output` (the editor's Output node position, `__output`
  today), `panning`, `groups`, `faces` (`noteFaces` today). Editor data
  under one name, written only when present.
- **`_comment` and `_provenance` become `comment`**, at the patch or the
  node (3.7).

### 4.4 Values on disk (proposed)

- **Numbers.** A float is written as the shortest decimal that reads
  back to the same float — `0.6`, not the `0.6000000238418579` the editor
  writes today for a float widened to double. An int setting is an
  integer, a bool is `true`/`false`.
- **Enum settings** are read as the index or the label (3.10 Q3) and
  written as the label. In scope today 107 `CombinedSource` operations
  and 19 `SVFSource` modes are integers, 12 and 1 are labels, and every
  `CurveNode` interp and mode is a label; after the converter all are
  labels.
- **Tables are column-major**, one key per column, every column one
  length — the shape 3.9 gave the formant peaks and `ExplicitPartials`
  has always had. The Envelope's stages (64 stage-form nodes in scope)
  become eight columns with `type` as labels; the Curve's knots (74) two
  columns and its expression knots (30) four. A ragged table is refused
  by name, which is the `FormantPeaks` configurator's rule made general.
- **Feeds** are `{"ref": id}` and `{"tap": id}`, a list of them on a
  multi input; a dynamic pin's feed is `{"ref": id}` — a tap into a
  setting means nothing and is refused.

### 4.5 The Envelope's preset form (decided 3.10 Q4; the mechanics proposed)

The codec reads `{"preset": "adsr", …knob keys…}` through the table of
six layouts that is `envelope_json.h` today, stores the stages it lays
out, and writes stages back. The table moves out of the nodes module
into the codec — the nodes stop including JSON, as the target says — and
the Envelope keeps its `make_ar`…`make_adsr` factories, which the table
calls. The editor's Preset combo and Apply call the same patch-module
function, so the two cannot drift. The 110 preset-form nodes in scope
are unchanged on disk until the editor saves them.

### 4.6 What is refused (proposed)

By name, with the path, at read:

- a key no declaration and no fixed key knows, at any level — the
  per-type half of the allowlist debt (2.5 I5) closed by construction;
- a type the registry does not have, with `retired_type_hint`'s converter
  named, as today;
- a value of the wrong shape for its kind; a string where a pin takes a
  value, naming the string (3.4's rule kept);
- a feed, a dynamic pin, the output, a layout entry or a group member
  naming no node; a duplicate id; a pin with both a constant and a feed;
- a dynamic pin on a structural setting, or on a name that is not a
  setting (today's rules kept);
- `instrument.paramMap`, naming `tools/parammap_to_wiring.py`;
  `instrument.release`;
- **a setting outside its declared range** (4.9 Q4). Today a node with
  a clamp hook clamps it and a node without one uses it as written. The
  checkpoint's first task is a lint over library and baselines counting
  such values; where a test patch legitimately exceeds a range, the
  declaration widens, never the file; then the four `on_setting_changed`
  clamp hooks go (CLEANUP 19), and the editor clamps at edit time.

What the codec does not check, because declarations cannot tell: that a
feed's source is a signal where a pin wants one, or a part of the right
kind where a slot wants one. That is the type's by-name door at build
(Part 2 §6) and stays the builder's (section 5).

### 4.7 The writer's discipline (proposed)

- **The model is complete, so the file is self-contained.** At read, the
  codec fills every declared pin that the file leaves out with its
  declared default and every declared setting likewise, into the model;
  the writer writes everything the model holds. A patch therefore never
  depends on an engine default that could later move, which is what the
  editor's files already give (it writes every pin and every setting),
  and `from_json(to_json(p)) == p` holds for every patch. A sparse
  hand-authored file loads, and becomes full on its first save (4.9 Q5).
- Keys sorted, as the JSON library writes a map; nodes in `Graph::nodes`
  order; indent two spaces; `layout`, `instrument`, `score`, `seconds`,
  `comment`, `dynamicPins`, `seed` written only when present.
- The writer is one function. The editor's two savers, its two clipboard
  schemas and its paramMap stash have nothing left to do once it holds a
  `Patch` (Part 7 removes them).

### 4.8 The conversion on disk (proposed)

The codec reads one form. Everything a file says differently today is
rewritten once, by one standalone converter, before the codec lands
(amended under 8.8 Q2, 2026-11-09: the converter's rules land over four
checkpoints, 4.2 to 4.5, each a pass over the same four folders, so the
tree is in the codec's form before the codec exists — 8.1):

| Rewrite | In scope | Tree |
|---|---|---|
| `instrument.paramMap` → wiring nodes (the target's plan; `parammap_to_wiring.py` exists; the wiring null gate checks it) | 82 files | 528 |
| `inputs` → `params` on channel and mixer nodes | 27 files | 1,049 |
| enum integers → labels | 126 nodes | — |
| Envelope stage lists → columns | 64 nodes | — |
| Curve `knots`, `exprKnots` → columns | 74 + 30 nodes | — |
| `ui` + `groups` → `layout`; `noteFaces` → `faces`; `__output` → `output` | 20 + 7 files | 977 + 329 |
| `_comment`, `_provenance` → `comment` | 13 files | 532 + 11 |
| `cutoff` → `cutoffFreq` | 1 file | — |
| `MultiSource` per-source `weight` → `weights` array | 3 files | 4 |
| inline `expandRule` → an `ExpandRule` node | 0 | 73 |
| retired type names, string evolutions (the 3.4 and 3.5 tools) | 0 | the archived forms |

Nearly every in-scope file changes once, mechanically, and no render
moves: the proof is the A/B the refactor already runs — the
pre-checkpoint CLI over the old files against the new CLI over the
converted files, byte for byte, and the null gate's manifest hashes
unchanged. One tool, `tools/convert_patch.py`, idempotent, with a
`--check` mode the gates can run; `parammap_to_wiring.py`,
`migrate_deleted_types.py` and `string_evolution_to_node.py` become its
rules and retire as separate tools. The folders follow the 10-05 plan:
library, baselines, audition and pending rewritten in the checkpoint
(the pending folder is 43 files, beyond the nine the 10-05 authorisation
named — 4.9 Q6), sweep and old converted on demand. The engine never
reads an old spelling; the converter is the only program that does.

### 4.9 The seven questions (decided 2026-11-08, each as written)

1. **One `params` bag** with `dynamicPins` beside it, and one `layout`
   block — rather than a block per kind (`values`, `wires`, `settings`,
   `arrays`).
2. **`Node::dynamicPins`** as the record's name for the map 3.1 first
   called `pushes`, so the file, the model and the settled vocabulary
   use one word.
3. **Tables column-major on disk**, stages and knots included (3.9's
   precedent) — rather than row-major stage objects, which read more
   naturally for an envelope but make a second table shape.
4. **A setting outside its range is refused by name**, after a lint count
   over the scope, with the declaration widened where a test patch
   legitimately exceeds it; the four clamp hooks then go. The
   alternatives: clamp silently at read, or accept as written as today.
5. **The model is complete**: defaults filled at read, everything
   written, so a file never depends on an engine default. The
   alternative writes only what differs from the defaults.
6. **One converter, one pass** over library, baselines, audition and all
   43 pending files, with sweep and old on demand, and the codec reading
   one form only.
7. **Floats written shortest-roundtrip**; `seed` stays in `params`.

## 5. The builder

Status: **decided with Matt, 2026-11-08, as proposed** ("s5 stands"):
the eight questions of 5.12 all resolved the way they are written; 3.2
and 3.4 amended by a clause each under Q1 and Q4, as 4.9's Q2 amended
3.1 by a word.

### 5.1 The shape (proposed)

```cpp
// patch/note_signals.h — the six stand-ins a graph build takes; render makes
// them from a voice's PerformSource (today's PerformContext, 2.2)
struct NoteSignals {
  std::shared_ptr<ValueSource> frequency, velocity, wheel, pressure, duration, onset;
};

// patch/built_graph.h — what a graph build returns (today's GraphResult plus
// what the two voice loops derive from it by hand)
struct BuiltGraph {
  std::unordered_map<std::string, std::shared_ptr<Component>> nodes;   // every node by id, signal or part
  std::shared_ptr<Component> output;              // the output node: a signal, or the mixer part in the plain-sound form (5.8)
  struct Signal { std::string id; ValueSource* node; };
  std::vector<Signal> signals;                    // the signals among the nodes, in id order: faults, capture, the live reseed
  std::vector<std::shared_ptr<ValueSource>> advance;   // the tap-only tails, in Graph::nodes order
  struct DynamicPin { std::shared_ptr<Component> node; std::string setting; std::shared_ptr<ValueSource> source; };
  std::vector<DynamicPin> dynamicPins;            // a setting and the signal evaluated into it once per note
  std::vector<std::string> warnings;              // what the host prints; the builder prints nothing
};

// patch/build_graph.h
BuiltGraph build_graph(const Graph& graph, const NoteSignals* note, const InstrumentBlock* instrument);
```

- **`nodes`** is today's `GraphResult::nodes`. **`signals`** is what
  `adopt_nodes` derives in the voice today (`VoiceGraph::Signal`, the
  same two fields), derived once here. **`advance`** is today's
  `collect_advance_ids` applied to the record instead of the JSON: a node
  with a tap consumer, no other consumer, and not the output, in file
  order. **`dynamicPins`** is what `bind_wiring` turns into
  `PushBinding`s; the builder resolves the setting and the signal, and
  `render` makes the binding (5.9). **`warnings`** replaces the loader's
  eleven `fprintf(stderr)` paths with text the host prints where it
  prints things — the editor's status line, the command-line tool's
  stderr — so the module does no I/O.
- **A null `note`** is the plain-sound build: a `PerformNode` is refused
  by name ("no note to read"), as today's "no voice context". **A null
  `instrument`** gives a `NameGate` no vocabulary: its name resolves to
  nothing and the build warns, as today's non-instrument path.
- **Failure** is one `std::runtime_error` naming node, pin and reason,
  thrown at build — control-side work, outside Part 2 §6's no-throw rule
  for render paths, as the codec's (4.1).
- **Nothing from `render` or `music`.** `MonoSource` is `render`'s, so
  the mono adapters leave the graph build. Today `add_mono` wraps every
  signal node of every build in one — voice builds included, which never
  read them — and seven of the eleven branches skip it, so a plain sound
  whose channel source is an Envelope, a Curve, a Var, a Range, a
  Vibrato or a NameGate fails with "No mono source". Both go: the
  plain-sound build wraps exactly the channel sources (5.8).

### 5.2 The order of work in a build (proposed)

Five steps, each a function; today's `build_graph` is one function of
348 lines against the meter's budget of 80.

1. **Sort.** A topological order over every feed that is not a tap —
   pin feeds, slot feeds and dynamic-pin feeds alike. A cycle left when
   the sort ends is refused naming its nodes (3.4, decided). The sort
   decides construction order and nothing else: `signals` is by id,
   `advance` and every other order-bearing list keep `Graph::nodes`
   order, so no render can see it. Today file order is build order and
   0 of 3,146 files break the feeds-first rule, so today's order and the
   sort agree on every file that loads.
2. **Create and fill.** For each node in that order:
   `SourceRegistry::create(type, seed)`, the three named concepts of 5.4
   where the type is one of them, then the record's six maps through
   the by-name doors — `values` into a Param's own constant or a
   `ConstantSource` wired into an Input (5.3); `wires` through
   `set_param` and `add_param` (a feed names a built node; a tap becomes
   a `TapSource` placeholder for step 3); `settings` through
   `set_setting`, an enum as its index; `strings` through `set_string`,
   the new door (5.10); `arrays` through `set_array`, an enum column as
   indices; `dynamicPins` kept for step 4. A door that answers false
   fails the build naming node, pin and the part's kind — today's
   `reject_wire`, the one place a wire is type-checked (Part 2 §6).
3. **Bind taps.** Every placeholder gets its target, which must be a
   signal — today's second pass, unchanged.
4. **Resolve dynamic pins.** The setting must be declared and not
   structural, the source a signal. The codec checked the first two at
   read (4.6); the builder checks again because a `Patch` edited in
   memory never passed the codec.
5. **Finish.** Each node's `validate()` (5.6); the advance tails; the
   output, which must be a signal or the mixer part (5.8); the signal
   list.

### 5.3 Constants: a Param's own, an Input's wired (proposed; 5.12 Q1)

Today every number in a file becomes a `ConstantSource` wired into the
pin (`resolve_param`), including on a Param that has a constant of its
own. Since the editor writes every pin, every editor-saved patch wires
every unwired Param to a constant, and each costs one virtual call per
sample in the base's sweep, which pulls every wired Auto pin. In scope
2,429 values under `params` are numbers; 736 of them sit on Params and
33 on Inputs, the rest on settings; across the tree 30,656 and 1,331. Part 2 §3 defined a Param as "a source, or a
constant when nothing is wired"; the loader predates the definition.

Proposed: **a Param's number is the pin's constant** — `Param::constant`,
nothing wired, nothing in the sweep. **An Input's number is a
`ConstantSource` wired in**, because an Input has no constant and reads 0
unwired; the form is real and legitimate — all 33 in scope and 1,330 of
the 1,331 in the tree are a `CombinedSource`'s `source1` or `source2`, a
signal multiplied or added with a constant (the other is one
`BowedStringEvolution.bow`). 3.2's constant line, "a param pin's number
while nothing feeds it", gains the Input clause (Q1).

Byte-identity. `Param::current()`, `next()` and `step()` return the
constant when nothing is wired — the same float the wired
`ConstantSource` returns, with no arithmetic between. What could differ
is a node that *tests* whether a Param is wired. The grep over every
node finds one: `Envelope::compute` maps through `minValue` and
`maxValue` only when either has a source, else returns the raw 0..1
value. (The other two tests, `KSString`'s `bow` and `NameGate`'s `in`,
are Inputs, which keep their `ConstantSource`s and behave as today;
`DelayLine`'s test is on a tap.) The Envelope maps always: with the
declared defaults 0 and 1, `lo + (hi - lo) * raw` is `raw` exactly in
IEEE arithmetic (`1.0f * raw` is `raw`; `0.0f + raw` is `raw`), so the
envelopes without the keys render as before, and the 38 in scope that
carry them (29 stage-form, 9 preset-form) render as before because they
are mapped either way. The A/B proves it over the 132. Two side effects,
both by design: the sweep shrinks by the constants, which is time and
not samples; and `ensure_constant`, which gives a dynamic pin on an
unwired Param a `ConstantSource` to write into, now makes it from the
pin's constant holding the file's number, where today the number sits in
the wired constant it finds. (Annotated 2026-11-09, 9.3: the second
side effect never arises — a dynamic pin is a setting, and
`ensure_constant`'s only callers, the paramMap's push and the Multiplex
clone fan-out, are deleted at 4.2, the function with them.)

### 5.4 Three names the builder knows (proposed; 5.12 Q5)

Everything else in 5.7 is a declaration. Three types stay named in the
builder, one per concept no declaration expresses:

- **`PerformNode` — a stand-in.** Its id resolves to the `NoteSignals`
  member its `field` setting names (frequency, velocity, wheel, pressure,
  duration, onset; 73 in scope: 67 frequency, 6 velocity). No object is
  created: several `PerformNode`s of one field are the same adapter, the
  pin model's "all instances render the same outputs", as today. It
  becomes a registered type so the codec and the editor have its
  declaration (3.3): a `Generator` with one enum setting and a `compute`
  that returns 0 and is never reached. Family Modulators, label "Note
  Field" — a menu placement, decided here under the 11-02 rule and
  logged in CLEANUP at the checkpoint; the editor's Note face keeps
  creating them per field (Part 7 may replace the device).
- **`NameGate` — a vocabulary.** `strings["name"]` resolves to the
  onset id against the block's `onsets`, 1-based; a name outside the
  vocabulary is a warning, as today's stderr line. Inside a Multiplex
  template the block now reaches the clones (5.5); today the clone build
  passes null and such a gate never fires. 0 in scope.
- **`MultiplexSource` — a fan-out** (5.5).

### 5.5 Multiplex: the template is a Graph (proposed; 5.12 Q6)

- **The template** is the sub-`Graph` reachable from the `source` feed's
  node over every wire kind — pin feeds, slot feeds, taps and dynamic
  pins — where today `extract_subgraph_json` follows `ref` alone, so a
  tap-closed loop inside a template is cut and fails at clone build with
  "Unresolved tap target". The builder gives the node an
  `InstanceBuilder` closure holding the sub-Graph, the `NoteSignals` and
  the onsets *by value*, for the loader's own reason: `rebuild_` runs
  from `prepare`, and `mark_dirty` can run it again later, so nothing in
  the closure may point into the caller. Clone *i* builds the sub-Graph
  through `build_graph` with every node's seed XOR-ed by `base ^ (i ·
  0x9E3779B9)` and that value injected where a node has none, `base` the
  root node's seed or 0 — today's rule to the bit. `set_template` takes
  the closure alone; `templateJson_` and `baseSeed_` go.
- **A dynamic pin inside a template is refused by name.** Today
  `bind_wiring` runs over the top-level graph only, so a dynamic pin on
  a template node is silently ignored. Honouring it would mean per-clone
  push bindings, the machinery the next bullet deletes. 0 in scope.
- **The clone fan-out goes with the paramMap.** `clone_param_slot`,
  `set_clone_param`, `CloneParam`, `PushBinding::cs`, `cloneSlot` and
  `targetNodeId`, and `VoiceGraph::topMultiplex` exist for one case of
  `build_bindings`' matrix: a paramMap entry on a Multiplex voice, pushed
  wholesale into every clone. `MPXTest3` is the one Multiplex in scope
  (fifty clones of a four-node Triangle template, `"frequency":
  "Var1.val"`). Converted, the `PerformNode` sits inside the template and
  each clone's copy reads the voice's frequency live, which is the value
  the push delivered once per note; the wiring null gate's manifest
  already holds its hash. Section 9 lists the deletions.
- **The editor** never calls `set_template` or `mark_dirty` (grep): its
  own graph plays the Multiplex's solo preview, the wired `source`
  passed through. After Part 7 it hears the fan-out through
  `build_instrument`. `mark_dirty` has one caller, an engine test; it
  stays as the node's dirty signal for Part 7's live template edit.

### 5.6 A node's own invariants (proposed; 5.12 Q7)

Two kinds of rule the declarations cannot carry remain after section 4:
a relation between two of a node's arrays, and a mode that makes a pin
inert. Today both sit in `wire_params_generic` behind a
`dynamic_cast<ShaperSource*>`.

- **`virtual std::string Component::validate() const`** — empty, or the
  first problem in words. The builder calls it after a node is filled
  and fails the build naming the node and the problem. The Shaper's
  three refusals move there: `values2` must match `values` in length
  (4 nodes in scope carry `values2`), `segs2` must match `segs` in
  length and in type column except the Expo/InverseExpo pair (2 carry
  `segs2`). A ragged *table* stays the codec's (4.4); these are
  relations between a table and a curve that may be absent.
- **The Shaper's two advisories become warnings on value, not on key.**
  Today "morph is ignored while hysteresis is on" fires when the `morph`
  *key* is present, and the editor writes `"morph": 0.0` on every Shaper
  it saves — four of the nine in scope carry it (bassoon1, oboe1, oboe2,
  `loop_hysteresis_bow`), and the last has hysteresis on, so the advisory
  names a knob nobody turned. The warning fires when `morph` is wired or non-zero under
  hysteresis, and "hysteresis without values2" when that is so; both
  return in `BuiltGraph::warnings`. 0 in scope trigger either.
- The `FormantPeaks` configurator's ragged check is the codec's table
  rule; the `FormantSequence` configurator's refusal of `formants` is the
  codec's unknown-key rule. Neither needs `validate`.

### 5.7 Where the eleven branches and twelve configurators go (proposed)

Every row's declared defaults were read against the branch's (5.3's
argument applies to each): the two agree except where the row says.

| Today | Becomes | Node change | In scope |
|---|---|---|---|
| `VarSource` branch: `val`, `var`, `varPct` as constructor arguments, `absolute` a bool | the pins and the setting are declared already; the factory default-constructs. The branch's `val` default is 0.0 against the declared 1.0: 0 of the 6 nodes lack `val` | the factory's argument list goes | 6 |
| `RangeSource` branch: `min`, `max`, `var`, `normalized` | declared already. `normalized`: the branch says false, the factory true, the declaration false (2.1's three defaults) — the declaration stands, which is what every file loads with today; the editor's new node follows it (false) | the factory's argument list goes | 12 |
| `Envelope` branch: the stage list read by hand, the preset form through `envelope_json.h`, the two accuracies read with a clamp before the generic pass reads them again (CLEANUP 29), `timeMode`, the seed | the stages are eight columns with `type` an enum column (3.3); the preset form is the codec's (4.5); the accuracies are read once by the settings pass; `timeMode` is an enum setting (fraction, seconds) that sets `absolute_time`; the seed goes to the factory; the mapping is unconditional (5.3) | declares the columns and `timeMode`; drops the wired-ness branch; the hand-read stage loop goes | 174 (64 stage-form, 110 preset) |
| `CurveNode` branch: `knots`, `exprKnots`, `interp`, `mode` into public members | `knots` a table of `x`, `y`; `exprKnots` a table of `x`, `form` (enum column: linear, power), `a`, `b`; `interp` an enum setting (linear, logx, loglog); `mode` an enum setting (points, expressions) — the branch's defaults: 45 nodes have no `mode` and read as points, 1 has no `interp` and reads as linear | the members become pins; the knot vector is rebuilt in `on_array_changed` | 75 |
| `PerformNode` branch | 5.4 | a registered type | 73 |
| `NameGate` branch: `name`, the vocabulary | 5.4; `name` a string setting | declares `name` | 0 |
| `SegmentSource` branch: `values`, `oneShot`, the seed as constructor arguments | all three declared already (an Array, a bool setting, the factory's seed); the constructor and `on_array_changed` both end in `refresh_units_()` | the factory default-constructs | 8 |
| `Vibrato` branch: seven settings set by hand with the branch's defaults | the declared defaults are the branch's exactly — speed 5, depth 0.02, attack 0.3, threshold 0, speedVar 0, depthVar 0, zeroCrossTendency 1 — so the settings pass does the same work | none | 11, all carrying all seven |
| `BWLowpassFilter` / `BWHighpassFilter` / `BWBandpassFilter` branch: `sections` a constructor argument, `cutoff` an alias | `sections` a structural int setting, default 2, applied before the prepare that builds the sections; `cutoff` renamed on disk (4.8) | declares `sections`; the three `cutoff` configurators go | 17; `sections` on 1 node, `cutoff` on 1 |
| `HybridKSSource` branch: `targetPartials` | an array | declares it | 1 |
| `MultiplexSource` branch | 5.5 | `set_template(InstanceBuilder)` | 1 |
| `FormantPeaks` configurator (ragged rows) | the codec's table rule | the configurator goes | 68 |
| `FormantSequence` configurator (`formants` refused) | the codec's unknown-key rule | goes | 3 |
| `BWLowpassFilter`, `BWHighpassFilter`, `SVFSource` configurators (`cutoff`) | the converter's rename | go | 1 file |
| `PhasedValueSource` configurator: `overlap`, `stages` of a source ref and four numbers | a `sources` multi input, a table of `percent`, `min`, `max`, `gain`, `overlap` a setting (3.3) | declares them; `add_stage` goes | 1 |
| `RepeatingSource` configurator: `duration`, `durVarPct`, `gap`, `gapVarPct` | all four are settings already, under the setting names; `gap` is `gapDuration`'s alias and the converter renames it | none | 1 |
| `CombinedSource` configurator: `operation` as an ordinal or a string with aliases, `gainAdj` | `operation` is an enum setting whose labels are Mix, Multiply, Fade, Sum: the 107 ordinals read as indices, the 12 strings (`sum`, `multiply`) match labels case-insensitively; `add`, the configurator's alias for Mix, is in 9 files tree-wide and 0 in scope, and the converter writes the label; `gainAdj` is a setting already | none | 119 |
| `CrossfadeSource` configurator: `ratio`, `overlap`, `gainAdj` | settings already, the configurator's defaults the declared ones | none | 0 |
| `MultiSource` configurator: per-source `weight` and `delay` on the feed objects | `weights` and `delays` arrays parallel to `source` (3.2) | declares the two arrays | 3 files, 9 weights |
| `StaticVarSource`, `StaticRangeSource` configurators: three plain members each | settings | declare them | 0 |
| the `expandRule` block in `wire_params_generic`: an inline object, or a ref whose unresolved or wrong-kind target is silently ignored (3.1 ruling 11) | `Partials` declares `Slot<ExpandRuleNode> expandRule` and reads `to_struct()` on `on_slot_changed`; the inline form is the converter's (73 tree-wide) | the slot | 0 |
| the Shaper block in `wire_params_generic` | `validate()` and two value-based warnings (5.6) | the hook | 9 |
| `reject_unknown_keys`, `refuse_string_pin` | the codec (4.6) | — | — |
| the two "bend inert" advisories (`tracks_frequency_live`; `bind_wiring`'s reachability walk) | one walk over the record — does a frequency stand-in reach this node's `frequency` pin while the node reads it once — into `warnings` | — | — |

Every "node change" lands under today's loader first, byte-identical,
so the builder arrives to nodes that already declare everything it
reads — 3.9's lesson ("the editor before the engine"), ordered in
section 8.

### 5.8 The plain-sound form (proposed; 5.12 Q3, Q4)

- **Two registered parts.** `SoundChannel` — an Input `source`, Params
  `volume` (1) and `pan` (0) — and `StereoMixer` — a multi
  `Slot<SoundChannel> channels`, Params `gainL` and `gainR` (1) — are
  `Component`s in `nodes`, not `DrivenPart`s: they compute nothing, and
  the plain-sound build reads their pins. They sit in a new family,
  **Output** (`source/output/`, one row in Part 3's table, one colour in
  the editor's theme), because they are neither utilities nor
  processors; the codec and the editor then know them like any type
  (4.2). 27 files in scope end in one, 1,049 in the tree.
- **3.4 amended by a clause (Q4):** the output names a signal, or the
  mixer part. Today the plain-sound path requires the output to be the
  `StereoMixer` node or a mono-renderable signal; the instrument path a
  signal.
- **`render`'s live mixer is renamed `Mixer`**, its `Channel` kept, so the
  registered type keeps the name the files and the menu use. Five host
  files name the live struct; the rename is mechanical.
- **`build_sound(const Patch&) → BuiltSound { int sampleRate; int frames;
  std::unique_ptr<Mixer> mixer; }`** in `render/build_sound.h` — today's
  `load_patch_file`, its result today's built struct named `Patch`,
  which the first checkpoint renames so the name is free (2.2). Three
  forms, as today: the mixer form, a `Mixer` assembled from the part —
  each channel's `source` signal wrapped in one mono adapter (today's
  `WaveSourceMono` and `ValueSourceMono` are identical but for the
  pointer type; one remains), the channels' and the mixer's constants
  made into `ConstantSource`s on `render`'s side because the live mixer
  pulls signals; the bare-output form, one unity channel around the
  signal; and the instrument form, `build_instrument` then the embedded
  score (5.9) then one unity channel, with **no `mix` lookup**: today
  that path builds the whole graph a second time to read `gainL` and
  `gainR` off a node named `mix`, which in the three in-scope files (the
  electric pianos; seven tree-wide) is a `MultiSource` with no gains, so
  both read their defaults. `frames` is `seconds` at the rate, five
  seconds when absent, and for an instrument the larger of that and the
  score's end (the 2026-09-16 rule). *(Corrected 2026-11-09, checkpoint
  4.1's review: the last clause is true only when the file states
  `seconds`. With no `seconds` an instrument's frame count is the score's
  end alone, not the larger of five seconds and the end — 69 of the 96
  in-scope files with a score have no `seconds` — and with neither a
  score nor `seconds` it is five seconds. `engine_tests` pins all three
  cases since 4.1.)*
- **Faults by id.** The `Mixer` keeps the build's `signals` and reports
  through them, which is the "until Part 4 gives it ids" in `mixer.h`.
- **Cross-loop sharing is refused by name**: a node reachable from two
  channels' sources, or from a channel and the mixer's own loop (its
  gains, the channels' volumes and pans), would be ticked by two clocks
  and compute twice per sample (2.3 rulings 8, D1, D2 left it
  unsupported). In scope 0 of the 27 share a node and 0 wire a gain, a
  volume or a pan — all 108 are constants. A tap-only tail in a plain
  sound stays a warning, as today: no voice ticks it, so the loop is
  dead. 0 in scope.

### 5.9 `build_instrument` and the embedded score (proposed; 5.12 Q8)

```cpp
// render/build_instrument.h
std::unique_ptr<PitchedInstrument> build_instrument(const Patch& patch, int minPolyphony = 0);
// render/embedded_score.h — until Part 5's play function; today's perform_score_json
double play_embedded_score(PitchedInstrument& inst, const std::string& scoreText);   // the score's end, seconds
```

- The block is required; polyphony is the larger of the block's and
  the floor (the live keyboard's 88-key need, as today). `volume`,
  `onsets`, `glideMs` and `sustaining` come from the block.
- **One voice loop** — today's is written twice, the meter's largest
  duplicate at 62 lines (2.5 §7). Per voice: the `PerformSource` and six
  `PerformOut`s, handed to `build_graph` as `NoteSignals`; the output,
  which must be a signal ("the output is a part" fails the build); the
  `signals`, `advance` and `nodes` taken into the voice; the envelope
  lists derived from `nodes` as today (`allEnvelopes`, the trigger
  bindings); each `DynamicPin` made a `PushBinding` with `isSetting`,
  the chain its source. Then `prepare` at `patch.sampleRate`. A host
  that wants another rate sets the field on its copy of the `Patch`
  first — data, not an argument.
- **The embedded score** is `play_embedded_score`: `perform_score_json`
  moved whole — the `NotePerformer` at 60 bpm, the v2 `hold` and `onset`
  keys, articulation and ornament on unphrased notes, the "hold ignored,
  not sustaining" warning, the open line finished. It returns the
  score's end for `build_sound`'s frame count. Three of the loader's five
  `music` includes move with it into this one file (`basics`, `conductor`,
  `music_json`), where Part 5 replaces them with the play function
  (target 3.11; 2.5 decided it stays a function of its own in `render`
  until then); the other two, `pitch_bend` and `pitch_curve`, are dead
  includes and go. `build_instrument` never performs a note.
- `hiBoost` is a field the command-line tool sets; it goes when the tool
  moves (section 7). `DrumKit` is assembled by hand by its hosts until
  Part 5.

### 5.10 What the engine gains and loses for this (proposed)

- **core**: `SettingType::String` and a string pin (`PinKind::String`,
  held in `setting_descriptors()` with the new type) with `set_string` /
  `get_string` doors; `ArrayDescriptor::enum_labels` and the `Array` pin's
  labels; `Component::validate()`; `Family::Output` (`kFamilyCount` 11).
- **registry**: `register_type(name, tag, factory)` — `JsonConfigurator`,
  `ResolveParamFn` and `get_configurator` deleted, the header without
  `json.hpp` (the target's sentence); `register_all_sources()` called once
  by whichever first asks the registry, the codec's declarations (4.2) or
  the builder.
- **nodes**: the rows of 5.7 marked as node changes; the two parts of
  5.8; `PerformNode`.
- **render**: `build_instrument`, `build_sound`, `play_embedded_score`,
  `Mixer`; the voice gains nothing it does not have.
- **gone with the loader**: the fourteen `dynamic_cast` sites become
  four — a built node asked whether it is a signal (the output, a tap's
  target, the signal list, a refusal's wording) and the voice's envelope
  lists; the other ten pick a branch or a form by type and go with it; the
  `thread_local` tap collector (the placeholders ride in a local of the
  build); `extract_subgraph_json` and `build_subgraph_with_seed_perturbation`
  (the sub-Graph and a recursive `build_graph`); `build_bindings` and
  `attach_perform_source` (the paramMap); the `mix` lookup; the second
  voice loop.

### 5.11 For the C# reader

- **A closure that outlives its maker.** C# lambdas capture variables,
  and the garbage collector keeps whatever they reach alive. A C++
  lambda captures what you tell it: `[&]` holds references into the
  enclosing frame, which is a dangling pointer the moment that frame
  returns, and `[=]` or a named copy holds values. The loader copies its
  `PerformContext` into the Multiplex builder for exactly this reason,
  and 5.5 keeps the rule: a closure that runs later holds copies, and
  the `shared_ptr`s inside the copies keep the adapters alive.
- **`std::optional<uint32_t>`** is a nullable value type (`uint?` in
  C#): a seed that may be absent without a sentinel like 0, which is a
  legal seed.
- **A prototype instead of reflection.** C# would read attributes off a
  type's properties. Here a type's declarations exist only on an
  instance, because each pin registers itself with its owner in its
  constructor (Part 2 §3), so "what does this type declare" is answered
  by making one and asking it — the prototype table of 4.2, made once
  and cached. That is the project's rule, explicit registries over
  reflection-like designs, applied to the one question reflection would
  answer.

### 5.12 The eight questions (decided 2026-11-08, each as written)

1. **A Param's number is the pin's constant and an Input's is a wired
   `ConstantSource`** (5.3); the Envelope maps unconditionally; 3.2's
   constant line gains the Input clause. The alternative keeps today's
   rule, a `ConstantSource` for every number, and the sweep pays for it.
2. **`BuiltGraph` carries the six members of 5.1** — nodes, output,
   signals, advance tails, dynamic pins, warnings — and the builder
   prints nothing. The alternative returns nodes and output alone and
   lets `render` derive the rest, as the voice loops do today.
3. **`SoundChannel` and `StereoMixer` are registered parts in a new
   `Output` family; `render`'s live mixer becomes `Mixer`; `build_sound`
   covers the three forms without the `mix` lookup; cross-loop sharing
   is refused by name** (5.8). The alternative keeps the two as the
   builder's special case, as today.
4. **3.4 amended**: the output names a signal, or the mixer part.
5. **Three named types, and only three** — `PerformNode` as a stand-in
   resolved to a `NoteSignals` member with no object built, `NameGate`'s
   vocabulary, Multiplex's fan-out (5.4). The alternative gives
   `PerformNode` a live object that reads the stand-in, one virtual call
   per sample per field.
6. **Multiplex**: the template is the sub-Graph over every wire kind; a
   dynamic pin inside a template is refused; the clone fan-out goes with
   the paramMap (5.5).
7. **`Component::validate()`** for a node's own invariants, the Shaper's
   advisories on value rather than key (5.6). The alternative keeps a
   type test in the builder.
8. **`play_embedded_score` in `render`**, called by `build_sound` for an
   instrument patch and never by `build_instrument` (5.9), with the three
   `music` includes it needs in that one file until Part 5.

## 6. The edit operations

Status: **decided with Matt, 2026-11-08, as proposed** ("All others
stand", after the `Fragment` record was explained and kept: "Absolutely
necessary. Name fine."): the eight questions of 6.12 all resolved the way
they are written; 3.8 amended by a clause under Q1 and 4.1 under Q2.

Today every edit lives in the editor's file as a function over its
`GraphNode` vector, its link list and its group list, with the engine
objects rewired in the same breath: `delete_node`, `delete_link`,
`rename_node`, `rename_group`, `replace_node_with`, `group_selection`,
`ungroup`, two clipboards (`clip_node_state` and its paste; the text
fragment `copy_selection_to_clipboard` / `paste_fragment_text`), the
Duplicate menu block, the two mode conversions and their stash,
`rewrite_label_refs`, `group_boundary` and `group_output_node`,
`sanitize_unique_id` and `unique_node_label`, `apply_envelope_preset` —
some 1,070 lines, reachable without a window only through six of the
fourteen headless modes (`--rename`, `--replace`, `--paste-frag`,
`--apply-preset`, `--convert-roundtrip`, `--dump-boundary`). The editor
has no undo; one Revert covers a preset insertion. The target makes the
edits methods of `Patch` (2026-10-05) and leaves the editor a view over
an `OpenPatch` (Part 7).

### 6.1 The shape of an operation (proposed; 6.12 Q1)

- **A method of `Patch` that returns its refusal as a value**: a
  `std::string` that is empty on success and names the reason otherwise
  ("`Reverb.source` already feeds from `Damp_lpf`"; "selection has two
  outputs: `Body`, `Tine`"), or a small result struct carrying the same
  string plus what the operation made or dropped (`Pasted{ids, dropped}`,
  `Replaced{kept, dropped}`). A refusal is an expected answer to an
  interactive edit, not an error, so no operation throws; the codec and
  the builder throw because a bad file or a bad graph is not expected.
- **Every operation leaves every invariant of 3.8 as it found it.** The
  check is `Patch::check() → std::vector<std::string>`: the 3.8 list as
  code, each violation named with its path. The codec calls it after a
  read (4.6's refusals are its registry half), the tests call it after
  every operation, and a host may call it before a build.
- **The registry is in the room.** The operations that need a type's
  declarations — add, connect, replace, set_setting, the table rows, the
  conversions — read them from the prototype table of 4.2; the ones
  that do not (rename, remove, group, move) read nothing.
- **3.8 amended by a clause (Q1):** `output` names a node *or is empty
  while a patch is being assembled*; the builder refuses an empty one by
  name ("the patch has no output"). A new `Patch{}` is valid and silent
  until a node is made its output. Today the editor's New plants a Sine,
  an Envelope and an Output node so the invariant holds from the first
  frame; that is the view's choice, not the model's.

### 6.2 Nodes (proposed)

- **`add(type, wantedId = "") → id`.** The type must be registered. The
  record is filled from the declarations — every Param's default into
  `values`, every setting's default into `settings`, every array's into
  `arrays` — which is the same state the codec's "model complete" gives
  a sparse file (4.7), so a node made in the editor and a node read from
  a file are indistinguishable. A seeded type draws its seed here: random
  at creation, stored in the file (the standing rule). **The id** is the
  wanted one, made unique by the numeric suffix the editor uses today
  (`Sine`, `Sine2`, `Sine3`); when none is wanted, the base is the type
  name with one trailing `Source`, `Filter` or `Node` dropped — the
  registry's `derived_label` without its spaces (`Sine`, `WhiteNoise`,
  `BWLowpass`, `Curve`, `Multiplex`). Any non-empty string is a legal id
  (3.4); the editor's two refusals, a `.` and a `__` prefix, were for the
  paramMap's `node.pin` targets and for the synthesized ids, and belong
  to the view if anywhere. Nothing is wired: a new node reads its
  constants.
- **`remove(id)`.** The node goes; every feed from it is dropped, so a
  consumer's Param returns to its declared default constant, an Input to
  nothing and a slot to empty; every dynamic pin it fed is demoted (6.3);
  its position, its group membership and any face field naming it go
  with it (3.6); if it was the output, the output is empty (6.1). Today
  `delete_node` does the same except that a dynamic pin it fed is left
  "promoted, unwired" as a `null` entry — a state no file in scope or in
  the tree holds (0 `null` entries over 179 dynamic-pin nodes).
- **`rename(id, newId)`.** Non-empty, and unique in the one namespace
  node ids, group names and face labels share — they must, because a
  group's member list holds node ids *and* group names and has to
  resolve each one way (the groups spec, 2026-08-14 §1 and §3). One
  path re-keys everything that names the node: feeds and taps on every
  node, dynamic-pin sources, the output, the layout position, group
  memberships, face fields. Today `rewrite_label_refs` covers extras,
  dynamic pins and groups, the paramMap residue, and relies on positions
  regenerating from labels at save; the 2026-08-19 stale-ref bug that
  stopped a saved patch rendering was a missed site of exactly this
  kind, and one path is the cure.
- **`replace(id, newType) → Replaced{kept, dropped}`.** A new node of
  the new type under the *same id*, at the same position, in the same
  group and faces, so nothing that names it moves (today's function
  makes a fresh label and renames the old identity onto it; the end
  state is the same). State is carried by name: a same-named Param's
  constant or feed, Input's constant or feed, slot's part, setting,
  string, array and dynamic pin. Feeds into pins the new type lacks are
  dropped and named; feeds *from* the node into consumers that would
  refuse its new kind (a part where a signal pin stood) are dropped and
  named too — today's replace keeps every outgoing wire and lets the
  builder find out. The motivating swap stands: WhiteNoise → RedNoise
  inside a group, the boundary untouched (Matt, 2026-09-06).
- **`duplicate(id, offset) → id`** is `paste` of a one-node fragment
  (6.5), so a duplicate keeps the feeds into it (its sources exist in
  the same patch) and lands at an offset. Today's Duplicate copies pin
  values and settings only — not arrays, not wires — which is the gap a
  single rule closes.
- **`set_seed(id, seed)`** and **`reseed(id)`** (a fresh random seed) —
  not user-facing (the standing rule), but a tool's and a test's.

### 6.3 Pins (proposed; 6.12 Q4, Q5)

- **`set_value(id, pin, v)`** sets a Param's or an Input's constant. A
  fed pin refuses ("`cutoffFreq` feeds from `Damp_curve`; disconnect it
  first"): the editor hides the widget of a fed pin today, which is the
  same rule drawn. A Param's declared range is enforced at render by the
  clamp-and-fault rule (Part 2 §6), not here: a constant outside it is
  legal to write and reports a fault when it plays.
- **`connect(src, dst, pin, tap = false)`.** The pin must be declared on
  `dst`'s type; `src` must be a signal for a Param or an Input and a part
  of the accepted kind for a slot; a tap goes into a Param or an Input
  only (4.4); a single pin's feed is replaced, a multi input's appended;
  the pin's constant, if any, is erased (3.2's "a constant or feeds,
  never both"); a non-tap feed that would close a cycle is refused naming
  the cycle (3.4). **Connectability is the door's answer**, asked once:
  the prototype table of 4.2 records, for each type, whether it is a
  signal or a part and, for each slot of each type, which part types its
  door accepts — found by offering each part prototype to each slot
  prototype at table-build time and clearing it, so the edit layer never
  guesses a kind that the build would refuse (Part 2 §6: one check, one
  place; here it is read ahead of time).
- **`disconnect(dst, pin, src = "")`** (the one feed of a single pin; a
  named feed of a multi input). A Param returns to its *declared default*
  constant, an Input to nothing, a slot to empty. The editor today
  returns a pin to the number it showed before the wire, which it
  remembers for the session only — a file never held it — and may keep
  doing so on top of this call (Q5).
- **`connect_setting(src, dst, setting)`** makes a dynamic pin: the
  setting must be declared and not structural (3.8), `src` a signal. This
  *is* promotion; there is no promoted-but-unfed state in the model (Q5).
  The pin model's settled floor — a Curve and nothing else may feed one
  (§9, Matt) — stays where it was put: the editor offers Curves, the
  model and the loader are permissive.
- **`disconnect_setting(dst, setting)`** is demotion: the setting keeps
  the value it has (§9 ruling 2: the stowed scalar, not the last chain
  value — which the model gives for free, because the value never left
  `settings`).
- **`set_setting(id, name, v)`** takes a number, a bool, or an enum's
  index or label; a value outside the declared range is refused (4.6 Q4
  made that the codec's rule; the editor clamps its widget before
  calling). **`set_string(id, name, s)`** for the string kind.
- **Arrays and tables.** `set_array(id, name, values)` for a lone array
  at any length; for a table group `add_row(id, group, at)` fills every
  column's new cell with its declared default and `remove_row(id, group,
  i)` takes the row from every column, so the group's one length (3.8)
  holds by construction; a direct `set_array` on a grouped column is
  refused unless the length matches its siblings.
- **`apply_preset(id, preset, knobs)`** on an Envelope node lays its
  stage columns out through the table of 4.5 — the same function the
  codec reads the preset form with, so the editor's Preset combo, the
  file and `--apply-preset` (an engine test afterwards) cannot drift.

### 6.4 The output, the block and the fields (proposed)

- **`set_output(id)`**: a signal type, or the mixer part (3.4 as amended
  in 5.8); any other part refused; `""` clears it (6.1).
- **`set_instrument(InstrumentBlock)`**: polyphony at least 1, glide not
  negative, onset names non-empty and distinct (an onset's id is its
  position, so a duplicate name would be unreachable). **`clear_instrument()`**
  is `make_plain_sound` (6.6), never a bare field clear, because the
  block's absence changes what the graph may hold.
- **`set_sample_rate`, `set_seconds`, `set_comment`** on the patch;
  `set_comment(id, …)` on a node.
- **`set_score(text)`** stores the passenger (3.5). The module never
  reads it, but it checks that the text parses as JSON so the file the
  codec writes stays a file; the content is the performer's business.
- **`move(id, pos)`, `move_output(pos)`, `set_panning(pos)`** on the
  layout; the view calls them on drag end, not per frame.

### 6.5 Fragments: copy, paste, duplicate (proposed; 6.12 Q2, Q3)

- **A `Fragment` is its own record**: `{ std::vector<Node> nodes;
  std::map<std::string, Vec2> positions; std::vector<Group> groups;
  std::vector<NoteFace> faces; }` with one more `to_json` / `from_json`
  pair in the codec. It is not a `Patch` because its feeds may name
  nodes it does not hold — that is the point of a fragment — and
  `Patch::check()` would rightly refuse it. 4.1's "a fragment is a
  `Graph`" becomes this (amended under Q2); the clipboard carries its
  JSON text, which is
  what rides the OS clipboard today, so a paste between two editor
  instances keeps working and the `mforceClip` marker is replaced by the
  record's own shape.
- **`copy(ids) → Fragment`**: the named nodes with every wire among them
  and every feed from outside left as it is, their positions, the groups
  whose members all lie in the selection (renamed members follow), and
  the faces whose fields all do. Today the text clipboard excludes Note
  faces, the Output, the mixer, the channel, Parameter nodes and groups
  ("v2"); with faces a record, groups a record and the two terminal types
  ordinary nodes, nothing is excluded.
- **`paste(const Fragment&, offset, intoGroup = "") → Pasted{ids,
  dropped}`**: every incoming id made unique by the suffix rule; feeds
  among the fragment follow the renames; a feed naming a node the patch
  already has keeps it — reconnect by name, which is how a copied voice
  finds `__perf_freq` again — and a feed naming nothing is dropped and
  counted; positions land at their relative places plus the offset
  (today 60 for a text paste, 40 per repeat for the internal one; the
  caller passes it); the fragment's groups and faces arrive renamed; the
  pasted nodes join `intoGroup` when given (the editor passes the group
  it is drilled into). The fragment's nodes are added by `add`'s rule,
  so a pasted node is a complete record.
- **Seeds (Q3).** A pasted or duplicated node *draws a fresh seed*:
  seeds are random at creation and a paste is a creation, and two nodes
  with one seed are one noise doubled, which no one asks of a copy.
  Today both clipboards keep the seed. Opening a file is not a paste and
  keeps every seed.

### 6.6 make-instrument and make-plain-sound (proposed; 6.12 Q6)

The target's two operations, replacing the editor's mode conversion
pair, its stash and `tools/add_instrument_block.py`.

- **`make_instrument() → std::string`**, on a plain sound. The output
  must be the mixer part with at least one channel whose `source` is fed
  (else refused by name). The first channel's source becomes the output;
  the mixer and channel nodes are removed with their positions; the block
  is set to `InstrumentBlock{}` — polyphony 4, the record's default, where
  today's conversion and the standalone tool write 1 (the live keyboard
  applies its own floor either way). **The pitch rule**: the first node in `Graph::nodes` order with a declared
  `frequency` Param that nothing feeds gets a `PerformNode` of field
  frequency wired into it, under the converter's id `__perf_freq`; with
  no such node the block is set and the result string says the
  instrument plays one pitch. That is the editor's rule today ("first
  node owning an unconnected numeric `frequency`"); the standalone tool
  prefers the ids `fas`, `src`, `osc`, `source` first, a convention of
  the 2026-08 migration that the 27 in-scope plain sounds do not use
  (every channel is `ch1`, every mixer `mix`). `seconds` stays (3.5). No
  score is written: BACKLOG row 10 decides where test notes live; the
  editor may add its default at save as it does now (Part 7).
- **`make_plain_sound() → std::string`**, on an instrument. Every
  `PerformNode` is removed and each pin it fed takes the field's standing
  value as its constant — frequency 440, velocity 0.8, wheel 0, pressure
  0, duration 1, onset 0, the editor's own stand-ins — so the sound
  stands still at A4 instead of falling silent; every dynamic pin is
  demoted and counted in the result (a plain sound has no note to push
  one: 0 in scope and 0 in the tree carry one); the faces go with their
  nodes; a `SoundChannel` (`ch1`) fed by the old output and a
  `StereoMixer` (`mix`) holding it are added beside the old output's
  position and the mixer becomes the output; the block is cleared;
  `seconds` is left as it is (absent reads as five seconds at build,
  5.8). Today's conversion deletes the Output and Parameter nodes and
  leaves a Note face where it stands, and the same-session stash restores
  the paramMap, score, seconds and polyphony on the way back; with the
  paramMap gone and the block a record, the stash has nothing left to
  hold, and regret is the editor's undo (Part 7).
- Both are *edits*: they return a refusal or a note, change the record
  only, and the next build hears the result. The headless
  `--convert-roundtrip` becomes an engine test of the pair.

### 6.7 Groups (proposed; 6.12 Q7)

- **`group(ids, name = "") → name`**: the members (node ids or group
  names at the current level); the name unique in the shared namespace,
  `Group`, `Group2`… by default; the position the members' centroid; when
  the members lie inside a group, the new group joins it and takes them
  out of its list. **The two-output rule** is kept and derived, never
  stored: a wire from a member to a non-member makes that member a
  boundary output, unless the member is a `PerformNode` (a face
  broadcasts note fields and is never a group's signal) or both ends are
  Wormholes (the hidden span is invisible to the interface) — the two
  exclusions the editor learned on 2026-09-06 and 2026-09-07; a second
  such member refuses with both names. The old refusal "the Output node
  cannot be grouped" has nothing to refuse: the output is a node id, not a
  node.
- **`ungroup(name)`**: the members rejoin the parent level; the exact
  inverse. **`rename_group(name, newName)`** in the shared namespace,
  re-keying the parent's member list. **`move_group(name, pos)`**.
- **`boundary(name) → {ins, out}`** is a query on `Patch`: the inbound
  crossings as (outside node, member, pin) and the single outbound member
  or, with none, the topologically last member that is a signal (what
  Listen taps). The collapsed face the editor draws and `--dump-boundary`
  are this query; the view adds nothing to it.
- The drill path, the listen tap and the per-group listen memory are
  view state (the groups spec §3; target 4.2) and stay in the editor.

### 6.8 Note faces (proposed; 6.12 Q8)

The editor shows one Note node with an output pin per field and the file
keeps one `PerformNode` per field (`GraphNode::perfFieldIds`; the ids
prepended at save). In the model the `PerformNode`s are nodes and the
face is `Layout::faces` (3.6). Two calls carry the device:

- **`add_face(label) → label`** (unique in the shared namespace) and
  **`remove_face(label)`**, which removes the face's `PerformNode`s and
  their feeds, since the face is their only handle.
- **`face_field(label, field) → id`**: the face's node for that field,
  made on demand as a `PerformNode` with the `field` setting under a
  fresh `__perf_<field>` id (the converter's convention) and recorded in
  the face; the view then `connect`s it like any node. A `PerformNode`
  renamed or removed re-keys or clears its face field (3.6).

Part 7 may replace the device; the record and the two calls would go
with it, and nothing else would notice.

### 6.9 The invariants, restated as postconditions (proposed)

After any operation, `check()` is empty when it was empty before:

1. ids non-empty and unique; group names and face labels in the same
   namespace;
2. `output` empty or naming a node; when naming one, a signal type or the
   mixer part;
3. every feed, tap, dynamic-pin source, group member, face field and
   layout key names a node (or a group, for a member);
4. a pin holds a constant or feeds, never both; a multi input is the only
   pin with several feeds; a tap feeds a Param or an Input only;
5. every pin, setting, string, array and dynamic pin names a declaration
   of its type, the type registered; a dynamic pin names a non-structural
   setting; a slot's part is of its accepted kind;
6. a table's columns have one length; a setting is within its declared
   range;
7. no cycle without a tap;
8. a plain sound (no block) has no `PerformNode` and no dynamic pin.

The tests are mutation tests, as the reviewers run them: apply the
operation, `check()`, compare against a copy taken before, and for every
refusal assert the patch is unchanged.

### 6.10 What the editor keeps and what it loses (proposed)

- **Loses**: the functions listed at the top of this section, some
  1,070 lines; the `Parameter` node type (`NT_PARAMETER`, 23 sites), which
  existed for the node-graph mode's keyboard playability; the conversion
  stash (`s_conv*`, `conv_stash_clear`); the internal clipboard and its
  schema (the target keeps one, the fragment); the six headless modes that
  drove these edits, each replaced by an engine test over `Patch`.
- **Keeps, as a view**: selection, hover, drag, the drill path, the
  listen tap, the Output face that shows the block's fields, the Note
  device drawn over `add_face` / `face_field`, the promotion circle drawn
  over `connect_setting` / `disconnect_setting`, the Mappings dialog's
  read-only table derived from the wires (target §5), and undo as a
  vector of `Patch` copies in `OpenPatch` (Part 7).
- **Headless `--roundtrip`, `--gencheck`, `--gatecheck` and the dumps**
  are section 7's: they test the editor against the engine, and most
  have nothing left to detect once there is one codec and one builder
  (target 4.3).

### 6.11 For the C# reader

- **Value semantics.** `Patch` is a value: `Patch before = patch;` is a
  deep copy, with no `Clone()` to write and no shared references to
  reason about, because every member is a `std::string`, a number, a
  `std::vector` or a `std::map` of the same. That is why undo is a
  vector of copies and why a mutation test can compare before and after
  with `==`. In C# the same record would need `ICloneable` or a copy
  constructor per class, and a careless field would alias.
- **`std::optional<InstrumentBlock>`** is the mode flag that is not a
  flag: the two forms differ in whether the block exists, and the type
  says so, where today a `GraphMode` enum is tested in 32 places and can
  disagree with the file it describes.
- **Refusals as values, failures as exceptions.** C# code often throws
  for both; here an operation the user can trigger returns why it
  declined, and a file or a graph that cannot be built throws, because
  the first is a conversation and the second is a stop.

### 6.12 The eight questions (decided 2026-11-08, each as written)

1. **Operations return their refusals as values and never throw;
   `check()` names every violation; 3.8 amended** so `output` may be
   empty while a patch is assembled. The alternative throws from an edit,
   as the editor's load path does.
2. **`Fragment` is its own record** with one codec pair (4.1 amended:
   the fragment is this record, not a `Graph`); `copy` keeps boundary
   feeds, `paste` reconnects them by name or drops them counted;
   `duplicate` is a one-node paste that keeps its feeds. The alternative
   makes a fragment a `Patch` and lets `check()` fail on it.
3. **A pasted or duplicated node draws a fresh seed.** The alternative
   keeps the seed, as both clipboards do today.
4. **Connectability is read from the prototypes once** — signal or part,
   and which part types each slot accepts — so the edit layer refuses
   exactly what the build would. The model lets any signal feed a dynamic
   pin; the Curves-only floor stays a view rule (pin model §9).
5. **`disconnect` restores the declared default; promotion is
   `connect_setting` and nothing else** — no promoted-but-unfed state.
   The alternative keeps a `null` dynamic pin (0 files hold one).
6. **make-instrument and make-plain-sound as 6.6**: the first unfed
   `frequency` in file order takes `__perf_freq`; a plain sound's
   `PerformNode`s become their standing values; dynamic pins demoted and
   counted; the new parts `ch1` and `mix`; no stash.
7. **Groups keep the derived two-output rule** with the face and
   Wormhole exclusions, and `boundary()` is a `Patch` query; ids, group
   names and face labels share one namespace.
8. **Note faces are `Layout::faces` over `add_face` / `face_field`**,
   the `PerformNode`s made on demand under `__perf_<field>`.

## 7. The hosts

Status: **decided with Matt, 2026-11-09, as proposed** ("Section 7
stands"): the seven questions of 7.8 all resolved the way they are
written.

A host is a program that holds a `Patch` and asks the engine for sound:
the command-line tool, the editor, the tests, the allocation gate, and
the Python that drives them or edits the files. Section 1 counted what
each does today; this section says what each does when Part 4 is done,
and what Part 4 changes in each on the way — which, for the editor, is
more than the target's order of surgery implies (7.3).

### 7.1 The seam every host uses (proposed)

```cpp
Patch load_patch(const std::string& path);                       // patch/patch_json.h
void  from_json(const nlohmann::json&, Patch&);                  //   the same reader over text a host already holds
std::unique_ptr<PitchedInstrument> build_instrument(const Patch&, int minPolyphony = 0);   // render
BuiltSound build_sound(const Patch&);                             // render: the three forms (5.8)
double play_embedded_score(PitchedInstrument&, const std::string& scoreText);           // render, until Part 5
```

Today's two entry points and two built structs become these five, and
**no host passes text to the engine**: a host that has a `Patch` builds
from it, a host that has a file loads it, and a host that has JSON text
— a test's inline patch, a clipboard fragment — reads it with
`from_json` and builds from the record. `register_all_sources()` is called by the first thing that asks
the registry (5.10), so the ten headless sites in the editor that call it
by hand, and the lazy flag inside today's `build_graph`, go. A host prints
`BuiltGraph::warnings` and the faults it collects; the engine prints
nothing (5.1).

### 7.2 The command-line tool (proposed; 7.8 Q1, Q2)

The target's order puts the tool onto the module first (§8, step 3); it
is the smallest host and the one every gate drives.

- **The default render** (`mforce_cli <patch> <out.wav>`) becomes
  `load_patch`, `build_sound`, `mixer->render`, the WAV, the statistics
  and `report_faults` over the `Mixer`: the same output, the same
  stderr, the load/render timing split kept. The gates that call it —
  the null gate's 78 renders, the roundtrip gate's pairs, the A/B
  scripts, the sweep drivers — see no change in the command line.
- **The seven Conductor modes** — `--chords`, `--melody`, `--josie`
  (four patches), `--compose`, `--play`, `--dun`, `--test-ornaments` —
  become `build_instrument(load_patch(path))`, `volume` set afterwards as
  today, the Conductor kept until Part 5 gives it the play function.
  (`--test-ornaments` loads `patches/PluckU.json`, a file that does not
  exist, so the mode fails today before it renders; section 9 lists it.)
- **`hiBoost` goes here** (Q1). The field on `PitchedInstrument`, the
  `--hiboost` flag on the chords mode, the five sites that set 0.3 in the
  tool and the one in `test_figures`. Every composition render moves — the
  listening decision Matt made on 2026-10-05 — so the comp lane's
  template null gate (`comp_template_null_gate.py`, `piano_default`
  through `--compose`) is re-frozen after the checkpoint, while the dsp
  null gate is untouched because a single-patch render never set it.
  Section 8 puts it in the first checkpoint, with the tool's move, so the
  move and the deletion are one listening event rather than two.
- **The explorer** (`--explore`, `--explore-filter`) edits a `Patch` copy
  per variant through section 6's operations — `set_value`,
  `set_setting`, a table cell — where today `set_patch_param` walks a JSON
  dot-path, and builds the copy in memory. The temp `.patch.json` it
  writes to get each variant through the path-only loader goes
  (`explore.cpp:334`); the variant files it saves as the sweep's record
  stay, written by `save_patch`. The spec's dot-path text
  (`node.param`, `node.array.3`) stays as the spec's language and is
  mapped onto the operations once. The instrument-style render
  (`prepare_voice`, the per-sample pull) stays until Part 5's live class.
- **`--dump-descriptors`** prints the declarations table of 4.2, in the
  JSON shape the lint and the converter read today; `--lint-template` and
  `--test-ornaments` are the music module's and are untouched.

### 7.3 The editor in Part 4: a translation layer, not a rebuild (proposed; 7.8 Q3)

Part 7 rebuilds the editor on `patch` and `render`; the target's order
leaves it for after this part. But Part 4 cannot leave the editor alone,
and the reason is the file: the codec reads one form (4.8, decided), the
converter rewrites every in-scope file into it, and from that moment the
editor's own loader cannot read the library and its two savers write
what the codec refuses — and every sound the editor makes goes through
the codec, because its six engine loads serialize the graph to text
first. "No back-compat in the engine" rules out the codec reading the
old forms, and the editor cannot keep writing them. So Part 4 gives the
editor the smallest thing that makes it speak the codec:

- **Two functions, `Patch → editor state` and `editor state → Patch`**,
  about three hundred lines against the ~1,330 they replace (corrected
  2026-11-09 with the loader's count):
  `load_graph_from_path`'s parsing (730 lines, after
  `convert_parammap_to_wiring` and the `s_loadedParamMap` stash go with
  the paramMap), `serialize_patch_graph` (about 434) and
  `save_node_graph` (about 165). The translation is mechanical: a `Node`'s
  `values` are pin constants, its `wires` are links found by pin name,
  its `settings`, `strings` and `arrays` are `settingValues`,
  `arrayValues` and the Curve's knot vectors, its `seed` is `jsonSeed`,
  its `dynamicPins` the JSON object the editor keeps; the block is the
  Output node's four fields; the `Layout` is `gridPos`, the groups, the
  faces and the panning; `score`, `seconds` and `sampleRate` are the three
  stashes; the output is the Output node's one link. **`jsonExtras` has
  nothing left to carry** (3.4 §7 booked its retirement to the codec) and
  goes; the `Parameter` node has no file form and goes (6.10).
- **The six engine loads** become `build_instrument(to_patch())` —
  Generate, the live instrument cache, the chords render, the audition
  folder's `build_instrument(load_patch(path))`, `--gatecheck`,
  `--dump-playback` — and `get_playback_patch_path`'s temp file
  (`mforce_playback.json`, written on every play while the graph is dirty
  and on every Listen tap) goes with them; the Listen tap becomes
  `set_output` on the copy that is built.
- **The text clipboard** stages through `Fragment` (6.5) instead of
  saving the whole graph to a temp file and reloading the merge; the
  internal clipboard goes (target §5).
- **What stays until Part 7**: the `GraphNode` model with its live
  `dspSource`, the edit functions of section 6's list (the engine gains
  `Patch`'s copies in Part 4 and the editor keeps its own until it holds
  a `Patch`), the `GraphMode` flag (derived from the block's presence
  from here on, deleted with the mode), the Output face, the Note device,
  the editor's own streaming graph and pan law, the `create_dsp` special
  cases (two of its five, VarSource and RangeSource, fall with 5.7's
  factory change; the Envelope default is CLEANUP 26).
- **What it buys now**: the editor and the command-line tool read and
  write one form through one code path, so the six roundtrip
  KNOWN_DIFFS — the two wiring-format baselines the editor's codec could
  not carry and the four whose render depends on tap and advance order —
  are expected to close, and section 8 measures that rather than
  assumes it.

### 7.4 The tests (proposed; 7.8 Q4)

- **`engine_tests`**: 168 test groups; 47 inline JSON patches reach the
  engine through 39 `load_instrument_patch_json` calls and one
  `load_patch_file`. They go through one helper, `load_text(json) →
  Patch`, then `build_instrument` or `build_sound`. The refusal tests
  (`load_fails_naming` and its needles) keep their needles where the
  message says the same thing — node, pin, reason — and follow the
  refusal where it moves: an unknown key, a string on a pin, a paramMap,
  a tap into a setting are the codec's; a wrong kind on a pin, a cycle, a
  `PerformNode` with no note, a part as output are the builder's.
- **New tests Part 4 brings**: the codec's identity,
  `from_json(to_json(p)) == p` and bytes-identical on a second save, over
  the 132 in-scope files; `Patch::check()` after every operation of
  section 6, as mutation tests (6.9); the declarations table — every
  registered type yields a prototype, the two parts and `PerformNode`
  included, and the slot acceptance matrix says what the door says;
  `build_graph` over a three-node `Graph` with no block, the seam a node
  test needs without a voice; the converter's idempotence and its
  `--check` mode, in Python under `tools/tests/` beside the three tests
  already there.
- **`test_figures`**: one `load_instrument_patch` becomes
  `build_instrument(load_patch(path))`; its `hiBoost` line goes (7.2).
- **`alloc_gate`**: `gate_instrument` builds through `build_instrument`,
  `gate_mixer` through `build_sound`; its own reading of the score block
  for the notes it plays stays — a tool may read the file (4.1).
- **The roundtrip gate** (`test_stable_roundtrip.py`, `rt_smoke.py`)
  stops driving `mforce_ui --roundtrip`: identity is an engine test, and
  the gate becomes a codec pass over the scope (load, save, compare
  bytes) with a render pair only where bytes differ, which after the
  conversion should be nowhere. `bisect_roundtrip_diff.py` retires with
  the diffs it bisected. (Amended under 8.8 Q4, 2026-11-09: `mforce_ui
  --roundtrip` stays in the gate until Part 7 as the translation layer's
  proof, every in-scope file returned byte for byte, beside the engine's
  codec identity test.)

### 7.5 The Python (proposed; 7.8 Q5)

- **Drivers of the binaries** — the null gate, the wiring null gate, the
  allocation gate wrapper, the A/B scripts, the comp template gate, the
  thirteen `_run_*`/`_score_*` sweep drivers, the hysteresis and
  noise-amplitude tests — are unchanged: the command line they call is
  the same. **The wiring null gate retires with the paramMap** once the
  disk conversion lands: its frozen hashes are the null gate's own
  renders of the converted files.
- **The lint** (`lint_patches.py`, which scrapes the loader's *sources*
  for the hand-read keys and ties the meter's test to that scrape)
  becomes `mforce_cli --check <path>...`: load every file through the
  codec, print each refusal with its path, render nothing. The
  declarations are the truth the lint always wanted and never had; the
  scrape, `MANUAL_KEYS` and the selftest go, and the meter's
  `kBranchKeys` test goes with the list.
- **The converters**: `parammap_to_wiring.py`, `migrate_deleted_types.py`
  and `string_evolution_to_node.py` become rules of `convert_patch.py`
  (4.8); `migrate_fullAdditive.py` and `migrate_legacy_additive_params.py`
  likewise where the forms they rewrite still exist in the tree, deleted
  where they do not. `add_instrument_block.py`,
  `rewire_instrument_output.py` and `fix_over_rewire.py` are deleted:
  make-instrument is the operation (6.6) and the other two are the
  paramMap era's. `add_default_score.py` stays until BACKLOG row 10
  decides the score; it edits a block the codec passes through.
- **The 83 `gen_*.py` generators** write today's forms by hand and are the
  sweep campaigns' record. They are not rewritten; a generator run again
  pipes its output through `convert_patch.py`, one line in the driver,
  which is also the answer to CLEANUP 42's nine generators that emit
  deleted formant types.

### 7.6 The headless modes, named one by one (proposed; 7.8 Q6, Q7)

| Mode | Fate | When |
|---|---|---|
| `--rename`, `--replace`, `--paste-frag`, `--apply-preset`, `--convert-roundtrip`, `--dump-boundary` | engine tests of `Patch`'s operations (6.10) | Part 4, as each operation lands |
| `--roundtrip` | the codec identity test in the engine; the mode itself stays as the translation layer's proof (7.4 as amended) | the test in Part 4; the mode until Part 7 |
| `--gatecheck` | an engine test: the envelope-reachability walk (`collect_envelopes`) over `BuiltGraph::nodes` | Part 4, with `build_instrument` |
| `--gencheck`, `--dump-playback`, `--dump-stream`, `--dump-layout`, `--dump-curve-opts` | diagnostics of the editor against the engine; with one builder most have nothing left to detect (target 4.3) | Part 7 |
| `--stamp` | the stale-build guard; its replacement is a build rule that never links over the running program (decided 2026-10-05) and belongs with the build | Part 8 (section 9 lists it) |

### 7.7 For the C# reader

- **A library prints nothing.** A .NET library that writes to `Console`
  is a smell for the same reason the loader's eleven `fprintf`s are: the
  library does not know who is listening. `BuiltGraph::warnings` and the
  fault list are the engine handing its words to whoever owns the
  terminal, the status line or the log.
- **A path-only API drags the file system into memory.** Because the
  loader took a path, the explorer wrote every variant to disk to load
  it and the editor wrote a temp file on every play. Taking the value
  (`const Patch&`) and letting the host choose where it came from is the
  same lesson as a C# method taking a `Stream` or an object instead of a
  file name.

### 7.8 The seven questions (decided 2026-11-09, each as written)

1. **The command-line tool moves first and `hiBoost` goes with that
   move** (7.2), so the composition renders move once, at the first
   checkpoint, and the comp template gate is re-frozen after it. The
   alternative deletes `hiBoost` in a checkpoint of its own.
2. **The explorer edits `Patch` copies through section 6's operations**
   and builds in memory; its temp load files go, its saved variants stay.
3. **The editor gets a translation layer in Part 4** — `Patch ⇄ editor
   state`, two functions — replacing its loader's parsing, its two
   savers, the text clipboard's staging and the six text loads; its model,
   edit functions and mode flag stay until Part 7; `jsonExtras` and the
   `Parameter` node go. The alternative is to hold the disk conversion
   until Part 7, which splits the codec's one form across two parts.
4. **Tests**: one `load_text` helper; needles follow the refusal; the
   four new test families of 7.4; the roundtrip gate becomes a codec
   pass with the editor out of the loop.
5. **The lint becomes `mforce_cli --check`**; the three converters become
   `convert_patch.py` rules; `add_instrument_block.py`,
   `rewire_instrument_output.py` and `fix_over_rewire.py` are deleted;
   `add_default_score.py` waits for BACKLOG row 10; generators pipe
   through the converter when rerun.
6. **`--gatecheck` becomes an engine test in Part 4**; `--gencheck` and
   the four dumps wait for Part 7.
7. **The stale-build guard is Part 8's**, as a build rule, and stays
   meanwhile.

## 8. Byte-identity plan

Status: **decided with Matt, 2026-11-09, as proposed** ("Section 8
stands"): the seven questions of 8.8 all resolved the way they are
written; 4.8 amended under Q2 and 7.4 under Q4.

Part 4 lands as eight checkpoints. Each ends as the protocol says: the
behaviour gates green, a commit, the independent review with its ten
items, and an architecture report with the meter's numbers (gates spec
§0, §3.8, §3.9). One rule sets the order:

**The files reach the codec's form before the codec exists.** Every
rewrite on disk happens under today's loader, with the null gate
watching, one family of forms per checkpoint — node, tool, tree and
editor together, the editor before the engine, as 3.4, 3.5 and 3.9 did
it. Then the module is built beside the loader and proven as a second
reader of the *same* files. Then the hosts flip and the loader is
deleted, and that step touches no file. The alternative, one conversion
at the flip, would need the nodes changed under a loader that
translates the old forms for them and an editor that does the same,
both written to be deleted, and would put the reader, the tree, the
hosts and the editor into one unreviewable step.

### 8.1 The checkpoints (proposed; 8.8 Q1)

| Checkpoint | What lands | On disk | Expected render changes |
|---|---|---|---|
| 4.1 The seam's render half; `hiBoost` | today's built struct `Patch` renamed `BuiltSound` and the live `StereoMixer` renamed `Mixer`, which frees both names; one voice loop behind both entry points; `perform_score_json` becomes `play_embedded_score` in a file of its own, with the three `music` includes it needs (two dead ones go); the `mix` lookup deleted; one mono adapter, made for a channel's source and nothing else; `hiBoost` deleted — the field and its two uses in the voice gain, the flag, six host sites. The entry points keep their names until 4.7 | nothing | none in the 132. Every composition render moves (`hiBoost`): the comp template gate's 22 templates rendered before and after, the manifest re-frozen; the decision is 2026-10-05's, and REVIEW offers one before/after pair |
| 4.2 The paramMap goes | `build_bindings`, `attach_perform_source`, the non-setting `PushBinding` path, the Multiplex clone fan-out and `topMultiplex` (5.5); the editor's `convert_parammap_to_wiring`, the `s_loadedParamMap` stash (41 references), the Mappings dialog's legacy table and Add row, and its make-instrument writing a `PerformNode` wire instead of a paramMap entry; `tools/convert_patch.py` born with `parammap_to_wiring.py` as its first rule; `add_instrument_block.py`, `rewire_instrument_output.py`, `fix_over_rewire.py` deleted; a file still carrying a paramMap fails by name | 82 in-scope files (67 library, 15 baselines), 111 audition, 9 pending — the nine the 10-05 authorisation named | none. The wiring null gate is the proof and retires afterwards. One departure by construction: a Multiplex voice under a bend or a glide, where the push held the note-on frequency and the wired stand-in follows it; `MPXTest3`'s score has neither |
| 4.3 Tables: Envelope and Curve | `ArrayDescriptor::enum_labels`, the enum column; `Envelope` declares its eight stage columns and `timeMode`, maps unconditionally (5.3), reads its two accuracies once (CLEANUP 29); `CurveNode` declares `knots`, `exprKnots`, `interp`, `mode`; the loader's Envelope branch reduces to the preset form's call and its Curve branch goes; the editor's stage table and curve editor read the node's arrays through the generic grouped table, as `FormantPeaks` does since 3.9 | stage lists become columns in 29 in-scope files (64 nodes), 335 audition, 33 pending; knots become columns in 15 in-scope files (74 + 30 nodes), 180 audition, 27 pending | none. One departure by construction: an envelope whose raw value is a negative zero leaves as a positive zero, which no WAV can show |
| 4.4 Every type a declaration | `SettingType::String` and the string pin; `Component::validate()`; the factories of `VarSource`, `RangeSource`, `SegmentSource` default-construct; `Vibrato`'s branch, the Butterworth `sections` setting, `HybridKSSource`'s array, `PhasedValueSource`'s sources and table, `MultiSource`'s `weights` and `delays`, the Static pair's settings, `Partials`' `expandRule` slot, the Shaper's checks, `NameGate`'s `name` — the rows of 5.7; ten of the twelve configurators deleted (the two formant ones wait for the codec's rules) | `cutoff` → `cutoffFreq` (1 in scope), `gap` → `gapDuration` (1), `MultiSource` weights (3 library, 1 pending), the Phased stages (1 baseline), `operation: "add"` → its label (8 audition, 1 pending) | none |
| 4.5 The terminal types and the document's spellings | `SoundChannel` and `StereoMixer` registered parts in the new Output family; `PerformNode` registered (75 → 78 registrations); the loader's plain-sound path reads the parts' pins; cross-loop sharing refused by name; the editor's two terminal types come from the registry and it reads and writes `layout` and `comment` | `inputs` → `params` in 27 in-scope files, 351 audition, 3 pending; `ui` + `groups` → `layout` in 20, 230, 35; `_comment` and `_provenance` → `comment` in 13 and 4 audition; enum integers → labels in 10 in-scope files (126 nodes), 207 audition, 27 pending | none. After this checkpoint every file in the four folders is in the codec's form |
| 4.6 The module, beside the loader | `engine/include/mforce/patch/`: the records, `Fragment`, `patch_json` with the preset table moved in, the declarations table with slot acceptance, `build_graph`, `Patch::check()`; `render`'s `build_instrument(const Patch&)` and `build_sound(const Patch&)`; `mforce_cli --check`. First task: the count of settings outside their declared range over the scope, declarations widened where a test patch legitimately exceeds one, then the four clamp hooks removed (4.6, CLEANUP 19). **The twin gate** (8.3) | nothing: `--check` green over the four folders is the proof the tree already is the form | none; nothing but the twin gate renders through the module yet |
| 4.7 The flip | the editor's translation layer (7.3), proven first by `mforce_ui --roundtrip` returning every in-scope file byte for byte (Q4); then one commit re-points the command-line tool, `alloc_gate`, `test_figures`, the tests' `load_text` helper and the editor, and deletes `patch_loader.{h,cpp}`, `InstrumentPatch`, `JsonConfigurator`, `ResolveParamFn`, the last two configurators, the JSON include in `source_registry.h`, `lint_patches.py` with its scrape, `kBranchKeys` with its meter test; the module map gains `patch` | nothing | none: 4.6's twin proved the reader. The six roundtrip KNOWN_DIFFS are measured — expected to close; any that stays is listed with its cause |
| 4.8 The operations | `Patch`'s edit operations with their mutation tests (6.2–6.9), `apply_preset`, make-instrument and make-plain-sound; the explorer on the operations; the editor's text clipboard through `Fragment`, the internal clipboard deleted; `--gatecheck` as an engine test; Part 4's CLEANUP sweep and closing report | nothing | none; the explorer's variants byte-identical to today's over one sample spec |

**4.1 landed 2026-11-09** (`e5518ef`..`7b32205`, the review's fix waves `e0cb2e2` and `3c012ea`; plan
`docs/superpowers/plans/2026-11-09-part4-checkpoint-4.1.md`; report R16,
`docs/autonomy/refactor/reports/2026-11-09-checkpoint-4.1.md`). As the
row says, with these facts the execution added. On disk nothing; 0 of the
132 moved, and the nine in-scope instrument files that carry no score —
which render silence, so that no gate had ever heard them — were rendered
with a score appended under both builds and are identical too. The comp
template gate holds 21 templates, not 22 (`test_k467_walker.json` is
prose): 21 piece JSONs identical, 21 WAVs moved, re-frozen at stage
`post41`. The composition renders moved by more than the row foresaw:
the gain law is the only change in the code, but on `piano_default`, a
one-voice patch, each note's rendered length follows its gained level and
the voice's random streams run on from note to note, so every note after
the first two is a new realisation (BACKLOG row 11 holds the question
that raises; it constrains Part 5's byte-identity plan for composition
renders). One adapter is made for a channel's source *and the bare
output* (the row says "a channel's source and nothing else"; the bare
output is the second place a plain sound consumes a signal). Two
channels naming one source are refused by name already, ahead of 4.5's
refusal of deeper sharing. 5.8's sentence on `frames` is corrected in
place.

### 8.2 Why this order (proposed)

- **4.1 first** because it holds the part's one listening change, and a
  behavioural change gets a checkpoint to itself (Part 2 §7's rule). The
  command-line tool moves in two steps: onto the seam's result types
  here, onto `Patch` at 4.7; 7.2's "the first checkpoint, with the
  tool's move" is this first step (Q5).
- **4.2 before every other rewrite** because the paramMap is the form
  with machinery behind it — the push path, the clone fan-out, the
  editor's third converter and its stash — and every later checkpoint is
  smaller once that machinery is gone. Its tool and its gate already
  exist.
- **4.3 before 4.4** because the enum column is needed by both tables,
  and the two tables carry the editor's two largest special editors.
- **4.5 last among the rewrites** because it changes the editor's node
  vocabulary (two of its four UI-only types become registered types) and
  the spellings the old engine loader does not read at all, so its risk
  is the editor's alone.
- **4.6 builds with nothing depending on it**, which is what lets the
  twin gate compare two complete readers.
- **4.7 is small because 4.6 was the proof.** The flip's one commit is
  re-pointing and deletion.
- **4.8 after the flip** because the explorer and the clipboard are
  hosts of the operations, and hosts hold a `Patch` only from 4.7.

### 8.3 The instruments (proposed; 8.8 Q3)

Every checkpoint, as since 3.1:

- **The null gate**, 78 of 78, after every task. At a rewriting
  checkpoint the manifest's files are rewritten in place and their
  hashes must not move.
- **The A/B at the close** over the 132, against the pre-checkpoint
  build's renders of the *unrewritten* files, 0 differ — the old arm
  rendered once from a worktree build of the base commit.
- **The allocation gate**, 0 of 132; `engine_tests`, with a twin wherever
  an expression moves and the reviewer mutating each; the roundtrip
  gate, no new diff; the meter's delta; the stamp.
- **The converter**: `convert_patch.py --check` green over the four
  folders after each rewriting checkpoint, a second run changing
  nothing, its unit tests beside the three already in `tools/tests/`.

Two instruments are Part 4's own:

- **The twin gate (4.6).** Every in-scope file rendered twice in one
  process — through today's loader and through `load_patch` and
  `build_sound` or `build_instrument` — and compared byte for byte,
  with the allocation count and the fault report compared too. Because
  4.2 to 4.5 put the tree into the codec's form, both readers read the
  *same* files, so the gate tests the reader and nothing else. Its main
  claim is 5.3's: a Param's own constant renders what the wired
  `ConstantSource` rendered. It also carries 5.2's measurement — the
  four tap-order baselines built with their nodes in reversed file
  order — and a timing A/B between the two readers (the sweep sheds its
  constants), reported, not gated.
- **The translation's roundtrip (4.7).** `mforce_ui --roundtrip` over
  the scope must return each file byte-identical: `Patch` → editor state
  → `Patch` is new code, and it loses things exactly where the six
  KNOWN_DIFFS did. This keeps the editor in the roundtrip gate until
  Part 7 deletes the translation, beside the engine's own codec identity
  test (Q4, amending 7.4).

### 8.4 The untracked folders (proposed; 8.8 Q6)

Library and baselines are tracked (the 132 patches); audition (708
files) and pending (44, one more than the 43 counted on 11-08) are not. The four
rewriting checkpoints run the converter over all four folders. For the
untracked two the rules are the ones already in force:

- pending's originals are kept at each rewriting checkpoint under
  `renders/scratch/ab4N/pending_before/` (3.9's rule), so the files as
  Matt last saw them are the copies under `ab42`;
- audition is rewritten in place;
- no gate or A/B time is spent on either (the 2026-10-30 rule):
  `--check` green is their proof, and a converted patch that later
  sounds wrong is diagnosed then.

Sweep (1,370 files), scratch (71) and old (821) are not converted
(4.9 Q6). What stops loading there, and when:

| After | Sweep | Scratch | Old |
|---|---|---|---|
| 4.2, a paramMap | 103 | 0 | 223 |
| 4.3, a stage list or knots | 626 / 948 | 54 / 53 | 124 / 39 |
| 4.5, `inputs` | 107 | 13 | 548 |
| 4.7, any form or key the codec does not know (`ui`, `groups`, `_comment`, a spec file's own keys) | 1,364 of 1,365 | 70 of 71 | 817 of 819 |

`python tools/convert_patch.py <file or folder>` is the way back in, and
every refusal names it.

### 8.5 What Part 4 hands on (proposed)

- **Part 5**: the play function in place of `play_embedded_score` and
  the Conductor's seven hand-written callers; `DrumKit`'s assembly;
  `PitchedInstrument`'s split; the live hand-off.
- **Part 7**: `OpenPatch`; the editor's `GraphNode` model, its edit
  functions and their six headless modes, the mode flag, its own stream
  and pan law, the translation layer, the five diagnostic modes, the Note
  device's replacement if any.
- **Part 8**: the stale-build guard as a build rule; `test_figures` into
  the test suite; the comment purge.
- **BACKLOG row 10**: where a patch's test notes live.

### 8.6 Execution (proposed)

The 11-06 pattern, stated here so each plan need not argue it: a plan
per checkpoint under `docs/superpowers/plans/`, written by Fable with a
Ledger, the split stated against the usage at plan time. Fable executes
the design-bearing tasks — the two kinds, `validate`, the records, the
codec, the builder, the translation layer, the operations. Opus 5.5
executors take the mechanical ones — the converter's rules, the tree
rewrites, the editor's deletions, the hosts' re-pointing, the
measurements — each verified by the coordinator before its commit. An
Opus 5.5 reviewer refutes with mutation tests. Reports R16 to R23, each
with its lessons section.

*R16 is 4.1's (2026-11-09). Its plan was amended with Matt before
execution: Opus 5.5 executed every task, the design-bearing ones included,
because the plan specified each move to the line; no executor committed,
and Fable verified each tree and committed it. The reviewer read only; the
mutation runs were an executor's. What that split caught and what it
missed is in R16's sections 6 and 8.*

### 8.7 For the C# reader

- **A twin** is a characterization test with a short life: the old and
  the new implementation in one test, the same inputs, the outputs
  compared bit for bit, and the old one deleted in the commit after. In
  .NET the same idea goes by golden-master or approval testing; the
  difference here is that the master is the old code itself, still
  running, not a stored file.
- **Why bytes, not tolerances.** The engine is built `/fp:precise`, so
  the same arithmetic in the same order gives the same bits on every
  run. A tolerance would pass exactly the mistakes a refactor makes — an
  expression reordered, a constant widened to double and back — which
  are small by nature. Byte-identity makes "nothing changed" a fact
  rather than a judgement, and the few places it cannot hold are listed
  by name as departures.

### 8.8 The seven questions (decided 2026-11-09, each as written)

1. **Eight checkpoints in this order** (8.1), each a family of forms or
   one structural step.
2. **4.8 amended: one converter whose rules land over four checkpoints**
   (4.2 to 4.5), each a pass over the same four folders, so the tree is
   in the codec's form before the codec exists — where 4.8 said one
   pass. The alternative is one pass at the flip.
3. **The twin gate on the real tree is 4.6's proof, and the flip
   touches no file** (8.3).
4. **7.4 amended: `mforce_ui --roundtrip` stays in the roundtrip gate
   until Part 7**, as the translation layer's proof, every in-scope file
   returned byte for byte — where 7.4 took the editor out of the loop.
5. **`hiBoost` at 4.1, the entry points keeping their names until 4.7**:
   the tool moves in two steps.
6. **The untracked folders as 8.4**: audition and pending rewritten at
   each of the four checkpoints, pending's originals kept each time, no
   gate time spent on either; sweep, scratch and old on demand.
7. **The two departures by construction are accepted as listed**: a
   Multiplex voice under a bend or glide (4.2), an envelope's negative
   zero (4.3).

## 9. Deletions, in order

Status: **decided with Matt, 2026-11-09, as proposed** ("Section 9
stands"): the five questions of 9.6 all resolved the way they are
written. As in the target's section 5, Matt's veto still applies to
every line.

The rule: a thing is deleted in the checkpoint that makes it dead, after
the gate that proves it dead has passed, together with everything that
existed only for it. Nothing is commented out or left behind a flag; Git
holds the history. The lines counted below are measured on `b30a237`.

### 9.1 By checkpoint (proposed; 9.6 Q1)

| At | Deleted | Takes with it | What proves it dead |
|---|---|---|---|
| 4.1 | `PitchedInstrument::hiBoost` | its two uses in the voice gain; the `--hiboost` flag; six host sites (five in the command-line tool, one in `test_figures`); the comp template gate's old manifest | decided 2026-10-05; the comp gate measures the move |
| 4.1 | the `mix` lookup | the second whole-graph build on every instrument-form load, with its throwaway voice and perform context | the null gate over the three electric pianos |
| 4.1 | the second voice loop | 62 duplicated lines and the node-indexing pair beside them, the meter's largest duplicate block | the null gate; both entry points share one loop |
| 4.1 | `WaveSourceMono` beside `ValueSourceMono`; `GraphResult::monoNodes`; `add_mono` | an adapter per signal node of every build, voices included; the "No mono source" failure for a channel fed by an Envelope, a Curve, a Var, a Range, a Vibrato or a NameGate | the null gate; a test of each as a channel source |
| 4.1 | two dead `music` includes in the loader (`pitch_bend.h`, `pitch_curve.h`) | two of the module map's seven render-to-music violations | the build |
| 4.2 | `instrument.paramMap` as a form: `build_bindings` (146 lines), `attach_perform_source`, the legacy copy of the bend-inert advisory | `PushBinding::cs`, `cloneSlot`, `targetNodeId` and `isSetting` (every binding left is a setting's); `VoiceGraph::topMultiplex`; `MultiplexSource`'s `clone_param_slot`, `set_clone_param`, `CloneParam`, `cloneParams_` and its two private helpers; `ensure_constant`, both overloads, whose only callers these are; the three paramMap tests, rewritten as wiring tests where the behaviour remains | the wiring null gate, then the A/B over the 132 |
| 4.2 | the editor's paramMap: `convert_parammap_to_wiring` (122 lines), the `s_loadedParamMap` stash (42 references), the Mappings dialog's legacy table and Add row with the curve editors over the stash | CLEANUP 44; make-instrument writes a `PerformNode` wire | the roundtrip gate; the dialog's read-only table still shows every wire |
| 4.2 | `parammap_to_wiring.py`, `migrate_deleted_types.py`, `string_evolution_to_node.py` as separate tools; `add_instrument_block.py`, `rewire_instrument_output.py`, `fix_over_rewire.py`; `null_gate_wiring.py` after its last green run | 1,048 lines of Python in seven files; the three converters live on as rules of `convert_patch.py` and their two test files follow them; CLEANUP 42's nine generators get the converter's pipe | `convert_patch.py --check` green; its unit tests |
| 4.3 | the loader's Curve branch and the stage half of its Envelope branch; the Envelope's test of whether a range pin is wired; the second read of the two accuracies | CLEANUP 29; the tolerance for a stale `holdPct` key; in the editor `envelope_stages_to_json` and `envelope_stages_from_json` (five uses), `GraphNode::curveKnots`, `curveInterp`, `curveExprMode`, `curveExprKnots` (74 references) and the two special editors' private state; the six `algev_test_*` baselines' `release` and `releaseMax` keys that no form ever read, dropped by the converter (CLEANUP 24) | the null gate; a twin for the stage layout and for the knot lookup |
| 4.4 | six loader branches (`VarSource`, `RangeSource`, `SegmentSource`, `Vibrato`, the Butterworth trio, `HybridKSSource`); the Shaper block and the `expandRule` block of `wire_params_generic`; ten configurators | constructor arguments on five types; `Partials::set_expand_rule`; `PhasedValueSource::add_stage` and its `Stage`; the weight and delay fields of `MultiSource::Entry`; the three `cutoff` aliases; in the editor two of `create_dsp`'s five special cases and the NameGate text field's own code | the null gate; a twin per moved expression |
| 4.5 | the loader's hand-written reading of the channel and the mixer (`inputs.source`, `inputs.channels`, the plain-string ids in `collect_advance_ids`); the spellings `inputs`, `ui`, `groups`, `_comment`, `_provenance` | in the editor the hand-built pins of its channel and mixer types and `add_channel_input`'s `ch N` pins (the registered mixer's multi slot serves); CLEANUP 25, one enum dialect on disk | the null gate over the 27 plain sounds; the roundtrip gate |
| 4.6 | the four clamp hooks of CLEANUP 19 | — (`envelope_json.h` leaves the nodes module here too, but it moves into the codec; it is not deleted) | `mforce_cli --check` over the scope, after the out-of-range count |
| 4.7 | `patch_loader.cpp` (1,702 lines) and `patch_loader.h`: both entry points, `InstrumentPatch`, `GraphResult`, `PerformContext`, `wire_params_generic`, `reject_unknown_keys` with `kBranchKeys` (120 keys), `refuse_string_pin`, the four branches left, the `thread_local` tap collector, `collect_advance_ids`, `extract_subgraph_json`, `build_subgraph_with_seed_perturbation`, `bind_wiring` (103 lines), `adopt_nodes`, `make_perform_context` | `JsonConfigurator`, `ResolveParamFn`, `get_configurator`, the two formant configurators and the JSON include in `source_registry.h`; `MultiplexSource::templateJson_`, `baseSeed_` and the three-argument `set_template`; the reading of a top-level `frames` (no file in the tree carries one); `lint_patches.py` (285 lines) with its source scrape and `MANUAL_KEYS`; the meter's `kBranchKeys` test; the nineteen `dynamic_cast<PitchedInstrument*>` in the tests (CLEANUP 28 — `build_instrument` returns the type); the dead id exclusion in the two roundtrip scripts (CLEANUP 43); `bisect_roundtrip_diff.py` if the six diffs close | 4.6's twin gate; then the full gate set on the flipped tree |
| 4.7 | the editor's loader parsing (`load_graph_from_path`, 730 lines), `serialize_patch_graph` (433), `save_node_graph` (164), `get_playback_patch_path` and its temp file | `jsonExtras` (15 references) and the "edit wins" rule; `s_loadedInstrumentExtras`; the `Parameter` node (23 sites and its theme colour); the four serialize-and-`dump()` engine loads (the two path loads re-point); ten `register_all_sources()` calls | `mforce_ui --roundtrip` byte for byte over the scope; `--gencheck` parity |
| 4.8 | the editor's internal clipboard (`clip_node_state`, `clip_instantiate`, copy, paste, cut — 232 lines — and `s_clipboard`); the text clipboard's two temp files; the explorer's `set_patch_param` JSON walker and its temp load files; `--gatecheck` as a mode | the third node schema; one of the two clipboards, as the target listed | the fragment tests; the explorer's variants byte-identical |

### 9.2 Listed by the target, not deleted in Part 4 (proposed)

Each stays because the code that would replace it belongs to another
part; each is named so no one hunts for it at Part 4's close.

- **Part 7**: the editor's `GraphNode`, link and group model; its edit
  functions (some 1,070 lines, section 6) and the six headless modes
  that drive them; the `GraphMode` flag (32 sites); the conversion pair
  and its stash (`s_conv*`, 21 references); its own streaming graph and
  pan-law copy; the two `create_dsp` special cases left (the Note
  stand-in, the new-Envelope default); the five diagnostic modes; the
  translation layer Part 4 adds.
- **Part 5**: `play_embedded_score` and its three `music` includes; the
  Conductor's seven callers in the command-line tool; `instrument.h`'s
  two `music` includes.
- **Part 8**: the stale-build guard (`build_stamp.h`, `--stamp`,
  `tools/stamp_test`), replaced by a build rule.
- **BACKLOG row 10**: the embedded score block and, with it,
  `add_default_score.py` and the editor's default one-note score.
- **Kept on purpose**: `retired_type_hint`, which names the converter for
  every retired name (CLEANUP 27's two lists stay two, one per
  language); `MultiplexSource::mark_dirty` (5.5); the 83 generators, as
  the sweep campaigns' record; `docs/matt/parammap_patches.md`, the
  2026-10-05 census the conversion plan was built on.

### 9.3 Found dead on the way (proposed; 9.6 Q2, Q4)

Not on the target's list; found while reading for this document.

- **`mforce_cli --test-ornaments`** loads `patches/PluckU.json`, which
  the 2026-08-10 restructure removed (`eb964a1`); the mode has failed
  at its first line since. Proposed: deleted at 4.1 with the tool's other
  edits. The alternative repoints it at a library patch; what it tested —
  ornaments under three humanize settings — is the comp lane's and has
  `test_figures`.
- **`migrate_fullAdditive.py` and `migrate_legacy_additive_params.py`**
  (281 lines) rewrite forms from before Part 3. Whether any file in the
  tree still carries those forms is measured at 4.2: rules of the
  converter if so, deleted if not.
- **A correction to 5.3.** Its last sentence describes `ensure_constant`
  making a constant "for a dynamic pin on an unwired Param". A dynamic
  pin is a setting; the function's only callers are the paramMap's push
  and the Multiplex clone fan-out, both gone at 4.2, so it is deleted
  before the builder exists and the side effect 5.3 describes never
  arises. 5.3 is annotated; nothing decided there changes.
- **Two corrected counts**, also annotated in place: the editor's loader
  is 730 lines, not the "about 790" of 1.3 and 7.3, so the translation
  layer replaces about 1,330 lines, not 1,390.

### 9.4 What Part 4 leaves the meter (proposed)

The module map gains `patch` (`engine/include/mforce/patch/`,
`engine/src/patch/`; may include `core` and the nodes) and `render` may
include it; `patch_loader.cpp` leaves `render`'s paths. The JSON
allowlist loses `source_registry.h`, `patch_loader.cpp` and
`envelope_json.h`, and gains the codec's file. Render-to-music
violations go from seven to five at 4.1 and stay five until Part 5
(three in the embedded score's file, two in `instrument.h`). The
function over budget by the widest margin, `build_graph` at 348 lines,
is gone at 4.7; `load_graph_from_path` at 730 and `serialize_patch_graph`
at 433 with it. Each checkpoint's report carries the meter's own delta;
these are the expectations it is read against.

### 9.5 CLEANUP items Part 4 closes

19 (4.6), 24 (4.3), 25 (4.5), 28 (4.7), 29 (4.3), 42 (4.2), 43 (4.7),
44 (4.2). Left open and named: 23 (the `adsr` release default, after
Part 8), 26 and 45 (the editor's new-node defaults, Part 7), 27 (two
lists of retired names, kept), 30 (the dead `Morph_env` node, a patch
edit and Matt's).

### 9.6 The five questions (decided 2026-11-09, each as written)

1. **The list and its order** (9.1), every line open to veto.
2. **`mforce_cli --test-ornaments` is deleted at 4.1**, dead since the
   2026-08-10 restructure. The alternative repoints it at a library
   patch.
3. **The three existing converters fold into `convert_patch.py` at its
   birth (4.2)**, their tests following them, and `null_gate_wiring.py`
   retires after its last green run there.
4. **The two pre-Part-3 migration tools are settled by measurement at
   4.2**: rules if their forms still exist anywhere in the tree, deleted
   if not.
5. **What stays for Parts 5, 7 and 8 is as 9.2 lists it** — in
   particular the editor's model, edit functions and mode flag survive
   Part 4, with the translation layer beside them.

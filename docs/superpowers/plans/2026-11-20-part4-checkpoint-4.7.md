# Part 4, checkpoint 4.7: the flip — Implementation Plan

**Goal:** Part 4 section 8.1's seventh row, as decided with Matt on 2026-11-09: every host reads a patch through the `patch` module, and the loader is deleted. The editor gets its translation layer (7.3): two functions between its node model and a `Patch`, proven by `mforce_ui --roundtrip` handing every in-scope record back unchanged. Then the command-line tool, the explorer, `alloc_gate`, `test_figures` and the tests' helper build through `load_patch`, `build_instrument` and `build_sound`; the twin gate runs one last time; and `patch_loader.{h,cpp}`, `InstrumentPatch`, the last configurator, the lint with its scrape and the twin gate itself are deleted. Two edits to code the loader runs come first: the `ExplicitPartials` over-read (CLEANUP 107), and a declared mark for a setting its node answers from its arrays, which is REVIEW R21's question come back with a consumer. No file under `patches/` or `scores/` changes and no render moves.

**Architecture:** Part 4 sections 3, 4, 7, 8 and 9 give the design; the survey behind this plan (2026-11-20: those sections whole, 1.3, 5.10 and 6.10; 4.6's plan and Ledger whole; report R21's sections 1, 2.8, 4.7, 6 and 7; CLEANUP whole; the module's fourteen headers and `patch_json.cpp`, `node_json.cpp`, `build_graph.cpp`, `build_sound.cpp`, `build_instrument.cpp`; `source_registry.{h,cpp}`, `pins.h`, `descriptors.h`; in `tools/mforce_ui/main.cpp` the node model, the loader, both savers, the text clipboard, the internal clipboard, the conversion pair, the six engine loads, the voice structs, every headless mode; the tests' two-reader helper and its table; `test_stable_roundtrip.py`, `rt_smoke.py`'s cases, `gates.py`, the meter's map and tests; the command-line tool's render and the explorer's; and two scripts written for it under `renders/scratch/ab47/planning/`) adds the facts below, each of which a gate or a test then proves.

(1) **The base.** Both built binaries are byte-equal to 4.6's close arms: the command-line tool `fe86ad64…94ce0577` (`cli_post46.exe`) and the editor `0c5b71d1…cd1e5512` (`ui_post46.exe`). 4.6's close arms are this checkpoint's old arms with no render.

(2) **What today's editor does to a record, measured** (`editor_today.py`: each of the 131 in the codec's own saved form, which the twin gate's `--resaved` arm left under `renders/scratch/ab46/final/resaved/`, through today's `mforce_ui --roundtrip`, input and output compared as parsed JSON with floats as float32). **2 of the 131 come back equal.** Nine kinds of difference and no other:

| What the editor does | Nodes | Files |
|---|---|---|
| drops `Envelope.sustainLevel` (its saver skips the key) | 173 | 126 |
| drops an empty `onsets` and a false `sustaining` from the block | | 104, 103 |
| writes a `faces` block for PerformNodes the record gave no face | | 87 |
| writes `seconds: 3` beside a score that had none | | 69 |
| drops `panning` (headless there is no view to ask) | | 16 |
| drops an array that is present and empty (`CurveNode.x`, `y`; the four `expr` columns) | 29 | 7 |
| writes the device's 48000 over the file's 22050 | | 2 |
| writes the PerformNodes first, where the record has them later | | 2 |
| writes `ExplicitPartials.maxPartials` as the row count | 3 | 2 |

No id is lost or added, no array is added, and no other value changes. The last row and the first are one question (fact 4).

(3) **The census** (`census47.py`, the four folders, importing nothing of the converter or the gates). In scope: 104 instruments and 27 plain sounds; 112 files hold no layout, 17 hold a position for every node, 2 a layout with no position; 101 hold PerformNodes (154 of them), 87 of those with no `faces`, and 10 hold two PerformNodes of one field; no PerformNode in scope is unread by a wire; 69 instruments hold a score and no `seconds`, 9 hold no score; the block's keys are `polyphony` 104 times, `volume` 19, `sustaining` once, and no `onsets` or `glideMs`. In audition and pending: 85 files whose PerformNodes are not first in the node list; 22 files with positions for some nodes and not others (159 nodes, which the load's strip pass places); 32 positions under a PerformNode's id; 4 files whose faces do not name exactly their PerformNodes; `onsets` in 5 files, every name plain and short. In none of the four folders: an id holding a `.`, a `null` under `dynamicPins`, a type the registry lacks other than pending's three `GrayScottSource` files.

(4) **Three settings whose number the node answers from its arrays.** An Envelope's `sustainLevel` is the stage table's four-slot value, 0 on any other shape (`Envelope::get_setting`). An `ExplicitPartials`' `maxPartials` and a `LayeredRedNoiseSource`'s `count` are set to the row count whenever a column arrives (`on_array_changed`). The record keeps the file's number, or the declared default. In scope the record's number is not the node's answer on **89 of the 173 Envelopes** (63 column-form that leave the key out, 22 `ar` presets, 3 `as` and 1 `asr` that give the knob) and on **3 of the 6 `ExplicitPartials` with rows**; no file of the four folders holds a `LayeredRedNoiseSource`. The editor shows the node's answer in its panel (it re-reads every setting from the node after the arrays arrive), so that is the only number it has to hand back. In the four folders no Envelope's sustain cells are outside 0 to 1 and no `ExplicitPartials` holds fewer than 5 rows or more than 96.

(5) **The editor wires a `ConstantSource` into every pin, Params included** (`create_dsp`, `update_node_dsp_unlocked`), and goes on doing so: its stream is its own until Part 7. So what 5.3 gave the builder, a Param's own constant, does not reach the editor's live nodes; `Envelope::replace_stages` still meets a wired trigger there, and CLEANUP 111 does not bite in this checkpoint.

(6) **What the load already treats as the view's.** `layout_position` writes a node's position headless only when the graph knows it, and `panning` is written only when a view exists. Two things do not follow that rule today: the strip pass that places unpositioned nodes runs inside the load and marks every one of them placed, and the faces packed for PerformNodes are written like any face.

(7) **The editor's sites.** `load_graph_from_path` (line 1408) has sixteen callers: the dialog (2226), the paste (10375, 10379), eleven headless modes, the command-line argument (12387) and the recents menu (12539). `serialize_patch_graph` has four (4052, 4250, 7314, 12128); `save_patch_graph` and `save_node_graph` are called by `save_to_path`, the clipboard's two stagings and six headless modes. `get_playback_patch_path` (3140) has no caller. The six engine loads are 4053, 4255, 7315, 7436, 11836 and 12127; `InstrumentPatch` is named at twelve lines, three of them comments; `register_all_sources()` at thirteen (twelve headless modes and the window's start); `jsonExtras` at fifteen; `NT_PARAMETER` at twenty-three; `s_loadedInstrumentExtras` at seven.

(8) **The tests.** `engine_tests` loads through `load_inst` and `load_sound` (lines 1591, 1598), which twin every load; twenty-three loader calls sit outside the helper, most of them pinning one of the loader's own sentences (4.6 counted seventeen such) and the rest the loader's arm of a comparison; the table of known differences has eighteen rows, every one an enum word that is not exactly a label; the generated reader twin is lines 14539 to 14742; `register_all_sources()` is called at fifteen places.

(9) **The other hosts.** The command-line tool calls the loader at ten sites in `main.cpp` (94, 253, 418 to 421, 559, 711, 783, 877) and two in `explore.cpp` (351, 374); `alloc_gate` at 109 and 152; `test_figures` at 1160.

(10) **What goes with the loader.** One configurator is left (`FormantSequence`'s, `source_registrations.cpp:296`, which exists to refuse a `formants` key the codec refuses by name); `MultiplexSource`'s three-argument `set_template` with `templateJson_` and `baseSeed_`; `lint_patches.py` and the meter's test that reads `kBranchKeys` (`test_structure.py:129`); in `modules.json` two paths and two allowlist entries; `tools/twin_gate/`, `tools/twin_gate.py` and the root CMake line.

(11) **The meter's two known answers move.** `test_ui_main_function_lengths` pins `main` at 1,707 lines and `draw_properties_panel` at 949; the headless modes and the Parameter's panel sit inside them. Each editor task retakes the two numbers from the meter.

**Execution (8.6 as amended 2026-11-14):** Fable wrote this plan and the layer's interfaces (the Interfaces section and the translation table are the specification), coordinates, writes a brief file per task, and reads the diff of Tasks 1 to 5 whole. Opus 5.5 executors take every task and **no executor commits**. (1) The coordinator reruns a gate only where it edited or a hash disagrees: otherwise it compares the executor's binaries with its own build by sha256, runs the null gate, and commits. (2) Gates are scoped by what changed. Task 1 edits code the loader runs and runs the whole engine gate set. Task 2 edits the module alone: the module's gate set with the twin. Tasks 3 and 4 edit the editor alone: the build, the tests, the smoke, the roundtrip gate and the editor's own arms, and the command-line binary must stay byte-equal to Task 2's. Tasks 5 and 6 change what every host reads through: the whole engine gate set, each time. (3) The mutation run is sized to what this checkpoint adds. (4) One review wave. Tasks run one at a time: one tree, one build. Usage at plan time, Friday 2026-11-20 at 12:07 HST: the weekly Fable window 81 percent (72 at the session's start: the survey cost four points and this plan five, both at xhigh), all-models 61, both resetting Saturday 2026-11-21 at 02:00 HST. 4.6 cost twenty Fable points to execute; this checkpoint has one more code task and its largest task is in the editor, where the reading is slowest, so twenty to twenty-four is the guess. It does not fit this week's window: execute after the reset, in a fresh session on Fable 5.2 at xhigh, with the prompt "Execute docs/superpowers/plans/2026-11-20-part4-checkpoint-4.7.md. Go."

**Tech Stack:** C++20 (MSVC, CMake 3.20+, `/fp:precise`), `engine_tests` (12,184 checks at the base, 85 s, no filter), Python 3.11 (`unittest`, `tools/tests/`, 353 at the base), `python tools/gates.py`, `null_gate_perform_source.py`, `ab_render_set.py`, `ab_render_time.py`, `alloc_gate.py`, `comp_template_null_gate.py`, `test_stable_roundtrip.py`, `rt_smoke.py`, `twin_gate.py` (until Task 6), `structure/check.py`, `convert_patch.py`.

**Spec:** `docs/architecture/part4-patch.md` §8.1 row 4.7, §8.2 to 8.6; §7.1 to 7.6 (the seam, the command-line tool, the editor's layer, the tests, the Python, the headless modes); §9.1's two 4.7 rows, §9.4; §3.1, §3.6, §4.7 (the records, the layout, the writer's discipline); §5.10 (the registry). 4.6's report R21 and its Ledger. REVIEW R21's question. Matt, 2026-11-02: easily-tweaked decisions are the session's. Matt, 2026-11-20: "Yes, at xhigh" to writing this plan on three readings (Ledger 1, 2, 3), which stay his to overturn at the go-ahead.

## Global Constraints

- **Byte-identity is total in scope, through whichever reader a host uses.** The A/B over the 131 against `renders/scratch/ab47/old/hashes.txt` → 0 DIFFER, under the loader (Tasks 1 to 4) and under the module (Tasks 5 and 6). `python tools/null_gate_perform_source.py --jobs 3` → 77/77, the manifest untouched.
- **Nothing is written under `patches/` or `scores/`.** A file on disk keeps its bytes; what a save through the codec would write is compared in scratch.
- **Everything the loader's path touches moves once, first (Task 1), and is proved under the loader.** After Task 1 no task edits a node, the voice or the mixer.
- **The loader stands until Task 6.** After Task 5 nothing but the twin gate names it; the gate's last run is on that tree.
- **A record the editor is handed comes back as it was.** Headless, with no edit made: the text the codec writes for `patch_from_editor()` equals the text it writes for the record `editor_from_patch()` was given, for every file the codec reads. A difference is a finding about the layer, never an entry on a list.
- **The editor's model stays.** `GraphNode`, the links, the groups, the edit functions, the mode flag, the stream and its pan law, the Note device, the two special editors and the internal clipboard are Part 7's or 4.8's. The layer is two functions and what they need; nothing else in the editor is redesigned.
- **A new refusal is asked "what does it refuse that the base played"**, with `cli_pre47.exe` or `ui_pre47.exe` and a file written for it.
- **A proof is shown failing once before it is called a proof**, and that goes for a gate: the roundtrip's comparison, each arm of the editor's sound comparison and each new smoke case is seen to fail on a fault put in for the purpose.
- **No test pins a patch count.** The count 131 lives in the checkpoint's scratch gate scripts alone.
- **Budgets for new code**: a function at most 80 lines; no new file-level mutable variable in the engine; in the editor the new statics replace old ones one for one and the count does not rise. The layer lives in `tools/mforce_ui/main.cpp` (Ledger 12); that file is over the 600-line limit already and gets shorter.
- **The real-time rule**: nothing on a render path changes after Task 1, and Task 1's edit there is a `resize` in a control-side hook.
- **No feature work.** The editor gains no behaviour but those the Ledger's italics name, each of which is the removal of a way it changed a file. A shape that fails to load today is refused by name and not made to work.
- **Every commit builds every target.**
- **No executor commits or stages.** Edits through script files that keep each file's line endings; one `cmake --build` at a time; the gate scripts stop at a failed build; a set's file list is checked against its folder; the reviewer reads code only at the commits while a mutation run edits the tree. Never `cd`; an executable's path given to `subprocess` is absolute.
- **Comments say what the code does now**: no date, backlog id, REVIEW, Part or checkpoint number, and no history.
- **Which meter numbers may move, and why**: `dynamic_cast` falls (the loader's four and the tests' nineteen go; a new one needs the coordinator's word, and holding the count with a `static_cast` on a type's name is forbidden); `main` and `draw_properties_panel` move and are retaken; duplicate blocks are reported by entry, not by count.
- **Dates:** `date` first; this plan is dated 2026-11-20.

## Review Focus

The refuter's oracle. Each row names the task that pins it.

1. **The ground moved nothing** (Task 1): the over-read's mend against the loader's render of the 131, the nine and the sets; a column that arrives alone leaves every column of the layout at one length; the mark on three settings and no other; the descriptor dump different in three lines.
2. **The record holds the node's answer** (Task 2): each of the three settings in each case of the table below; a read is idempotent; a ragged table is still refused by `check()`'s sentence and no node throws first; `check()`'s range rule passes over a derived setting and nothing else; the twin gate's every arm unmoved.
3. **The layer is exact** (Task 3): each row of the translation table in both directions; headless, nothing the record did not say is written and nothing it said is dropped; the nine kinds of fact 2 each closed and each held by a fixture that fails with its line put back; what the layer cannot hold is refused before the open graph is touched.
4. **The editor's live nodes are what they were** (Task 3): `--dump-stream` over the scope against `ui_pre47.exe`; settings reach a node before its arrays; a Curve's six columns in one push.
5. **The view's facts** (Task 3): which facts are the view's (a position the record did not give, the panning, a packed face); that a running editor writes them and a headless one does not; that these branches are read, not run, and the report says so.
6. **The editor's sound is the module's** (Task 4): the six loads; the copy that is built takes the device's rate, and under a Listen tap the tapped node as its output at unity volume; `InstrumentPatch` gone from the editor with every voice still owning its instrument; the build's warnings shown.
7. **Every other host on the module** (Task 5): the command-line tool's default render byte for byte and its stderr line for line but for the warnings; the seven Conductor modes; the explorer; `alloc_gate`; the tests' eighteen rows each an asserted refusal or a rewritten patch, none dropped; every sentence the loader's tests pinned now pinned on the module; the nineteen faults.
8. **The deletion is whole** (Task 6): nothing names the loader, a configurator, `InstrumentPatch`, the lint or the twin gate; `source_registry.h` includes no JSON; the module map and the allowlist say so.
9. **The roundtrip gate is a fair instrument**: what it compares, its control (a fixture that must differ), what it counts, and what it cannot hear (the running editor).
10. **The ten-item brief** (`docs/architecture/REVIEW_BRIEF.md`) over the whole diff, with line citations, and every report sentence checked against the code. The coordinator's own claims, Architecture (1) to (11) and Ledger 1 to 24, go into the brief as numbered items to break.

---

## The interfaces

### The mark (Task 1)

```cpp
// core/pins.h
// Where a Setting's number comes from. Stored: the setting holds what it was
// given. Derived: the node answers it from its arrays whenever they are set
// (a count from its rows, a level from its stage table), so a number given
// beside those arrays is overruled.
enum class SettingOrigin { Stored, Derived };

struct SettingBase : PinBase {
  bool structural;
  bool derived;                                   // SettingOrigin::Derived; fixed at declaration
  ...
};

template <class T> struct Setting final : SettingBase {
  ...
  // A setting the node answers from its arrays.
  Setting(Component* owner, const char* n, T def, T mn, T mx, SettingOrigin origin);
};

// core/descriptors.h
struct SettingDescriptor {
  ...
  bool structural = false;
  // The node answers this setting from its arrays whenever they are set: a
  // number given beside those arrays is overruled.
  bool derived = false;
};

// patch/type_declarations.h — TypeDeclarations::Setting gains
  bool derived{false};                                   // the node answers it from its arrays
```

Three declarations take it: `Envelope::sustainLevel`, `ExplicitPartials::maxPartials`, `LayeredRedNoiseSource::count`. `--dump-descriptors` prints `"derived": true` on those three and no key on any other.

### The record holds the node's answer (Task 2)

```cpp
// engine/src/patch/node_fill.h — internal to the module.
// A node's settings in declaration order (a string through set_string), then
// its arrays in declaration order, each taken from the record where the
// record holds it. The one order every reader of a record fills a node in.
void fill_settings_and_arrays(Component& node, const Node& record);
```

`build_graph.cpp`'s function of that name moves there without its table rule, which the builder goes on applying after the call. `node_json.cpp`'s `complete()` ends with one more step, for a type that declares a derived setting: where the record's arrays keep the table rule, a scratch instance from the registry is filled through `fill_settings_and_arrays`, and each derived setting in the record takes `get_setting`'s answer. `Patch::check()`'s range rule passes over a derived setting.

| Type | The file | The record after a read |
|---|---|---|
| `Envelope` | four-slot columns, no `sustainLevel` | the slots' value |
| | four-slot columns, a `sustainLevel` that disagrees | the slots' value |
| | columns of another shape, any `sustainLevel` | 0 |
| | preset `adsr`, the knob given or not | the knob, or 0.7 |
| | preset `as`, `asr`, `ads`, `adr`, `ar`, the knob given or not | 0 |
| `ExplicitPartials` | rows, with or without `maxPartials` | the row count |
| | no rows | the file's `maxPartials`, or 16 |
| `LayeredRedNoiseSource` | rows, with or without `count` | the row count |
| | no rows | the file's `count`, or 3 |

### The editor's state

```cpp
struct GraphNode {
  ...
  // Where this node sat in the record it was read from; -1 for a node made
  // in the editor. A save writes the record's nodes in the record's order
  // and the nodes made since after them.
  int recordIndex{-1};
  // The arrays this node's record holds: the ones a record gave, and any the
  // editor has changed since. A node made in the editor holds every array it
  // shows that is not empty, and an Envelope its eight stage columns.
  std::set<std::string> heldArrays;
  // Output node: the instrument's volume. The panel has no row for it.
  float volume{1.0f};
  // Note face: field -> where that field's file node sat in the record.
  std::map<std::string, int> perfFieldIndex;
  // Note face: the load made this face for PerformNodes the record gave no
  // face. A view writes it like any face; headless it is not written.
  bool facePacked{false};
};
// Gone from GraphNode: jsonExtras, glideMsSet, paramName, paramNameBuf.

static int s_patchSampleRate = 0;                 // the open patch's own rate; 0: a graph made here, which takes the device's
static std::string s_loadedScore;                 // the score as the record's JSON text; empty: none
static std::optional<double> s_loadedSeconds;
static std::optional<ImVec2> s_recordPanning;     // the record's panning, for a save with no view to ask
// Unchanged: s_loadedScoreAbsent, s_loadedComment, s_loadedOutputNode.
// Gone: s_loadedInstrumentExtras.
```

### The layer (Task 3)

```cpp
// What this editor cannot show of `patch`, as a sentence; empty when it can
// show all of it. Asked before the open graph is touched.
static std::string editor_refusal(const mforce::Patch& patch);

// The editor's graph made from `patch`, over a cleared editor. Places
// nothing: a node the record gives no position has none.
static void editor_from_patch(const mforce::Patch& patch);

// The record of the editor's graph. With `ids`, also each GraphNode's id in
// the record. Headless it holds what the record held and what was edited; a
// running editor adds what only a view knows (Ledger 3).
static mforce::Patch patch_from_editor(std::unordered_map<int, std::string>* ids = nullptr);

// The record the engine builds for playback: the editor's graph at the
// device's rate, and under a Listen tap the tapped node as its output at
// unity volume. Never saved.
static mforce::Patch playable_patch(bool listenTap, std::unordered_map<int, std::string>* ids = nullptr);

// The strip pass: every node with no position laid out below what has one.
// The load's own code, moved; a running editor's load calls it, and
// --dump-layout.
static void place_unpositioned_nodes();
```

**The translation, row by row.** Left to right is `editor_from_patch`; right to left is `patch_from_editor`.

| The record | The editor's state | Reading | Writing |
|---|---|---|---|
| `graph.nodes`, in order | `s_nodes`; `recordIndex`; a face's `perfFieldIndex` | a node per record node that is not a PerformNode, in order; each index kept | by index; then the nodes made since in `s_nodes` order; then a face's new field nodes |
| `Node::id`, `type` | `label`, `typeName`, `synthesizedId` | as given | `sanitize_unique_id`, as today |
| `seed`, `comment` | `jsonSeed`, `comment` | as given | as held |
| `values` | a pin's `defaultValue`, its `constantSrc`; `hasConstant` on an input | each number onto its pin | a Param with no link: its value; an input with no link: its value when `hasConstant` |
| `wires` | links to an out pin or a tap pin; to a face's field pin for a PerformNode's id | a link per feed, a pin's feeds in order | per pin in `s_links` order; `tap` from a tap pin |
| `settings`, `strings` | `settingValues`, `stringValues` | from the record, then pushed to the node (`apply_config`) before any array; then every setting read back from the node | every one, `sustainLevel` among them |
| `arrays` | `arrayValues`; `heldArrays` | the record's arrays cached and pushed, a Curve's six columns in one `push_curve`; `heldArrays` the record's names; an array the record does not hold is pulled from the node afterwards | the held ones |
| `dynamicPins` | the JSON object | `{setting: {"ref": id}}` | each entry that holds a ref; a `null` (promoted, not wired) is not written |
| a PerformNode: id, `field`, comment | a face's `perfFieldIds`, pin name, `perfFieldComments` | consumed into its face | a node for every entry of `perfFieldIds`, wired or not; a wired pin with no entry takes a fresh `__perf_<field>` |
| `graph.output` | the link into the Output node; with no block, the Mixer or `s_loadedOutputNode` | as today | as today |
| `instrument` | the mode; the Output node's `polyphony`, `volume`, `sustaining`, `onsetsBuf`, `glideMs` | all five | all five |
| `layout.positions`, `layout.output` | `gridPos`, `gridPosKnown` | the record's; a position under a PerformNode's id has nowhere to go | headless: the known ones; a view: every node's (`layout_position`) |
| `layout.panning` | `s_recordPanning`; the view | kept, and given to the view | headless: the record's; a view: its own |
| `layout.groups` | `s_groups` | name, members, position | the same |
| `layout.faces` | Note nodes: `label`, `perfFieldIds`, `gridPos`; `facePacked` | a face per record face; then each PerformNode no face names onto the first *packed* face whose slot for its field is free, else a new packed face | a face that is not packed; a view writes the packed ones too |
| `sampleRate` | `s_patchSampleRate` | kept | the patch's; a graph made here: the device's |
| `seconds`, `score`, `comment` | `s_loadedSeconds`, `s_loadedScore` with `s_loadedScoreAbsent`, `s_loadedComment` | kept; the score's first note sets the transport, as today | as held; an instrument made here gets the one-note score and `seconds` 3, as today |

`editor_refusal` answers for the Output panel's one text box: `onsets` joined by ", " longer than the box holds, or a name that is empty, holds a comma, or begins or ends with a space. No file of the four folders meets it.

### The hosts (Tasks 4 and 5)

```cpp
// tools/mforce_cli/patch_file.h — the tool's two ways in from a file. Each
// prints what the build had to say on stderr, one line a warning.
struct FileInstrument { std::unique_ptr<mforce::PitchedInstrument> instrument; int sampleRate{48000}; };
FileInstrument instrument_from_file(const std::string& path, int minPolyphony = 0);
mforce::BuiltSound sound_from_file(const std::string& path);

// tools/engine_tests/main.cpp — the tests' one door.
struct Loaded { std::unique_ptr<mforce::PitchedInstrument> instrument; int sampleRate{48000}; std::vector<std::string> warnings; };
static mforce::Patch read_text(const std::string& json);
static Loaded load_inst(const std::string& json, int minPolyphony = 0);
static mforce::BuiltSound load_sound(const std::string& path);
static std::string load_refused(const std::string& json);   // the sentence a read or a build refuses with; empty when it loads
```

In the editor a live voice owns `std::shared_ptr<mforce::PitchedInstrument>` where it owned an `InstrumentPatch`; the rate is the editor's own context's, which is the rate `playable_patch` builds at.

---

## File structure

| Old | New |
|---|---|
| `engine/include/mforce/source/additive/explicit_partials.h` | Task 1: `init_arrays()` resizes `layout_.mult2`, `ampl1` and `ampl2` to `mult1`'s length, a missing row taking that column's row default, before it computes the offsets. Nothing else |
| `engine/include/mforce/core/pins.h`, `descriptors.h` | Task 1: `SettingOrigin`, `SettingBase::derived`, one constructor, `SettingDescriptor::derived`, as the Interfaces give them |
| `envelope.h:62`, `explicit_partials.h:16`, `layered_red_noise_source.h:19` | Task 1: each declaration ends `, SettingOrigin::Derived}` |
| `engine/include/mforce/patch/type_declarations.h`, `engine/src/patch/type_declarations.cpp`, `tools/mforce_cli/main.cpp` (the dump) | Task 1: `Setting::derived`, read from the descriptor, printed where true |
| `engine/src/patch/build_graph.cpp` | Task 2: `fill_settings_and_arrays` leaves for `engine/src/patch/node_fill.h` and a new `node_fill.cpp`; the builder calls it and then applies the table rule |
| `engine/src/patch/node_json.cpp`, `patch.cpp` | Task 2: `complete()` settles the derived settings; the range rule passes over them |
| `tools/mforce_ui/main.cpp` | Task 3: the state above; `editor_refusal`, `editor_from_patch`, `patch_from_editor`, `playable_patch`, `place_unpositioned_nodes`; `load_graph_from_path` is `load_patch`, the refusal, then the graph; `save_to_path` is `patch_from_editor`, `save_patch`, then `check()`'s word on the status line; the four `serialize_patch_graph` sites hand the loader `nlohmann::json(playable_patch(…)).dump()` until Task 4; the text clipboard stages in memory; the conversion pair's stash follows the new types; `--roundtrip` compares and takes `--after-refusing <file>`; `--dump-layout` calls the strip pass. **Deleted:** `load_graph_json`, the load's refusal prelude, `enum_column_element`, `perform_field_index`, `setting_to_json`, `array_to_json`, `layout_groups`, `serialize_patch_graph`, `save_patch_graph`, `save_node_graph`, `get_playback_patch_path`, `jsonExtras` with its five erasures and the walk over it in `rewrite_label_refs`, `s_loadedInstrumentExtras`, `glideMsSet`, the two clipboard temp files |
| `tools/mforce_ui/main.cpp`, `theme.h` | Task 4: the six loads through `build_instrument`; `InstrumentPatch` out of `Voice`, `voice_schedule`, `voice_schedule_unlocked`, `acquire_pool_slot`, `get_cached_instrument`, the dying list and the drum kit's holder; the chords render's resample branch, which nothing reaches; the build's warnings to stderr and the first to the status line; one `register_all_sources()` at the top of `main` for the thirteen; `Parameter` gone (its type name, its pins, its panel, its menu item, its theme colour, twenty-three references); `patch_loader.h` no longer included |
| `tools/mforce_cli/main.cpp`, `explore.cpp` | Task 5: `patch_file.h`; the ten sites and the explorer's two; the default render prints the build's warnings before its timing lines |
| `tools/alloc_gate/main.cpp`, `tools/test_figures/main.cpp` | Task 5: `build_instrument(load_patch(path))`, `build_sound(load_patch(path))` |
| `tools/engine_tests/main.cpp` | Task 1: the over-read, the mark. Task 2: the settle. Task 5: the helper on the module alone; the table's eighteen rows resolved; the twenty-three direct loader calls; the generated twin without its loader arm; one `register_all_sources()`; no `dynamic_cast<PitchedInstrument*>` |
| `tools/rt_smoke.py`, `tools/test_stable_roundtrip.py` | Task 3: the cases below; the gate reads `--roundtrip`'s exit code and renders a pair only where a record came back different |
| `engine/include/mforce/render/patch_loader.h`, `engine/src/patch_loader.cpp` | Task 6: deleted |
| `engine/include/mforce/core/source_registry.h`, `engine/src/source_registry.cpp`, `engine/src/source/source_registrations.cpp` | Task 6: `JsonConfigurator`, `ResolveParamFn`, `get_configurator`, the JSON include, `register_type`'s fourth parameter and `FormantSequence`'s configurator deleted |
| `engine/include/mforce/source/combiner/multiplex_source.h` | Task 6: the three-argument `set_template`, `templateJson_`, `baseSeed_` deleted |
| `tools/lint_patches.py`, `tools/twin_gate/`, `tools/twin_gate.py`; `tools/bisect_roundtrip_diff.py` once the six known differences have closed | Task 6: deleted, with every test that imports the lint and the meter's `kBranchKeys` test |
| `engine/CMakeLists.txt`, `CMakeLists.txt`, `tools/structure/modules.json`, `docs/architecture/MODULES.md`, `tools/alloc_probe/CMakeLists.txt` (a comment) | Task 2: `node_fill.cpp`. Task 6: the loader's source, the twin gate's subdirectory, `render`'s path, the two allowlist entries, the `tests` module's two paths |

---

### Task 0: Date, base, the arms, the scripts, the fixtures (Opus 5.5)

- [ ] `date`; sweep stale `mforce_ui_*.exe`; the editor not running; `git status -sb`. Commit this plan with the STATUS handoff that points at it (`git add docs/superpowers/plans/2026-11-20-part4-checkpoint-4.7.md docs/autonomy/STATUS.md`, never `GOALS.md`): "docs: checkpoint 4.7's plan; STATUS". Record the sha as the base. Build; `gates --fast` (the base's result recorded verbatim).
- [ ] **The base binaries.** Both sha256 must equal Architecture (1). Equal: copy them to `renders/scratch/ab_tree/cli_pre47.exe` and `ui_pre47.exe`, and the old arms are 4.6's with no render, copied under `renders/scratch/ab47/`: `old/hashes.txt` with `library_baselines_131.txt`; `scoreless/`; `synth/` (108, with `what.txt` and a `set.txt` checked against the folder); `synth45/`; `synth44/`; comp stage `post46`. Not equal: say why before anything else.
- [ ] **The gate scripts**, copied from `ab46/` and repointed: `engine_gates.py`, `module_gates.py`, `coord_commit.py`, `briefs/rules.md` with this plan's Global Constraints. Mended: a set whose folder holds a file its list lacks fails the run. New, `editor_gates.py <stage>`: the build, `engine_tests`, ctest, the tool tests, `rt_smoke.py`, the roundtrip gate over library and baselines, the editor's three arms against their old hashes, and both binaries' sha256. Shown to stop at a failed build.
- [ ] **The two planning scripts rerun** from `renders/scratch/ab47/planning/` into `t0/`: every number of Architecture (2), (3) and (4). A number that differs from this plan is reported, not reconciled.
- [ ] **The editor's old arms**, rendered once with `ui_pre47.exe`, three jobs, each WAV hashed: `--dump-stream <file> <wav> --secs 2` for each of the 131 and the nine; `--gencheck <file> <wav>` for each in-scope instrument that holds a score, its strip lines kept; `--dump-playback <file> <wav> --keyboard` for each of the 104 instruments and the nine; `--gatecheck <file>` for the same, its line kept. Five files of each arm rendered twice: an arm that does not repeat is not an instrument for that file and is reported.
- [ ] **The layer's fixtures**, `renders/scratch/ab47/synth_ui/make.py`, each a small file in the codec's form that `cli_pre47.exe --check` reads, with `what.txt` giving each row's verdict (`SAME`, `DIFFERS` with the path that differs, `REFUSED` with the sentence's fixed words) and `set.txt`:
  - (p) *order, faces, positions*: PerformNodes after other nodes and out of field order; a PerformNode no wire reads, and one only a dynamic pin reads; two PerformNodes of one field with no faces; faces that name some PerformNodes and not others; a face with no fields; faces with no `pos` beside nodes with positions; positions for some nodes and not others; `panning` with no positions; a group of a node, a face and a group.
  - (r) *rate, length, score*: an instrument at 22050; a score and no `seconds`; `seconds` and no score; `"score": []`; a plain sound with no `seconds`; a plain sound that holds a score.
  - (b) *the block*: all five fields off their defaults with three onsets; onsets too long for the box (`REFUSED`); an onset name that holds a comma (`REFUSED`).
  - (a) *arrays and the three settings*: a Curve with `x` and `y` present and empty and its expression columns given; an `ExplicitPartials` with `maxPartials` and no rows, with rows and no `maxPartials`, and with rows and a `maxPartials` that disagrees; a `LayeredRedNoiseSource` likewise; an Envelope whose four sustain slots hold 0.9 beside a `sustainLevel` of 0.3; an `as` preset that gives the knob; an Envelope whose sustain slots hold 2.6; a Shaper with none of its arrays; a `MultiSource` with `weights` and no `delays`.
  - (d) *the pins*: a number on an input; a tap into a Param and into a list; a list of one feed; a dynamic pin with its setting's own number beside it; a node with a seed and a comment.
  - (c) *the control*: a file with a position under a PerformNode's id, which the layer has nowhere to keep (`DIFFERS` at `layout.positions`).
  Each (a) row that Task 2 changes says so: its record before and after.

### Task 1: The ground (Opus 5.5; the coordinator reads the whole diff)

- [ ] **RED first.** *The over-read*: on a fresh `ExplicitPartials`, after `set_array("mult1", …)` with forty rows and nothing else, every column of the node's layout holds forty rows (through whatever public face the layout has; a test-local subclass where it has none), and a thousand samples render; the same after each of the other three columns arrives in turn; after all four, the layout is the one the four columns give, cell for cell. *The mark*: over every registered type, the settings whose descriptor says `derived` are exactly `Envelope.sustainLevel`, `ExplicitPartials.maxPartials` and `LayeredRedNoiseSource.count`; the declarations table says the same; a derived int is still structural and `sustainLevel` is not.
- [ ] The edits of the File structure's first four rows and nothing else.
- [ ] `--dump-descriptors` against the base's: three lines. The tool tests; the converter's `--check` over the four folders.
- [ ] The engine gate set (stage `t1`): the 131, the nine, the three synthetic sets, the comp gate, the allocation gate, the roundtrip gate at its six known. The twin gate, every arm, over the 131. The binary copied to `renders/scratch/ab_tree/cli_t1_47.exe`. Commit: "nodes, core: an ExplicitPartials keeps its columns at one length while they arrive; a setting its node answers from its arrays says so".

### Task 2: The record holds the node's answer (Opus 5.5; the coordinator reads the whole diff)

- [ ] **RED first.** Every row of the Interfaces' table through `read_text`, for each of the three types. A second read of a saved read is equal and its text byte-equal. A node whose table is ragged is refused by `check()`'s sentence, and its type's hooks are not run on it. A fragment's node is settled as a patch's. A derived setting outside its declared range reads (the sustain slots at 2.6; seventeen rows on a `LayeredRedNoiseSource`, whose `count` is declared 1 to 16), and a setting that is not derived is refused there as before. A record built through the builder from a settled read and from the same record with the file's own numbers put back ends in the same node: its settings and its arrays read back equal for the three types.
- [ ] `node_fill.h` and `node_fill.cpp`; the builder's call; `complete()`'s last step; `check()`'s exemption.
- [ ] `python renders/scratch/ab47/module_gates.py t2 --check --twin`: the twin gate's every arm `SAME` over the 131 with its counts unmoved; `--check` over the four folders at four refused; 4.6's synthetic set against `what.txt` with no row departing. `twin_gate --resaved` over the 131 into `renders/scratch/ab47/resaved/`: the reference every later roundtrip is compared with. How many records the step changed, by a diff of that folder against 4.6's: Architecture (4) says 89 Envelopes and 3 `ExplicitPartials`. `--check` over audition and pending timed against 4.6's 14 s.
- [ ] Commit: "patch: a record holds, for a setting its node derives, the node's answer".

### Task 3: The editor's translation layer (Opus 5.5; the coordinator reads the whole diff)

- [ ] **RED first: the proof mode.** `mforce_ui --roundtrip <patch> <out>`: `load_patch`, `editor_from_patch`, `patch_from_editor`, `save_patch(out)`; then the two records' texts as the codec writes them, compared. Exit 0 and `roundtrip ok` when equal; exit 2 and one line naming the first path that differs with both values when not; exit 1 when the file is refused, with the sentence. `--after-refusing <other>`: after the load, `<other>` is opened too and must be refused (exit 3 if it loads); the graph that is saved is still the first file's. The comparison is written first, over the old path (the old load, the old save, the saved text read back through the codec), and run over `renders/scratch/ab47/resaved/` and `synth_ui/`: it must say different for all but a handful of files, as Architecture (2) measured, and name a path for each. That run is the comparison's RED; the layer then turns it green.
- [ ] **The state and the two functions**, as the Interfaces and the translation table give them, each function at most 80 lines (a node read, a node written, the faces, the layout and the block are each their own). `editor_from_patch` ends with `update_all_dsp()`. `load_graph_from_path`: `load_patch(path)`, then `editor_refusal`, both before `stop_playback()`; then the cleared editor and `editor_from_patch`; a running editor then calls `place_unpositioned_nodes()` when the record gave any position and sets `s_needsLayout` when it gave none. `save_to_path`: `patch_from_editor()`, `save_patch`, then `check()`: a record that breaks a rule is saved, and the status line says in red what a reload will refuse (Ledger 9).
- [ ] **What the edits owe the new state**: `push_array`, `push_table` and `push_curve` add what they push to `heldArrays`; `pull_arrays` adds what it pulls that is not empty; `clip_instantiate` and `replace_node_with` add each array they set on the node directly; `rename_node` clears `facePacked`; the Output panel's `glideMs` row loses its flag; the internal clipboard's `clip_node_state` and `clip_instantiate` keep their schema and lose `paramName`.
- [ ] **The transitional sound path**: the four `serialize_patch_graph` sites become `nlohmann::json(playable_patch(tap, &ids)).dump()` handed to the loader call that is there. Task 4 replaces those calls.
- [ ] **The text clipboard** in memory: copy takes its nodes and positions from `nlohmann::json(patch_from_editor())`; paste merges into that document as today, reads the result with `from_json`, and only then replaces the graph, so a paste the codec refuses leaves the graph as it was and puts the sentence on the status line. The fragment's own format is unchanged.
- [ ] **The deletions** of the File structure's Task 3 row. `grep` for each deleted name afterwards: none.
- [ ] **`rt_smoke.py`**: the six shapes judged by `--roundtrip`'s exit code and a render pair. The cases restated against the codec: the fourteen refusals keep their needles, each sentence now opening with its path; *list-number* is refused by the command-line tool's `--check` too; *note-field* takes `1` and writes `velocity`, and refuses `"Velocity"` with the labels' sentence; *settings-order* comes back with `sustainLevel` 0.9; *preset-form*, *table-labels*, *empty-table*, *plain*, *two-channels*, *layout*, *comment*, *bare-output*, *string-setting*, *list-weights*, *apply-preset*, *make-instrument* and *plain-stream* as they are. New: *control* (the (c) fixture must exit 2 naming `layout.positions`); *refused-open* (`--after-refusing` with a file of an unknown type saves the bytes the plain roundtrip saves). **`test_stable_roundtrip.py`**: per file, `--roundtrip`; exit 0 is the verdict; exit 2 prints the path and renders the pair to say whether the sound moved; the id comparison and its dead exclusion go, here and in the smoke's `ids_of`; the summary counts files, identical, different and refused, and a run that compared no file fails.
- [ ] **The proofs.** `--roundtrip` over library and baselines: every file exit 0, the six known among them, or each that is not is reported with its path before anything is changed. Over the nine, `synth44/`, `synth45/` and 4.6's `synth/` (every file the codec reads: exit 0), and `synth_ui/` against `what.txt`. The editor's arms against Task 0's hashes: `--dump-stream` over the 131 and the nine; `--gencheck`; `--dump-playback --keyboard`. A file whose arm moved is reported with its cause; among the six known, a moved `--gencheck` is expected where the old saver changed the patch, and is compared with the command-line tool's render of the same file. The command-line binary byte-equal to Task 2's. The meter: the two known answers retaken; no new function over 80 lines.
- [ ] `python renders/scratch/ab47/editor_gates.py t3`. Commit: "ui: the editor reads and writes a Patch through one translation; its loader's parsing, its two savers and jsonExtras deleted".

### Task 4: The editor builds through the module (Opus 5.5; the coordinator reads the whole diff)

- [ ] The File structure's Task 4 row. Generate, the live cache and the chords render build `playable_patch(…)`; the drum kit and `--gatecheck` build `load_patch(path)`; `--dump-playback` builds `playable_patch(false)`. The warnings of a build go to stderr, each on its line, and the first to the status line when the build raised no error.
- [ ] **RED first**, as headless runs: `--gencheck` on a patch whose `KSString` a bend cannot reach prints the build's warning once; `--dump-playback --keyboard` on a 22050 Hz baseline writes a 48000 Hz file whose samples are Task 0's.
- [ ] **The proofs.** The editor's arms against Task 0's hashes, all four; the roundtrip over the scope still exit 0 everywhere; `rt_smoke.py`. `grep -n "InstrumentPatch\|patch_loader\|load_instrument_patch\|NT_PARAMETER\|register_all_sources" tools/mforce_ui/main.cpp`: one line, the one call. The command-line binary byte-equal to Task 2's.
- [ ] `python renders/scratch/ab47/editor_gates.py t4`. Commit: "ui: the editor's six builds go through the module; InstrumentPatch and the Parameter node leave it".

### Task 5: Every other host on the module (Opus 5.5; the coordinator reads the tests' diff whole)

- [ ] **The command-line tool**: `patch_file.h`; the default render through `sound_from_file`, its timing lines, its `Wrote:` line and its statistics as they are, `report_faults` over the mixer; the seven Conductor modes through `instrument_from_file`; the explorer's two loads. **`alloc_gate`** and **`test_figures`**.
- [ ] **The tests.** `load_inst`, `load_sound`, `read_text` and `load_refused` as the Interfaces give them; `namespace reader_twin` keeps `live_c4` and `render_sound` and loses the table, `judge` and both twins. **The table's eighteen rows**: each becomes a `load_refused` assertion with its needle in the function that owned the load, or, where that function is about something else, its patch takes the exact label; the split, row by row, is put to the coordinator before the commit. **The direct loader calls**, twenty-three outside the helper: each that pins a sentence becomes `load_refused`, its needle kept where the module says the same words, and where it does not the module's words from 4.6's Refusals table, the path included; each that is the loader's arm of a comparison goes with the comparison, and what the comparison held of the module stays as an assertion on the module alone. The generated twin keeps its second and third properties. One `register_all_sources()` at the top of `main` for the fifteen. The nineteen casts go with `Loaded`.
- [ ] **The nineteen faults** (`renders/scratch/ab46/t6a/mutations.py`: 6, 8, 10, 11, 49, 56, 57, 58, 67, 69, 77, 82, 91, 92, 93, 94, 95, 96, 109), each applied to the tree as it now stands, the tests run, the tree restored: every one caught, or it gets its test in this task and is shown caught.
- [ ] **The proofs.** The engine gate set (stage `t5`), **through the module**: the 131 against the old arm → 0 differ; the nine; `synth44/`; `synth45/`; 4.6's `synth/` judged by `what.txt` (a `SAME` row at its old hash; a row the module refuses, refused with its needle; a row the loader failed and the module reads, rendered and its hash recorded; `b2n` different from its old hash); the comp gate against `post46` → 21 same; the allocation gate; the null gate; the roundtrip gate. **The twin gate's last run**, every arm, on this tree. The default render's stderr over the 131 against the base's: the build's warnings where the loader's `[load` lines were, each file's listed. `--check` over the four folders. Timing against `cli_pre47.exe` over the 131, three rounds, twice, reported. The binary copied to `cli_t5_47.exe`.
- [ ] Commit: "cli, tools, tests: every host builds through the patch module".

### Task 6: The loader goes (Opus 5.5)

- [ ] The File structure's Task 6 rows. `grep -rn` over `engine/` and `tools/` for `patch_loader`, `load_patch_file`, `load_instrument_patch`, `InstrumentPatch`, `JsonConfigurator`, `ResolveParamFn`, `get_configurator`, `kBranchKeys`, `lint_patches`, `twin_gate`, `templateJson_`, `baseSeed_`: none but in `tools/structure/metrics.json`, which the close retakes.
- [ ] The engine gate set (stage `t6`) without the twin; `module_gates.py t6 --check`; `editor_gates.py t6`; the meter under a temporary index, `--delta` against the base: `source_registry.h` with no JSON, no file on no module, the include edges listed.
- [ ] Commit: "render, core, tools: the loader, its configurators, the lint's scrape and the twin gate are deleted".

### Task 7: Close

- [ ] **Opus 5.5, the mutation run**, sized to what this checkpoint added; cheapest detector first (the roundtrip over `synth_ui/`, then the smoke, then the tests, then the editor's arms). The ground: the resize removed; the mark taken off each of the three. The settle: skipped; arrays before settings in `node_fill`; a setting that is not derived settled; the ragged guard removed; `check()`'s exemption widened to every setting. The layer, reading: a number not put on its pin; `hasConstant` not set; a tap read as a ref; a list's feeds reversed; arrays pushed before settings; a Curve pushed a column at a time; `heldArrays` left empty; a setting not read back; a seed, a comment, a face's comment dropped; `recordIndex` not kept; a straggler packed onto a record's face; the block's `volume` not kept; the rate not kept. The layer, writing: each row of the translation table dropped or altered in turn; a packed face written headless; a `null` dynamic pin written; an unwired field's node dropped; the device's rate written. `playable_patch`: the rate not overridden; the tap not applied; the volume not unity. The hosts: the warnings not printed; `Loaded`'s rate wrong. The gates themselves: `--roundtrip` comparing a record with itself; exit 0 on a difference; `--after-refusing` not opening its file; the roundtrip gate reading no exit code; an editor arm hashing the wrong file. A mutation nothing catches is a finding.
- [ ] **Opus 5.5 refuter**, beside the mutation run, read-only at the commits (brief `renders/scratch/ab47/review/brief_r22.md`: the ten Review Focus items, the Interfaces, the translation table, the Ledger, the base and the range; the blind surfaces — the running editor, whose view-only branches no command runs; a load during a stream; a paste inside an open group; the conversion pair; what a save does to a graph `check()` refuses). It writes files the old editor was lenient about and opens them in `ui_pre47.exe` and in the new one. **One fix wave** by an executor, its comment fixes with it.
- [ ] **Opus 5.5, the measurements**, once, on the fix wave's tree: `engine_gates.py post47` whole; `editor_gates.py post47`; `module_gates.py post47 --check`; the full `python tools/gates.py`; `--check` over the four folders; `structure/check.py --delta <base>`; timing against `cli_pre47.exe` over the 131 (three rounds, twice); `engine_tests`' time; the UI relinked and the stamp current.
- [ ] **Fable, docs:** report `docs/autonomy/refactor/reports/<the close's date>-checkpoint-4.7.md` (R22, ten sections: section 1 — nothing audible, nothing changed on disk; what changes in the editor, each as one line with what was and what is (Ledger's italics); what stops loading from the command line and in the editor, with the sentence, and what `--check` says of the four folders; the first save of a library file. Section 2 — the layer, drawn: a file followed from disk through the codec, the record, the editor's nodes and back, on `oboe1`; what is the view's. Section 10's lessons, as the code offers them — likely an enum that picks a constructor against a bool parameter and against C#'s named arguments; `std::optional` against a sentinel value and a companion flag; a value copied to be changed (`playable_patch`) against `Clone()`; shared ownership across the audio thread without a wrapper struct; an adapter between two models, and why it is two functions and not a class; a tool's exit code as its interface); Part 4 — §8.1's landed paragraph, §7.3 as landed, §8.3's roundtrip, §9.1's two rows, §3.1 and §4.2 with `derived`, §4.6's range sentence; Part 2's descriptor; REVIEW R22 and R21's question closed; BACKLOG row 3 (4.7 DONE; 4.8 next); CLEANUP — 28, 43, 64, 75, 76, 87, 103, 107, 109, 110, 113, 115 and 121 each as it now stands, 84 and 97 rebooked, the new rows; STATUS; the Ledger; `tools/structure/metrics.json` and `MODULES.md` retaken. Commit.

---

## Ledger

Rulings made in planning; execution notes are appended per task. *Italics mark what the plan asks Matt to notice before the go-ahead.*

1. *"Byte for byte" is read against the codec's own save of each file, not the file on disk.* The files on disk are sparse and stay as they are (4.9 Q5: a file becomes full on its first save), so no save through the codec can equal them: 4.6's `--resaved` arm found all 131 saved texts different from their files. What the row can mean, and what Task 3 proves, is that the editor hands back the record it was given. *What follows from 4.9 Q5 and will be seen: the first Save of a library patch from the editor rewrites it whole, every default written and a preset Envelope as its columns, one large diff per file.* The alternative is a pass that rewrites the tree into the codec's full form, which the row's "on disk: nothing" rules out.
2. *REVIEW R21's question is answered yes: a declaration marks a setting its node answers from its arrays, and the record holds the node's answer.* At 4.6 no consumer could tell the file's number from the node's and the read was "not now". The editor can: it shows the node's number and has no other to hand back, and they differ on 89 in-scope Envelopes and 3 `ExplicitPartials`. The cost is `SettingOrigin` with one constructor in `pins.h`, one bool on `SettingDescriptor` (Part 2's interface), three declarations, and one step in the codec's completion. The twin gate proves it harmless before the loader is deleted. *The alternative keeps the record as it is and gives the roundtrip proof a named exception for three keys; the file then keeps a number that means nothing and 4.8's operations and the explorer each have to know to ignore it.* A separate enum and not a third `SettingRole`: whether a setting takes a push and where its number comes from are two questions, and `maxPartials` answers both.
3. *What only a view knows is written by a running editor and not by a headless one.* Three facts: a position the record did not give (the strip pass, the first-frame grid), the panning, and a face the load packed for PerformNodes the record gave no face. A running editor's Save writes all three, as it does today: they are what the user sees. Headless there is no view, and the layer hands back what the record held. Positions and panning already work this way; the strip pass leaves the load for the running editor's load and `--dump-layout`, and a packed face carries a flag. So the roundtrip proves the layer on everything that reaches a sound or a record, and the view's branches are read, not run; the report says so.
4. *A file keeps its own sample rate through a Save.* Today the editor writes the device's rate over it, which is what moved the two 22050 Hz baselines' renders. The editor still builds and plays at the device's rate; the patch's rate is carried and written back. A graph made in the editor takes the device's rate, as today.
5. *A Save no longer adds `seconds` beside a score that had none*, and the block is written whole (an empty `onsets`, a false `sustaining`, `glideMs`). The first is what the record said; the second is the codec's complete block. An instrument made in the editor still gets the one-note score with `seconds` 3.
6. *What stops opening in the editor is what the codec refuses.* Report R21's section 1 becomes true of File > Open: each shape is refused with its path and sentence on the status line and the open graph is left alone. Of Matt's files it is the four in pending that do not render today; the three `GrayScottSource` files open today as inert nodes and will not. The refusals the editor's own loader made go with it; the codec makes each of them.
7. *A setting promoted to a pin and left unwired is not saved as promoted.* The record has no place for it (6.3: promotion is a connection) and the codec refuses the `null` the editor wrote for it. It stays promoted while the graph is open.
8. *From Task 5 a file from sweep, scratch or old that holds any spelling the codec does not know stops rendering from the command line* (8.4's last row, decided; `python tools/convert_patch.py` is the way back in, and the refusal names it where the retired table has a row), *and an explorer variant that puts a setting outside its declared range is refused by name* where the loader played it.
9. *A Save always writes, and says so when the record breaks a rule.* The editor can hold a graph `check()` refuses (a tap into a slot, CLEANUP 96). Refusing to save would hold the user's work hostage; saving in silence writes a file that will not reopen. The status line carries `check()`'s first sentence in red. Reversible in one place.
10. **The flip is three commits, not one.** Task 4 moves the editor's builds, Task 5 every other host, Task 6 deletes. After Task 5 the loader stands unreferenced for one commit, which is what lets the twin gate run its last time on the tree that no longer needs it. The row's concern, that no commit leaves hosts on two readers with a gate comparing neither, is kept: between Tasks 4 and 5 the editor is on the module and the rest on the loader, each proven against its own old arm.
11. **The editor's sound path changes in two steps, each a proof of one thing.** Task 3 feeds the loader the layer's record as text, so its arms prove the layer; Task 4 swaps the reader, so its arms prove the swap. Four one-line calls are written in Task 3 and replaced in Task 4.
12. **The layer lives in `main.cpp`.** The model it translates is file-local there, and moving the model to a header is Part 7's rebuild. The file gets about 900 lines shorter.
13. **Nodes are written in the record's order; `topo_sort` no longer orders a save.** The old loader needed a feed before its reader and the saver sorted for it; the builder sorts for itself. The record's order is the order dynamic pins are pushed and tails ticked in, so keeping it is also the safer rule. A node made in the editor goes after the record's.
14. **Every PerformNode the record held is written, wired or not.** Today's saver writes one per wired pin and drops the rest, which is how a Note only a dynamic pin reads was lost (CLEANUP 58's second half). A face with no fields is written as a face. A field the user unwires keeps its file node; the clutter is booked.
15. **A straggler is packed onto a packed face, never onto a record's.** Today a PerformNode its file's faces do not name claims a free slot on one of them, which changes that face on the next save (four audition files).
16. **`heldArrays` is a presence bit per array, because the record is not complete in arrays** (4.6's ruling 11) and a file tells an absent array from an empty one. An array the record does not hold is shown from the node and written only once edited.
17. **`check()`'s range rule passes over a derived setting.** Its number is the node's answer, and the declared range is the knob's: an Envelope's slots may hold 2.6 (`envelope.h` says so of a test envelope). Without the exemption the settle would refuse shapes the base plays. The four folders hold no derived value outside its range.
18. **One `register_all_sources()` per program**, at the top of the editor's `main` (for its thirteen) and the tests' (for their fifteen); the declarations table's own first ask covers every program that asks the table first.
19. **The command-line tool keeps a struct of its own for an instrument from a file** (`FileInstrument`): its seven Conductor modes read the rate beside the instrument, and Part 5 replaces all seven. It is the tool's, not the engine's.
20. **The text clipboard's two temp files go now**, ahead of 9.1's 4.8 row: the savers that wrote them are deleted here. The fragment's format and the internal clipboard are 4.8's.
21. **The roundtrip gate reads an exit code.** The comparison is made in the editor's process, where both records are. The gate's control is a fixture that must come back different; its summary counts what it compared.
22. **Booked, not done:** the Shaper's `morph` advisory (CLEANUP 84) to 4.8's sweep, so that this checkpoint edits no node after Task 1; `Envelope::replace_stages` and a trigger's constant (CLEANUP 111) to Part 7, since the editor still wires a constant there; converter rules for a `null` dynamic pin and a top-level `frames`, no file of the four folders holding either; a position under a PerformNode's id, which the layer drops (32 in audition, none in scope); an unwired field's leftover file node; an id that holds a `.`, which `sanitize_unique_id` would rename (none in the four folders); `Edit > Convert` losing `volume` (CLEANUP 54, 4.8); the running editor's view branches with no headless driver (CLEANUP 67).
23. **The old arms are 4.6's close arms, unrendered** (Architecture 1), and the editor's arms are new: Task 0 renders them once with the base editor.
24. **Matt's "Yes, at xhigh" (2026-11-20) is read as approval to write this plan on rulings 1, 2 and 3**, not as approval of them; every italic ruling waits for the go-ahead.

### Execution notes

**How this checkpoint is run (Matt, 2026-11-20).** Asked whether to start before the weekly reset, with the Fable window at 81 percent and the usage tool showing extra usage enabled, Matt asked: "Can you do 1 task at a time and wait for my go-ahead for each?" and then said "Start Task 0". So: **one task at a time.** The coordinator runs a task, verifies it, commits, and stops; nothing of the next task starts until Matt says so in the session. Each stop reports what the task did and what its gates said; any number that differs from this plan; the usage as the tool reads it then (weekly Fable, all-models, the 5-hour window, and the extra-usage field given as the tool's reading and never as a charge); the next task, the Ledger rulings it acts on, and a guess at its Fable cost. Before a task starts the coordinator reads the 5-hour window and says first if the task could fill it: with extra usage on, a full window bills where it would otherwise pause. **"Start Task 0" is the go-ahead for Task 0 alone.** The italic rulings are not approved by it: each is put to Matt at the stop before the task that acts on it (ruling 2 before Task 1; 17 before Task 2; 1, 3, 4, 5, 6, 7 and 9 before Task 3; 8 before Task 5). The guesses per task: Task 0 one to two points, 1 two, 2 two, 3 five to six, 4 two to three, 5 three to four, 6 one to two, 7 four to five.

## Self-review notes

- The order is ground, codec, the editor's layer, the editor's builds, the other hosts, the deletion, the close. The ground comes first so that every later difference is not a node's; the codec's change comes before the layer because it changes the reference the roundtrip is compared with; the editor moves before the other hosts so that its proof is its own.
- The translation table was written from the code of the loader and both savers, each row checked against what the record's type holds. Every one of the nine measured differences has its row: the three settings (Task 2 and the settings row), the block, the faces, `seconds`, `panning`, the arrays, the rate, the order.
- Not verified in planning and left to Task 0: that each editor arm repeats on the same file; that `--gencheck` under the old editor equals the command-line tool's sound on the six known files' neighbours; that every `synth_ui` fixture reads under the base's `--check`.
- Not verified in planning and left to Task 1: which public face shows an `ExplicitPartials`' layout lengths; that the resize leaves the final layout cell for cell (the twin gate and the 131 say).
- Not verified in planning and left to Task 2: that 4.6's 108 synthetic rows all stand after the settle; that a scratch build of an Envelope per read does not move `--check`'s time by more than a few seconds over audition.
- Not verified in planning and left to Task 3: that every array edit in the editor goes through `push_array`, `push_table`, `push_curve` or `pull_arrays` (the shape editor's four apply functions and the stage table are the ones to read); that the six known roundtrip differences all close; whether any in-scope file's `--gencheck` moves; that a load with no ImNodes frame can keep `s_recordPanning` without asking the view.
- Not verified in planning and left to Task 5: how many of the seventeen sentences keep their needle; how the eighteen rows split between an asserted refusal and a rewritten patch.
- The plan does not touch `patches/`, `scores/`, the converter's rules, any node after Task 1, the voice, the mixer, the embedded score's player, or the editor's model beyond the fields the Interfaces name.

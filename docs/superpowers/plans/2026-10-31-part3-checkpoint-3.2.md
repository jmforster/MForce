# Part 3, checkpoint 3.2: families declared at registration; folders; the create menu generated; colours through `Theme` — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Each registered type declares its family, its menu label and its shelf once, where it is registered; the per-node `category()` copy and the `SourceCategory` enum are gone; the editor's create menu and its Replace-with menu are generated from the registry; node colours come from a `Theme` object keyed by family; the node headers live in one folder per family under `source/`; no render changes.

**Architecture:** Today three hand-kept classifications disagree: the folder a node's header lives in (`source/`, `filter/`, `core/`, `source/additive/`), the category each type reports twice (a `SourceCategory` on its registry entry and a `category()` virtual on the node, sixty overrides), and the editor's hand-written menu of eighty-two entries with its own name-matching colour overrides. This checkpoint makes the registry entry the one classification: `register_type(name, TypeTag{family, label, shelf}, factory, configurator)`. The registry serves `family(name)`, `label(name)` (derived from the type name unless given), `shelf(name)`, and `types_in(family, shelf)` in registration order; `Component::category()` and `SourceCategory` are deleted. The editor's two menu functions become one loop over `kFamilyOrder` and `types_in`, with the Experimental shelf as a submenu mirroring the families (Matt, 2026-10-31: everything not in today's main menus goes on that shelf; the classification can be refined later). A `Theme` struct holds one colour per family and one per special editor node, with built-in defaults equal to today's literals. Last, the headers move into family folders by `git mv`, and the `filter` module folds into `source`.

**Tech Stack:** C++20 (MSVC, CMake 3.20+; designated initializers for `TypeTag`), ImGui/imnodes (the editor), the existing `engine_tests` harness, `python tools/gates.py`, `python tools/null_gate_perform_source.py`, `python tools/test_stable_roundtrip.py`, `python tools/alloc_gate.py`, `python tools/ab_render_set.py`, `python tools/ab_render_time.py`, `python tools/structure/check.py`.

**Spec:** `docs/architecture/target-architecture.md` section 3.2 ("One classification. Each type declares its family once, where it is registered. Folder, colour and the create menu all follow from it; the menu is generated from the registry." / "`source` and `filter` stop being a dependency boundary." / "'Filter' survives as a family name.") and its Theme decision ("One `Theme` object holds every colour and other look-and-feel value, with built-in defaults, loadable from JSON. Node colours are keyed by family"); `docs/architecture/part3-nodes.md` section 3 (the ten families and their members, "accepted as proposed 2026-10-05; a living list") and section 10 row 3.2 ("none"). Matt, 2026-10-31 (brainstorm): "just throw them all under experimental, don't get slowed down by classifications that can be decided later"; the playback (one classification at registration with label and shelf; the menu generated with Experimental mirroring the families; `Theme` with today's colours; folders by family last) was approved as it stood.

## Global Constraints

- **Byte-identity is total.** Part 3 section 10 row 3.2 says "none". `python tools/null_gate_perform_source.py --jobs 3` reports `78/78 manifest entries identical` after EVERY task; the A/B at the close over `renders/scratch/ab31/library_baselines.txt` (132 patches) reports 0 DIFFER. Nothing in this checkpoint touches a render path: a moved render is a finding to explain before any manifest moves.
- **Measurement scope is `patches/library` + `patches/baselines`** (CLAUDE.md "Measurement scope").
- **The menu reads as today.** Every type listed in today's main family menus is on the main shelf under its new family with today's label; every type in today's Experimental submenu and the eight types in no menu are on the Experimental shelf. Task 1's test pins the main shelf's content and order per family against today's menu, read from the UI source at execution.
- **Node colours follow the family.** A type whose family changed changes colour; Task 2 lists them in the Ledger and the report names the visible change. No other colour literal moves in this checkpoint (the other fifty-odd are Part 7's).
- **No new file over 600 lines, no new function over 80** (`tools/structure/limits.json`); test functions included — split at 80. `python tools/structure/check.py --delta <Task 0 sha>` is pasted into the report; `include_violations` and `unmapped_files` must not rise after the folder move. The meter's known-answer test pins `draw_properties_panel` at 1035 lines: a UI edit that changes a measured function's length updates that number in the same commit and says so.
- **No dated changelog, backlog id, REVIEW or checkpoint number in code comments.**
- **Commit after every task; `python tools/gates.py --fast` before every commit** (ctest, the stamp, the roundtrip smoke with its known wiring diff, the allocation gate). The whole-corpus roundtrip (`test_stable_roundtrip.py patches/baselines patches/library --jobs 3`) runs after every task that touches the editor (Tasks 1, 2, 3): the backlog-79 six and nothing else.
- **Work on main, no worktree.** The in-tree build is `build/`.
- **The A/B base** is `renders/scratch/ab_tree/cli_pre32.exe`: Task 0 copies the current build's CLI (its engine is the tree at `f1f8244`, 3.1's seal) before any code moves and renders the old arm into `renders/scratch/ab32/old`.
- **Edits go through script files**, never heredocs with backslashes (memory `feedback_bash_heredoc_limits`).
- **Dates:** `date` first (the machine is HST); this plan is dated 2026-10-31.

## Review Focus

The five input classes the spec implies but no existing test exercises, most likely to bite first. Each line names the task whose tests pin it.

1. **A patch naming a type by its loading alias** (`KSPianoString`, 116 nodes on disk; `FullAdditiveSource`): the alias is a registry entry like any other and must keep loading and rendering the same bytes. Task 0 pins it: the null gate (library carries the alias), plus a test that `create("KSPianoString")` is a `KSString` and that the alias sits on the Experimental shelf under Physical.
2. **A type with no label today** (NameGate, StaticVarSource, StaticRangeSource, Biquad, BowTable, Mesh2D, PierceFilter, KSPianoString): the derived label must be non-empty, free of the type suffix and unique within its family. Task 0 pins `derived_label` on the eight and on the awkward shapes (`HybridKSSource` → Hybrid KS, `BWLowpassFilter` → BW Lowpass, `Mesh2D` → Mesh 2D, `ADSREnvelope` → ADSR, `Envelope` → Envelope, `CurveNode` → Curve).
3. **The editor's envelope recognition** (`isEnvelope`, two sites keyed on `SourceCategory::Envelope`): after the change it must cover exactly today's set — `Envelope`, the six presets, `PhasedValueSource` — and the two newcomers to the family (`SegmentSource`, `Vibrato`) are the one visible change (their strips take the envelope colour). Task 2 pins the family's membership by test; the Ledger records the two.
4. **The Replace-with menu** must offer what the create menu offers: both call the one generated function (today's `source_family_menus()` is already shared). Task 1 keeps one function; the test pins `types_in`'s content, which is all either menu draws.
5. **The folder move** must leave every include resolving, every header mapped, and no new include direction: `python tools/structure/check.py --delta` shows `include_violations +0`, `unmapped_files 0`; a full build of engine, CLI, gate, UI and tests; the null gate. Task 3's steps run all of them before the commit.

---

## File structure

| File | Responsibility after this checkpoint |
|---|---|
| `engine/include/mforce/core/source_registry.h`, `engine/src/source_registry.cpp` (modify, Task 0) | `enum class Family` (ten), `enum class Shelf { Main, Experimental }`, `struct TypeTag { Family family; const char* label; Shelf shelf; }`, `register_type(name, TypeTag, factory, configurator)`, `family(name)`, `label(name)`, `shelf(name)`, `types_in(family, shelf)` in registration order, `registered_types()` as `std::vector<TypeEntry>` in registration order, `family_name(Family)`, `kFamilyOrder`, `derived_label(typeName)`. |
| `engine/include/mforce/core/descriptors.h`, `core/component.h` (modify, Task 0) | `SourceCategory` and `Component::category()` deleted. |
| `engine/src/source_registrations.cpp` (modify, Task 0, reordered Task 1) | eighty-five registrations carry a `TypeTag`; blocks ordered by family in menu order. |
| every node header with a `category()` override (modify, Task 0) | the override deleted (sixty). |
| `tools/mforce_cli/main.cpp` (modify, Task 0) | `--dump-descriptors` prints `"family"` as the family's name, not an integer category. |
| `tools/engine_tests/main.cpp` (modify, Tasks 0–3) | the registry tests (families, labels, shelves, the alias, the menu's content), the sweeps over `registered_types()` with the new entry type, the twelve `category()` asserts become family asserts. |
| `tools/mforce_ui/main.cpp` (modify, Tasks 1, 2, 3) | `source_family_menus()` and `experimental_family_menu()` generated from the registry; `node_title_color` through `Theme`; the two name-matching helpers gone; includes follow the moves. |
| `tools/mforce_ui/theme.h` (new, Task 2) | `struct Theme` with a colour per family and per special node, `kDefaultTheme` with today's literals, `const Theme& theme()`. |
| `engine/include/mforce/source/<family>/…` (moves, Task 3) | one folder per family: `oscillator/`, `noise/`, `wavetable/`, `additive/` (exists), `physical/`, `filter/`, `envelope/`, `modulator/`, `combiner/`, `utility/`; `engine/include/mforce/filter/` gone; the node headers leave `core/`. |
| `tools/structure/modules.json` (modify, Task 3) | the `filter` module folds into `source`; `may_include` lists drop `filter`; the moved `core` node headers belong to `source`. |
| `docs/architecture/part3-nodes.md`, `target-architecture.md` (modify, Task 4) | section 3's note on the Experimental shelf; section 10 row 3.2's result; the target's 3.2 landed line. |

---

### Task 0: One classification, declared at registration

**Files:**
- Modify: `engine/include/mforce/core/source_registry.h`, `engine/src/source_registry.cpp`, `engine/include/mforce/core/descriptors.h:10-19` (`SourceCategory`), `engine/include/mforce/core/component.h:23` (`category()`), `engine/src/source_registrations.cpp` (85 registrations), every header with `SourceCategory category() const override` (60; `grep -rln 'SourceCategory category() const override' engine/include`), `tools/mforce_cli/main.cpp:1049-1075` (`run_dump_descriptors`), `tools/engine_tests/main.cpp` (the twelve `category()` asserts, the test node at `:641`, the sweeps at `:4821`, `:4843-4856`, `:6566-6582`)
- Test: `tools/engine_tests/main.cpp` — `run_registry_family_tests` (new)

**Interfaces:**
- Produces (every later task consumes these by these names):
  ```cpp
  // source_registry.h
  enum class Family { Oscillator, Noise, Wavetable, Additive, Physical, Filter, Envelope, Modulator, Combiner, Utility };
  constexpr int kFamilyCount = 10;
  inline constexpr Family kFamilyOrder[kFamilyCount] = { /* the ten, in the order above: Part 3 section 3's table */ };
  const char* family_name(Family f);                        // "Oscillators", "Noise", "Wavetable", "Additive", "Physical", "Filters", "Envelopes", "Modulators", "Combiners", "Utility"
  enum class Shelf { Main, Experimental };
  struct TypeTag { Family family; const char* label = nullptr; Shelf shelf = Shelf::Main; };   // label null: derived from the type name
  struct TypeEntry { std::string name; TypeTag tag; };
  void SourceRegistry::register_type(const std::string& type_name, TypeTag tag, SourceFactory factory, JsonConfigurator configurator = nullptr);
  Family      SourceRegistry::family(const std::string& type_name) const;   // Utility for an unknown name, as get_category returned
  std::string SourceRegistry::label(const std::string& type_name) const;    // the tag's label, or derived_label(type_name)
  Shelf       SourceRegistry::shelf(const std::string& type_name) const;
  std::vector<TypeEntry> SourceRegistry::registered_types() const;          // registration order
  std::vector<TypeEntry> SourceRegistry::types_in(Family f, Shelf s) const; // registration order
  std::string derived_label(std::string_view typeName);   // strip one trailing Source|Filter|Node|Envelope (never the whole name), then a space before each capital that follows a lower-case letter or a digit and before a digit that follows a letter; runs of capitals stay together ("HybridKSSource" -> "Hybrid KS", "BWLowpassFilter" -> "BW Lowpass", "Mesh2D" -> "Mesh 2D", "ADSREnvelope" -> "ADSR", "Envelope" -> "Envelope", "CurveNode" -> "Curve", "WanderNoise2Source" -> "Wander Noise 2")
  ```
  `get_category` and `SourceCategory` are gone; `has` and `create` are unchanged.

The family of each type is Part 3 section 3's table applied to the registry's eighty-five names; the shelf is today's menu (Matt, 2026-10-31): **main** = every type in `source_family_menus()`; **experimental** = every type in `experimental_family_menu()` plus the eight in no menu. The label is today's menu label where the type has one, else derived. The complete mapping, which Task 0's script writes into the registrations and Task 0's test asserts:

| Family | Main shelf (today's label) | Experimental shelf |
|---|---|---|
| Oscillator | SineSource "Sine", SawSource "Saw", PulseSource "Pulse", TriangleSource "Triangle", FMSource "FM" | — |
| Noise | RedNoiseSource "Red", PinkNoiseSource "Pink", WhiteNoiseSource "White", BlueNoiseSource "Blue", VioletNoiseSource "Violet", VelvetNoiseSource "Velvet", PerlinNoiseSource "Perlin", CrackleNoiseSource "Crackle" | LayeredRedNoiseSource "Layered Red", MurmurationNoiseSource "Murmuration", WanderNoiseSource "Wander 1", WanderNoise2Source "Wander 2", WanderNoise3Source "Wander 3" |
| Wavetable | WavetableSource "Wavetable", HybridKSSource "Hybrid KS", EKSEvolution "EKS Evolution", PluckEvolution "Pluck Evolution", AveragingEvolution "Averaging Evolution" | BowedStringEvolution "Bowed String Evolution", ReedEvolution "Reed Evolution", BrassEvolution "Brass Evolution", ReactionDiffusionEvolution "Reaction-Diffusion (Gray-Scott)", SortErosionEvolution "Sort Erosion (->saw)", CellularAutomatonEvolution "Cellular Automaton (Wolfram)", HistogramEqualizeEvolution "Histogram Equalize", BezierPullEvolution "Bezier Pull (->curve)", BitRotateEvolution "Bit Rotate (glitch)" |
| Additive | AdditiveSource "Full", BasicAdditiveSource "Basic", AdditiveSource2 "Alternate", FullPartials "Full Partials", SequencePartials "Sequence Partials", ExplicitPartials "Explicit Partials", CompositePartials "Composite Partials", ExpandRule "Partials Expand Rule", Formant "Formant", FormantSequence "Formant Sequence", FormantSpectrum "Formant Spectrum", BandSpectrum "Band Spectrum", FixedSpectrum "Fixed Spectrum" | — |
| Physical | KSString "KS String", AllpassResonator "Allpass Resonator", DelayLine "Delay Line" | KSPianoString (derived), Mesh2D (derived), PierceFilter (derived), BowTable (derived) |
| Filter | DelayFilter "Delay", Reverb "Reverb", Limiter "Limiter", HammerBank "Hammer Bank", DistortedSource "Distortion", BWBandpassFilter "BW Bandpass", BWLowpassFilter "BW Lowpass", BWHighpassFilter "BW Highpass", SVFSource "SVF (State Variable)", Shaper "Shaper" | Biquad (derived) |
| Envelope | ADSREnvelope "ADSR", ASREnvelope "ASR", ADREnvelope "ADR", AREnvelope "AR", ASEnvelope "AS", ADSEnvelope "ADS", Envelope "Envelope", Vibrato "Vibrato", PhasedValueSource "Phased", SegmentSource "Segment" | — |
| Modulator | RepeatingSource "Repeating", VarSource "Var", RangeSource "Range", CurveNode "Curve" | NameGate (derived) |
| Combiner | CombinedSource "Combined", CrossfadeSource "Crossfade", MultiSource "Multi", MultiplexSource "Multiplex" | — |
| Utility | Wormhole "Wormhole" | StaticVarSource (derived), StaticRangeSource (derived) |

(63 main, 22 experimental, 85 in all. VarSource, RangeSource and CurveNode appear in today's menu where the executor finds them — the function continues past line 10910 — with the labels there; if a label differs from this table, today's label wins and the Ledger says so. A type's position within a family follows today's menu order, top to bottom, main shelf first; Task 1 reorders the registrations file to match.)

- [ ] **Step 0: Date, base, old arm** — `date`; `git rev-parse --short HEAD`; copy `build/tools/mforce_cli/Release/mforce_cli.exe` to `renders/scratch/ab_tree/cli_pre32.exe` (the engine at `f1f8244`; build first if `gates.py --fast`'s stamp says the build is stale); `python tools/ab_render_set.py render renders/scratch/ab31/library_baselines.txt renders/scratch/ab32 old --cli renders/scratch/ab_tree/cli_pre32.exe` (132/132).
- [ ] **Step 1: Write the failing tests** — `run_registry_family_tests()`:
  ```cpp
  register_all_sources();
  auto& reg = SourceRegistry::instance();
  CHECK(reg.family("SineSource") == Family::Oscillator && reg.shelf("SineSource") == Shelf::Main && reg.label("SineSource") == "Sine");
  CHECK(reg.family("KSString") == Family::Physical && reg.family("Shaper") == Family::Filter && reg.family("SegmentSource") == Family::Envelope);
  CHECK(reg.family("WanderNoiseSource") == Family::Noise && reg.shelf("WanderNoiseSource") == Shelf::Experimental && reg.label("WanderNoiseSource") == "Wander 1");
  CHECK(reg.shelf("Mesh2D") == Shelf::Experimental && reg.label("Mesh2D") == "Mesh 2D");            // derived
  CHECK(reg.family("KSPianoString") == Family::Physical && reg.shelf("KSPianoString") == Shelf::Experimental);
  CHECK(std::dynamic_pointer_cast<KSString>(reg.create("KSPianoString")) != nullptr);               // the alias still builds the type
  CHECK(derived_label("HybridKSSource") == "Hybrid KS" && derived_label("BWLowpassFilter") == "BW Lowpass");
  CHECK(derived_label("ADSREnvelope") == "ADSR" && derived_label("Envelope") == "Envelope" && derived_label("CurveNode") == "Curve");
  CHECK(derived_label("WanderNoise2Source") == "Wander Noise 2" && derived_label("StaticVarSource") == "Static Var");
  // every type has a family, a non-empty label unique within its family and shelf, and the counts are the table's
  int main = 0, experimental = 0;
  for (Family f : kFamilyOrder) for (Shelf s : {Shelf::Main, Shelf::Experimental}) {
      std::set<std::string> labels;
      for (const auto& e : reg.types_in(f, s)) { CHECK(!reg.label(e.name).empty()); CHECK(labels.insert(reg.label(e.name)).second); (s == Shelf::Main ? main : experimental)++; }
  }
  CHECK(main == 63 && experimental == 22 && main + experimental == int(reg.registered_types().size()));
  ```
- [ ] **Step 2: Run it to see it fail** — `engine_tests` → `C2039 'family': is not a member of 'mforce::SourceRegistry'` (and `Family`, `Shelf`, `derived_label` undeclared).
- [ ] **Step 3: The registry** — the Interfaces block in `source_registry.h`/`.cpp`: `Entry` gains `TypeTag tag` and the registry a `std::vector<std::string> order_` appended on `register_type` (registration order; `registered_types()` and `types_in` walk it); `derived_label` in `source_registry.cpp` with the rule stated in the Interfaces block; `family_name` a `switch`. Delete `SourceCategory` from `descriptors.h` and `category()` from `component.h`.
- [ ] **Step 4: The registrations** — a script rewrites the eighty-five `register_type("X", SourceCategory::Y,` heads to `register_type("X", {.family = Family::F, .label = "L", .shelf = Shelf::S},` from the table (omit `.label` where derived, omit `.shelf` where Main) and deletes the sixty `SourceCategory category() const override { … }` lines (one line each; where a holder's comment above it says "graph node holding", leave the comment). `tools/engine_tests/main.cpp:641`'s override goes with them; the twelve `CHECK(x.category() == SourceCategory::…)` become `CHECK(reg.family("X") == Family::…)` with the table's family (or are deleted where the test's point was the override itself — say which in the Ledger); the sweeps read `TypeEntry` (`for (const auto& e : reg.registered_types())`, `e.name`, `e.tag.family`); the sweep's "a sound source at its defaults makes sound" check keys on `Family::Oscillator || Family::Noise || Family::Wavetable` and its `kSilentUntilWired` loses the entries no longer in those families (`KSString`, `KSPianoString`, `AllpassResonator`, `DelayLine`, `Mesh2D` are Physical now; `SegmentSource` is Envelope). `run_dump_descriptors` prints `e["family"] = family_name(reg.family(name))` in place of `e["category"] = int(cat)` (no tool reads `category`; `lint_patches.py` reads the pin lists only).
- [ ] **Step 5: Build everything and run the gates** — `cmake --build build --config Release` (engine, CLI, gate, UI, tests: the UI still compiles because `node_title_color` is changed in this task to call `reg.family` with the ten cases mapped to today's colours — Oscillator, Physical, Modulator, Combiner teal; Noise reddish orange; Wavetable olive tan; Additive orange; Filter lavender; Envelope pale gold; Utility grey — and the two `SourceCategory::Envelope` sites to `Family::Envelope`; the `is_noise_type`/`is_wavetable_type` helpers stay until Task 2); `engine_tests` ALL PASS; null gate `78/78`; `gates.py --fast`.
- [ ] **Step 6: Commit** — `git commit -m "registry: each type declares its family, label and shelf once; the per-node category and SourceCategory are gone"`.

---

### Task 1: The menu generated

**Files:**
- Modify: `tools/mforce_ui/main.cpp:10808-10960` (`source_family_menus`, `experimental_family_menu`, `menu_sep`, `menu_placeholder`), `engine/src/source_registrations.cpp` (block order)
- Test: `tools/engine_tests/main.cpp` — `run_menu_content_tests` (new)

**Interfaces:**
- Consumes: `kFamilyOrder`, `family_name`, `reg.types_in(f, s)`, `reg.label(name)`.
- Produces: the editor's `source_family_menus()` — `for (Family f : kFamilyOrder) { auto types = reg.types_in(f, Shelf::Main); if (types.empty()) continue; if (ImGui::BeginMenu(family_name(f))) { for (e : types) menu_source(reg.label(e.name).c_str(), e.name.c_str()); ImGui::EndMenu(); } }`, with the one `ImGui::Separator()` today's menu draws between Additive and Envelopes kept after the fourth family; `experimental_family_menu()` the same over `Shelf::Experimental` under `BeginMenu("Experimental")`, skipping empty families (no placeholders).

- [ ] **Step 1: Write the failing test** — `run_menu_content_tests()`: for each family, `types_in(f, Shelf::Main)` names in order equal today's menu for that family, written out as a table in the test from today's `source_family_menus()` (the executor reads lines 10808–10925 of the UI at this step and copies the type names in order, mapped to the new families: Generators' first five to Oscillator; its KS String, Allpass Resonator, Delay Line (from Loop) to Physical in that order; Hybrid KS to Wavetable after WavetableSource; Phased and Repeating to Envelope and Modulator; Segment to Envelope after Vibrato; Shaper to Filter after SVF; Wormhole to Utility); and `types_in(f, Shelf::Experimental)` equal today's Experimental submenu plus the eight, in the table's order. Expected to FAIL: the registrations file is in the old category order (White, Pink, Red … against the menu's Red, Pink, White …).
- [ ] **Step 2: Run it to see it fail** — `engine_tests` → the order CHECKs for Noise (and others) FAIL.
- [ ] **Step 3: Reorder the registrations** — move the registration blocks in `source_registrations.cpp` into family order, main shelf in today's menu order then the experimental ones; nothing but block order changes (a script that cuts each `reg.register_type(…);` statement with its leading comment and reassembles). `register_all_sources()` registers in that order; `registered_types()` and `types_in` follow.
- [ ] **Step 4: The editor** — replace the two hand-written functions with the generated ones (Interfaces block); delete `menu_sep` and `menu_placeholder` if nothing else calls them (grep); the special editor nodes (output, channel, mixer, parameter, note) stay where `show_create_menu` draws them.
- [ ] **Step 5: Gates** — build; `engine_tests` ALL PASS; null gate `78/78`; the whole-corpus roundtrip (the six); `gates.py --fast`; open the editor once and read the create menu against today's: the same entries, the same order, Experimental's four sub-menus now showing the eight newcomers under Physical, Filters, Modulators, Utility (record what you saw in the Ledger).
- [ ] **Step 6: Commit** — `git commit -m "editor: the create and replace menus are generated from the registry; the registrations stand in menu order"`.

---

### Task 2: Colours through `Theme`

**Files:**
- Create: `tools/mforce_ui/theme.h`
- Modify: `tools/mforce_ui/main.cpp:150-215` (`is_noise_type`, `is_wavetable_type`, `kDynPinGold`, `node_title_color`), `:12562-12563` and `:12728-12729` (`isEnvelope`, `envelopeColor`), every other use of the two helpers (four lines in all; grep)
- Test: `tools/engine_tests/main.cpp` — the family-membership check for `Family::Envelope` (in `run_registry_family_tests`)

**Interfaces:**
- Produces:
  ```cpp
  // theme.h — the editor's colours, keyed by family and by special node; built-in defaults
  struct Theme {
    ImU32 family[kFamilyCount];                   // indexed by int(Family)
    ImU32 output, parameter, note, curve, unknown; // the editor's own nodes and the fallback
    ImU32 dynPinGold;                              // the performance cadence (was kDynPinGold)
    ImU32 envelopeStrip;                           // the strip view's envelope colour (was envelopeColor)
    ImU32 for_type(const std::string& typeName) const;   // what node_title_color computed
  };
  const Theme& theme();                            // kDefaultTheme until Part 7 loads one from JSON
  ```
  Defaults are today's literals: Oscillator, Physical, Modulator, Combiner `IM_COL32(95,165,150,255)`; Noise `(185,95,85)`; Wavetable `(155,150,110)`; Additive `(195,145,90)`; Filter `(145,130,175)`; Envelope `(200,185,110)`; Utility and unknown `(128,128,128)`; output `(120,130,145)`; parameter `(190,140,170)`; note and dynPinGold `(205,170,60)`; curve `(170,145,75)`.

- [ ] **Step 1: Write the failing test** — in `run_registry_family_tests`: the Envelope family is exactly `Envelope`, `AREnvelope`, `ASEnvelope`, `ASREnvelope`, `ADSEnvelope`, `ADREnvelope`, `ADSREnvelope`, `PhasedValueSource`, `SegmentSource`, `Vibrato` (both shelves) — the set the editor's `isEnvelope` now reads. (Passes after Task 0; it is the pin for Review Focus 3, written here because this task is where the editor starts reading it.)
- [ ] **Step 2: `theme.h`** — the struct and the defaults; `for_type` is `node_title_color`'s body with the special-node branches reading the fields and the registry branch `family[int(reg.family(typeName))]` — no name matching: `is_noise_type` and `is_wavetable_type` are deleted (their other callers, if any, read the family instead).
- [ ] **Step 3: The editor** — `node_title_color(typeName)` returns `theme().for_type(typeName)`; `kDynPinGold` uses become `theme().dynPinGold`; the two `isEnvelope` sites read `reg.family(n.typeName) == Family::Envelope` and `envelopeColor` becomes `theme().envelopeStrip`. List in the Ledger every type whose colour changed because its family did (expected: HybridKSSource teal → tan; SegmentSource teal → pale gold; PierceFilter and Biquad lavender → teal; Shaper, DistortedSource teal → lavender; Vibrato unchanged; BowTable, Mesh2D, KSString, AllpassResonator, DelayLine unchanged).
- [ ] **Step 4: Gates** — build; `engine_tests` ALL PASS; null gate `78/78`; the whole-corpus roundtrip (the six); `gates.py --fast`; open the editor on `patches/library/winds/oboe1.json` and a wavetable patch: the colours read as before except the listed changes.
- [ ] **Step 5: Commit** — `git commit -m "editor: node colours come from a Theme keyed by family; the name-matching overrides are gone"`.

---

### Task 3: Folders by family

**Files:**
- Move (`git mv`): the headers below; modify every `#include` that names a moved path (`engine/`, `tools/`: about 110 lines; `grep -rn '#include "mforce/\(source\|filter\|core\)/' engine tools` lists them), `tools/structure/modules.json`.

| Into `engine/include/mforce/source/…` | From |
|---|---|
| `oscillator/` | `source/sine_source.h`, `saw_source.h`, `triangle_source.h`, `pulse_source.h`, `fm_source.h` |
| `noise/` | `source/noise_sources.h`, `white_noise_source.h`, `pink_noise_source.h`, `red_noise_source.h`, `layered_red_noise_source.h`, `wander_noise_source.h` |
| `wavetable/` | `source/wavetable_source.h`, `wave_evolution.h`, `hybrid_ks_source.h` |
| `additive/` | stays |
| `physical/` | `source/ks_string.h`, `allpass_resonator.h`, `delay_line_source.h`, `mesh2d_source.h`, `pierce_filter.h`, `bow_table_source.h` |
| `filter/` | `filter/biquad_source.h`, `filters.h`, `hammer_bank.h`, `limiter.h`, `reverb.h`, `svf_source.h`; `source/shaper_source.h` |
| `envelope/` | `core/envelope.h`, `envelope_presets.h`, `envelope_json.h`; `filter/vibrato.h`; `source/phased_value_source.h`, `segment_source.h` |
| `modulator/` | `core/var_source.h`, `range_source.h`, `curve_node.h`, `name_gate.h`; `source/repeating_source.h` |
| `combiner/` | `source/combined_source.h`, `multiplex_source.h`; `core/multi_source.h` |
| `utility/` | `source/wormhole_source.h` |

`ConstantSource` and `TapSource` stay in `core/dsp_value_source.h` (they are the contract's own). The `.cpp` files stay in `engine/src/` (the module map lists them by name). `engine/include/mforce/filter/` is removed.

- [ ] **Step 1: The moves and the includes** — a script does the `git mv`s and rewrites every include path (exact old path → new path, over `engine/` and `tools/`, `.h`, `.cpp`, `CMakeLists.txt`); `modules.json`: the `filter` module is deleted, `source`'s paths stay `engine/include/mforce/source/` (which now covers every family folder) plus its `.cpp`s, and every `may_include` list drops `"filter"`; `core`'s `may_include` stays `[]`; a header that moved out of `core` and is included by a `core` header would be a violation — Review Focus 5 (there are none today: no `core` header includes `source/` or `filter/`).
- [ ] **Step 2: Build everything** — `cmake --build build --config Release`; every target links. Expected: zero errors (a missed include shows as C1083 and names the path).
- [ ] **Step 3: Gates** — `engine_tests` ALL PASS; `python tools/structure/check.py --delta <Task 2 sha>`: `files +0`, `include_violations +0`, `unmapped_files 0`; null gate `78/78`; the whole-corpus roundtrip (the six); `gates.py --fast` (its ctest runs the structure meter's known-answer tests — `test_structure.py` pins nothing under `filter/`, confirmed at planning).
- [ ] **Step 4: Commit** — `git commit -m "nodes: one folder per family under source/; filter folds into source; the node headers leave core"`.

---

### Task 4: Close the checkpoint

- [ ] **Step 1: Every gate** — `python tools/gates.py` (all): ctest, stamp, roundtrip smoke (the known wiring diff), the allocation gate, null gate `78/78`, roundtrip (the six), wiring null gate `78/78`.
- [ ] **Step 2: The A/B** — `python tools/ab_render_set.py render renders/scratch/ab31/library_baselines.txt renders/scratch/ab32 new` then `compare renders/scratch/ab32 old new` → `0 DIFFER` of 132.
- [ ] **Step 3: Timing** — `python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre32.exe build/tools/mforce_cli/Release/mforce_cli.exe` on a quiet machine (nothing else of this session's running; if the base arm's walls are not within 5 percent of a previous clean run, say so and run once more); no per-sample path changes in this checkpoint, so the expectation is noise.
- [ ] **Step 4: The meter** — `python tools/structure/check.py --delta <Task 0 sha>`; paste it; `--snapshot`.
- [ ] **Step 5: The refuter, the wave, the re-review** — the Fable refuter (read-only, the ten-item brief with checkpoint-specific questions: the mapping against Part 3 section 3; the derived labels; the registrations' order as the menu's; `Theme`'s defaults against the deleted literals; the include rewrite's completeness; the module map), a fix wave with tests first, the same refuter's re-review by message, a re-check of the wave's follow-up.
- [ ] **Step 6: The report and the docs** — `docs/autonomy/refactor/reports/<date>-checkpoint-3.2.md` in the ten-section format (section 1 names the one visible change — the types whose colour followed their family — and the Experimental shelf's eight newcomers; section 5 the gates, the A/B, the timing, the meter; section 6 the rulings; section 7 the debt: `Theme`'s remaining literals and its JSON loading (Part 7), the preset envelopes still in the menu until 3.5, the `.cpp`s not in family folders, the loader's envelope-preset branch naming `core/`-era includes if any; section 8 the refutation; section 10 lessons — designated initializers; `enum class` with a parallel name table; `git mv` and what history keeps); Part 3 section 3 gains a note that the Experimental shelf holds today's parked types and the eight unlisted (Matt, 2026-10-31) and section 10 row 3.2 its result; the target architecture's section 3.2 gains a landed line; REVIEW R9 awaiting; BACKLOG row 2 (3.2 DONE, 3.3 next); STATUS.
- [ ] **Step 7: Commit** — `git commit -m "docs: checkpoint 3.2 report (R9), REVIEW R9 awaiting, Part 3 row 3.2, the target's landed line, backlog, STATUS handoff, meter snapshot"`.

---

## Ledger

Rulings made while planning (each becomes a line in the report's section 6; the controller appends rulings made during execution below them, dated by commit).

1. **The registry entry is the one classification; the per-node `category()` is deleted.** The target says "each type declares its family once, where it is registered"; a virtual on every node is a second declaration that drifted (the holders say Combiner or Generator for what the table calls Wavetable). Nothing at run time asks a node object for its family — the editor and the loader ask the registry by type name — so the sixty overrides and `Component::category()` go, and the tests that asserted them assert the registry. Cost if wrong: a node in hand without its registry name cannot say its family; no such caller exists.
2. **A `TypeTag` struct with designated initializers**, not more positional parameters on `register_type`: `{.family = Family::Noise, .shelf = Shelf::Experimental}` reads at the registration; a fourth and fifth positional argument would not. `label` and `shelf` default so the common registration names only its family.
3. **Labels: today's where today has one, derived otherwise.** The menu must read as today (a byte-identity analogue for the editor); the eight unlisted types have no label and take the derived form, whose rule is one function with a test. An override is kept only where it differs from the derivation. Cost: a few dozen `.label` strings in the registrations that a reader might think redundant; the test says which are not.
4. **The Experimental shelf is a registration fact, binary.** Matt's 2026-08-26 grouping becomes `Shelf::Experimental` on the type; the generated menu mirrors the families under it; today's placeholder sub-menus ("(none yet)") go, an empty family is not drawn. Everything not in today's main menus is on that shelf (Matt, 2026-10-31), including the `KSPianoString` alias, which 3.5 deletes.
5. **Registration order is menu order.** The alternative, an `order` field on the tag, is a second thing to keep in step; moving the registration blocks once into family order costs a script and nothing after. Every sweep over `registered_types()` is order-free (checked: the split, the configurator, the audit, the part set).
6. **`Family` is ten values with a parallel name table** (`family_name`), display names plural where the table says so ("Oscillators", "Filters"), enum names singular. `kFamilyOrder` is Part 3 section 3's order and the menu's: Oscillators, Noise, Wavetable, Additive, Physical, Filters, Envelopes, Modulators, Combiners, Utility.
7. **Colours follow the family, so a type whose family changed changes colour.** The target's sentence is "folder, colour and the create menu all follow from it"; keeping per-type colour overrides to preserve today's picture would be the third hand classification by another name. The changes are few (ruling in Task 2's Ledger lists them) and the report names them.
8. **`Theme` holds the node colours and the editor's five special-node colours and nothing else in this checkpoint.** The other fifty-odd literals and the JSON loading are Part 7's, where the editor is rebuilt; a `Theme` that already exists then grows. Cost if wrong: Part 7 moves the rest into a struct that already has the shape.
9. **The menu's one separator (between Additive and Envelopes) is kept** as the one presentation fact not in the registry: today's menu groups the sound-making families above the line and the shaping ones below; Part 3 section 3's order preserves that boundary after the fourth family. Cost: one line in the editor; if Matt wants it gone, one line.
10. **Folders last, by `git mv`, with no file renamed or split.** The moves are the largest textual change (about 110 include lines) and carry no behaviour; they land after every behaviour-bearing task is green so a build break has one cause. Splitting the multi-type headers (`noise_sources.h`, `filters.h`, `wave_evolution.h`, `partials.h`) is 3.6's; they move whole into their family's folder.
11. **The `.cpp` files stay in `engine/src/`.** A node's `.cpp` is an implementation detail the module map already lists by name; the classification the target asks for is the header's folder. 3.6 may revisit when it splits.
12. **`ConstantSource` and `TapSource` stay in `core`.** They are the contract's own (a Param's default, the tap edge), not nodes a user creates; the registry does not list them.
13. **The envelope headers leave `core`** (`envelope.h`, `envelope_presets.h`, `envelope_json.h`), with `var`, `range`, `curve`, `name_gate` and `multi`: `core` is "the contract and the primitives every node shares; the registry", and these are nodes. `render` includes `source/` already (`multiplex_source.h`), so `instrument.h`'s `#include "mforce/source/envelope/envelope.h"` is no new direction; `modules.json` says `render` may include `source`.
14. **A/B set, base and scope as 3.1's**: library + baselines, the old arm from the build at `f1f8244`, 0 DIFFER expected; nothing in this checkpoint touches a render path.
15. **Order of tasks**: the registry first (one classification exists before anything reads it), then the menu (the first reader), then the colours (the second), then the folders (no reader), then the close.

(Execution rulings, from the executing-plans ledger, by task.)

## Self-review notes

- **Spec coverage.** Target 3.2 "each type declares its family once, where it is registered" — Task 0; "folder, colour and the create menu all follow from it" — Tasks 3, 2, 1; "the menu is generated from the registry" — Task 1; "`source` and `filter` stop being a dependency boundary" — Task 3 (the module folds); "'Filter' survives as a family name" — `Family::Filter`, "Filters"; the Theme decision's "node colours are keyed by family" — Task 2 (built-in defaults; JSON loading left to Part 7 by ruling 8); Part 3 section 3's table — Task 0's mapping; section 10 row 3.2 "none" — the Global Constraints, Task 4 steps 1–2; Matt's shelf decision — ruling 4 and the mapping's right column.
- **Step scan.** Every code step names the file, the signature or the rule; the mapping table is the data the registrations and the tests carry; the derived-label rule is stated with its examples; the colours are the exact literals; the moves are a table; every verification step has its command and expected output. Open by design: the exact labels of the Var, Range and Curve entries wherever today's menu lists them (read at Task 0 step 4 and Task 1 step 1; today's label wins), and which `category()` asserts become family asserts versus deletions (Task 0 step 4 says to ledger each).
- **Type consistency.** `Family`, `Shelf`, `TypeTag`, `TypeEntry`, `kFamilyOrder`, `kFamilyCount`, `family_name`, `derived_label`, `reg.family/label/shelf/types_in/registered_types`, `Theme::for_type`, `theme()`, `cli_pre32.exe`, `renders/scratch/ab32/{old,new}` are used with those names throughout.
- **Review Focus.** 1 — Task 0 step 1's alias checks and the null gate; 2 — Task 0 step 1's `derived_label` cases and the uniqueness loop; 3 — Task 2 step 1; 4 — Task 1's one generated function and its content test; 5 — Task 3 steps 2–3.
- **Proportion.** Code appears for the registry's API, the test assertions, the generated menu's loop, the `Theme` struct and the defaults; the rest is a mapping table, a moves table and commands. The engine change is one enum, one tag, one registry query set and eighty-five one-line edits plus sixty deletions; the editor change is two functions and one struct; the folder move is paths.

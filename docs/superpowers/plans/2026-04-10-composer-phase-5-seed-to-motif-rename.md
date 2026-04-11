# Composer Strategy — Phase 5 Implementation Plan (Seed → Motif Rename)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rename all composition-time symbols named `Seed` to `Motif` across the C++ codebase and JSON schema. The "seed material" concept in the composer has been referred to as a `Seed` since Phase 1a; the spec's final terminology is `Motif`. This is a mechanical rename with zero behavioral change.

**Architecture:** Pure rename. Same class, same fields, same methods, same JSON round-trip logic. Only identifiers change. The golden hash MUST remain identical to the Phase 2 final hash after this phase — any drift is a bug in the rename.

**Tech Stack:** C++17. Header-only. Mechanical verification.

---

## Scope Guardrails

- **Bit-identical output required.** Every byte of the rendered golden WAV must match the Phase 2 final hash after this phase.
- **`Randomizer` stays as is.** `rng_`, `rng`, `generationSeed` — these names refer to random number generator state, not to motivic material. They do NOT rename.
- **`masterSeed` stays as is.** That's an RNG input to `Composer`, not a motif.
- **`FigureTemplate::seed` stays as is.** That's a per-figure RNG seed for reproducibility, not a motif reference.
- **`FigureSource::Reference` stays as is.** It still means "refer to a motif by name." Only the FIELD that holds the name renames from `seedName` to `motifName`.
- **Golden template gets the JSON rename.** `patches/template_golden_phase1a.json` contains `"seedName": "motif_a"` in two places and `"seeds": [...]` for the motif pool. Both rename.
- **Other committed templates NOT updated.** `patches/template_ode_to_joy*.json`, `patches/template_mary.json`, `patches/template_binary.json` all reference `seeds`/`seedName` in their JSON. They will FAIL to load under the new loader. That's acceptable for this plan — whoever next uses those templates will drive-by fix them. Log a note in the commit message.
- **Single branch: `main`.** Commit directly.

---

## The rename table

Exact name pairs:

| Before | After |
|---|---|
| `struct Seed` | `struct Motif` |
| `Seed::name` | `Motif::name` (field unchanged — "name" already generic) |
| `Seed::figure` | `Motif::figure` (field unchanged) |
| `Seed::userProvided` | `Motif::userProvided` (field unchanged) |
| `Seed::generationSeed` | `Motif::generationSeed` (field unchanged — "generation seed" is RNG semantics) |
| `Seed::constraints` | `Motif::constraints` (field unchanged) |
| `PieceTemplate::seeds` | `PieceTemplate::motifs` |
| `PieceTemplate::find_seed(name)` | `PieceTemplate::find_motif(name)` |
| `FigureTemplate::seedName` | `FigureTemplate::motifName` |
| `Composer::realizedSeeds_` | `Composer::realizedMotifs_` |
| `Composer::realized_seeds()` | `Composer::realized_motifs()` |
| `Composer::realize_seeds_(piece, tmpl)` | `Composer::realize_motifs_(piece, tmpl)` |
| JSON: `"seeds"` (on PieceTemplate) | JSON: `"motifs"` |
| JSON: `"seedName"` (on FigureTemplate) | JSON: `"motifName"` |

**Things that stay:**
- `Composer::rng_` (Randomizer instance — RNG, not motif)
- `FigureTemplate::seed` (uint32_t RNG seed)
- `Motif::generationSeed` (uint32_t RNG seed)
- `PieceTemplate::masterSeed` (uint32_t RNG seed)
- `FigureSource::Reference`
- The golden template's motif is still named `"motif_a"` — no change to motif identity, only to the field names that carry it

---

## File Structure

Modified files (all existing):

| File | Change |
|---|---|
| `engine/include/mforce/music/templates.h` | `struct Seed` → `struct Motif`; `PieceTemplate::seeds` → `motifs`; `find_seed` → `find_motif`; `FigureTemplate::seedName` → `motifName`. |
| `engine/include/mforce/music/templates_json.h` | JSON field renames `"seeds"` → `"motifs"` and `"seedName"` → `"motifName"`. Round-trip both sides. |
| `engine/include/mforce/music/composer.h` | `realizedSeeds_` → `realizedMotifs_`; `realize_seeds_` → `realize_motifs_`; `realized_seeds()` → `realized_motifs()`; all call sites inside the file updated. |
| `engine/include/mforce/music/default_strategies.h` | If any reference to `realizedSeeds_`, `seedName`, etc. remains here (may be from Phase 1a's moves), rename. |
| `engine/include/mforce/music/classical_composer.h` | Check for any residual reference (unlikely after the facade collapse, but grep to be sure). |
| `patches/template_golden_phase1a.json` | `"seeds"` → `"motifs"`; `"seedName"` → `"motifName"` (two call sites). |
| `renders/template_golden_phase1a.sha256` | Unchanged — rename is bit-identical. |
| `renders/template_golden_phase1a.wav` | Unchanged. |

**Files NOT touched**:
- `patches/template_ode_to_joy*.json`, `patches/template_mary.json`, `patches/template_binary.json` — left with old JSON field names; they'll fail to load until drive-by fixed. This is acceptable scope.
- `engine/include/mforce/music/figures.h`, `structure.h`, `strategy.h`, etc. — `Seed` doesn't appear in those types.
- `tools/mforce_cli/main.cpp` — if the CLI constructs a `Seed` anywhere (unlikely after Phase 1a), rename there too. Grep to verify.
- `engine/third_party/imgui/imgui.cpp` — hits on "Seed" are unrelated (ImGui internal).

---

## Task 1: Rename in C++ types (`templates.h`, `composer.h`, `default_strategies.h`)

**Files:**
- Modify: `engine/include/mforce/music/templates.h`
- Modify: `engine/include/mforce/music/composer.h`
- Modify: `engine/include/mforce/music/default_strategies.h` (if referenced)
- Modify: `engine/include/mforce/music/classical_composer.h` (defensive grep)

All C++-side renames happen in one task. JSON changes happen in Task 2.

- [ ] **Step 1: Rename in `templates.h`**

Open `engine/include/mforce/music/templates.h`. Find `struct Seed` (around line 107). Rename to `struct Motif`. Leave field names as-is (`name`, `figure`, `userProvided`, `generationSeed`, `constraints`).

Find `PieceTemplate::seeds` (around line 188). Rename the field:
```cpp
std::vector<Motif> motifs;
```

Find `PieceTemplate::find_seed` (around line 213-216). Rename to `find_motif`:
```cpp
const Motif* find_motif(const std::string& name) const {
  for (auto& m : motifs) if (m.name == name) return &m;
  return nullptr;
}
```

Find `FigureTemplate::seedName` (around line 85). Rename:
```cpp
std::string motifName;          // references a Motif by name
```

Find any COMMENTS in the file mentioning "Seed" or "seeds" in the composition sense (not RNG seeds). Update to say "Motif" / "motifs". Example: the struct comment block above `Seed` that says "Seeds — raw thematic material".

**Critical**: do NOT rename `uint32_t generationSeed{0};` on the Motif struct. That's an RNG seed, not a motivic identity. The field name stays `generationSeed`.

- [ ] **Step 2: Rename in `composer.h`**

Open `engine/include/mforce/music/composer.h`. The following identifiers need renaming:

```cpp
// Field declaration (around line 85):
std::unordered_map<std::string, MelodicFigure> realizedSeeds_;
// becomes:
std::unordered_map<std::string, MelodicFigure> realizedMotifs_;

// Accessor (around line 63):
const std::unordered_map<std::string, MelodicFigure>& realized_seeds() const {
  return realizedSeeds_;
}
// becomes:
const std::unordered_map<std::string, MelodicFigure>& realized_motifs() const {
  return realizedMotifs_;
}

// Private method (around line 116):
void realize_seeds_(const Piece& /*piece*/, const PieceTemplate& tmpl) {
// becomes:
void realize_motifs_(const Piece& /*piece*/, const PieceTemplate& tmpl) {

// Call site in setup_piece_ (around line 112):
realize_seeds_(piece, tmpl);
// becomes:
realize_motifs_(piece, tmpl);
```

Inside `realize_motifs_`:
- `tmpl.seeds` → `tmpl.motifs` (iteration variable `seed` → `motif`)
- `realizedSeeds_.clear()` → `realizedMotifs_.clear()`
- `realizedSeeds_[seed.name]` → `realizedMotifs_[motif.name]`
- Loop variable `auto& seed : tmpl.seeds` → `auto& motif : tmpl.motifs`
- `seed.userProvided` → `motif.userProvided`
- `seed.figure.units.empty()` → `motif.figure.units.empty()`
- `seed.figure` → `motif.figure`
- `seed.generationSeed` → `motif.generationSeed` (FIELD NAME UNCHANGED — only the struct instance variable renamed from `seed` to `motif`)
- `seed.constraints` → `motif.constraints`
- `seed.name` → `motif.name`

Inside `DefaultFigureStrategy::realize_figure` (out-of-line at bottom of `composer.h`, around line 243):
- `ctx.composer->realized_seeds()` → `ctx.composer->realized_motifs()` (two occurrences — Reference case and Transform case)
- `figTmpl.seedName` → `figTmpl.motifName` (two occurrences — Reference case and Transform case)

- [ ] **Step 3: Rename in `default_strategies.h`**

Grep `engine/include/mforce/music/default_strategies.h` for any of: `seedName`, `realizedSeeds`, `realize_seeds`, `\.seeds\b`, `find_seed`, `\bSeed\b`. For each hit, rename to the Motif equivalent. If there are no hits, no changes needed in this file.

Expected: no hits (all composition-time Seed references were moved to composer.h in Phase 1a).

- [ ] **Step 4: Rename in `classical_composer.h` (defensive)**

Grep the file for the same patterns. Expected: no hits (the façade doesn't touch motifs directly). If any hit, rename.

- [ ] **Step 5: Full grep sweep for leftover Seed references**

Run a full grep across `engine/include/mforce/music/` and `tools/`:
- Pattern: `\bSeed\b|seedName|realizedSeeds|realize_seeds|find_seed`
- Paths: `engine/include/mforce/music/`, `tools/`
- Glob: `*.{h,cpp}`

Expected hits AFTER rename:
- `generationSeed` (acceptable — RNG seed, not renamed)
- `masterSeed` (acceptable — RNG seed)
- `FigureTemplate::seed` field (acceptable — RNG seed)
- `seed + 200` (acceptable — RNG arithmetic)
- Any comment that contains the word "seed" in RNG context (acceptable)

UNACCEPTABLE hits:
- Any `Seed` identifier (struct name)
- Any `seedName` identifier
- Any `realizedSeeds_` or `realized_seeds()` or `realize_seeds_`
- Any `PieceTemplate::seeds` reference
- Any `find_seed` reference

If any unacceptable hit remains, fix it before building.

- [ ] **Step 6: Build**

```
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Release
```

Expected: clean build. If it fails, the error will point at an unrenamed reference — fix it.

- [ ] **Step 7: Commit (partial — JSON rename in Task 2 below)**

Do NOT commit yet. Wait until Task 2 completes — that's when the golden template JSON is updated to match the new field names, without which the render would fail.

---

## Task 2: Rename in JSON schema (`templates_json.h`) and golden template

**Files:**
- Modify: `engine/include/mforce/music/templates_json.h`
- Modify: `patches/template_golden_phase1a.json`

- [ ] **Step 1: Rename in `templates_json.h`**

Open `engine/include/mforce/music/templates_json.h`. Grep for `seedName` and `"seeds"` and `seeds`. Update each occurrence:

On the `PieceTemplate` serialization (search for `PieceTemplate` in the to_json and from_json functions):

```cpp
// to_json (look for emission of seeds):
if (!pt.seeds.empty()) j["seeds"] = pt.seeds;
// becomes:
if (!pt.motifs.empty()) j["motifs"] = pt.motifs;

// from_json:
if (j.contains("seeds")) {
  for (auto& sj : j.at("seeds")) {
    Seed s;
    from_json(sj, s);
    pt.seeds.push_back(std::move(s));
  }
}
// becomes:
if (j.contains("motifs")) {
  for (auto& mj : j.at("motifs")) {
    Motif m;
    from_json(mj, m);
    pt.motifs.push_back(std::move(m));
  }
}
```

If there's a separate `to_json(json&, const Seed&)` / `from_json(const json&, Seed&)` function pair, rename to `Motif`:
```cpp
inline void to_json(json& j, const Motif& m) { ... }
inline void from_json(const json& j, Motif& m) { ... }
```

On the `FigureTemplate` serialization, find the `seedName` emission and read:

```cpp
// to_json:
j["seedName"] = ft.seedName;
// becomes:
j["motifName"] = ft.motifName;

// from_json:
ft.seedName = j.value("seedName", std::string(""));
// becomes:
ft.motifName = j.value("motifName", std::string(""));
```

**Backward compatibility note**: this plan does NOT add fallback support for old `"seedName"`/`"seeds"` JSON fields. Any template using old field names will fail to load with a missing-field error. This is the accepted tradeoff — migrating the other committed templates is out of scope per the addendum spec.

- [ ] **Step 2: Update golden template JSON**

Open `patches/template_golden_phase1a.json`. Find all occurrences of `"seeds"` and `"seedName"`. Rename:

```json
"seeds": [
  {
    "name": "motif_a",
    ...
```
becomes:
```json
"motifs": [
  {
    "name": "motif_a",
    ...
```

And:
```json
{"source": "reference", "seedName": "motif_a"}
```
becomes:
```json
{"source": "reference", "motifName": "motif_a"}
```

There are exactly two `"seedName"` occurrences and one `"seeds"` occurrence in the golden template. Verify by grep after editing.

Do NOT touch the other templates (`template_ode_to_joy*.json`, `template_mary.json`, `template_binary.json`). They stay broken.

- [ ] **Step 3: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 4: Render the golden and verify bit-identical hash**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/phase5_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase5_check_1.wav
```

**Expected hash: the Phase 2 final hash** (whatever was pinned in `renders/template_golden_phase1a.sha256` at the end of Phase 2). The rename is cosmetic; output MUST be bit-identical.

Cross-check:
```
sha256sum renders/template_golden_phase1a.wav renders/phase5_check_1.wav
```
Both hashes must match.

If the hashes differ, the rename accidentally touched runtime logic. Most likely cause: a missed reference to `realizedSeeds_` that silently now points at the wrong (empty) container, so the Reference figure source returns the empty-fallback. Diagnose.

- [ ] **Step 5: Commit Phase 5**

Once the hash matches, commit:

```
git add engine/include/mforce/music/templates.h
git add engine/include/mforce/music/composer.h
git add engine/include/mforce/music/default_strategies.h
git add engine/include/mforce/music/classical_composer.h   # only if touched
git add engine/include/mforce/music/templates_json.h
git add patches/template_golden_phase1a.json
git commit -m "refactor(composer): rename Seed to Motif throughout

Phase 5 cleanup: composition-time 'Seed' → 'Motif' to match the
spec's final terminology. Pure rename, no behavioral change, golden
render bit-identical to Phase 2 pin.

Renamed:
  struct Seed → Motif
  PieceTemplate::seeds → motifs
  PieceTemplate::find_seed → find_motif
  FigureTemplate::seedName → motifName
  Composer::realizedSeeds_ → realizedMotifs_
  Composer::realized_seeds() → realized_motifs()
  Composer::realize_seeds_() → realize_motifs_()
  JSON: 'seeds' → 'motifs', 'seedName' → 'motifName'

RNG-related 'seed' identifiers are unchanged (masterSeed,
generationSeed, figure.seed, rng_, the +200 offset, etc.).

Not updated: template_mary.json, template_binary.json,
template_ode_to_joy*.json — they still use the old JSON field
names and will fail to load until drive-by fixed."
```

Clean up:
```
rm renders/phase5_check_1.wav renders/phase5_check_1.json 2>/dev/null
```

---

## Phase 5 exit criteria

1. `cmake --build` succeeds.
2. Grep for `\bSeed\b`, `seedName`, `realizedSeeds`, `realize_seeds`, `find_seed` in `engine/include/mforce/music/*.h` and `tools/*.cpp` returns zero unexpected hits (RNG-related names are expected).
3. Golden WAV hash matches the Phase 2 final pin byte-for-byte.
4. Commit landed on main.

## What is explicitly NOT in this plan

- No behavioral changes.
- No additional `Motif` features (named lookup tables, per-motif metadata, etc.).
- No migration of `template_mary.json`, `template_binary.json`, `template_ode_to_joy*.json`.
- No CLI changes.
- No tests.

# Composer Composition Model — Infrastructure (Plan A) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the infrastructure prerequisites for the composer composition model refactor — motif pool relocation, Locus cleanup, FigureConnector expansion, motif metadata, history-query helpers, `HarmonyComposer` → `ChordProgressionBuilder` rename — without any behavioral regression on the five K467 golden renders.

**Architecture:** Purely mechanical C++ header-only refactors. No new strategies, no new compositional logic. Every task ends with a build + render + SHA-256 hash compare against `docs/superpowers/plans/2026-04-14-baseline-hashes.txt`. Work on branch `composer-model-infrastructure` off latest `main`.

**Tech Stack:** C++20 header-only under `engine/include/mforce/music/`, MSVC via CMake at `build/tools/mforce_cli/Release/mforce_cli.exe`. Verification via `certutil -hashfile <wav> SHA256`. The `--compose` CLI takes an instrument patch first and the composition template via `--template`.

---

## Representative render set (used for golden verification at every task)

Paths below are relative to the worktree root; substitute as appropriate. All five must produce byte-identical WAVs as specified in `docs/superpowers/plans/2026-04-14-baseline-hashes.txt`.

```
mforce_cli.exe --compose patches/PluckU.json renders/k467_v1         1 --template patches/test_k467_v1.json
mforce_cli.exe --compose patches/PluckU.json renders/k467_v2         1 --template patches/test_k467_v2.json
mforce_cli.exe --compose patches/PluckU.json renders/k467_v3         1 --template patches/test_k467_v3.json
mforce_cli.exe --compose patches/PluckU.json renders/k467_v4         1 --template patches/test_k467_v4.json
mforce_cli.exe --compose patches/PluckU.json renders/k467_structural 1 --template patches/test_k467_structural.json
```

Baseline hashes (from `2026-04-14-baseline-hashes.txt`):
```
k467_v1_1.wav            1e9754c9fe129d161ce6e2889333d773de4f39195caef71c5fc1de6d8b7f9354
k467_v2_1.wav            9ae4904c4999cd8797054c8f48f384f046a30831d12ee07ca55a64e1e8e85c1b
k467_v3_1.wav            418107958eae5c6e8bb9ffdc4202b7252c3e3bf97d544abfb55f054fa0649bec
k467_v4_1.wav            59927f4b5756fb5dc07d381ed4f3793e27027b64d0ab021f4e5c977648c9f0ab
k467_structural_1.wav    d7be258e0e6c5261708a434e476423a7bd05275752c495d9673a504d5b9c5ce2
```

**"Verify goldens"** means: re-run the five render commands, SHA-256 each output WAV, confirm every hash matches the baseline. No mismatches allowed.

---

## File structure (what each task touches)

### Files created

- `engine/include/mforce/music/chord_progression_builder.h` — renamed from `harmony_composer.h` (Task 6b)

### Files modified (main changes per task)

- `engine/include/mforce/music/templates.h` — PieceTemplate gains motif pool maps + accessors (Task 1); Motif gains metadata (Task 5); FigureConnector adds leadStep (Task 4)
- `engine/include/mforce/music/composer.h` — `realize_motifs_` moves to free function (Task 1); Composer pool maps become thin forwarders (Task 1) then are deleted (Task 3); Locus construction uses non-const PieceTemplate (Task 3); HarmonyComposer reference updated (Task 6b)
- `engine/include/mforce/music/locus.h` — `Composer*` field removed, `PieceTemplate*` becomes non-const (Task 3)
- `engine/include/mforce/music/piece_utils.h` — `reader_before` and range queries added (Task 6)
- `engine/include/mforce/music/templates_json.h` — Motif JSON expanded (Task 5); FigureConnector JSON gains leadStep field + bare-int shorthand (Task 4)
- `engine/include/mforce/music/shape_strategies.h` — call-site migration from `locus.composer->*motif*` to `locus.pieceTemplate->*motif*` (Task 2)
- `engine/include/mforce/music/phrase_strategies.h` — same call-site migration (Task 2)

### Files deleted

- `engine/include/mforce/music/harmony_composer.h` — replaced by `chord_progression_builder.h` (Task 6b)

---

## Task 0: Baseline reverify + branch setup

**Files:**
- None (setup only)

- [ ] **Step 1: Create the working branch**

```
git -C C:/@dev/repos/mforce checkout main
git -C C:/@dev/repos/mforce pull
git -C C:/@dev/repos/mforce checkout -b composer-model-infrastructure
```

Expected: working on new branch `composer-model-infrastructure`.

- [ ] **Step 2: Build the current state**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```

If `build/` is absent, configure first:
```
cmake -S C:/@dev/repos/mforce -B C:/@dev/repos/mforce/build
```

Expected: `build/tools/mforce_cli/Release/mforce_cli.exe` exists and builds without error.

- [ ] **Step 3: Render the representative set**

Run the 5 render commands from the "Representative render set" section above.

Expected: five WAVs produced, each printing `Composed #1: renders/k467_*.wav`.

- [ ] **Step 4: Verify hashes match baseline**

For each WAV, run:
```
certutil -hashfile C:/@dev/repos/mforce/renders/k467_v1_1.wav SHA256
```
(and similarly for v2, v3, v4, structural).

Expected: every hash matches the corresponding entry in `docs/superpowers/plans/2026-04-14-baseline-hashes.txt`. If any mismatch, stop here and investigate — the baseline drifted in a way not related to this plan.

---

## Task 1: Relocate motif pool to `PieceTemplate`

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (add pool maps + accessors to PieceTemplate)
- Modify: `engine/include/mforce/music/composer.h` (extract `realize_motifs_` to free function; Composer's pool maps become thin forwarders)

- [ ] **Step 1: Add pool maps and accessors to `PieceTemplate`**

Edit `engine/include/mforce/music/templates.h`. In the `PieceTemplate` struct, add after the existing `std::vector<Motif> motifs;`:

```cpp
// Realized motif content, populated during setup_piece_ by realize_motifs.
// Keyed by motif name. Separate maps per content-kind parallel the existing
// Composer-side maps.
std::unordered_map<std::string, MelodicFigure> realizedFigures;
std::unordered_map<std::string, PulseSequence> realizedRhythms;
std::unordered_map<std::string, StepSequence>  realizedContours;

// Read accessors — used by strategies during compose.
const MelodicFigure* find_motif(const std::string& name) const {
  auto it = realizedFigures.find(name);
  return it == realizedFigures.end() ? nullptr : &it->second;
}
const PulseSequence* find_rhythm_motif(const std::string& name) const {
  auto it = realizedRhythms.find(name);
  return it == realizedRhythms.end() ? nullptr : &it->second;
}
const StepSequence* find_contour_motif(const std::string& name) const {
  auto it = realizedContours.find(name);
  return it == realizedContours.end() ? nullptr : &it->second;
}
```

Ensure `<unordered_map>` is included near the top of the file if not already present.

- [ ] **Step 2: Introduce free function `realize_motifs` that operates on `PieceTemplate&`**

Edit `engine/include/mforce/music/composer.h`. Find the existing private method `realize_motifs_` on Composer. Extract its body into a free function declared in the same file's namespace, above the Composer class:

```cpp
// Free function — populates the realized motif maps on PieceTemplate
// from PieceTemplate::motifs. Called by Composer::setup_piece_.
// Writes into tmpl.realizedFigures / realizedRhythms / realizedContours.
inline void realize_motifs(PieceTemplate& tmpl) {
    // ... body lifted verbatim from Composer::realize_motifs_ ...
    // Replace `realizedMotifs_[...]` with `tmpl.realizedFigures[...]`
    // Replace `realizedRhythms_[...]` with `tmpl.realizedRhythms[...]`
    // Replace `realizedContours_[...]` with `tmpl.realizedContours[...]`
}
```

Keep Composer's private `realize_motifs_` member but rewrite its body to simply call the free function and then copy (temporarily) from `tmpl.realizedFigures` into Composer's own member maps for backward compat with Task 2's not-yet-migrated call sites:

```cpp
void realize_motifs_(const Piece& piece, PieceTemplate& tmpl) {
  (void)piece;
  ::mforce::realize_motifs(tmpl);
  // Transition-mode: mirror template-side maps into Composer-side maps so
  // existing callers (`composer->realized_motifs()`, `find_rhythm_motif(name)`,
  // `find_contour_motif(name)`) keep working until Task 2 migrates them.
  realizedMotifs_   = tmpl.realizedFigures;
  realizedRhythms_  = tmpl.realizedRhythms;
  realizedContours_ = tmpl.realizedContours;
}
```

Update the call site in `Composer::setup_piece_` — currently `realize_motifs_(piece, tmpl);`. Ensure `tmpl` parameter is non-const (was `const PieceTemplate&`, must become `PieceTemplate&`). The `setup_piece_` caller may need to const_cast or change its own signature. Cast at the call site rather than propagating `non-const` throughout if that touches many sites — this is transitional.

- [ ] **Step 3: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```
Run the 5 render commands, hash, compare to baseline. All must match.

- [ ] **Step 4: Commit Task 1**

```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/templates.h engine/include/mforce/music/composer.h
git -C C:/@dev/repos/mforce commit -m "refactor(composer): relocate motif pool to PieceTemplate

Introduces mforce::realize_motifs free function writing to the
PieceTemplate-side realizedFigures / realizedRhythms / realizedContours
maps. Composer::realize_motifs_ now mirrors those maps into its own
member maps during transition (call sites migrate in Task 2).

Goldens match baseline bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: Migrate strategy call sites to read from `PieceTemplate`

**Files:**
- Modify: `engine/include/mforce/music/composer.h` (inline strategy bodies reading `ctx.composer`/`locus.composer` for motif lookup)
- Modify: `engine/include/mforce/music/shape_strategies.h` (if any bodies there reach motifs)
- Modify: `engine/include/mforce/music/phrase_strategies.h` (same)
- Modify: any other file that calls `composer->realized_motifs()` / `find_rhythm_motif(name)` / `find_contour_motif(name)`

- [ ] **Step 1: Grep for all call sites**

Run:
```
grep -rn 'realized_motifs\|find_rhythm_motif\|find_contour_motif' C:/@dev/repos/mforce/engine/ C:/@dev/repos/mforce/tools/
```

Record every hit. Expected sites (from prior knowledge):
- `composer.h` inside `DefaultFigureStrategy::realize_figure` — 4 sites in the Reference/Transform case branches
- `composer.h` detail::resolve_rhythm — 1 site
- `composer.h` detail::resolve_contour — 1 site
- `composer.h` ShapeCadentialApproachStrategy body — 1 site

- [ ] **Step 2: Replace `locus.composer->realized_motifs()` with `locus.pieceTemplate->realizedFigures`**

For each hit, replace:
- `locus.composer->realized_motifs()` → `locus.pieceTemplate->realizedFigures`
- `locus.composer->find_rhythm_motif(name)` → `locus.pieceTemplate->find_rhythm_motif(name)`
- `locus.composer->find_contour_motif(name)` → `locus.pieceTemplate->find_contour_motif(name)`
- (equivalent `ctx.composer->*` if any remain anywhere)

Mind semantic equivalence: `locus.composer->realized_motifs()` returned `const std::unordered_map&`, and `locus.pieceTemplate->realizedFigures` is that exact map mirrored. Callers that do `auto it = map.find(name)` keep working unchanged.

- [ ] **Step 3: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```
Then render-set + hash-verify as in Task 0 Steps 3–4. Hashes must match.

- [ ] **Step 4: Commit Task 2**

```
git -C C:/@dev/repos/mforce add -u engine/
git -C C:/@dev/repos/mforce commit -m "refactor(composer): strategies read motifs from PieceTemplate

Migrates every locus.composer->realized_motifs() /
locus.composer->find_rhythm_motif(n) / locus.composer->find_contour_motif(n)
to locus.pieceTemplate->realizedFigures /
locus.pieceTemplate->find_rhythm_motif(n) / locus.pieceTemplate->find_contour_motif(n).

Composer-side pool maps still exist (populated by realize_motifs_
mirroring); they're vestigial post-this-task and get deleted in Task 3.

Goldens match bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: Drop `Composer*` from `Locus`; delete vestigial Composer pool maps

**Files:**
- Modify: `engine/include/mforce/music/locus.h` (remove Composer* field and fwd decl)
- Modify: `engine/include/mforce/music/composer.h` (delete Composer pool maps and their accessors; update Locus construction sites)

- [ ] **Step 1: Remove `Composer*` from `Locus`**

Edit `engine/include/mforce/music/locus.h`:

```cpp
#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

namespace mforce {

// `Composer*` is gone from Locus. If a strategy needs Composer-level
// services beyond what PieceTemplate provides, that's a sign those
// services should move to PieceTemplate (motif pool did).

struct Locus {
  const Piece* piece;
  PieceTemplate* pieceTemplate;   // non-const — plan_* may mutate in Plan B
  int sectionIdx;
  int partIdx;
  int passageIdx{-1};
  int phraseIdx{-1};
  int figureIdx{-1};

  Locus with_passage(int p) const { Locus l = *this; l.passageIdx = p; l.phraseIdx = -1; l.figureIdx = -1; return l; }
  Locus with_phrase (int p) const { Locus l = *this; l.phraseIdx  = p; l.figureIdx = -1; return l; }
  Locus with_figure (int f) const { Locus l = *this; l.figureIdx  = f; return l; }
};

} // namespace mforce
```

- [ ] **Step 2: Delete Composer's pool maps and accessors**

In `engine/include/mforce/music/composer.h`, delete:

- Private members `realizedMotifs_`, `realizedRhythms_`, `realizedContours_`.
- Public accessors `realized_motifs()`, `find_rhythm_motif(name)`, `find_contour_motif(name)`.
- The mirror-copying lines inside `realize_motifs_` (retained during Task 1 for backward compat).

`realize_motifs_` now simply calls the free function:
```cpp
void realize_motifs_(PieceTemplate& tmpl) { ::mforce::realize_motifs(tmpl); }
```

Update the call in `Composer::setup_piece_` accordingly (drop the now-absent `const Piece&` argument if the signature was changed earlier).

- [ ] **Step 3: Update Locus construction sites in `composer.h`**

Grep for `Locus{` and `Locus locus{`:
```
grep -n 'Locus{' C:/@dev/repos/mforce/engine/include/mforce/music/composer.h
```

Each construction that passed `this` (the Composer pointer) as the third positional field needs that argument removed. Typical current shape:
```cpp
Locus locus{&piece, &tmpl, this, sectionIdx, partIdx};
```
Becomes:
```cpp
Locus locus{&piece, &tmpl, sectionIdx, partIdx};
```

`&tmpl` currently might be `const PieceTemplate*` — now `Locus.pieceTemplate` is non-const, so callers need `const_cast<PieceTemplate*>(&tmpl)` if the caller still has a const reference, OR the caller's signature needs updating. Prefer signature updates where the call chain is short; const_cast only as a transitional shim.

- [ ] **Step 4: Build; fix any residual compile errors**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```

Expected errors to find and fix:
- Any remaining `ctx.composer->*motif*` or `locus.composer->*motif*` (Task 2 should have caught these, but a build check here catches stragglers)
- Any remaining `composer->realize_motifs_(const Piece&, PieceTemplate&)` signature mismatch
- Any `struct Composer;` forward declaration in locus.h — delete it

- [ ] **Step 5: Render + verify goldens**

Full 5-render hash check. All must match.

- [ ] **Step 6: Commit Task 3**

```
git -C C:/@dev/repos/mforce add -u engine/
git -C C:/@dev/repos/mforce commit -m "refactor(composer): drop Composer* from Locus; delete vestigial pool maps

Locus now holds only Piece* + PieceTemplate* + index coordinates.
PieceTemplate* becomes non-const so future plan_* methods can mutate
the pool; during this plan's remaining tasks, mutation does not occur.

Composer-side realizedMotifs_ / realizedRhythms_ / realizedContours_
and their accessors are gone. The motif pool now lives only on
PieceTemplate.

Goldens match bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: Add `FigureConnector.leadStep` field

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (add `leadStep` to FigureConnector)
- Modify: `engine/include/mforce/music/templates_json.h` (to_json / from_json for FigureConnector incl. bare-int shorthand)
- Modify: `engine/include/mforce/music/composer.h` and/or `default_strategies.h` (phrase-realization path applies connector.leadStep when placing a figure's first note — see semantics below)

- [ ] **Step 1: Add `leadStep` field to `FigureConnector`**

Edit `engine/include/mforce/music/templates.h`. Find `struct FigureConnector`. Add:

```cpp
struct FigureConnector {
  int elideCount{0};
  float adjustCount{0};
  int leadStep{0};   // NEW — step to place the following figure's first
                     // note, measured from the post-elide/adjust cursor.
                     // Defaults to 0 for backward compatibility.
};
```

- [ ] **Step 2: Update `FigureConnector` JSON round-trip**

Edit `engine/include/mforce/music/templates_json.h`. Find existing `to_json(FigureConnector)` / `from_json(FigureConnector)`.

`to_json`:
```cpp
inline void to_json(nlohmann::json& j, const FigureConnector& fc) {
  // Emit only the fields that differ from their default, for terse output.
  j = nlohmann::json::object();
  if (fc.elideCount != 0)  j["elideCount"]  = fc.elideCount;
  if (fc.adjustCount != 0) j["adjustCount"] = fc.adjustCount;
  if (fc.leadStep != 0)    j["leadStep"]    = fc.leadStep;
}
```

`from_json`: accept three JSON shapes — full object, partial object, bare integer shorthand.
```cpp
inline void from_json(const nlohmann::json& j, FigureConnector& fc) {
  fc = FigureConnector{};   // reset to defaults
  if (j.is_number_integer()) {
    // Shorthand: bare int = {leadStep: N, elideCount: 0, adjustCount: 0}
    fc.leadStep = j.get<int>();
    return;
  }
  if (j.is_null()) {
    // null = all defaults
    return;
  }
  if (!j.is_object()) {
    throw std::runtime_error("FigureConnector: expected object, integer, or null");
  }
  fc.elideCount  = j.value("elideCount", 0);
  fc.adjustCount = j.value("adjustCount", 0.0f);
  fc.leadStep    = j.value("leadStep", 0);
}
```

- [ ] **Step 3: Wire `leadStep` into phrase realization**

Edit `engine/include/mforce/music/composer.h` (or wherever the phrase-realization path applies connectors). Locate the connector-processing block inside `DefaultPhraseStrategy::realize_phrase` — the code that handles `conn.elideCount` and `conn.adjustCount`. After the elide and adjust operations apply (and the running cursor has rewound for elided steps), the strategy must, before stamping the next figure's first unit's step, ADD `conn.leadStep` to the expected placement delta.

Concretely: the current code computes the first unit's step as it was authored on the FigureTemplate. Under the new model, the effective first-unit step becomes `authored_first_step + conn.leadStep`. Since Task 4 does not yet enforce "motifs are placement-neutral" (Plan B's work), we apply `leadStep` additively on top of whatever the figure already had.

For goldens to match: existing templates author `leadStep = 0` (default), so the addition is a no-op. New templates (not authored yet) can use `leadStep` to express placement.

Implementation (pseudocode; locate the exact site by grep for `conn.elideCount` in `composer.h`):
```cpp
// Existing: elide + adjust logic on prevFig
// NEW: apply leadStep to the NEXT figure's first unit, at the site where
// fig (the newly-built MelodicFigure from realize_figure) is about to be
// appended to phrase.
if (!fig.units.empty()) {
  fig.units[0].step += conn.leadStep;
}
```

- [ ] **Step 4: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```
Hash-verify the 5 goldens.

**Failure debugging:** if hashes differ, the most likely cause is double-application of `leadStep` (once by the figure's authored first-step, once by connector). Trace one failing render through the phrase-realization path to see where the extra step is coming from.

- [ ] **Step 5: Commit Task 4**

```
git -C C:/@dev/repos/mforce add -u engine/
git -C C:/@dev/repos/mforce commit -m "feat(composer): FigureConnector.leadStep field + JSON shorthand

Adds leadStep (default 0) to FigureConnector for placement control.
JSON round-trip accepts full object, partial object, or bare integer
shorthand (int -> {leadStep: N, elideCount: 0, adjustCount: 0}).

Phrase-realization path applies leadStep additively to the first unit
of each figure after its template is realized. With leadStep=0 (default),
behavior is unchanged. Goldens match bit-identically.

Plan B will complete the placement-neutral motif story (first unit's
authored step becomes 0 by convention; leadStep becomes the sole
source of placement). This task only adds the field and JSON.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: Expand `Motif` with metadata (roles, origin, derivation)

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (enums + fields on Motif)
- Modify: `engine/include/mforce/music/templates_json.h` (Motif JSON round-trip)
- Modify: `engine/include/mforce/music/composer.h` (`realize_motifs` free function — accommodate topological order if derived motifs present)

- [ ] **Step 1: Add `MotifRole` and `MotifOrigin` enums**

Edit `engine/include/mforce/music/templates.h`. Add near the top of the namespace (after any existing enums, before `struct Motif`):

```cpp
enum class MotifRole {
  Thematic,
  Cadential,
  PostCadential,
  Discursive,
  Climactic,
  Connective,
  Ornamental,
};

enum class MotifOrigin {
  User,
  Generated,
  Derived,
  Extracted,
};
```

- [ ] **Step 2: Extend `Motif` struct with metadata fields**

In `templates.h`, find `struct Motif`. Add after the existing fields:

```cpp
struct Motif {
  // ... existing name, content, generationSeed ...

  std::set<MotifRole> roles;                       // NEW — multi-tag; empty default
  MotifOrigin origin{MotifOrigin::User};            // NEW
  std::optional<std::string> derivedFrom;           // NEW — parent motif name
  std::optional<TransformOp> transform;             // NEW — how derived
  int transformParam{0};                            // NEW
};
```

Ensure `<set>` and `<optional>` are included at the top of the file if not already.

- [ ] **Step 3: Add JSON helpers for `MotifRole` and `MotifOrigin`**

Edit `engine/include/mforce/music/templates_json.h`. Add string-enum converters:

```cpp
NLOHMANN_JSON_SERIALIZE_ENUM(MotifRole, {
  {MotifRole::Thematic,      "Thematic"},
  {MotifRole::Cadential,     "Cadential"},
  {MotifRole::PostCadential, "PostCadential"},
  {MotifRole::Discursive,    "Discursive"},
  {MotifRole::Climactic,     "Climactic"},
  {MotifRole::Connective,    "Connective"},
  {MotifRole::Ornamental,    "Ornamental"},
})

NLOHMANN_JSON_SERIALIZE_ENUM(MotifOrigin, {
  {MotifOrigin::User,      "User"},
  {MotifOrigin::Generated, "Generated"},
  {MotifOrigin::Derived,   "Derived"},
  {MotifOrigin::Extracted, "Extracted"},
})
```

- [ ] **Step 4: Update `Motif` JSON to_json / from_json**

Find existing `to_json(Motif)` / `from_json(Motif)` in `templates_json.h`. Extend them:

```cpp
inline void to_json(nlohmann::json& j, const Motif& m) {
  j = nlohmann::json::object();
  j["name"] = m.name;
  // ... existing content serialization ...
  if (m.generationSeed != 0) j["generationSeed"] = m.generationSeed;

  // New fields — emit only when non-default for terse output.
  if (!m.roles.empty()) {
    j["roles"] = nlohmann::json::array();
    for (auto r : m.roles) j["roles"].push_back(r);
  }
  if (m.origin != MotifOrigin::User) j["origin"] = m.origin;
  if (m.derivedFrom)                 j["derivedFrom"] = *m.derivedFrom;
  if (m.transform)                   j["transform"] = *m.transform;
  if (m.transformParam != 0)         j["transformParam"] = m.transformParam;
}

inline void from_json(const nlohmann::json& j, Motif& m) {
  m = Motif{};
  j.at("name").get_to(m.name);
  // ... existing content deserialization ...
  m.generationSeed = j.value("generationSeed", 0u);

  if (j.contains("roles") && j.at("roles").is_array()) {
    for (const auto& r : j.at("roles")) {
      m.roles.insert(r.get<MotifRole>());
    }
  }
  m.origin = j.value("origin", MotifOrigin::User);
  if (j.contains("derivedFrom") && !j.at("derivedFrom").is_null()) {
    m.derivedFrom = j.at("derivedFrom").get<std::string>();
  }
  if (j.contains("transform") && !j.at("transform").is_null()) {
    m.transform = j.at("transform").get<TransformOp>();
  }
  m.transformParam = j.value("transformParam", 0);
}
```

Verify the existing `TransformOp` enum has a JSON converter. If not, add one (`NLOHMANN_JSON_SERIALIZE_ENUM` for TransformOp) with entries matching the existing enum values.

- [ ] **Step 5: Add `RhythmTail` to `TransformOp` (referenced in Plan B spec)**

Edit `engine/include/mforce/music/templates.h`. Find the `TransformOp` enum:

```cpp
enum class TransformOp {
  // ... existing: None, Invert, Reverse, Stretch, Compress, VaryRhythm,
  //     VarySteps, NewSteps, NewRhythm, Replicate, TransformGeneral ...
  RhythmTail,   // NEW — derive a PulseSequence from a MelodicFigure by
                // taking the rhythm tail (skip first N pulses); param = N.
};
```

If a `TransformOp` JSON `NLOHMANN_JSON_SERIALIZE_ENUM` exists, add `{TransformOp::RhythmTail, "RhythmTail"}` to it.

Note: the `apply_transform` method in `DefaultFigureStrategy` does NOT need to handle `RhythmTail` in this plan — that's Plan B's concern. This task only adds the enum value for downstream use.

- [ ] **Step 6: Update `realize_motifs` to handle derivation order**

Edit `engine/include/mforce/music/composer.h`. In the free function `realize_motifs` (from Task 1), update it to realize motifs in order that respects `derivedFrom`: a motif with `derivedFrom = "X"` must be realized AFTER motif X.

Simplest implementation that covers the current case (all user-authored motifs have no derivation):

```cpp
inline void realize_motifs(PieceTemplate& tmpl) {
  // Process in declaration order, but defer motifs whose derivedFrom is
  // not yet realized. Repeat until no more motifs can be realized.
  std::vector<const Motif*> pending;
  for (const auto& m : tmpl.motifs) pending.push_back(&m);

  for (int safety = 0; safety < 1000 && !pending.empty(); ++safety) {
    size_t before = pending.size();
    std::vector<const Motif*> stillPending;
    for (const Motif* mp : pending) {
      const Motif& m = *mp;
      if (m.derivedFrom) {
        // Check parent is realized (in at least one map).
        bool parentReady =
          tmpl.realizedFigures.count(*m.derivedFrom) ||
          tmpl.realizedRhythms.count(*m.derivedFrom) ||
          tmpl.realizedContours.count(*m.derivedFrom);
        if (!parentReady) { stillPending.push_back(mp); continue; }
      }
      // Realize m — body lifted from the existing realize_motifs_ logic.
      // ... existing variant-switch on m.content that populates the
      // appropriate realizedFigures/Rhythms/Contours[m.name] entry ...
    }
    if (stillPending.size() == before) {
      throw std::runtime_error("realize_motifs: circular or dangling derivedFrom reference");
    }
    pending = std::move(stillPending);
  }
}
```

For this plan's task, no existing motif has `derivedFrom` set — the topological logic is present but inert; the loop degenerates to one pass matching existing behavior exactly.

- [ ] **Step 7: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```
Hash-verify the 5 goldens.

- [ ] **Step 8: Commit Task 5**

```
git -C C:/@dev/repos/mforce add -u engine/
git -C C:/@dev/repos/mforce commit -m "feat(composer): Motif metadata (roles, origin, derivation)

Adds to Motif:
- roles: std::set<MotifRole> with 7 values (Thematic, Cadential,
  PostCadential, Discursive, Climactic, Connective, Ornamental).
  Multi-tag, empty default.
- origin: MotifOrigin (User/Generated/Derived/Extracted, default User)
- derivedFrom: std::optional<std::string> — parent motif name
- transform: std::optional<TransformOp> — how derivation was done
- transformParam: int — transform-specific parameter

Adds TransformOp::RhythmTail (for Plan B's rhythm-derived figures;
apply_transform handling in this plan is a no-op for this new op).

realize_motifs gains topological ordering that respects derivedFrom.
No existing motif has derivedFrom set, so the ordering is inert; the
loop degenerates to one pass matching prior behavior. Existing JSON
patches load unchanged; all new fields default to empty/None/0.

Goldens match bit-identically.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: Add history-query helpers to `piece_utils`

**Files:**
- Modify: `engine/include/mforce/music/piece_utils.h` (add `reader_before`, `range_in_*_before`)

- [ ] **Step 1: Add `reader_before` helper**

Edit `engine/include/mforce/music/piece_utils.h`. Add after the existing `pitch_before`:

```cpp
// Returns a PitchReader seeded to pitch_before(locus), with the current
// section's Scale. Strategies that need to walk forward from the prior
// cursor use this instead of constructing PitchReader manually.
inline PitchReader reader_before(const Locus& locus) {
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  PitchReader pr(scale);
  pr.set_pitch(pitch_before(locus));
  return pr;
}
```

- [ ] **Step 2: Add `PitchRange` struct**

In `piece_utils.h`, above the range-query function declarations:

```cpp
struct PitchRange {
  Pitch lowest;
  Pitch highest;
  bool empty{true};
};
```

- [ ] **Step 3: Add `range_in_phrase_before`**

```cpp
// Lowest/highest pitches realized within the CURRENT phrase, from figure 0
// up to (but not including) locus.figureIdx. If locus.figureIdx <= 0 or
// the current phrase has no realized content yet, returns empty=true.
inline PitchRange range_in_phrase_before(const Locus& locus) {
  PitchRange r;
  const Passage* pass = passage_at(locus);
  if (!pass) return r;
  if (locus.phraseIdx < 0 || locus.phraseIdx >= (int)pass->phrases.size()) return r;

  const Phrase& ph = pass->phrases[locus.phraseIdx];
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  PitchReader pr(scale);
  pr.set_pitch(ph.startingPitch);
  int endFig = locus.figureIdx < 0 ? (int)ph.figures.size() : locus.figureIdx;
  for (int fi = 0; fi < endFig; ++fi) {
    for (const auto& unit : ph.figures[fi]->units) {
      pr.step(unit.step);
      if (unit.rest) continue;
      Pitch p = pr.get_pitch();
      if (r.empty) { r.lowest = p; r.highest = p; r.empty = false; }
      else {
        if (p.note_number() < r.lowest.note_number()) r.lowest = p;
        if (p.note_number() > r.highest.note_number()) r.highest = p;
      }
    }
  }
  return r;
}
```

- [ ] **Step 4: Add `range_in_passage_before`**

```cpp
inline PitchRange range_in_passage_before(const Locus& locus) {
  PitchRange r;
  const Passage* pass = passage_at(locus);
  if (!pass) return r;
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  PitchReader pr(scale);
  // Seed from passage template's startingPitch.
  const auto& passTmpl = locus.pieceTemplate
                              ->parts[locus.partIdx]
                              .passages.at(locus.piece->sections[locus.sectionIdx].name);
  if (passTmpl.startingPitch) pr.set_pitch(*passTmpl.startingPitch);
  else if (!pass->phrases.empty()) pr.set_pitch(pass->phrases[0].startingPitch);

  int endPhrase = locus.phraseIdx < 0 ? (int)pass->phrases.size() : locus.phraseIdx;
  // All phrases 0..endPhrase-1 completely walked, plus partial phrase at phraseIdx.
  for (int pi = 0; pi < endPhrase; ++pi) {
    pr.set_pitch(pass->phrases[pi].startingPitch);
    for (const auto& fig : pass->phrases[pi].figures) {
      for (const auto& unit : fig->units) {
        pr.step(unit.step);
        if (unit.rest) continue;
        Pitch p = pr.get_pitch();
        if (r.empty) { r.lowest = p; r.highest = p; r.empty = false; }
        else {
          if (p.note_number() < r.lowest.note_number()) r.lowest = p;
          if (p.note_number() > r.highest.note_number()) r.highest = p;
        }
      }
    }
  }
  if (locus.phraseIdx >= 0 && locus.phraseIdx < (int)pass->phrases.size() && locus.figureIdx > 0) {
    const Phrase& ph = pass->phrases[locus.phraseIdx];
    pr.set_pitch(ph.startingPitch);
    int endFig = std::min(locus.figureIdx, (int)ph.figures.size());
    for (int fi = 0; fi < endFig; ++fi) {
      for (const auto& unit : ph.figures[fi]->units) {
        pr.step(unit.step);
        if (unit.rest) continue;
        Pitch p = pr.get_pitch();
        if (r.empty) { r.lowest = p; r.highest = p; r.empty = false; }
        else {
          if (p.note_number() < r.lowest.note_number()) r.lowest = p;
          if (p.note_number() > r.highest.note_number()) r.highest = p;
        }
      }
    }
  }
  return r;
}
```

- [ ] **Step 5: Add `range_in_piece_before`**

```cpp
// Within the current Part (locus.partIdx) across all sections of the
// piece, considering every realized passage up to and including the
// current passage up to this Locus.
inline PitchRange range_in_piece_before(const Locus& locus) {
  PitchRange r;
  if (locus.partIdx < 0 || locus.partIdx >= (int)locus.piece->parts.size()) return r;
  const Part& part = locus.piece->parts[locus.partIdx];

  // Walk every prior section's passage fully, then current section's passage
  // up to the Locus via range_in_passage_before.
  for (int si = 0; si < locus.sectionIdx; ++si) {
    const auto& secName = locus.piece->sections[si].name;
    auto it = part.passages.find(secName);
    if (it == part.passages.end()) continue;
    // Full walk of this passage — merge into r.
    const Scale& scale = locus.piece->sections[si].scale;
    PitchReader pr(scale);
    if (!it->second.phrases.empty()) pr.set_pitch(it->second.phrases[0].startingPitch);
    for (const auto& ph : it->second.phrases) {
      pr.set_pitch(ph.startingPitch);
      for (const auto& fig : ph.figures) {
        for (const auto& unit : fig->units) {
          pr.step(unit.step);
          if (unit.rest) continue;
          Pitch p = pr.get_pitch();
          if (r.empty) { r.lowest = p; r.highest = p; r.empty = false; }
          else {
            if (p.note_number() < r.lowest.note_number()) r.lowest = p;
            if (p.note_number() > r.highest.note_number()) r.highest = p;
          }
        }
      }
    }
  }

  // Merge in current-passage partial.
  PitchRange partial = range_in_passage_before(locus);
  if (!partial.empty) {
    if (r.empty) { r = partial; }
    else {
      if (partial.lowest.note_number()  < r.lowest.note_number())  r.lowest  = partial.lowest;
      if (partial.highest.note_number() > r.highest.note_number()) r.highest = partial.highest;
    }
  }
  return r;
}
```

- [ ] **Step 6: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```

No consumer yet — goldens trivially unchanged. Hash-verify anyway.

- [ ] **Step 7: Commit Task 6**

```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/piece_utils.h
git -C C:/@dev/repos/mforce commit -m "feat(composer): piece_utils history-query helpers

Adds:
- reader_before(Locus): PitchReader seeded to pitch_before with
  the current section's scale
- PitchRange { lowest, highest, empty }
- range_in_phrase_before(Locus): min/max pitches in current phrase
  up to the Locus
- range_in_passage_before(Locus): min/max across current passage up
  to the Locus (including prior phrases complete, current partial)
- range_in_piece_before(Locus): min/max across this Part's prior
  sections and current passage up to Locus

Purely additive, no consumer yet. Goldens trivially unchanged.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Task 6b: Rename `HarmonyComposer` → `ChordProgressionBuilder`

**Files:**
- Rename: `engine/include/mforce/music/harmony_composer.h` → `engine/include/mforce/music/chord_progression_builder.h`
- Modify: `engine/include/mforce/music/composer.h` (update include + one call site)
- Modify: any other file including `harmony_composer.h`

- [ ] **Step 1: Grep for every reference to `HarmonyComposer` and `harmony_composer.h`**

```
grep -rn 'HarmonyComposer\|harmony_composer' C:/@dev/repos/mforce/engine/ C:/@dev/repos/mforce/tools/
```

Expected sites:
- `engine/include/mforce/music/harmony_composer.h` (the file itself, line 1+ — `struct HarmonyComposer`)
- `engine/include/mforce/music/composer.h` — `#include "mforce/music/harmony_composer.h"` and `HarmonyComposer::build(...)` call in `setup_piece_`

- [ ] **Step 2: Copy the file to the new name and rename the struct**

```
cp C:/@dev/repos/mforce/engine/include/mforce/music/harmony_composer.h C:/@dev/repos/mforce/engine/include/mforce/music/chord_progression_builder.h
```

Edit the new file `engine/include/mforce/music/chord_progression_builder.h`:
- Replace every occurrence of `HarmonyComposer` with `ChordProgressionBuilder`.
- Update the top-of-file comment block to reflect the rename and throwaway-status context (see spec's Harmony resolution section).

Example top-of-file comment:
```cpp
// ---------------------------------------------------------------------------
// ChordProgressionBuilder — named-progression lookup and scaling.
//
// Throwaway-grade: hard-coded named lambdas, no context awareness (doesn't
// know the Piece key, doesn't consult prior Sections, doesn't plan
// modulations). Solves exactly one problem: "stop making every patch
// hand-author a 4-chord array in JSON."
//
// When/if we want pluggable context-aware progression generation, promote
// to a ChordProgressionStrategy with plan_* / compose_* pattern. For now,
// this is sufficient for melody-only openings like K467 bars 1-12.
// ---------------------------------------------------------------------------
```

- [ ] **Step 3: Update `composer.h`**

Edit `engine/include/mforce/music/composer.h`:
- Replace `#include "mforce/music/harmony_composer.h"` with `#include "mforce/music/chord_progression_builder.h"`.
- Replace the single call site `HarmonyComposer::build(sd.progressionName, sd.beats)` with `ChordProgressionBuilder::build(sd.progressionName, sd.beats)`.

- [ ] **Step 4: Delete the old file**

```
git -C C:/@dev/repos/mforce rm engine/include/mforce/music/harmony_composer.h
```

- [ ] **Step 5: Build and verify goldens**

```
cmake --build C:/@dev/repos/mforce/build --config Release --target mforce_cli
```
Hash-verify the 5 goldens. Purely mechanical rename — hashes must match.

- [ ] **Step 6: Commit Task 6b**

```
git -C C:/@dev/repos/mforce add engine/include/mforce/music/chord_progression_builder.h engine/include/mforce/music/composer.h
git -C C:/@dev/repos/mforce commit -m "refactor(composer): rename HarmonyComposer -> ChordProgressionBuilder

'HarmonyComposer' was over-promoted (Composer is already the orchestrator
class) and 'Harmony' is ambiguous (contrapuntal coincidence, voicing,
inversions — none of which this type handles). 'ChordProgressionBuilder'
names exactly what it does: builds chord progressions from named
recipes.

Pure rename — same API, same 5 named progressions, same static build()
method. Single call site in Composer::setup_piece_ updated. No
behavioral change; goldens match bit-identically.

Throwaway-status preserved and documented in the new file's top comment.
Future context-aware strategy will be a new type (ChordProgressionStrategy)
that plugs into the plan/compose pattern; this builder remains as the
simple named-lookup default.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

## Wrap-up: squash, push, merge

- [ ] **Step 1: Review commit history**

```
git -C C:/@dev/repos/mforce log --oneline main..HEAD
```

Expected 7 commits (Tasks 1, 2, 3, 4, 5, 6, 6b).

- [ ] **Step 2: Push branch**

```
git -C C:/@dev/repos/mforce push -u origin composer-model-infrastructure
```

- [ ] **Step 3: Merge to main when approved**

Either create a PR (`gh pr create ...`) or fast-forward merge locally after review:
```
git -C C:/@dev/repos/mforce checkout main
git -C C:/@dev/repos/mforce merge --ff-only composer-model-infrastructure
git -C C:/@dev/repos/mforce push origin main
git -C C:/@dev/repos/mforce branch -d composer-model-infrastructure
```

---

## Success criteria

1. All 7 Task commits on branch `composer-model-infrastructure` with goldens matching after each.
2. `engine/include/mforce/music/harmony_composer.h` no longer exists.
3. `engine/include/mforce/music/chord_progression_builder.h` exists with equivalent API.
4. `Locus` no longer has a `Composer*` field; `Locus.pieceTemplate` is a non-const pointer.
5. `Composer` has no motif-pool member maps; motifs live on `PieceTemplate`.
6. `Motif` struct has new fields: `roles`, `origin`, `derivedFrom`, `transform`, `transformParam`.
7. `FigureConnector.leadStep` field exists (default 0) with JSON round-trip including bare-int shorthand.
8. `piece_utils` exposes `reader_before` plus three `range_in_*_before` functions.
9. Code compiles clean under `/W4 /permissive-` (MSVC).
10. All five representative K467 renders produce hashes bit-identical to `2026-04-14-baseline-hashes.txt`.

## Risk register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Task 4 (leadStep wiring) causes hash drift because of double-application | Medium | Existing templates all have leadStep=0 (default), so additive application is a no-op. If hashes drift, trace one render; the bug is likely in elide/adjust ordering. |
| Task 1 (pool relocation) breaks motif-dependent patches | Low | During-transition mirror from template→composer maps preserves existing accessors until Task 2 completes migration. |
| Task 5 JSON changes break existing patch loading | Low | All new fields default to empty/None/0; existing JSON has none of them. `from_json` uses `.value(key, default)` for safety. |
| Task 6b accidentally leaves dangling `HarmonyComposer` references | Low | Step 1 grep captures all sites; Step 5 build surfaces anything missed. |
| Non-const `Locus.pieceTemplate` breaks some call site that expected const | Low-Medium | `const_cast` at the call site as a transitional shim; permanent fix when Plan B's plan_* phase requires the mutability. |

---

## Execution

Each task is a clean build + render + hash gate + single commit. Tasks 0, 6 are pure add (no existing behavior touched). Tasks 1–3 are sequential refactor stages. Task 4 adds a field with preserved default semantics. Task 5 adds metadata (new empty/default for existing content). Task 6b is a name change.

None of these stages add new compositional behavior; no new audible output is expected. If you hear something new, something is wrong.

# PatternLibrary + LibraryPassageStrategy — Design Spec

**Date:** 2026-04-27
**Context:** Emerges from the audition session of 2026-04-26 — the AAAB rep test surfaced that "stamp the same motif at different anchors with one breaker at the end" is the smallest pattern that reads as phrase-shaped music. The Pattern Library generalizes this: a library of structural recipes (Patterns) the composer can author once and the framework deploys with new RFB-generated motif content each time.
**Status:** Draft.
**Predecessor:** Foundation refactor (Phrase.connectors, FC.leadStep) on `main` through `0017f2d` (the merged figure-builder-redesign branch).

---

## Goal

I. **Define a JSON schema** for compiled "Passage Pattern Libraries" — the on-disk format the engine reads.

II. **Build a standalone parser tool** that reads `.ppl` files at `lib/ppl/*.ppl` and emits the JSON form to `lib/json/*.json`. The engine never reads `.ppl` directly; the tool runs separately (offline build step).

III. Add a passage-level strategy `library_passage` that:

1. Loads compiled libraries from `lib/json/*.json` (lazy on first use).
2. Picks (or accepts by name) one Pattern matching the passage template's requirements.
3. Generates one RFB motif per unique slot letter in the Pattern, names them, stores them in the piece's motif pool.
4. Walks the Pattern's passage line, deploying each Phrase definition as a Phrase, with figure-level slot references producing Reference figures plus FC values for `~` (random leadStep) and dash-elision (`-`) markers.
5. Applies cadence markers (`hc`, `fc`) per phrase deployment.

The Pattern itself is **structural authoring** (slot positions, which slots reuse, where cadences fall, where elision happens) — it is *not* musical content authoring. Each unique slot resolves to RFB-generated motif content at compose time, so the same Pattern produces different actual music every run.

---

## .ppl file format

A `.ppl` file contains one library with multiple Patterns:

```
Name: Pop1
Genres: Pop, Rock
Roles: Chorus

BillyLife(Verse,Chorus/16): P1:AA~A~B P2:AA~A~-C P3:AA~A~--D | P1 P2hc P1 P3fc
MileyFlowers(Verse/8): P1: AAB | P1 P1
BlueMorn(Verse/16): P1:ABC | P1 P1 P1 P1
```

### Header lines

Header runs from line 1 until the first blank line OR the first line that matches a Pattern format. Order is free. Recognized header keys:

- `Name: <string>` — library identifier (used for diagnostics; not currently a lookup key).
- `Genres: <comma-separated>` — genre tags (parsed but **ignored** in Phase 1).
- `Roles: <comma-separated>` — role tags (parsed but **ignored** in Phase 1).

### Pattern line format

```
<PatternName>(<Roles>/<LengthBars>): <PhraseDef>+ | <PassageDeployment>+
```

- `<PatternName>` — token, no spaces, used for lookup by name.
- `<Roles>` — comma-separated role tags. **Ignored in Phase 1.**
- `<LengthBars>` — integer bars. Beats = bars × 4 (4/4 assumed in Phase 1).
- `<PhraseDef>+` — one or more phrase definitions, space-separated.
- `|` — separator between phrase definitions and passage deployment.
- `<PassageDeployment>+` — one or more phrase references with optional cadence markers.

### Phrase definition syntax

```
P<index>: <FigureToken>+
```

`P<index>` is `P1`, `P2`, `P3`, etc. Token-internal whitespace allowed (e.g. `P1: AAB` and `P1:AAB` both parse).

`<FigureToken>` grammar:

```
<motifSlot> <variant>? <elideAfter>?

motifSlot   ::= [A-Z]
variant     ::= '~' | "'" | "''"
elideAfter  ::= '-'+
```

- **`A`** — motif slot A, default placement (FC.leadStep = 0; cursor walks from previous figure's end).
- **`A~`** — same motif content, new starting note. FC.leadStep set to a random value in `{−3, −2, −1, +1, +2, +3}` (current DefaultPhraseStrategy random-leadStep behavior).
- **`A'`** — motif slot A, actually MODIFIED. Modification semantics deferred (Phase 2+). Phase 1 parser recognizes the token; strategy treats it as `A~` (warn-once) or as `A` (silently) — see Q3.
- **`A''`** — doubly modified. Same Phase 1 deferral.
- **`A-`** — elide 1 unit from the end of the preceding figure (this `A`). FC.elideCount = 1 on the connector AFTER this figure.
- **`A--`** — elide 2 units. Each additional dash = one more elide unit.
- Combined: `A~-` valid (random leadStep AND following figure elides this one's last unit). Order: motif → variant → dashes.

Note the dash position: a dash AFTER a figure means "this figure's last unit gets elided." That maps to FC.elideCount on the connector that follows the elided figure (i.e., on the connector before the next figure). So `AA~A~-C` parses as 4 figures + 4 connectors:

```
fig 0: A   (default placement)             FC[0]: leadStep=0
fig 1: A~  (random leadStep)               FC[1]: leadStep=random
fig 2: A~  (random leadStep)               FC[2]: leadStep=random
fig 3: C   (default placement)             FC[3]: elideCount=1 (truncates fig 2's last unit), leadStep=0
```

### Passage deployment syntax

```
<PhraseRef> <CadenceSuffix>?

PhraseRef     ::= 'P' [0-9]+
CadenceSuffix ::= 'hc' | 'fc'
```

- `P1` — deploy phrase P1, no cadence (cadenceType = 0).
- `P2hc` — deploy P2 with half cadence (cadenceType = 1, cadenceTarget = 4 = scale degree V).
- `P3fc` — deploy P3 with full / authentic cadence (cadenceType = 2, cadenceTarget = 0 = tonic).

Multiple deployments space-separated. Same phrase definition can be deployed multiple times (BillyLife deploys P1 twice).

---

## Architecture

Two-piece split:

- **Standalone tool** `tools/ppl_to_json` — reads `lib/ppl/*.ppl`, emits `lib/json/*.json`. Owns all `.ppl` parsing logic. Run manually or in a build step when patterns change.
- **Engine module** `engine/include/mforce/music/pattern_library.h` — owns the `Pattern` data types and JSON loader. Reads `lib/json/*.json` only. No `.ppl` knowledge.

### JSON schema

Each `.json` file mirrors one `.ppl` file:

```json
{
  "name": "Pop1",
  "genres": ["Pop", "Rock"],
  "roles": ["Chorus"],
  "patterns": [
    {
      "name": "BillyLife",
      "bars": 16,
      "roles": ["Verse", "Chorus"],
      "phrases": [
        {
          "index": 1,
          "figures": [
            {"slot": "A"},
            {"slot": "A", "newAnchor": true},
            {"slot": "A", "newAnchor": true},
            {"slot": "B"}
          ]
        },
        {
          "index": 2,
          "figures": [
            {"slot": "A"},
            {"slot": "A", "newAnchor": true},
            {"slot": "A", "newAnchor": true, "elideAfter": 1},
            {"slot": "C"}
          ]
        }
      ],
      "passage": [
        {"phrase": 1},
        {"phrase": 2, "cadence": "half"},
        {"phrase": 1},
        {"phrase": 3, "cadence": "full"}
      ]
    }
  ]
}
```

Optional fields default: `newAnchor=false`, `elideAfter=0`, `modifiedDepth=0`, `cadence="none"`.

### Engine types (mirror the JSON)

```cpp
namespace mforce::pattern_library {

enum class CadenceMarker { None, Half, Full };

struct FigureToken {
    char  motifSlot;             // 'A'..'Z'
    bool  newAnchor{false};      // ~ marker
    int   modifiedDepth{0};      // 0 = unchanged, 1 = ', 2 = '', ... (Phase 1: warn+ignore)
    int   elideAfter{0};         // dashes after this figure
};

struct PhraseDef {
    int                       index;     // 1, 2, 3 ...
    std::vector<FigureToken>  figures;
};

struct PassageStep {
    int             phraseIndex;  // refers to PhraseDef.index
    CadenceMarker   cadence{CadenceMarker::None};
};

struct Pattern {
    std::string                name;
    int                        bars{0};         // length in bars
    std::vector<std::string>   roles;           // ignored Phase 1
    std::vector<PhraseDef>     phrases;
    std::vector<PassageStep>   passage;
};

struct PatternLibrary {
    std::string                name;
    std::vector<std::string>   genres;          // ignored Phase 1
    std::vector<std::string>   roles;           // ignored Phase 1
    std::vector<Pattern>       patterns;
};

// Load all *.json files from a directory; merge into one library list.
std::vector<PatternLibrary> load_libraries(const std::string& dirPath);

// Parse a single library's JSON contents.
PatternLibrary from_json(const nlohmann::json& j);

// Look up a Pattern by name across all loaded libraries.
const Pattern* find_pattern_by_name(
    const std::vector<PatternLibrary>& libs,
    const std::string& name);

// Pick a Pattern by length (bars) — random across matching patterns.
const Pattern* pick_pattern_by_length(
    const std::vector<PatternLibrary>& libs,
    int bars,
    Randomizer& rng);

} // namespace mforce::pattern_library
```

The standalone `.ppl` parser is forgiving on whitespace (multiple spaces, tabs, optional space after `:` and around `|`) but strict on grammar. Errors include line number + a brief description and abort the conversion.

---

## LibraryPassageStrategy

`engine/include/mforce/music/library_passage_strategy.h` — passage-level strategy registered as `"library_passage"`.

### Config

`PassageTemplate` gains an optional field:

```cpp
struct LibraryPassageConfig {
    // Either a specific pattern name to use, or empty to pick by length.
    std::string patternName;

    // Length budget if pattern not specified by name (passes to picker).
    // Otherwise, the named pattern's bars value is used.
    int barsHint{0};

    // Seed for slot motif generation. 0 = derive from masterSeed.
    uint32_t seed{0};
};

struct PassageTemplate {
    // ... existing ...
    std::optional<LibraryPassageConfig> libraryConfig;
};
```

### Behavior

1. **Resolve Pattern**: if `cfg.patternName` set, look up by name. Else pick by `cfg.barsHint` (from a registered library set, currently loaded from `lib/`).

2. **Resolve seed**: `cfg.seed > 0` → use it; else derive from piece masterSeed and locus.

3. **Generate slot motifs**: for each unique `motifSlot` letter (`'A'`, `'B'`, ...) appearing in the Pattern's PhraseDefs, generate one RFB motif with default constraints (no per-slot config in Phase 1). Name it `<PatternName>_<slot>` (e.g., `"BillyLife_A"`). Store in `pieceTemplate->add_motif(...)`. Each slot appears once in the pool, regardless of how many figures reference it.

4. **Compute figure-beat budget**: per Q2, exact distribution rule TBD. Phase 1 default: total beats = `Pattern.bars × 4`; per-figure beats = `total / total_figure_slot_count`. Used as RFB constraint when generating motifs (each motif's `totalBeats`).

5. **Walk passage**: for each `PassageStep`:
   a. Build a `PhraseTemplate` from the referenced `PhraseDef`. Each `FigureToken` in the def becomes a `FigureTemplate` with `source = Reference`, `motifName = <PatternName>_<slot>`. Per-figure FC values populate `phrase.connectors`:
   - `newAnchor` → `connectors[i].leadStep` synthesized at compose time as random in `{−3..−1, +1..+3}`. **Caveat**: the current DefaultPhraseStrategy already does this for any non-authored connector slot; LibraryPassageStrategy should mark which slots got their leadStep synthesized vs preserved 0, OR the random behavior should be moved into LibraryPassageStrategy itself (see Q4).
   - `elideAfter` → `connectors[i+1].elideCount = elideAfter` (elision applies to the connector that JOINS this figure to the next).
   b. Apply `step.cadence` to the PhraseTemplate's `cadenceType` / `cadenceTarget`.
   c. Compose the Phrase via `default_phrase` (or whatever the registered fallback is); append to the Passage.

6. **Return** the Passage.

### Output invariant

Each `Phrase` in the Passage has:
- `figures.size() == connectors.size()` (foundation-refactor invariant).
- `connectors[0].leadStep == 0` for figures with no `~` marker (figure 0 placement deferred to phrase startingPitch / pitch_before).
- `connectors[0].leadStep != 0` allowed if the first figure in the phrase has a `~` marker.

---

## Pending decisions (Q1–Q5)

Defaults locked in below; override before plan execution to redirect.

### Q1 — Loading patterns from disk vs hardcoding

**Default: load from `lib/json/*.json` at startup or on first `library_passage` use.** Library files version-controlled in the repo. New patterns require no recompile.

Alternative: hardcode the parser's results into a C++ literal table. Faster startup but requires recompile. Rejected — Matt explicitly wants to author patterns by hand.

### Q2 — Length budget distribution (RESOLVED: option D)

**Locked in: option D — last-of-three rule.** When a phrase has exactly 3 figures, the third figure's beat budget is doubled. Otherwise figure budgets are uniform within the phrase.

Implementation:
1. For each phrase definition, compute its figure-unit count: `unitCount = sum(figure_units)` where each figure contributes 1 unit, except the third figure of a 3-figure phrase contributes 2.
2. Sum across all deployments to get `total_units`.
3. `beat_per_unit = Pattern.bars × 4 / total_units`.
4. Each figure's `totalBeats = figure_units × beat_per_unit`.

Worked examples (all yield clean 4 beats/unit = 1 bar/unit):

- **BillyLife** (16 bars, 4 deployments × 4-fig phrase): 4 × 4 = 16 units. 64 / 16 = 4 beats/unit. Each figure = 4 beats (1 bar).
- **MileyFlowers** (8 bars, 2 deployments × 3-fig phrase, last-of-3 doubles): per phrase = 1+1+2 = 4 units; 2 × 4 = 8 units. 32 / 8 = 4 beats/unit. A=4, A=4, B=8 beats.
- **BlueMorn** (16 bars, 4 deployments × 3-fig phrase): per phrase = 1+1+2 = 4 units; 4 × 4 = 16 units. 64 / 16 = 4 beats/unit. A=4, B=4, C=8 beats.

Last-of-three rule applies only when the phrase has exactly 3 figures. Phrases of 2 / 4 / 5+ figures get uniform budgets.

### Q3 — Prime tokens (`A'`, `A''`) in Phase 1

**Default: parse them and warn-once on first use; treat semantically as `A~` (random new anchor) for Phase 1.** Phase 2+ implements actual modification (invert, vary, transform, etc.).

Alternative: silently treat as `A` (no warning, no random anchor). Rejected — masks intent.

Alternative: parse them, error out. Forces Phase 2 implementation before any pattern uses primes. Rejected — Matt's authoring patterns now and primes are part of the syntax.

### Q4 — `~` marker random-leadStep handoff

The current DefaultPhraseStrategy synthesizes a random leadStep for any non-authored connector slot (per the 2026-04-26 calibration). LibraryPassageStrategy needs a way to:
- Authoritatively set leadStep = 0 for `A` figures (no-`~`).
- Authoritatively set leadStep = random for `A~` figures.

Two approaches:

**A.** Each PhraseTemplate produced by LibraryPassageStrategy populates `connectors[i]` explicitly (as a non-nullopt FC with leadStep = 0 or `connectorRng.select_int(...)`). DefaultPhraseStrategy's randomization branch never fires because `connectors[i]` is always present.

**B.** Revert DefaultPhraseStrategy's random-leadStep change (back to leadStep=0 default for non-authored connectors). LibraryPassageStrategy synthesizes its own random leadSteps for `A~` figures. Restores K467 golden bit-identity.

I recommend **B**. The 2026-04-26 random-leadStep change in DefaultPhraseStrategy was a calibration test — useful for the audition but not appropriate as a default for all patches (it drifts K467 goldens). LibraryPassageStrategy taking ownership of the random leadStep aligns with "the strategy decides per-figure placement."

### Q5 — Per-slot motif constraints (deferred per your guidance)

**Default: not in Phase 1.** Each unique slot generates an RFB motif with default constraints (`totalBeats` set per Q2, no other constraints). Phase 2+ adds per-slot constraints in the Pattern syntax.

---

## File layout (Phase 1)

| File | Change |
|---|---|
| `tools/ppl_to_json/main.cpp` | NEW — standalone parser tool. Reads `lib/ppl/*.ppl`, emits `lib/json/*.json`. |
| `tools/ppl_to_json/CMakeLists.txt` | NEW — builds the standalone tool. |
| `CMakeLists.txt` | Add `add_subdirectory(tools/ppl_to_json)`. |
| `engine/include/mforce/music/pattern_library.h` | NEW — engine-side types + JSON loader (no `.ppl` knowledge). |
| `engine/include/mforce/music/library_passage_strategy.h` | NEW — passage strategy class. |
| `engine/include/mforce/music/composer.h` | Include + register `library_passage`. Revert random-leadStep change in DefaultPhraseStrategy per Q4. |
| `engine/include/mforce/music/templates.h` | Add `LibraryPassageConfig` + optional field on `PassageTemplate`. |
| `engine/include/mforce/music/templates_json.h` | JSON round-trip for `LibraryPassageConfig`. |
| `tools/test_figures/main.cpp` | Engine-side integration tests (compose with `library_passage` against `lib/json/Pop1.json`). |
| `lib/ppl/Pop1.ppl` | Move existing `lib/Pop1.ppl` into `lib/ppl/`. |
| `lib/json/Pop1.json` | Generated by the parser tool. |
| `docs/ComposerRefactor3.md` | Mark Pattern Library landed. |

---

## Success criteria

- Parser round-trips Pop1.ppl: `parse(file_contents)` produces a `PatternLibrary` with 3 Patterns matching the documented interpretation.
- `library_passage` strategy reachable via `passageTmpl.strategy = "library_passage"` with `libraryConfig.patternName = "BillyLife"`.
- Composing the BillyLife pattern produces a Passage with 4 phrases (P1, P2 with HC, P1, P3 with FC), each containing the expected figure count, with motif slots `A`, `B`, `C`, `D` resolving to a single auto-generated motif each, referenced consistently across phrases.
- K467 goldens preserved (Q4 revert returns DefaultPhraseStrategy to bit-identity with pre-2026-04-26 behavior).
- Auditioning a render of `BillyLife` from a piece template should produce audible AAA-pattern + cadence-break-with-encroachment shape.

---

## Out of scope for Phase 1

- Per-slot motif constraints (`A: { totalBeats, minPulse, ... }`).
- Prime semantics (`A'` actual modification).
- Genre / role-driven Pattern picking.
- Pattern variants (e.g., "the same Pattern with stochastic perturbation of structure").
- Cross-pattern motif sharing (each Pattern owns its motif slots).
- Multi-time-signature support (4/4 assumed; bars × 4 = beats).
- Slot generation with anything other than RFB defaults.
- Editor / GUI for authoring `.ppl` files.
- Versioning / schema evolution of `.ppl` files.

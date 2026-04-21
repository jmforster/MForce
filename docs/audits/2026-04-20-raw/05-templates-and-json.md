# Agent 5 — Templates + JSON Audit (raw)

**Scope:** `engine/include/mforce/music/templates.h`, `templates_json.h`, example patches `patches/test_k467_*.json`, `patches/template_binary.json`.
**Date:** 2026-04-20
**Agent:** Explore subagent

---

## MForce Template/Authoring-JSON Audit

### 1. Template Hierarchy Consistency

**Observation:** Figure/Phrase/Passage/Piece are structurally inconsistent in depth and decoration:

- **FigureTemplate** (templates.h:120-189): 25 fields. Heavy constraint-based with shape/direction/cadence parameters. No connectors.
- **PhraseTemplate** (templates.h:249-277): 11 fields. Contains `std::vector<FigureTemplate>` plus optional connectors array. No mention of shape types.
- **PassageTemplate** (templates.h:341-365): 10 fields. Contains `std::vector<PhraseTemplate>` or (optionally) `std::vector<PeriodSpec>`. Adds `ChordAccompanimentConfig`.
- **PieceTemplate** (templates.h:393-495): 10 fields (header) + realized motif maps. Contains sections/parts. Includes motif pool.

**Divergence:** Hierarchical levels don't mirror each other. FigureTemplate is "fat" (shape-generation machinery), while PhraseTemplate is "thin" (mostly just contains figures). Phrase/Passage/Piece don't have field parity. This makes it hard to reason about "what can be customized at each level." A classical period structure is hardwired (PeriodSpec with antecedent/consequent), but jazz/EDM would want looser grouping.

### 2. JSON Ergonomics

**For a non-programmer musician:**

- **Excellent:** Motif library with `userProvided: true`, explicit `figure.units` with `{duration, step}` tuples (test_k467_period.json lines 14-20). Simple structure mimics score thinking.
- **Good:** `role` array (e.g., `["Thematic", "Cadential"]`) is intuitive; `cadenceType` (1=HC, 2=PAC) is standard music-theoretic shorthand.
- **Hard:**
  - `"figure": {"units": [{...}]}` requires understanding Pitch/Step duality. A musician wants to say "G4" or "G" in scale, not {"octave": 4, "pitch": "G"}.
  - `cadenceTarget` is a scale degree index (0-6 in C major), not a note name. No obvious way to say "PAC on vi" vs. "IAC on iii."
  - Connectors array (`leadStep`, `elide`, `adjust`) is cryptic without domain knowledge.
  - `FigureDirection` enum (ascending_arc, sine_ascending, turnaround_ascending) requires reading code to understand.
  - String transforms (`rhythmTransform: "stretch"`, `rhythmTransformParam: 0.5`) are inconsistent with enum-based `transform` field.

**Verdict:** Authoring a motif is reasonable. Authoring a full piece requires heavy C++ literacy (role matching, cadence placement, connector arithmetic).

### 3. Silent Field Drops

**Function field:** Found it. templates_json.h:366 **does** parse `function` from JSON into `ft.function`. And templates_json.h:296 **does** emit it. So the historical "function was silently dropped" bug is fixed.

**Other asymmetries found:**

| Field | Struct (templates.h) | to_json (templates_json.h) | from_json (templates_json.h) |
|-------|----------------------|----------------------------|------------------------------|
| ChordAccompanimentConfig.to_json | ✓ defined (line 316-335) | ✗ **MISSING** | ✓ from_json (line 676) |
| PassageTemplate.chordConfig | ✓ optional (line 364) | ✓ implicit via PassageTemplate (line 674 emits periods) | ✓ read at line 727-731 |
| PhraseTemplate.connectors | ✓ optional (line 258) | ✓ emitted (line 498-513) | ✓ parsed (line 550-567) |

**Silent drop:** ChordAccompanimentConfig has **no to_json** function. When PassageTemplate emits, it implicitly assumes nlohmann/json can serialize ChordAccompanimentConfig via its fields, but there's no explicit serialization. This is a trap: if ChordAccompanimentConfig structure changes, JSON stability is at risk.

### 4. Case-Sensitivity & Enum Parsing

**Inconsistency found:**

- **MotifRole** (templates_json.h:78): PascalCase in JSON ("Thematic", "Cadential"). Case-sensitive. No fallback. Throws on mismatch (line 87).
- **MotifOrigin** (templates_json.h:98): PascalCase ("User", "Generated"). Throws on mismatch (line 104).
- **PartRole** (templates_json.h:117): snake_case ("melody", "bass"). Silently defaults to Melody on unrecognized (line 125).
- **MelodicFunction** (templates_json.h:141): snake_case ("statement", "cadential"). Case-insensitive (line 145 converts to lowercase). Defaults to Free on mismatch (line 150).
- **FigureSource** (templates_json.h:23): snake_case ("generate", "reference"). Silently defaults to Generate (line 30).
- **FigureShape** (templates_json.h:178): snake_case ("scalar_run", "cadential_approach"). Defaults to Free (line 196).
- **FigureDirection** (templates_json.h:216): snake_case ("ascending", "ascending_arc"). Defaults to Ascending (line 227).
- **PeriodVariant** (templates_json.h:581): PascalCase ("Parallel", "Modified"). Throws on mismatch (line 586).

**Trap for authors:** Mixing case styles is a minefield. PartRole silently defaults; MotifRole throws. A typo in `{"role": "Thematic_"}` silently fails to set role; a typo in `{"origin": "user"}` throws. Inconsistency invites bugs.

### 5. Defaults & Minimal Story

**Can you write a minimal valid patch with zero boilerplate?** Try:

```json
{
  "keyName": "C",
  "scaleName": "Major",
  "bpm": 120,
  "sections": [{"name": "Main", "beats": 32}],
  "parts": [{
    "name": "melody",
    "role": "melody",
    "passages": {
      "Main": {
        "phrases": [{
          "figures": [
            {"source": "generate", "totalBeats": 8},
            {"source": "generate", "totalBeats": 8}
          ]
        }]
      }
    }
  }]
}
```

This parses (test_k467_pass3.json proves it, lines 23-40). **Verdict:** Yes, "it just works" with minimal input. But the moment you want cadences, roles, or period structure, you must add 20+ lines.

**Surprising defaults:**
- `cadenceTarget: -1` means "composer decides," not "no cadence." Confusing.
- `totalBeats: 0.0` means "sum of children" or "composer decides." Overloaded semantics.
- `function: Free` falls back to parent phrase's function; undocumented behavior.

### 6. Motif Pool Distinction

**User-authored vs. auto-generated:**

- **`userProvided: bool`** (templates.h:204): True = hand-entered, don't regenerate.
- **`origin: MotifOrigin`** (templates.h:210): User, Generated, Derived, Extracted.
- **`derivedFrom: std::optional<std::string>`** (templates.h:211): Tracks parent motif name.
- **`transform: std::optional<TransformOp>`** (templates.h:212): How derivation was performed.

**Visibility in JSON:** All metadata fields are serialized (templates_json.h:390-398). test_k467_period.json lines 46-48 show explicit provenance:
```json
"origin": "Derived",
"derivedFrom": "Fig1"
```

**Distinction:** Clear. But no timestamp, generation seed tracking, or audit trail. If a user regenerates a motif, you lose the original. Plan B's `add_derived_motif` (templates.h:490-595) generates new motifs on-the-fly; their presence in JSON isn't guaranteed unless serialized.

### 7. Classical Bias in Template Shape

**Period/cadence assumptions (templates.h:283-309):**

- `PeriodVariant::Parallel` — antecedent and consequent share basic motif.
- `PeriodVariant::Modified` — consequent derives via `TransformOp` (Invert, Reverse, VarySteps, VaryRhythm, RhythmTail only).
- `PeriodVariant::Contrasting` — independent motifs.
- `antecedent` cadence = HC (half cadence, target scale degree 4 or custom).
- `consequent` cadence = PAC (perfect authentic, target degree 0 or custom).

**Jazz/blues/rock/EDM portability:**

| Genre | Period Model Fit | Issue |
|-------|------------------|-------|
| Jazz | Poor | No chord-scale contexts, vamp repeats, or ii-V turnarounds. |
| Blues | Poor | 12-bar blues is 3×4-bar phrases + turnaround, not antecedent/consequent. Hardwired cadences don't apply. |
| Rock | Moderate | Verse/chorus repeats fit as Parallel periods, but no syncopation/groove scaffolding. |
| EDM | Poor | 8/16-bar loops with sidechaining, filter sweeps. No concept of "antecedent" in 4-on-the-floor. |
| Modal (folk) | Poor | No functional harmony. PeriodVariant assumes tonal resolution. |

**Forcing fit:** An EDM author would fake a 4-bar period with two 2-bar phrases, each calling `generate` with a drum pattern template. But PeriodSpec has no "loop N times" option. Contrasting variant allows independent figures, but no shorthand for repetition.

**StepMode enum** (templates.h:73) includes ChordTone, but only affects constraint interpretation, not cadence logic. No modal scales (Dorian, Phrygian, Mixolydian) in the shape-generation model.

### 8. Versioning & Migration

**No version field in JSON schema.** PieceTemplate and SectionTemplate have no version or format tag. If you rename `cadenceType` → `cadenceCategory`, old patches silently use the default (0 = no cadence). No migration hook. No deprecation warning.

**Example risk:** If a future version splits `cadenceTarget` into `cadenceRootDegree` and `cadenceQuality`, existing JSON breaks silently.

### 9. Validation & Error Reporting

**Silent defaults (permissive):**
- `FigureSource::Generate` missing required field → uses default (minNotes=2, maxNotes=8).
- `cadenceType` missing → defaults to 0 (no cadence).
- `role` in figure → stays unset; no error.

**Loud errors (strict):**
- `MotifRole` value typo → `runtime_error`.
- `MotifOrigin` value typo → `runtime_error`.
- `PeriodVariant` value typo → `runtime_error`.
- Malformed JSON → nlohmann/json throws `std::exception` with no MForce context.

**Verdict:** Inconsistent. Some enums throw; others silently default. No error accumulation; first parse error kills the load. No diagnostics (line number, expected values, etc.).

### 10. Recommendations (Ranked)

**1. Add ChordAccompanimentConfig::to_json (Critical)**
   - File: engine/include/mforce/music/templates_json.h, after line 675.
   - Implement symmetric to_json for ChordAccompanimentConfig to mirror from_json.
   - Prevents silent data loss when round-tripping PassageTemplate with chordConfig.

**2. Unify enum case convention (Medium)**
   - Pick one: either PascalCase for all (*Role, *Origin, *Variant) or snake_case for all (source, transform, cadence_type).
   - Update templates_json.h enum parsers to be consistent: all throw on unknown value, or all default gracefully.
   - Add comments documenting the chosen case and default behavior.

**3. Add JSON schema version field (Medium)**
   - Add `"version": "1.0"` at PieceTemplate top level.
   - Document migration path in comments.
   - Enables future schema changes without silent failures.

**4. Expose period/cadence abstractions for non-classical genres (High)**
   - Add optional `LoopSpec { count: int, variant: LoopVariant }` alternative to PeriodSpec.
   - Extend `PeriodVariant` to include `Repetition` (for vamps, EDM loops).
   - Add optional `harmonyMode: enum { Tonal, Modal, Blues, Functional }` to PieceTemplate to gate classical cadence logic.

**5. Improve error diagnostics (Low)**
   - Catch nlohmann/json exceptions at PieceTemplate from_json entry point.
   - Re-throw with context (file path, field name, expected type).
   - Print expected enum values on mismatch.

**6. Document step/degree coordinate system (Low)**
   - Add JSON examples in template headers showing scale-degree naming (degree 0 = tonic, etc.).
   - Clarify octave vs. relative-pitch semantics in Pitch JSON.

---

**Bottom line:** The framework is authoring-ready for classical forms (K467 patches prove it) but strongly classical-biased. Enum inconsistency, missing to_json for ChordAccompanimentConfig, and silent defaults on unrecognized values are concrete ergonomic hazards for both machines and humans. Genre portability requires architectural, not cosmetic, changes.

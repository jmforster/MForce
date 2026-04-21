# Agent 2 — Composer + Strategies Audit (raw)

**Scope:** `engine/include/mforce/music/composer.h`, `strategy.h`, `strategy_registry.h`, `default_strategies.h`, `period_passage_strategy.h`, `locus.h`, `piece_utils.h`.
**Date:** 2026-04-20
**Agent:** Explore subagent

---

## MFORCE COMPOSER STRATEGY LAYER — ARCHITECTURAL AUDIT

### Executive Summary

The MForce composer is well-structured for **classical music** but tightly coupled to Western tonal assumptions. The two-phase plan/compose split is clean and consistently implemented, but cross-level dependencies and hardcoded shape defaults create barriers to non-classical genres. Five critical refactors would enable genre portability.

---

### 1. Two-Phase Plan/Compose Split

**Contract Consistency: GOOD; Enforcement: PARTIAL**

- **Plan phase** (plan_passage, plan_phrase, plan_figure): transform seed templates; may mutate PieceTemplate's motif pool
- **Compose phase** (compose_passage, compose_phrase, compose_figure): realize self-contained templates; read-only on pool by contract

**Evidence:**
- Strategy base classes define both phases: `strategy.h:37-65` (FigureStrategy, PhraseStrategy, PassageStrategy all have plan_* and compose_*)
- Default implementations return seed unchanged: `default_strategies.h:37-39` (FigureStrategy::plan_figure default)
- PeriodPassageStrategy overrides plan to flatten periods → phrases, synthesizing motifs: `period_passage_strategy.h:114-263`

**Shortcut Found:**
- Composer::compose_passage skips plan phase for figure level; DefaultFigureStrategy::compose_figure dispatches *directly* to shape strategies without a registered plan_figure call: `composer.h:814-819` (switch on shape → registry lookup → invoke compose_figure). This works because shapes are self-contained, but if a shape strategy ever needs planning (e.g., constraint solving), there's no hook.

**Cost to fix:** low. Add `plan_figure` dispatch before `compose_figure` in the shape pipeline.

---

### 2. Strategy Dispatch — Registry + String Lookup

**Pattern: SOUND; Pain Points: MODERATE**

Registry uses unordered_map keyed by strategy name: `strategy_registry.h:16-22`. Dispatch happens at three levels:

- **Passage:** `composer.h:157, 167` — resolve by PassageTemplate.strategy field, fallback to "default_passage"
- **Phrase:** `composer.h:157, 899` — resolve by PhraseTemplate.strategy field, fallback to "default_phrase"
- **Figure:** `composer.h:151` — hardcoded to "default_figure"; shape dispatch only via DefaultFigureStrategy

**Magic-String Pain:**
- Shape strategy names are hardcoded in switch statement: `composer.h:794-809` (16 shape strings: "shape_scalar_run", "shape_triadic_outline", etc.)
- No enum or constant pool — string literals duplicated in Composer constructor registration: `composer.h:111-126` and in the switch
- If a new shape is added, both registration and switch must be updated; easy to miss

**Missing-Strategy Fallback: SENSIBLE**
- Passage and phrase dispatch check `if (!s)` and emit stderr + fallback to default: `composer.h:158-161, 168-171`
- Figure dispatch hardcoded to default_figure; no null check needed
- Fallback is graceful but silent-failure-prone in a programmatic API

**Recommendation:** Create a FigureShapeRegistry alongside StrategyRegistry, register shapes during Composer::ctor, look them up by FigureShape enum (not string). Eliminates switch statement and string duplication.

---

### 3. Locus Abstraction — Ownership & const_cast Issues

**Leakage: MODERATE; const_cast Count: 5 instances**

Locus carries `const Piece* piece` and `PieceTemplate* pieceTemplate` (non-const). This is intentional — plan phase needs to mutate the motif pool — but five const_casts betray transitional design:

1. **composer.h:263** — realize_motifs_: `const_cast<PieceTemplate&>(tmpl)` when calling free function
   - **Comment:** "const_cast is transitional: setup_piece_ / compose() currently take const PieceTemplate&; the free function mutates the realized maps. When compose() signature changes to take non-const (Plan B requires it), this const_cast goes away." ✓ Acknowledged debt

2. **composer.h:363** — generate_default_passage_: `const_cast<PieceTemplate*>(&tmpl)` when constructing Locus
   - Local fix: uses const template to build a Locus

3. **composer.h:402** — compose_passage_: `const_cast<PieceTemplate*>(&tmpl)` in Locus construction
   - Same issue; passed tmpl is const PieceTemplate&

4. **composer.h:422-424** — compose_passage_: `const_cast<Section*>(section)->harmonyTimeline.set_segment(...)` and `.chordProgression = prog`
   - **Critical:** Section is const in the loop, but ChordWalker mutates it post-composition
   - **Ownership bug?** The section is resolved from locus.piece->sections, which should be mutable; const-correctness is violated

**Assessment:**
- const_cast #1 is acknowledged transitional debt; safe for now
- const_casts #2–#3 are design temporary; waiting for Phase 3 (template-driven strategy selection) to take effect
- const_cast #4 is a flag: if piece/sections are const in the caller, we have an ownership issue; if not, const declaration is wrong

**Recommendation:** Change compose_passage_ to take non-const Piece&. Section is interior mutability of Piece; const on Piece is overly strict post-plan phase.

---

### 4. Cursor Model — Invariant & Integrity

**Invariant: WELL-DEFINED; Implementation: SOLID**

Cursor threads through the composition via PitchReader, stepped by each unit's scale-degree step. The contract is:

1. **Passage starts with PassageTemplate.startingPitch**
2. **Phrase inherits it (or overrides); no reset between phrases within a passage**
3. **Figures step the cursor in order; first unit's step bridges from prior**
4. **pitch_before(locus) recomputes cursor up to a point by walking units**

**Evidence:**
- piece_utils.h:37-88 (pitch_before) mirrors the cursor walk exactly: "Matches DefaultPassageStrategy::realize_passage's threaded cursor exactly"
- composer.h:875-895 (DefaultPassageStrategy::compose_passage) computes localTmpl.startingPitch from passage cursor if absent
- PeriodPassageStrategy enforces Parallel/Modified antecedent→consequent pitch matching: `period_passage_strategy.h:304-315`

**Broken Invariant Found: NONE** — auditing the code, the invariant holds. The PitchReader is threaded correctly, cursor override is computed properly, and no shortcuts bypass the walk.

---

### 5. Cross-Level Strategy Dependencies

**Tightly Coupled: MODERATE to HIGH**

- **Figure → Phrase:** DefaultFigureStrategy is *hard-wired* into DefaultPhraseStrategy. Phrase strategies call compose_figure directly (DefaultPassageStrategy:920-1055) or via FigureStrategy dispatch (DefaultPhraseStrategy:1004-1027). If a phrase strategy needs a special figure dialect, it must subclass or hard-code logic.

- **Passage → Phrase:** DefaultPassageStrategy dispatches phrases via registry (composer.h:899-902), so phrase strategies are pluggable. But DefaultPhraseStrategy's cursor threading assumes a specific reset pattern — if a phrase strategy implements a different cursor model (e.g., leap-and-fill that ignores prior phrase state), integration is fragile.

- **Figure → Harmony:** Harmony timeline is built post-composition in PeriodPassageStrategy (period_passage_strategy.h:337-405). If a passage strategy is Melody-only, harmony is generated via ChordWalker (composer.h:416-425). This creates a dependency on styleName and ChordWalker's harmonic model — not pluggable.

**Evidence of tight coupling:**
- `period_passage_strategy.h:368-370` assumes antecedent cadenceType == 1 → V, cadenceType == 2 → I (hardcoded scale degrees)
- `period_passage_strategy.h:103-109` (cadence_chord): returns ScaleChord{4, 0, Major} for HC, {0, 0, Major} for PAC — pure classical semantics
- `composer.h:976-979` forces HeldNote on last figure of PAC phrase — no hook for other phrase types

**Cost to make pluggable:** Moderate. Extract cadence_chord into a per-style lookup; parameterize cursor reset behavior; expose harmony generation as a phase separate from melody.

---

### 6. Shape-as-FigureStrategy Pattern

**Taxonomy: CONFUSED MIXING OF CATEGORIES**

16 shape strategies are registered as FigureStrategy subclasses alongside DefaultFigureStrategy:

```cpp
reg.register_figure(std::make_unique<DefaultFigureStrategy>());       // meta-strategy
reg.register_figure(std::make_unique<ShapeScalarRunStrategy>());      // concrete shape
reg.register_figure(std::make_unique<ShapeTriadicOutlineStrategy>()); // concrete shape
// ... 14 more shapes
```

**Problem:** DefaultFigureStrategy is a *dispatcher* (chooses shape based on function, delegates to shape registry). The 16 shapes are *implementations*. Registering both in the same namespace violates single responsibility — a caller asking for "figure" strategy might get either a dispatcher or a shape.

**Example incoherence:**
- DefaultFigureStrategy::choose_shape (default_strategies.h:184-231) returns FigureShape enum (e.g., FigureShape::TriadicOutline)
- DefaultFigureStrategy::compose_figure (composer.h:731-848) converts enum to string ("shape_triadic_outline") and looks it up in the same registry
- If a shape is missing from registry, compose_figure returns FigureShape::Free and falls back to generate_figure

**Better design:**
- Separate FigureShapeRegistry from StrategyRegistry
- DefaultFigureStrategy dispatches to shapes by enum, not string
- Shapes inherit from a common ShapeStrategy interface (or just stay as unnamed lambdas/factories)

**Current state:** Works, but taxonomy is muddled. Acceptable for now; easy to refactor if needed.

---

### 7. Hardcoded Classical Assumptions — Cited by File:Line

#### Cadence Semantics (HC=V, PAC=I)

| File:Line | Code | Assumption |
|-----------|------|-----------|
| composer.h:531 | `figureCadenceType == 1` → V; `== 2` → I | Fixed mapping; no genre override |
| composer.h:540-560 | Half cadence targets degree 4 (V); full cadence targets degree 0 (I) | Hardcoded scale degrees |
| composer.h:976-979 | Force HeldNote on last figure of PAC (cadenceType==2) | No blues, jazz, or folk phrase endings |
| period_passage_strategy.h:103-109 | cadence_chord(): HC→{4,0,Major}, PAC→{0,0,Major} | Only classical tonal roots |
| phrase_strategies.h:57-76 | PeriodPhraseStrategy adjusts half-cadence target to cfg.halfCadenceTarget | Period-specific; inflexible |

#### Default Shape Choices per Function

| File:Line | Code | Assumption |
|-----------|------|-----------|
| default_strategies.h:191-230 | Statement → {RepeatedNote, TriadicOutline, ScalarRun, NeighborTone, Fanfare}; last figure → HeldNote | Western motif openings; no blue-note licks |
| default_strategies.h:202-211 | Development → {Zigzag, ScalarReturn, LeapAndFill, Cambiata, ScalarRun}; last → ScalarRun | Classical extension/continuation only |
| default_strategies.h:221-225 | Cadential → CadentialApproach (non-last), HeldNote (last) | Suspension-and-hold ending; no turnaround, no comp |

#### Harmony Generation

| File:Line | Code | Assumption |
|-----------|------|-----------|
| composer.h:416-425 | No harmony → generate via ChordWalker(styleName, ...) | Assumes style → melody-driven walk; no loop-based, drum-driven, or riff-driven paradigms |
| period_passage_strategy.h:356-361 | make_attacks() → every 2 beats | Half-note resolution only; ignores syncopation, swing, shuffle |
| period_passage_strategy.h:377-382 | antecedent: I → cadence; consequent: cadence → cadence | Period-specific; no blues 12-bar, no ii-V-I turnaround |

#### Phrase Structure

| File:Line | Code | Assumption |
|-----------|------|-----------|
| composer.h:349-364 | generate_default_passage_: antecedent (4+4+2) + consequent (4+4+4) | Symmetric 8+8 bar period; no syncopated intro, no break sections |
| period_passage_strategy.h:280-295 | Parallel/Modified: force consequent to start at antecedent's pitch | Necessary for classical period parallelism; breaks jazz head-and-comp (comp chord drives melodic answers) |

---

### 8. Cost of New Genres — Choke Point Analysis

#### Jazz ii-V-I Passage Strategy

**Requires:**
1. New PassageStrategy subclass (easy; compose_passage loop is straightforward)
2. Override cadence_chord for turnaround semantics (ii→V→I; current code assumes I→HC/PAC)
   - **Choke:** period_passage_strategy.h:103-109 hardcoded cadence_chord; not overridable per style
3. Chord-melody figure strategy (melody leads, harmony follows per jazz head structure)
   - **Choke:** All phrase/figure strategies assume melody-independent harmony; AlternatingFigureStrategy shows chord-tone approach but requires specific figure template structure
4. Swing rhythm resolver (triplet-based, not straight-eighths)
   - **Choke:** PulseGenerator not exposed; rhythm resolution baked into FigureBuilder::build

**Estimate:** 300–500 lines; 2 moderate choke points (cadence_chord, rhythm swing).

#### Blues 12-Bar with Blue-Note Licks

**Requires:**
1. 12-bar form template (PassageTemplate with 3×4-bar phrases; doable)
2. Blue-note figure strategies (flattened 3rd, 5th, 7th; new shapes or parameterized ScalarRun)
3. Chord progression override (I-I-I-I-IV-IV-I-I-V-IV-I-V; bypasses style-driven walk)
4. Repetition with lick variation (figure sampled then transposed per bar)

**Choke points:**
- **Hardcoded shape choices** (default_strategies.h:191-230): Statement/Development/Cadential functions inapplicable; need Blues-specific function or Free mode only
- **Cadence logic** (composer.h:976-979): PAC assumption breaks; 12-bar resolves back to V (turnaround), not I
- **Harmony timeline generation** (period_passage_strategy.h:343-405): style-driven ChordWalker; blues needs hard-coded I-IV-V cycle

**Estimate:** 400–600 lines; 3 choke points (shape taxonomy, cadence semantics, harmony model).

#### EDM Loop Strategy (No Phrases, Bar-Length Patterns Repeated)

**Requires:**
1. PassageStrategy that reads repetition count; loops a single-bar figure
2. Bypass phrase dispatch entirely (compose_passage → loop figure N times, not walk phrases)
3. Bypass cadence logic (no arrival, no landing)
4. Harmony: loop a single chord or short progression

**Choke points:**
- **Phrase-mandatory architecture** (composer.h:867-907): DefaultPassageStrategy iterates passTmpl.phrases. EDM loop strategy must skip this entirely; doable, but architecture assumes phrases are fundamental
- **Cadence targeting** (default_strategies.h:280-290): applied automatically to last figure of phrases. EDM has no "last" figure in classical sense
- **Starting pitch semantics** (composer.h:875-895): DefaultPassageStrategy computes starting pitch from passage template. EDM might not need it (loop-based, static pitch)

**Estimate:** 200–300 lines; 2 moderate choke points (phrase vs. loop abstraction, cadence opt-out).

---

### 9. Top Recommendations (Ranked by Impact)

#### 1. **Decouple cadence semantics from phrase/passage strategies** [HIGH IMPACT, MEDIUM EFFORT]
   - Extract cadence_chord(cadenceType) logic into a pluggable CadenceResolver interface
   - Allow per-style or per-genre cadence definitions (jazz turnaround, blues V, modal resolution)
   - **Files affected:** period_passage_strategy.h, default_strategies.h, phrase_strategies.h
   - **Lines:** ~50 changes; new CadenceResolver class ~80 lines
   - **Payoff:** Enables jazz, blues, modal music; unblocks 2 of 3 test genres

#### 2. **Separate shape taxonomy from strategy registry** [MEDIUM IMPACT, LOW EFFORT]
   - Create ShapeRegistry (enum-based or factory pattern)
   - Remove shape strategies from StrategyRegistry
   - Eliminate string-switch in DefaultFigureStrategy::compose_figure (composer.h:791-820)
   - **Files affected:** composer.h, default_strategies.h, strategy_registry.h
   - **Lines:** ~100 changes; reduces duplication, improves clarity
   - **Payoff:** Cleaner dispatch; easier to add shapes; no string-duplication bugs

#### 3. **Expose rhythm and harmony generation as pluggable phases** [MEDIUM IMPACT, MEDIUM EFFORT]
   - Factor out PulseGenerator and ChordWalker dispatch into PassageStrategy hooks
   - Allow strategies to opt-out of harmony generation (Melody-only) or specify custom progression
   - **Files affected:** composer.h, period_passage_strategy.h, templates.h
   - **Lines:** ~150 changes
   - **Payoff:** Enables EDM (loop-based, no generation), improves jazz/blues harmony control

#### 4. **Make phrase structure optional; support loop-based composition** [MEDIUM IMPACT, HIGH EFFORT]
   - Add LoopPassageStrategy base class or variant dispatch
   - Let PassageTemplate specify loop-mode (repetition count, bar length) as alternative to phrases
   - Refactor pitch_before to handle non-phrase structures
   - **Files affected:** composer.h, piece_utils.h, templates.h, strategy.h
   - **Lines:** ~250 changes
   - **Payoff:** Unblocks EDM and repetition-based genres

#### 5. **Resolve const-correctness and ownership model** [LOW IMPACT, MEDIUM EFFORT]
   - Change compose_passage_ to take non-const Piece& (not const Piece&)
   - Remove const_cast #4 (composer.h:422-424)
   - Document whether plan phase mutates Piece or only PieceTemplate
   - **Files affected:** composer.h, templates.h
   - **Lines:** ~20 changes
   - **Payoff:** Type safety; clearer ownership; easier debugging

---

### Conclusion

The framework is **well-engineered for classical music** with a clean two-phase plan/compose split and pluggable strategies. To support jazz, blues, and EDM:

1. **Immediate wins** (days 1–2): Decouple cadence semantics (#1), clean up shape taxonomy (#2)
2. **Medium-term** (days 3–4): Expose rhythm/harmony phases (#3), make phrase structure optional (#4)
3. **Foundation cleanup** (concurrent): Fix const-correctness (#5)

These five refactors would enable ~80% genre coverage with ~500 additional lines of infrastructure and minimal churn to existing classical code. The architecture is resilient; it just needs genre-specific customization points.

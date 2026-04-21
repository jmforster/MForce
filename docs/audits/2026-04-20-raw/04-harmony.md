# Agent 4 — Harmony Layer Audit (raw)

**Scope:** `engine/include/mforce/music/chord_walker.h`, `style_table.h`, `harmony_timeline.h`, related harmony files, `basics.h` (ScaleChord, ChordDef, KeyContext), `styles/classical_mozart.json`, `styles/classical_tonic_dominant.json`.
**Date:** 2026-04-20
**Agent:** Explore subagent

---

## Harmony Layer Audit — MForce C++ Platform

### 1. `walk()` vs `harmonize()` — Distinct Strategies

**`ChordWalker::walk()`** (lines 40–93 in chord_walker.h):
- **Purpose**: Generate a harmonic progression by walking a StyleTable's state machine (Markov chain). Generates chords autonomously based on transition weights and optional melody constraints.
- **Inputs**: StyleTable, WalkConstraint (start/end chord, duration, optional cadence timing), seed.
- **Outputs**: ChordProgression (sequence of scale-relative chords with durations).
- **Logic**: Iteratively picks durations and next chords from the style table. If `cadenceBeat` is set, splits into approach section + forced cadential chord.

**`ChordWalker::harmonize()`** (lines 98–208):
- **Purpose**: Harmonize a pre-composed melody by assigning chords at each melody attack beat. Chords are chosen to (a) contain the melody note and (b) transition well from the previous chord.
- **Inputs**: StyleTable, WalkConstraint (includes melody profile as MelodySpan vector), attack beats, seed.
- **Outputs**: ChordProgression aligned to melody timing.
- **Logic**: For each attack beat, looks up the melody scale degree, prefers chords containing that tone, breaks ties via style-table transitions.

**Factoring verdict**: ✓ **Correct split.** They serve different workflows: autonomous harmony generation (`walk()`) vs. melody-first harmonization (`harmonize()`). Both are compositionally valuable. The shared WalkConstraint container is appropriate—it abstracts "constraints" generically.

---

### 2. StyleTable Expressive Power — Genre Gaps

**Current design** (style_table.h, lines 116–141):
- First-order Markov: `transitions[currentLabel]` → vector of (target, weight) pairs.
- Variable-order overrides: `overrides["prevLabel,currentLabel"]` → higher-precedence transitions.
- Loaded from JSON; two examples provided (classical_mozart.json, classical_tonic_dominant.json).

**Classical adequacy**: ✓ Excellent fit.
- mozart.json encodes diatonic voice-leading (V→I, ii→V→I, vi→IV patterns).
- overrides capture context like "after I, prefer vi→ii over vi→IV" (line 16 in mozart.json).

**Jazz ii-V-I patterns**: ⚠️ **Possible, but laborious.**
- To author "ii-V-I in every visited key," you'd need to manually author 12 entries in transitions ("ii": [...], "V7": [...], "I": [...]) OR use slash-chord notation (see below).
- No automatic key-relative progression builder; compare ChordProgressionBuilder (chord_progression_builder.h), which is hard-coded with lambdas (lines 89–96: "ii-V-I" scales to arbitrary beat count, but it's monolithic, not re-keyable mid-piece).

**Blues (I7-IV7-V7, quick-IV turnarounds)**: ⚠️ **Limited by chord quality scope.**
- StyleTable uses only ScaleChord (degree + alteration + quality). It can model I7, IV7, V7 (see chord_walker.h line 285, ChordDef handling).
- But no **12-bar form modeling**: StyleTable is stateless regarding bar count. You'd need to manually expand a 12-bar blues into 48 beat-level transitions—doable, but verbose.

**Modal (drone + voicing changes, no functional cadence)**: ❌ **Not representable.**
- StyleTable encodes functional harmony transitions (V→I, ii→V). A modal vamp (I-I-I with voicing rotation) requires **voicing state**, which StyleTable doesn't track.
- Workaround: author a style table with I→I override (high weight), but you lose voicing intent.

**Rock/pop (borrowed chords, modal interchange)**: ⚠️ **Partial support.**
- ChordLabel can parse bVI, bVII (line 35 in style_table.h: alteration = -1).
- But StyleTable has no notion of "bVI is borrowed from parallel minor"—it's just another scale degree. Blues major scale (I, II, I, IV, V) in a major key requires authoring bVII separately.

**Non-Western (raga-based pitch centers, no harmony)**: ❌ **Out of scope.**
- StyleTable assumes chord-based harmony. Ragas are ornamental around a drone (Sa); chords don't model this.

**EDM (single-chord vamps, drum-groove focus)**: ✓ **Adequate.**
- I→I with high weight works for vamp modeling.

---

### 3. ChordLabel Parser Scope

**Current capabilities** (style_table.h, lines 28–111):
- Format: `[b|#]<Roman>[quality]`
- Roman numerals: I–VII (case-sensitive for major/minor hint).
- Quality suffixes: M, m, 7, M7, m7, dim (o), aug (+).
- Alteration prefix: `b` (−1) or `#` (+1).
- Examples: "I" → I Major, "V7" → V Dom7, "bVII" → bVII Major.

**Missing extended/altered notation**:
- ❌ No V7b9, V7alt, V+, V7#5 (common jazz alterations). Parser treats "V7b9" as unknown suffix (line 79 assigns it as qualityName).
- ❌ No slash chords (V/V, IV/vi). Would require "/" parsing and root-over-bass handling; not in ChordDef scope.
- ❌ No modal-interchange notation shortcuts (e.g., "IVm" for iv in major context). Parser will parse it as Iv (lowercase iv) = ii minor, which is wrong.

**Verdict**: Parser is **minimal but serviceable for diatonic harmony**. Jazz/blues would need style-table authors to invent extended quality names (e.g., "7alt" as a ChordDef lookup key), which isn't intrinsically wrong but adds friction.

---

### 4. KeyContext / Modulation — Mid-Phrase Support

**Design** (structure.h, lines 119–129):
- `KeyContext` = (beat, key, optional scaleOverride).
- `Section::keyContexts` = vector of KeyContexts (line 153).
- `Section::active_scale_at(beat)` method (lines 157–160) returns the active scale at a given beat.

**Verdict**: ✓ **Modulation is geometry-clean.** Sections can have multiple key changes; lookup is O(n) linear scan, adequate for typical ~3–5 modulations per section.

**Limitation**: KeyContext is **Section-local**. No cross-Section modulation planning (e.g., "modulate from C major to G major over the next two sections"). Section boundaries are hard breaks; if you want a gradual modulation, you must subdivide the Section.

**HarmonyTimeline awareness**: ✗ **HarmonyTimeline is key-agnostic.** It stores ChordProgression segments (harmony_timeline.h, lines 11–22). ChordProgression holds scale-relative ScaleChords (figures.h, line 675). **No automatic transposition when key changes.** If a Section modulates from C→G, the HarmonyTimeline's chords (still in C-major scale degrees) aren't reinterpreted. A chord composition strategy must handle this; it's not automatic.

---

### 5. Cadence Modeling — Hard-Coded Logic

**Current encoding** (period_passage_strategy.h, lines 103–105):
```cpp
static ScaleChord cadence_chord(int cadenceType, int /*targetDegree*/) {
  if (cadenceType == 1) {  // half cadence → V
    ...
  }
  // cadenceType == 2 → I (full cadence)
}
```

**Hard-coded assumptions**:
- cadenceType 1 = half cadence (HC) → target V (degree 4).
- cadenceType 2 = full/perfect authentic cadence (PAC) → target I (degree 0).
- No intermediate cadence (IAC/plagal) types modeled; no deceptive cadence (V→vi).

**Verdict**: ❌ **Brittle and classical-only.**
- Blues, funk, pop often omit cadences or use vamp-outs (fade to silence on the same chord). No cadenceType for this.
- Modal music has no functional cadence; cadenceType 0 (none) works, but if you want modal closure (e.g., a plagal motion or a modal half-step), you can't express it.
- Modal interchange (e.g., I→IV♭→I in major) isn't a "cadence" in the classical sense but is a harmonic gesture; no notation for it.

**Recommendation**: Expand cadenceType enum: `{None, HC, PAC, IAC, Plagal, Deceptive, Modal, Vamp, ...}` or allow custom target chords independent of type.

---

### 6. Harmonic Rhythm — Grid-Locked

**Constraint structure** (chord_walker.h, lines 25–33):
- WalkConstraint specifies `minChordBeats` and `maxChordBeats` (float durations in beats).
- `walk()` picks durations uniformly at random within this range (pick_duration, lines 211–232: base × {0.5, 1.0, 2.0}).
- Chords are placed on beat boundaries; no syncopation, no pushed/anticipatory changes.

**Verdict**: ✗ **Fully quantized to beat grid.** No anticipation, no Brazilian syncopation, no rubato-like chord placement. Adequate for classical/baroque (where harmonic rhythm aligns with meter), insufficient for jazz (anticipations are common) or contemporary styles (groove-based anticipation).

**Workaround**: Manually craft chord timings in JSON (if you author the ChordProgression directly). But walk() and harmonize() don't support sub-beat timing.

---

### 7. Voice-Leading Awareness — None

**Current data flow**:
1. ChordWalker generates ScaleChord sequence.
2. ChordProgression.resolve() (figures.h, lines 691–697) converts to Chord objects (root, octave, inversion, spread).
3. Inversion/spread are **uniform per chord**, not voice-leading-aware (both are constructor parameters).

**Verdict**: ❌ **No voice-leading logic.**
- No smoothest-voicing heuristic (e.g., "minimize jump from last chord's highest pitch to next chord's root").
- Inversion and spread are static per call site (e.g., all root position, all spread=0).
- Jazz drop-2 voicings (2nd-highest note is dropped an octave) would require a custom ChordDictionary, but ChordDictionary (basics.h, lines 360–369) is just a lookup table of ChordDefs (interval lists). No voice-leading context.

**Plugging it in**: A post-processing strategy could iterate through the resolved Chord sequence and apply voice-leading rules (swap inversions, reorder pitches). The structure allows this—Chord.pitches is mutable (basics.h, line 341)—but no built-in facility exists.

---

### 8. Chord Pool / Resolution — Triads + 7ths, Static Voicing

**Data structure** (basics.h):
- ScaleChord → {degree, alteration, quality} (lines 375–383).
- Quality is a ChordDef pointer → intervals (lines 320–329).
- Chord.resolve() applies inversion/spread to Chord.pitches (lines 343, 348–350).

**Voicing coverage**:
- ✓ Triads (root-3rd-5th): any quality with ≤3 intervals.
- ✓ 7ths: Major7, Minor7, Dom7, Diminished (built-in ChordDefs).
- ❌ Drop-2, drop-3, rootless voicings: **Not expressible.** Spread parameter (lines 336, 338) is poorly named—it's not documented. Presumably it adds octaves or gaps, but no dedicated "rootless" or "drop-2" voicing type.
- ❌ Shell voicings (root-3rd-7th omit 5th): Would require custom ChordDefs (e.g., "Major7shell" = [P1, M3, m7]).

**Workaround**: Define new ChordDefs in the static registry (likely in a .cpp file, not shown here). But framework has no high-level "voicing strategy" concept.

---

### 9. Summary: Genre Support Matrix

| Genre | walk() | harmonize() | StyleTable | Cadence | Voice-Leading |
|-------|--------|------------|-----------|---------|---|
| Classical | ✓ | ✓ | ✓ | ✓ (HC/PAC only) | ✗ |
| Jazz (ii-V-I) | ⚠️ (manual) | ⚠️ | ⚠️ (verbose) | ✗ | ✗ |
| Blues (I7-IV7-V7) | ⚠️ | ✓ | ⚠️ | ✗ | ✗ |
| Rock/pop (borrowed) | ⚠️ | ✓ | ⚠️ (authoring) | ✗ | ✗ |
| Modal (drone+voicing) | ❌ | ⚠️ (no voicing) | ❌ | ⚠️ | ✗ |
| Non-Western (raga) | ❌ | ❌ | ❌ | N/A | N/A |
| EDM (vamp) | ✓ | ✓ | ✓ | ✗ (ok) | ✗ |

---

### TOP 5 RECOMMENDATIONS (Highest Leverage)

#### **1. Expand Cadence Types & Support Non-Functional Closures (Effort: Low, Impact: High)**
- Replace hardcoded cadenceType enum with a richer grammar: `{None, HC, PAC, IAC, Plagal, Deceptive, Modal, Fade, Custom}`.
- Allow custom endChord in WalkConstraint independent of cadenceType.
- Enables jazz turnarounds, blues fade-outs, modal closure, pop borrowed-chord moments.
- **File to modify**: period_passage_strategy.h (lines 103–105), templates.h (line 262, cadenceType field).

#### **2. Introduce Voicing Strategy Abstraction (Effort: Medium, Impact: High)**
- Create `VoicingStrategy` interface: `apply(const Chord& prev, Chord& next)` → optimized pitch order for voice-leading.
- Built-in strategies: `SmoothedVoicing` (minimize jumps), `DropNVoicing` (drop-2/3), `RootlessVoicing`, `ShellVoicing`.
- Call post-resolution in Composer or Section rendering.
- Enables jazz, leads to idiomatically voiced progressions without manual interval authoring.
- **New file**: mforce/music/voicing_strategy.h.

#### **3. Add Sub-Beat Harmonic Rhythm & Anticipation (Effort: Medium, Impact: Medium)**
- Extend WalkConstraint to allow `std::optional<float> anticipationBeats` (e.g., anticipate chord by 0.5 beats).
- Extend pick_duration to respect beat-subdivision hints from a style table (new field: `subBeatDivision`).
- Enables syncopation, jazz anticipations, Brazilian groove patterns.
- **Files to modify**: chord_walker.h (WalkConstraint, pick_duration), style_table.h (add subBeatDivision field).

#### **4. Modal / Non-Functional Harmony Mode (Effort: Medium, Impact: Medium)**
- Add StyleTable mode flag: `harmonyMode = {Functional, Modal, Groove}`.
- Modal mode: ignore cadence logic, allow I→I vamps, disable cadence_chord() call.
- Groove mode: harmony locked to a fixed chord, style table provides voicing/rhythm variation only.
- Enables EDM, ambient, raga-adjacent pieces.
- **File to modify**: style_table.h (add mode field), period_passage_strategy.h (conditional cadence logic).

#### **5. Cross-Section Modulation Planning (Effort: High, Impact: Medium)**
- Add Piece-level modulation graph: `std::vector<Modulation> { {fromSection, toSection, fromKey, toKey, duration} }`.
- Compose a transition passage (e.g., V-of-G7 for half-step up) automatically.
- Requires lifting key-change awareness to Composer; non-trivial refactor.
- Enables longer-form works with planned tonal journey (symphonies, multi-movement suites).
- **Files to modify**: structure.h (Piece), composer.h (modulation planning), period_passage_strategy.h (transition insertion).

---

**Conclusion**: MForce's harmony layer is **well-architected for classical/common-practice** (walk/harmonize factoring is clean, StyleTable is elegant for functional harmony). **It needs genre-specific extensions** (voicing, modal mode, anticipation, richer cadence types) to credibly claim full-genre support. The foundation is solid; the gaps are **content and strategy**, not architectural.

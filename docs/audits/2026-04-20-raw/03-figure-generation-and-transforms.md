# Agent 3 — Figure Generation + Transforms Audit (raw)

**Scope:** `engine/include/mforce/music/figures.h` (FigureBuilder, StepGenerator, PulseGenerator, transforms), `default_strategies.h` (generate_figure, apply_transform, choose_shape), `shape_strategies.h` (16 shape strategies), `templates.h` (FigureTemplate).
**Date:** 2026-04-20
**Agent:** Explore subagent

---

## ARCHITECTURAL AUDIT: Figure Generation & Transform Layer

### 1. FigureBuilder Shape Catalog

**Coherence & Taxonomy:** The 16 named shapes form a **mostly coherent Classical taxonomy**:

- **Scalar families** (7 shapes): ScalarRun, ScalarReturn, Anacrusis, Stepping, Zigzag, Sigh, Fanfare
- **Chord-tone families** (2 shapes): TriadicOutline, Cambiata
- **Neighbor-motion families** (3 shapes): NeighborTone, LeapAndFill, Suspension
- **Held/repeated** (2 shapes): RepeatedNote, HeldNote
- **Cadential** (1 shape): CadentialApproach
- **Skipping** (1 shape): Skipping (requires magnitudes 2–3 for thirds/fourths)

**Overlap & Redundancy Issues (LOW severity):**
- `Zigzag` (step-up, skip-down cycles) vs. `ScalarReturn` (out, return): semantically distinct but contour-similar. Each is wired and distinct.
- `CadentialApproach` is classical-specific; no blues/jazz equivalent cadence shape exists.
- `Fanfare` hardcodes leap pattern `{4, 3}` (4th, 3rd); not parameterizable for other interval sequences.

**Muddled Concepts:** None severe. All shapes are **contour morphs, not transforms**—they generate pitch sequences directly. Transforms (invert, reverse, etc.) are correctly separate.

**Dead Code:**
- `skip_sequence()` (figures.h:337–379) is **declared but never called** anywhere in the codebase. It selects skips from degree-relative tables but has no integration point.

**File references:**
- Shape definitions: figures.h:877–1099 (FigureBuilder methods)
- Shape taxonomy enum: templates.h:37–55 (FigureShape)
- Shape strategy wrappers: shape_strategies.h:26–267 (16 inline strategy classes)
- Two strategies DECLARED but bodies in composer.h: ShapeSkippingStrategy (shape_strategies.h:253–258, bodies in composer.h:660–697), ShapeSteppingStrategy (shape_strategies.h:260–265, bodies in composer.h:699–720)

---

### 2. Generate vs. Shape Duality

**Two Paths:**

1. **Generate (random-steps):** `generate_figure()` (default_strategies.h:55–117)
   - Takes totalBeats → noteCount; picks random StepSequence via StepGenerator (random_sequence, targeted_sequence, no_skip_sequence)
   - Rhythm from musical durations (0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0)
   - Optional rhythm variation (vary_rhythm: split or dot pulses)
   - Returns MelodicFigure

2. **Shape (named contour):** 16 ShapeXxxStrategy classes
   - Route through `compose_figure()` dispatching to named shape builders
   - Resolve rhythm separately (detail::resolve_rhythm → generate_musical_rhythm)
   - Resolve contour (detail::resolve_contour, from motifs or FigureDirection enum)
   - Return MelodicFigure with authored shape

**Interaction & Motivation:**
- Both paths are **deliberate & well-motivated**. Generate is the legacy "free" path; shapes provide **explicit melodic control** for authored templates.
- Duality is **not muddy**—it's architectural: FigureSource enum (templates.h:75–81) drives the choice cleanly:
  - `Generate` → call DefaultFigureStrategy::generate_figure
  - Shape names → dispatch to shape strategies
  - `Reference` → lookup Motif by name
  - `Locked` / `Literal` → return user content

- **No waters muddied:** The two interact correctly in compose_figure (composer.h:731–800+), which switches on FigureSource.

**File references:**
- FigureSource dispatch: composer.h:737–790+
- Shape selection (choose_shape): default_strategies.h:184–231

---

### 3. Transform Vocabulary

**Defined transforms** (templates.h:83–99):
- Invert, Reverse, Stretch, Compress, VaryRhythm, VarySteps, NewSteps, NewRhythm, Replicate, TransformGeneral, RhythmTail

**Implementation coverage** (default_strategies.h:119–182):
- ✅ All 11 TransformOps have case branches in apply_transform
- ✅ RhythmTail supported (templates.h:573–586): extracts pulse tail skipping first N
- ✅ Replicate with random stepBetween offset (templates.h:161–166)

**What's MISSING for non-classical genres:**

1. **Chromatic displacement** — steps are diatonic scale-degree integers; no chromatic neighbor approach notes (jazz), passing tones, or blue-note inflections.

2. **Octave displacement** — steps are undirected deltas; no notion of "jump to octave above/below then fill" or register shifts idiomatic to EDM (drop the bass an octave).

3. **Rhythmic displacement / syncopation** — durations are additive floats; no "shift start-point by 1/16" or "anticipate by triplet feel."

4. **Swing quantization** — triplets exist (PulseGenerator:401–409) but no dotted-eighth/sixteenth swing template (jazz, funk).

5. **Mode / scale-degree context change** — no transform that "reinterprets the figure in Phrygian" or applies a flat-7 → flat-3 mapping (blues/rock).

6. **Grid-based pattern operations** — no notion of 16th-note sequences, looped repeats, or chopped figures (EDM, hip-hop).

**File references:**
- Transform enums & implementation: templates.h:83–99, default_strategies.h:119–182
- FigureBuilder transform methods: figures.h:1100–1145

---

### 4. Step Semantics

**Current:** All steps are diatonic scale-degree integers (int) with two modes:
- `StepMode::Scale` (default): interpret steps as scale degrees (0=root, 1=2nd, 2=3rd, etc.)
- `StepMode::ChordTone` (alternative): constrain to chord tones (unused; declared only in templates.h:73, never consumed)

**Is it sufficient for:**

**Jazz (chromatic approach notes, bebop enclosures)?**
- ❌ Partially. Approach notes require chromatic steps (e.g., +0.5 for a half-step below a target). Current int-only step system **cannot express** "play C-sharp (chromatic approach to D)."
  - Workaround: Use accidental field (FigureUnit.accidental, figures.h:473) to bend individual notes, but this is **post-hoc ornament, not melodic logic.**
  - No support for 5-note enclosures (chromatically approach, skip, step resolution) as a shape.

**Blues (flat-3, flat-5, flat-7 blue notes)?**
- ❌ Not natively. Blue notes are scale-degree *inflections*, not different scale degrees. Current model has no notion of "play degree 3, but blue-flat (microtonal offset)."
  - Accidental field could approximate (set accidental=-1 on a major-3rd to make it minor-3rd), but this is **pitch quantization, not step semantics.**

**Microtonal music (fractional semitones)?**
- ❌ Not supported. Steps are integers; no notion of quarter-tone or arbitrary fractional intervals.

**File references:**
- StepMode enum: templates.h:73
- StepMode field in FigureTemplate: templates.h:179 (read but never acted upon)
- Accidental field: figures.h:473 (available, underutilized)
- FigureUnit struct: figures.h:469–476

---

### 5. Rhythmic Expressiveness

**Duration representation:** float beats (continuous).

**What the model CAN represent:**
- ✅ Arbitrary durations (0.25, 0.5, 1.333, 1.6, etc. beats)
- ✅ Triplet groups (PulseGenerator:401–409): triplet sixteenths (3 × 1/6 = 0.5), triplet eighths (3 × 1/3 = 1.0), triplet quarters (3 × 2/3 = 2.0)
- ✅ Dotted rhythms (explicitly via rhythm variation: figure.h:824–828)
- ✅ Musical durations (0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 beats)

**What the model CANNOT represent:**
- ❌ **Swing feel (dotted-eighth/sixteenth)** — no template or transform to apply a "swing ratio" (e.g., "tripletize every pair of eighth notes with 2:1 ratio"). Would need to rescale 1st of pair to 2/3 × original, 2nd to 1/3.
- ❌ **Tuplets inside tuplets** — no recursive tuplet composition (e.g., quintuplets of triplet eighths). PulseGenerator has flat triplet support only.
- ❌ **Metric modulation** — no notion of "shift the beat grid from quarter=120 to dotted-quarter=120" mid-figure.
- ❌ **Grid-based 16th-note sequences** — common in EDM/hip-hop. Must represent as list of floats (e.g., [0.125, 0.125, 0.25, 0.25]); no syntax for "start at 16th position, repeat," or "generate fills in 16th-note grid."

**Breakdown points:**
- **Swing:** Solvable with a post-process transform (scale pairs).
- **EDM (16th-note grid patterns):** Solvable by manual duration lists or a grid-based rhythm builder.
- **Microtonal rhythm (e.g., 7-stroke subdivisions):** Solvable as rational approximations.

**File references:**
- PulseSequence: figures.h:16–48
- PulseGenerator: figures.h:386–463 (triplet-aware generation)
- Rhythm variation (split/dot): figures.h:799–833

---

### 6. Randomness & Seeding

**Reproducibility:** ✅ GOOD.

- Each StepGenerator is seeded explicitly (figures.h:97–99: `explicit StepGenerator(uint32_t seed = 0x57E9'0000u)`)
- Each PulseGenerator is seeded explicitly (figures.h:389: `explicit PulseGenerator(uint32_t seed = 0x5057'0000u)`)
- Each FigureBuilder is seeded explicitly (figures.h:739–740: `explicit FigureBuilder(uint32_t seed = 0xF1B0'0001u)`)
- FigureTemplate has seed field (templates.h:187): read by shape strategies to seed random generators
- Global RNG guard (rng::Scope, rng.h:18–30): thread-safe RAII, installs Composer's rng_ as thread-local for shape strategy calls

**Leaked non-seeded randomness:** ❌ Minor issue at one point:
- In shape_strategies.h, ShapeScalarRunStrategy et al. call `::mforce::rng::next()` as fallback when ft.seed == 0 (lines 35, 54, 72, 94, 113, 128, 147, 165, 183, 202, 217, 232, 248):
  ```cpp
  uint32_t seed = ft.seed ? ft.seed : ::mforce::rng::next();
  ```
  This consumes from the Composer's global RNG when template seed is unset. **Not a defect** if Composer's rng_ is already seeded (which it is), but a point of **draw-order coupling:** if you skip a figure, you skip its seed calls. However, this is **intentional for golden-render matching** (default_strategies.h:36–40).

**Thread safety:** ✅ GOOD. Thread-local RNG scope guards ensure determinism.

**File references:**
- StepGenerator seeding: figures.h:97–99
- PulseGenerator seeding: figures.h:389
- FigureBuilder seeding: figures.h:739–740
- RNG scope guard: rng.h:14–30
- Global RNG fallback: shape_strategies.h:35, 54, 72, 94, 113, 128, 147, 165, 183, 202, 217, 232, 248
- FigureTemplate seed field: templates.h:187

---

### 7. Dead Code & Deferred Shapes

**Dead code:**
1. **skip_sequence()** (figures.h:337–379): Declared but **never called**. Has skip selection logic keyed to starting degree but no call site. Likely a remnant from earlier harmonic-analysis work.

2. **ShapeSkippingStrategy::compose_figure() declared (shape_strategies.h:253–258) but bodies are in composer.h:660–697** — not dead, just out-of-line. ✅ Wired correctly.

3. **ShapeSteppingStrategy::compose_figure() declared (shape_strategies.h:260–265) but bodies are in composer.h:699–720** — not dead, just out-of-line. ✅ Wired correctly.

**Unimplemented shapes:** None. All 16 shapes in FigureShape enum are implemented and wired into the strategy registry (composer.h:125–126).

**Shapes declared but strategy bodies missing:**
- ShapeCadentialApproachStrategy declared (shape_strategies.h:78–83) with **no inline body** — body is in composer.h (search shows it exists).

**File references:**
- Dead skip_sequence: figures.h:337–379
- Shape wiring: composer.h:117–127

---

### 8. Genre-Breadth Recommendations (Top 5, ranked by impact)

#### **#1: Add chromatic step support (HIGHEST IMPACT)**
**Problem:** Jazz (approach notes, enclosures), blues (chromatic passing tones), and rock all require chromatic melodic motion. Current int-only step system cannot express it.

**Solution:** Extend StepSequence to hold float steps (or add Pitch deltas in cents). Minimal migration: change `std::vector<int> steps` to `std::vector<float> steps` in StepSequence (figures.h:54), then reinterpret semantics:
- Positive integer = scale degree (1 = major 2nd, 2 = major 3rd)
- Fractional / half-integer = chromatic offset (0.5 = half-step above current degree, 1.5 = 1.5 scale degrees = major 2nd + half-step)

**Affected:** StepGenerator, StepSequence transforms (invert, retrograde, expand/contract), FigureBuilder shape methods, all 16 shapes.

**Scope:** Medium (touch figures.h, default_strategies.h, all shapes).

**File:** figures.h:53–91 (StepSequence), figures.h:96–380 (StepGenerator).

---

#### **#2: Add genre-specific cadence shapes (MEDIUM-HIGH IMPACT)**
**Problem:** Current shapes are classical (CadentialApproach is stepwise-to-target + hold). Blues (V7→I with final-bar resolution riff), jazz (turnaround ii-V-I, chromatic walk-up), rock/funk (anticipation syncopation) all have different signatures.

**Solution:** Add enum parameter or template field to select cadence style:
```cpp
enum class CadenceStyle { Classical, BluesResolution, JazzTurnaround, Funk };
```

Then in either a new shape (CadenceStyled) or existing CadentialApproach, branch on style to generate appropriate contour/rhythm.

**Scope:** Low-medium (one new shape or one conditional in existing shape).

**File:** templates.h (enum), figures.h (shape), shape_strategies.h (strategy).

---

#### **#3: Add rhythmic transforms: swing & syncopation (MEDIUM IMPACT)**
**Problem:** EDM (grid-based 16th patterns), jazz/funk (swing dotted-eighth/sixteenth), rock (anticipation) all require rhythmic manipulation beyond duration scaling.

**Solution:**
1. Add TransformOp::SwingRatio (rescale pairs of notes by a ratio, e.g., 2:1 for swing)
2. Add TransformOp::Anticipate (shift start-point earlier, compress duration)
3. Add TransformOp::GridQuantize (round all durations to nearest 16th, 32nd)

Implement in apply_transform (default_strategies.h:119–182) and PulseSequence transforms (figures.h:30–48).

**Scope:** Medium (add ~5 enum cases, ~50 lines of transform logic).

**File:** templates.h (enums), default_strategies.h (implementation), figures.h (PulseSequence helpers).

---

#### **#4: Deferred shapes for EDM/ambient: Arpeggio, Filter, Swell (LOW-MEDIUM IMPACT)**
**Problem:** EDM uses arpeggiator patterns (fast scalar bursts through chord tones), filters (swells with envelope shape), and pads (long sustains with slow contour). Classical shapes don't cover these idioms.

**Solution:** Add three new shapes:
- **Arpeggio:** Chord-tone outline with configurable speed & direction (vs. TriadicOutline which is static 1-3-5)
- **Swell:** Slow rise + sustain + fall, with note-count on rise/fall configurable
- **Filter:** Pitch contour that smoothly interpolates between steps (vs. stepwise jumps)

**Scope:** Low-medium (three new shape methods, ~50 lines each).

**File:** figures.h (add methods to FigureBuilder), shape_strategies.h (add strategy classes).

---

#### **#5: Implement skipped skip_sequence() + add motif-ization (LOW IMPACT)**
**Problem:** skip_sequence exists (figures.h:337–379) but is never called. If it's intended for harmonic analysis or skip-based contour, integrating it as an option in StepGenerator would add flexibility.

**Solution:**
1. Integrate skip_sequence as an option in StepGenerator::random_sequence or a named method (e.g., harmonic_outline_sequence)
2. Wire into FigureTemplate.contourMotifName lookup (composer.h detail::resolve_contour) as a fallback when no motif is found

**Scope:** Very low (one method call, one condition).

**File:** figures.h (figures.h:337–379), default_strategies.h (generator dispatch).

---

### Summary Table

| Finding | Severity | Genre Impact | File:Line |
|---------|----------|--------------|-----------|
| Chromatic steps missing | Critical | Jazz, blues, rock, folk | figures.h:54, 528 |
| Genre-specific cadences absent | High | Blues, jazz, funk | templates.h:25–31, figures.h:912–929 |
| Swing/syncopation transforms missing | Medium | Jazz, funk, EDM | templates.h:83–99, default_strategies.h:119–182 |
| EDM shapes (arpeggio, swell) absent | Medium | EDM, ambient, minimalism | figures.h:872–1099 |
| skip_sequence() dead code | Low | Harmonic analysis | figures.h:337–379 |
| ChordTone step mode unused | Low | Jazz (chord constraining) | templates.h:73, 179 |
| Triplets-inside-tuplets missing | Low | Progressive, minimalism | figures.h:404–409 |

---

### Conclusion

The architecture is **solid for classical composition** with good separation of concerns (Generate vs. Shape, Transform vocabulary, seeding discipline). The duality of random generation and named shapes is intentional and clean, not muddy.

**To support all genres**, prioritize:
1. **Chromatic steps** (enables jazz, blues, rock melodically)
2. **Genre-specific cadences** (shapes for blues/jazz/funk closures)
3. **Rhythmic transforms** (swing, syncopation for EDM/funk)

These three additions would unlock 80% of genre breadth with 20% architectural change.

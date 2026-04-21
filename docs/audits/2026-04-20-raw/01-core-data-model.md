# Agent 1 — Core Data Model Audit (raw)

**Scope:** `engine/include/mforce/music/basics.h`, `figures.h`, `structure.h`, and related base types.
**Date:** 2026-04-20
**Agent:** Explore subagent

---

## Architectural Audit: MForce Music Data Model

### **STRENGTHS**

1. **Scale abstraction is foundational & extensible** (basics.h:130–229)
   The `ScaleDef` class defines scales via step intervals (ascending/descending), not hard-coded chromatic pitches. This cleanly supports non-12-tone scales: pentatonic, heptatonic, microtonal, or non-Western tunings. The `step_note()` function in conductor.h (lines 47–81) already steps through arbitrary scales. **No bias here—this is done right.**

2. **Articulation & ornament design via variant is compositionally rich** (basics.h:14–96)
   Bend, Slide, Mordent, Trill, Turn, BendMordent—all modeled as stateless directives that performers compile to PitchCurves. This supports jazz bends, guitar slides, classical trills, and electronic parameter curves equally well. The variant pattern is open to extension.

3. **Figure-unit cursor model elegantly decouples pitch from duration** (figures.h:469–476, 542–562)
   `FigureUnit` pairs `step` (scale-degree movement) with `duration`, allowing figures to be rhythm-pitch composites that compose independently. `StepSequence` and `PulseSequence` are first-class and transformable. This naturally supports riffs, loops, EDM rhythms, and jazz comping changes.

4. **Strategy pattern for composition is permissive** (strategy.h:21–66)
   Four-level hierarchy (Section, Passage, Phrase, Figure) with pluggable strategies. `SectionStrategy`, `PassageStrategy`, etc. have empty defaults—implementers override only what they need. **No forced "period" structure here**—that's in `PeriodPassageStrategy`, a choice, not a constraint.

5. **Pitch curve model supports continuous pitch** (pitch_curve.h:24–88)
   Bends, slides, and microtonal glissandi are first-class via `PitchCurve` with semitone offsets and ramp transitions. No quantization to semitones in the curve itself. Blues bends, sitar slides, and EDM pitch sweeps all fit.

---

### **CLASSICAL BIAS & SHORTCUTS**

1. **"MelodicFunction" enum bakes in cadential vocabulary** (templates.h:24–31)
   `Statement, Development, Transition, Cadential` are Western classical structures. **Non-Western and populist music don't have "cadences."** Blues has turnarounds; jazz has II-V-I resolutions; EDM has loop repetition. This enum should be renamed or split, or composers for other genres ignore it and just use `Free`.
   **Action:** Rename to `PhraseFunction` (wider) or add variants like `ChordChange`, `LoopMoment`, `BreakPoint` to signal non-classical roles without forcing them.

2. **Period form baked as a first-class strategy** (period_passage_strategy.h, templates.h:~200+)
   `PeriodSpec`, antecedent/consequent, cadence types (HC/PAC/IAC), `compute_cadence_beat()`—these are all classical. A user wanting verse/chorus or 12-bar blues or minimalist phasing has to **work around** this, not with it.
   **Not a bug** (strategies are pluggable), but it signals "classical is the reference architecture." Other genres are retrofits.

3. **ChordProgression + ScaleChord assume tonal harmony** (basics.h:371–399, figures.h:673–698)
   `ScaleChord` is "degree + alteration + quality (Major/Minor/Dom7/etc.)." This assumes:
   - Diatonic scale as reference (degree 0–6 for heptatonic)
   - Tertian chords (root-3rd-5th)

   **Breaks for:** Jazz (altered dominants, tritone subs, voicings that aren't rooted chords), blues (I-IV-V with variable voicing), non-Western (modal harmony, drone-based, atonal), EDM (pad textures without "chords"). The model doesn't prevent these, but there's no native abstraction. You'd have to either build chords note-by-note (losing harmonic semantics) or abuse `ScaleChord` with weird degrees.

4. **Conductor hardcodes order-of-operations for classical interpretation** (conductor.h:749–855)
   - Performs phrases via `perform_phrase()`, which:
     - Steps through chord tones OR scale degrees based on `ChordFigure` vs. `MelodicFigure`
     - Applies dynamics (velocity ramp) from `DynamicMarking`
     - Applies ornaments (Trill, Mordent, Turn) with fixed sub-note durations

   **Problems:**
   - Dynamics are linear ramps; no exp/log envelopes (needed for EDM synth, organ swells)
   - Ornament timing is tied to the parent note's duration via `ornament_subnote_duration()`, assuming Western durations (quarter, eighth notes). A microtonal trill or EDM LFO osc isn't a "ornament"—it's a parameter modulation.
   - No concept of "performance layer" for humanization/swing beyond the `humanize` millisecond jitter.

5. **StepGenerator assumes melodic contour constraints from tonal music** (figures.h:96–235)
   - Gap-fill (85% probability stepwise recovery after skips) is text-book Schoenberg
   - Climax enforcement (single highest note in 60–75% zone) is narrative arc (classical/folk)
   - Consecutive skip blocking (unless triadic) is counterpoint rule

   These are **fine for melody**, but they're not adjustable per-genre. For bebop (chromatic runs, wider leaps), minimalism (repeating patterns), or a synth arpeggio, these constraints produce wrong shapes. You can call `scalar_run()` or `fanfare()` instead (lines 881–1061), but the RNG-based generator itself is biased.

6. **Meter is 12-TET + simple time signatures** (basics.h:262–276)
   ```cpp
   struct Meter {
     int numerator, denominator;
   };
   ```
   Supports 4/4, 3/4, 5/4, etc., but:
   - No additive meters (e.g., 2+3+2 = 7/8 in Balkan music)
   - No variable meter per measure or automated meter switches
   - No "ternary feel" or swing sixteenths (these are typically encoded as velocity/timing, not meter)

   **Not a blocker**, but simple-fraction meters alone miss many non-Western and jazz contexts.

---

### **MISSING CONCEPTS**

1. **Looping & repetition as first-class structures**
   EDM, minimalism, and much folk/world music build via loops: a 4-bar loop repeats 16x with gradual morphing. No loop type exists. You can build this via Figures + Sections, but there's no native "repeat this Figure N times" with variant logic per iteration (like Steve Reich phasing).
   **Need:** `LoopTemplate` or `RepeatSpec` in templates.h with `iterations`, `morphVariant`, `phaseOffset`.

2. **Rhythmic feel / swing as a first-class concept**
   Jazz swing (12/8 shuffle feel, triplet-based), rock shuffle, EDM grid sync—all require timing micro-adjustments or note grouping semantics. Currently, rhythm is just durations. Swing is hidden in performance-layer `humanize` jitter.
   **Need:** `RhythmicFeel` enum or `SwingProfile` struct with swing%, triplet weight, groove pattern.

3. **Chord voicing strategies beyond "inversion + spread"**
   `Chord::inversion` (0=root, 1=1st, 2=2nd) and `spread` work for classical. For jazz/EDM/funk:
   - Upper structures (e.g., Cmaj7#11: root voicing + upper-voice guide tones)
   - Polychords (C/E, implying layered harmony)
   - Drop-2, drop-3 voicings (standardized jazz comping patterns)
   - Synth pad textures (voiced via register, not root-position logic)

   **Need:** More expressive voicing vocabulary or a plugin interface for custom voicing algorithms.

4. **Modulation beyond key changes**
   `KeyContext` (structure.h:120–129) switches scales at a beat. But:
   - No chromatic mediant / key area shifting (common in film scores, modern pop)
   - No gradual modulation (mixolydian → major pivot)
   - No parallel key (C minor ↔ C major) as a distinct operation

   **Low priority** (achievable via scale override), but limiting for composers wanting smooth key changes.

5. **Microtonal pitch names / non-octave scales**
   `PitchDef` (basics.h:102–111) is hardcoded to the 12-tone chromatic (C, C#, D, …, B). Non-Western quartertones, Pythagorean tuning, or non-octave scales like Bohlen-Pierce would require rewriting PitchDef and all dependent code.
   **Workaround:** Use `note_number()` (float MIDI) directly and bypass `Pitch` lookup. But you lose the symbolic layer.
   **Need:** Decouple `PitchDef` from 12-TET. Use a `TuningSystem` registry and `Pitch` as a (degree, tuning, octave) tuple.

6. **Riff / ostinato as a semantic type**
   A riff is a short, memorable, repeating melodic/harmonic idea. It's fundamental to rock, funk, hip-hop, and electronic music. Currently, it's just a Figure that you manually repeat. No metadata marks it as a riff, no built-in transformations (transpose the riff, warp its rhythm, layer multiple riffs).
   **Need:** `RiffTemplate` with `source` (motif), `repetitions`, `transposition` sequence, `morphing` rules.

7. **Timbre / instrument parameter automation**
   MForce is built for sound + composition, but composition models don't express timbre changes. The `Part.instrumentType` is static. For a pad sweep, filter cutoff glide, or filter resonance envelope, you'd have to drop to the DSP layer.
   **Boundary case** (could be DSP's job), but if composition is "all aspects of music," this is a gap. A `ParameterAutomation` type per `Passage` or `Section` would bridge.

---

### **RECOMMENDATIONS** (ranked by unblocking power)

**1. Generalize "MelodicFunction" → "PhraseFunction" and add genre-specific roles (QUICK WIN)**
   - Rename enum (trivial refactor)
   - Add variants: `LoopMoment`, `Turnaround`, `BreakPoint`, `Drone`, `Fill`
   - Default to `Free` if unused
   - **Impact:** Signals that the model is genre-agnostic; lets rock/funk/EDM composers tag phrases without forcing cadence semantics
   - **Effort:** 1–2 hours (grep + rename)
   - **Blocks:** Nothing immediately, but unblocks strategy work for other genres

**2. Refactor PitchDef to use a TuningSystem abstraction (MID-PRIORITY)**
   - Create `struct TuningSystem { name; vector<float> intervals; bool isOctaveRepeating; }`
   - Make `Pitch` carry `(degree, tuningSystem, octave)` instead of assuming chromatic
   - Keep 12-TET as the default for backward compatibility
   - **Impact:** Unlocks non-Western scales, microtones, and generative systems like Bohlen-Pierce
   - **Effort:** 4–6 hours (moderate refactor, mostly in lookup paths)
   - **Blocks:** Anything microtonal; also foundation for non-Western music

**3. Add LoopTemplate to templates.h and implement a LoopPassageStrategy (MID-PRIORITY)**
   - Define:
     ```cpp
     struct LoopSpec {
       FigureTemplate figure;
       int iterations;
       std::vector<TransformOp> morphPerIteration; // transform each repeat
       float phaseOffset; // for Reich-style phasing
     };
     ```
   - Implement `LoopPassageStrategy::compose_passage()` that unfolds N iterations, applying transforms
   - **Impact:** EDM, minimalism, and electronic music become native, not hacks
   - **Effort:** 3–4 hours (new template type + strategy)
   - **Blocks:** EDM / minimalism / Reich-style generative music

**4. Extend StepGenerator with genre presets & adjustable parameters (QUICK WIN)**
   - Add bitflags: `USE_CLIMAX`, `USE_GAPFILL`, `USE_SKIP_BLOCK`
   - Or enum: `Genre { Classical, JazzBebop, Minimalist, Folk }`
   - Tune probabilities & constraints per genre
   - **Impact:** Makes figure generation genre-aware without breaking existing code
   - **Effort:** 2–3 hours
   - **Blocks:** Bebop, minimalism, folk melody generation

**5. Create a RhythmicFeel / SwingProfile abstraction and wire it into Conductor (MID-PRIORITY)**
   - Define:
     ```cpp
     struct RhythmicFeel {
       enum Type { Straight, Swing, Shuffle, Groove };
       Type type;
       float swingRatio; // 0.5 = no swing, 0.67 = triplet-based
       std::vector<float> velocityProfile; // per beat in a groove pattern
     };
     ```
   - Conductor applies this to timing offsets + velocities during `perform_note()`
   - **Impact:** Jazz swing and funk groove become native, not hacks
   - **Effort:** 3–5 hours
   - **Blocks:** Jazz, funk, R&B rhythm semantics

**6. (Future) Separate "harmonic language" from "tonal hierarchy"** (ARCHITECTURAL)
   - Current: `ScaleChord` assumes tertian chords on scale degrees
   - Generalize: `struct HarmonicElement { pitchSet, roles?, voicing?, duration }`
   - Allows: modal harmony, drone-based, atonal, polychords, cluster chords
   - **Effort:** 8–10 hours (large refactor)
   - **Blocks:** Everything non-tonal (jazz modal, blues drone, non-Western, modern classical)

---

## **SUMMARY**

The model is **clean and extensible at the foundation** (scales, articulations, figures, strategy pattern). But it assumes **classical tonality + period form** as the reference architecture:

- Period form, cadences, and "MelodicFunction" are hardcoded
- ChordProgression is Tertian-chord-specific
- Conductor's performance interpretation (dynamics, ornaments) is Western-classical in timing
- StepGenerator's constraints (gap-fill, climax) are tonal-narrative shaped
- No native support for loops, swing, riffs, or non-12-TET scales

**None of these are bugs**—the model *allows* other genres via careful workarounds. But a user building EDM or jazz has to fight the defaults, not collaborate with them. The **quick wins** (rename MelodicFunction, add genre presets to StepGenerator, add RhythmicFeel) unblock multiple genres with minimal refactor. The **mid-priority** work (LoopTemplate, TuningSystem) gives EDM/minimalism and non-Western music native semantic support.

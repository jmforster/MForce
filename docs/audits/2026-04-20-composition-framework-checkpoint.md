# Composition Framework Checkpoint — 2026-04-20

Synthesis of five parallel audits across the composition framework (core data model, composer + strategies, figure generation + transforms, harmony, templates + JSON). Raw agent reports preserved under `docs/audits/2026-04-20-raw/`.

## Executive summary

The foundation is clean; the fur is classical. Audits converge on the same verdict across data model, composer, figure gen, harmony, and templates: **core abstractions extend gracefully (scales, figures, strategies, pitch curves), but most default behavior assumes Western tonal common-practice and genre-portability will require lifting that bias out of the core into swappable layers.** No structural rewrite needed — the strategy pattern and cursor model are sound. A dozen focused refactors reach ~80% genre breadth; deeper work (microtonal, voicing, modulation planning) extends the remainder.

---

## What's solid (load-bearing foundations to preserve)

- **`ScaleDef` via step intervals** (`basics.h:130-229`) — genuinely tuning-agnostic. Ready for microtones with a small `TuningSystem` wrapper.
- **`FigureUnit` cursor model** (`figures.h:469-476`) — decouples pitch (scale-step) from rhythm (float duration). Clean composable atom.
- **Strategy pattern, 4 levels** (`strategy.h:21-66`) — `{Figure,Phrase,Passage,Section}Strategy` pluggable via registry. Not classical per se; only the *defaults* are.
- **Two-phase plan/compose split** — `plan_*` may mutate the motif pool, `compose_*` is side-effect-free. Contract is consistent; one small hole: shape dispatch skips `plan_figure` (`composer.h:814-819`).
- **`walk()` vs `harmonize()`** — correctly factored; one generates autonomously, the other harmonizes a melody. Keep both.
- **Articulation/Ornament as variant** (`basics.h:14-96`) — open to extension (Bend, Slide, etc. work for any genre).
- **Seeding discipline** — reproducibility is thorough. Every RNG is explicit; thread-local scope guards prevent leakage.

## Classical bias that must be lifted

These are the load-bearing classical assumptions. Each becomes a choke point for jazz, blues, EDM, modal, non-Western:

| Concern | Location | Bias |
|---|---|---|
| Cadence semantics | `period_passage_strategy.h:103-109`, `composer.h:531-560, 976-979` | `cadenceType 1→V`, `2→I` hard-coded. No IAC, plagal, deceptive, modal, vamp, fade |
| Shape choices per function | `default_strategies.h:184-231` | Statement→{TriadicOutline,...}, Cadential→HeldNote. No jazz/blues/funk lexicon |
| Step sequences are diatonic ints | `figures.h:54` and throughout | No chromatic approach notes, no blue-note inflections, no microtones |
| `MelodicFunction` enum | `basics.h` (Free/Statement/Development/Transition/Cadential) | Classical narrative vocabulary; no Loop/Riff/Vamp/Turnaround/Drone |
| `StepGenerator` constraints | `figures.h:96-235` | Gap-fill after skip (85%), climax enforcement in 60-75% zone, consecutive-skip blocking — all Schoenberg/counterpoint priors |
| Harmonic rhythm grid | `chord_walker.h`, `period_passage_strategy.h:356-361` | Chord changes snap to 2-beat half-note grid. No anticipations, no pushed changes |
| ChordLabel parser | `style_table.h:28-111` | No V7b9/alt, no slash chords, no "IVm" notation |
| Voice leading | Absent | Inversion/spread static per call site; no cross-chord smoothing |
| Meter representation | `basics.h:262-276` | Simple fractions. No additive 2+3+2, no swing ratio, no metric modulation |
| `PeriodSpec` in template shape | `templates.h:283-309` | Antecedent/consequent is the only structural primitive above phrase — no `LoopSpec`, `VerseChorusSpec`, `BlockSpec`, `TurnaroundSpec` |

## Shortcuts, anti-patterns, bugs

- **5 `const_cast`s in `composer.h`** — four are acknowledged transitional (Plan B will drop them when `compose()` takes `PieceTemplate&`), but **#4** (`composer.h:422-424`) mutates `Section::harmonyTimeline` and `chordProgression` through a `const` pointer. That's an ownership bug, not just const-correctness. Fix: `compose_passage_` should take `Piece&`, not `const Piece&`.
- **Shape-strategy taxonomy confusion** — `DefaultFigureStrategy` (a *dispatcher*) is registered as a `FigureStrategy` alongside 16 `ShapeXxxStrategy` (*implementations*). A caller asking the registry for "figure" strategy gets either. The dispatcher then does a **string switch** (`composer.h:794-809`) to look shapes back up *by string* in the same registry. The string names are duplicated in Composer::ctor registration (`composer.h:111-126`). Fix: separate `FigureShapeRegistry`, enum-keyed.
- **Silent JSON holes:**
  - `ChordAccompanimentConfig` has `from_json` but **no `to_json`** — round-trip loses data
  - Enum case-sensitivity is inconsistent across the template schema: `MotifRole`/`MotifOrigin`/`PeriodVariant` are PascalCase-and-throw; `PartRole` is snake_case-silent-default; `MelodicFunction` is now case-insensitive (just fixed). Pick one convention
  - No schema version field — silent rename pain in future
- **Dead code:** `StepSequence::skip_sequence()` declared but never called (`figures.h:337-379`). `StepMode::ChordTone` read (`templates.h:179`) but no branch ever acts on it. Delete or wire.
- **Magic strings** — strategy names, shape names, progression names, style names. No enum, no compile-time check. A typo silently falls back to default.
- **Shape strategies declare methods in `shape_strategies.h` but some implementations live in `composer.h`** (Skipping, Stepping, CadentialApproach). Same header-only project, but the split is unobvious.
- **`generate_figure` ignores `ft.shape`** — the bug we fixed last session. The whole Generate-vs-Shape duality is honest once known but opaque from the outside; `choose_shape` picks a shape that `generate_figure` throws away unless routed through `compose_figure`.
- **Cadence workaround in production:** PAC final figure is force-rewritten to `HeldNote` (`composer.h:976-979`) because `apply_cadence` only tweaks the last step. Real fix is restructuring the cadential tail (approach + sustained arrival).

## Missing concepts (genre-neutral)

**Structural:** `LoopSpec` (ostinato/EDM loops with per-iteration morph), `RiffSpec` (named recurring short figure with transposition-across-changes), `VerseChorusSpec`, `TurnaroundSpec`, `DroneSpec`.

**Rhythmic:** `RhythmicFeel` / `SwingProfile` (jazz swing, shuffle, Latin clave, EDM groove). `Tempo`/`TempoChange` curve. Additive-meter, metric modulation.

**Harmonic:** `VoicingStrategy` interface (drop-2, rootless, shell, upper structures). Modal and non-functional harmony modes. Cross-section modulation planning. Sub-beat anticipation.

**Pitch:** Chromatic steps (float step values — `0.5` = chromatic). Blue-note inflections. `TuningSystem` abstraction for non-12-TET, non-octave scales.

**Expression:** Per-note parameter automation (filter cutoff, brightness, vibrato depth) — straddles composition/DSP boundary but belongs somewhere.

**Meta:** Corpus ingest → motif/progression/rhythm distillation. JSON schema versioning/migration. Validation with line-number diagnostics.

## Metrics: judging musicality before listening

Brainstorming broadly. Grouped by class, with the cheapest at the top:

### Surface sanity (cheap, local)
- No melodic leap > octave unless explicitly authored
- Every phrase has at least one chord-tone attack on a downbeat
- Every cadential figure lands on a longer duration than body figures
- No clipping in rendered audio (peak < 0.95)
- All expected motif names resolve
- Chord-tone coverage: fraction of melody notes that are chord tones (by role: passing/chord/non-chord)

### Structural metrics (computable on Piece JSON)
- **Self-similarity matrix**: similarity of every bar vs every bar → repetition index, sectional boundaries
- **Motif recurrence count**: each motif's usage count, transform diversity (how many variants)
- **Pitch-class histogram vs scale**: how tonally consistent (KS-divergence from expected mode)
- **Step-vs-leap ratio**: conjunct vs disjunct motion, per phrase
- **Contour typology** (Huron): classify each phrase as ascending/descending/arch/inverted-arch/flat — then score diversity
- **Climax prominence**: highest pitch, its timing (Huron's "golden ratio" expectation), duration
- **Range and tessitura** per voice/part
- **Rhythm entropy**: Shannon entropy of durations
- **Pulse-density trajectory**: notes per beat over time; look for arc
- **Metric-accent alignment**: do longer/louder notes land on strong beats?
- **Phrase-endpoint rests**: breathing between phrases
- **Cadence arrival strength**: final note duration, chord-tone status, pitch register

### Information-theoretic (model-free)
- **Compression ratio**: zlib the note sequence — too high = repetitive, too low = noise
- **Markov entropy**: 2nd/3rd-order pitch entropy compared to a genre norm
- **Surprise profile**: IDyOM-style per-note surprise (information content) plotted over time — good music has tension-release arcs, not flatline

### Corpus-referenced
- Extract feature vector (histogram + contour + rhythm stats)
- Nearest-neighbor in a reference corpus (Mozart sonatas, Ellington, Chicago blues, chiptune, Reich)
- Distance-to-nearest + which genre dominates nearest-N
- Flag outlier features

### Model-based (expensive)
- Perplexity under a small language model per genre
- "Sounds like genre X" classifier confidence

### Agentic (LLM-as-judge)
- Serialize Piece as a score representation (DURN, ABC, MusicXML)
- Pairwise preference judge: "which of these two candidates sounds more like [intended genre]?"
- Dimensioned rubric: coherence / interest / resolution / genre-fit / surprise — 1–5 each
- Failure-mode finder: "identify the 3 least musical moments and why"

### Multi-verse workflow
Best metric per se is an ensemble. Usage: render N candidates per template (varied seed), score all, user only listens to top-K ranked by composite metric. Judges tie-break.

## Agent-driven iterative development

**Natural fits** (self-contained, testable in isolation):

1. **Shape strategies** — 16 now, easily 30+. Each is 20-50 lines + golden test. Fleet of agents can expand the catalog (bebop enclosure, blues lick, Reich-phase, EDM arp, Balinese gamelan figure). Judge agent validates by listening/metric.
2. **Style tables** — JSON only. Agent per genre: research corpus → build transition table → validate by sampling. Outputs in `styles/`.
3. **Genre passage strategies** — `JazzPassageStrategy`, `BluesPassageStrategy`, `LoopPassageStrategy`, `ModalPassageStrategy`, etc. Each is a self-contained subclass.
4. **Motif libraries** per genre — authored by agents, evaluated by judge agents.
5. **Metric implementations** — each metric above is a pure function over Piece JSON. Parallel agent work.
6. **Corpus ingest** — batch MIDI/MusicXML→motifs/progressions distillation. Offline, embarrassingly parallel.
7. **Template authoring** — agent takes high-level user intent ("funky 16-bar with a breakdown") → template JSON. Iterative refinement via judge feedback.

**Poor fits** (too entangled for parallel):
- Core data-model refactors (cursor model, strategy interface changes) — sequential, high coordination
- Conductor performance changes — crosses DSP/composition boundary

## Alternative architectures worth considering

1. **`GenrePack` plugin** — lift `{style_table, cadence_resolver, shape_preferences, rhythm_feel, motif_library, voicing_strategy}` into a swappable object. Template references by name: `"genre": "bebop"` and all defaults follow. Makes genre-portability a first-class axis.
2. **Piece-as-IR + multi-target rendering** — treat the composed Piece JSON as an IR. Render to audio (current Conductor), MusicXML, MIDI, ABC, LilyPond. Unlocks external tooling, corpus comparison, DAW round-trip.
3. **Multi-verse + judge loop** — render N, score, rank, surface top-3 to user. Makes metric work pay off immediately.
4. **Constraint-based composition** — alongside generative walk, allow template authors to express constraints (motif budget, density targets, cadence positions) and solve. Mirror of our SAT-oscillator experiments.
5. **Typed variant templates** — replace flat structs-with-optional-fields with `std::variant<PeriodSpec, LoopSpec, VerseChorusSpec, ThroughComposedSpec>`. Eliminates silent field drops; compile-time structure check.

## Priority roadmap

**Phase 1 — Foundation cleanup** (~1 week, high ROI, blocks nothing)
- Fix `const_cast` #4 bug; make `compose_passage_` take `Piece&`
- Add `ChordAccompanimentConfig::to_json`
- Unify enum JSON conventions (all snake_case, all lenient-default with warn)
- Extract `FigureShapeRegistry` (enum-keyed) separate from `StrategyRegistry`
- Delete dead code (`skip_sequence`, unused `StepMode::ChordTone` branch or wire it)
- Add JSON schema `"version"` field
- Extract `cadence_chord` logic into `CadenceResolver` interface

**Phase 2 — Genre-portability foundations** (~2-3 weeks)
- `GenrePack` abstraction
- `LoopPassageStrategy` + `LoopSpec`
- Rename `MelodicFunction` → `PhraseFunction`; add Loop/Riff/Vamp/Turnaround/Drone variants
- Float step sequences (chromatic approach) — or a `(scaleDegree, chromaticOffset)` tuple
- Expand `cadenceType` enum + allow custom endChord
- Expand `ChordLabel` parser (altered dominants, slash chords)

**Phase 3 — Expressive layer** (~3-4 weeks)
- `RhythmicFeel` / swing profile wired into Conductor
- `VoicingStrategy` interface (smooth, drop-2, shell, rootless)
- Sub-beat harmonic rhythm / anticipation
- Modal harmony mode

**Phase 4 — Evaluation infrastructure** (~2-3 weeks, parallelizable)
- Implement structural metrics (fleet of agents, each one metric)
- LLM-judge pipeline + multi-verse render/rank
- Corpus ingest → reference feature distributions

**Phase 5 — Long-tail reach**
- `TuningSystem` abstraction (microtonal, non-12-TET)
- Cross-section modulation planning
- Parameter automation (timbre over time)
- Non-Western cyclic forms, rasas, raga grammar

---

*Note from Matt immediately following this checkpoint (2026-04-20): "No way. Not even close. We are taking a step back." The roadmap above is the Claude-synthesized version and did not land as the right direction. Preserved here for reference; any subsequent planning should start from Matt's redirect, not from this.*

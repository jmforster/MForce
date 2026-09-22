# Composition generators — service status

Matt's rule, 2026-09-21 (comp ground rule 5): **all generator code except
the Markov Figure generator is cataloged but SHELVED** until recalled or
retired. Cataloging declares service status; it deletes nothing — every
line of code below still ships and still has null-gate coverage via its
keeper template. Recall or retirement is Matt's call, per entry, one word
in a session or in REVIEW.

The crawl (spec 2026-09-21-comp-crawl-mary-design.md) uses NONE of the
shelved machinery: fully-specified templates, Locked figures, span-mode
block chords through the legacy root-position voicing path.

| Generator | Where | Status | What it was |
|---|---|---|---|
| Markov Figure generator | tools/ + corpus/mtd_seg pipeline (order-2 joint model over MTD) | **LIVE** | The atom source — figure cells that "beat random out of the gate"; the walk stage builds on it |
| Figure transform library (as FIT OPERATORS) | figure_transforms.h via derived motifs (`derivedFrom`+`transform`, walk1 spec §2) | **LIVE** (recalled 2026-09-21) | Recalled from the shelf for the walk: declared derivations with reasons (rep3_a/rep3_b/head_a), NOT random variation sprinkling — that failure mode stays shelved below |
| Harmonic anchor selector | anchor_selector.h (opt-in `anchorMode: "harmonic"`) | **LIVE** (new 2026-09-21) | Walk round 1: figure starting degrees chosen against the chord timeline — R1-R3 hard, stacked chord-tone weights, seeded roulette |
| RandomFigureBuilder path | `FigureSource::Generate` in composer.h | SHELVED | Constraint-driven random figure content |
| AlternatingFigureStrategy | passage_strategies.h | SHELVED | ABAB chord-tone/scalar figures over a progression |
| Wandering / Bruckner-pedal / sequence / connective passage strategies | passage_strategies.h + per-strategy configs in templates.h | SHELVED | Run 15-18 passage generation stages |
| Period / Sentence / TwoFigure / Elaborated phrase strategies | phrase_strategies.h, period_passage_strategy.h, two_figure_phrase_strategy.h, elaborated_phrase_strategy.h | SHELVED | Structured phrase construction (antecedent/consequent, basic-idea repetition) |
| Figure transform wiring in the phrase builder | composer.h transform dispatch + templates (invert/retrograde/rotate/ornament on repeats) | SHELVED | Run 15/16 literal-repeat variation |
| Cadence generation | `apply_cadence` + cadentialArrival shaping (composer.h) | SHELVED | Harmonically-targeted phrase endings; crawl templates carry cadences in their chords instead |
| Passage-mode scorer + phrase composite screens | corpus/mtd_seg harnesses | SHELVED | Run 17 mechanical screening; open semantics questions parked with it (backlog 20, REVIEW 14) |
| Voicing selectors / profiles / dictionary | voicing machinery behind `voicingSelector` (composer.h, voicing_profile.h) | SHELVED | Two-level voicing model; the crawl uses only the legacy inversion/spread path (`chordConfig`) |
| PatternLibrary / LibraryPassageStrategy | pattern_library.h + tools/ppl_to_json | SHELVED | .ppl pattern realization ("computer music" verdict standing) |

Null-gate keeper templates (scores/baselines/, post-cull 2026-09-21, 19
files) cover every shelved path so recall starts from a known-good floor;
the 9 feature-subset templates moved to
scores/old/baselines_culled_2026-09-21/ (archive, never deleted).

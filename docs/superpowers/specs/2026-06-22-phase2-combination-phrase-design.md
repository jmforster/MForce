# Phase 2 v1 — Combination Phrase from Markov Atoms

**Date:** 2026-06-22
**Status:** Approved (design); pre-implementation
**Depends on:** Phase 1 Markov figure generator (`corpus/mtd_seg/markov_*.py`)
**Scope:** Python chooser + existing engine render. **Zero C++ changes** in v1.

## Goal

Turn good atoms into a hearable **phrase**. Sample 2 independent figures from the Markov
pool, randomly choose a *combination pattern* (repetition + transform + placement), author a
template JSON, and render it through the existing engine to WAV.

The bet (Matt's Phase-1 insight): the atom was the hard part. Repetition and transformation of
good atoms should stay musical; only stitching *unrelated* atoms is not. v1 tests that bet.

## Why Python chooser + existing engine

The engine already has everything *below* the decision: figure transforms (`invert`,
`retrograde`, `replicate`), multi-figure phrase composition (`DefaultPhraseStrategy` walks
`figures[]`), `FigureConnector` seam control, a motif pool (`userProvided` seeds + `Reference`),
and a CLI render path (`mforce_cli --compose <patch> <out> --template <t.json>`). The only
missing piece is the random *chooser*. We put it in Python where it's cheap to iterate, and
promote it to a C++ `CombinationPhraseStrategy` (next to `TwoFigurePhraseStrategy`) once the
pattern set sounds right.

Python materializes the concrete figures (applies transforms itself: invert = negate steps,
retrograde = reverse) and injects each distinct figure as a `userProvided` seed — so the engine
receives **plain figures + connectors** and we depend on no engine-side transform parsing.

## Three random axes per phrase

1. **Pattern** over figures A, B — small curated set, e.g.
   `AB`, `AAB`, `AAAB`, `ABAB`, `AABB`, `AAA'B`, `ABA'B`  (A' = a transformed A).
2. **Transform** — A' ∈ {retrograde, invert} (deterministic, clear). Applied at chosen positions.
3. **Repetition placement** — for a run of repeated A, choose the connector `leadStep`:
   - same note: `leadStep = -net_step(A)`
   - climbing: `leadStep = 0` (each starts where the last ended)
   - sequence up a 3rd: `leadStep = -net_step(A) + 2`
   Python knows `net_step` of each materialized figure, so it computes each seam.

## Template JSON shape (grounded in existing parser)

Mirrors `patches/template_mary.json` + the connector parser at `templates_json.h:679`.

```json
{
  "keyName": "C", "scaleName": "Major", "bpm": 84.0, "masterSeed": <seed>,
  "seeds": [
    {"name": "A",  "figure": {"units": [{"duration":0.5,"step":0}, ...]}, "userProvided": true},
    {"name": "B",  "figure": {"units": [...]}, "userProvided": true}
    /* transformed copies (e.g. "A_retro") injected as their own seeds when a pattern uses A' */
  ],
  "sections": [{"name": "Main", "beats": <total>}],
  "parts": [{
    "name": "melody", "role": "melody",
    "passages": {"Main": {"phrases": [{
      "startingPitch": {"octave": 4, "pitch": "C"},
      "figures":    [{"source":"reference","seedName":"A"},
                     {"source":"reference","seedName":"A"},
                     {"source":"reference","seedName":"A"},
                     {"source":"reference","seedName":"B"}],
      "connectors": [null, -N, -N, L]
    }]}}
  }]
}
```

- `connectors` is parallel to `figures`; entry forms: `null` (default) · integer (`leadStep`
  shorthand) · `{"elide":N,"adjust":beats,"leadStep":S}`. `connectors[0]` is the lead-in to the
  first figure (no predecessor → `null`).
- `N = net_step(A)`; `L` = lead into B (default 0 for v1 unless placement says otherwise).
- `elide`/`adjust` left at defaults in v1 (bonus seam knobs for later dovetailing).

## Components (all under `corpus/mtd_seg/`)

1. **`markov_phrase.py`** — the chooser/author:
   - load `MarkovModel`, sample 2 independent figures (reuse `gen_model`).
   - roll the 3 axes (seeded RNG, stored in JSON for reproducibility).
   - materialize figures + transformed copies; compute connectors from `net_step`.
   - write a template JSON to `corpus/mtd_seg/phrases/<id>.json` (and echo the chosen pattern).
   - batch mode: emit K phrases for audition.
2. **Render driver** — invoke `mforce_cli --compose <patch> renders/markov_phrases/<id> --template <t.json>`
   for each, producing WAVs under `renders/markov_phrases/`. Use one stock instrument patch
   (pick a known-good simple one during implementation).

## Verifications required during implementation (flagged, not assumed)

- **Connector indexing**: confirm `connectors[i]` is the lead-in to `figures[i]` and
  `connectors[0]` is the dummy/default — by rendering a known phrase and checking realized
  pitches against hand-computed expectations (memory says `FC[0]` dummy, dense-parallel; verify).
- **Reference resolution**: confirm `seeds[]` with `userProvided:true` populate the motif pool
  and `{"source":"reference","seedName":...}` resolves to them under `--compose`.
- **Single-phrase passage**: confirm `default_passage` renders a one-phrase `phrases[]` with no
  explicit passage strategy.
- **Stock patch**: identify a minimal existing instrument patch that `--compose` renders cleanly.

## Deliverable

K rendered WAV phrases under `renders/markov_phrases/`, each a 2-figure combination, with the
chosen pattern/transform/placement logged per file. Listenable in `mforce_ui`'s audition panel
(48 kHz/16-bit stereo — confirm the CLI WAV writer matches; resample/convert if not).

## Explicit non-goals (v1)

- No C++ changes; the chooser is Python. (Promotion to `CombinationPhraseStrategy` is the
  follow-up once patterns are validated.)
- One phrase per render (passage = single phrase). Multi-phrase / period structure later.
- No harmony/chords — pure diatonic melody.
- Contrast-conditioned fig2 deferred ([[project_contrast_figure_todo]] — fig1 and fig2 are
  independent draws for now).
- `elide`/`adjust` seam smoothing left at defaults.

## Decision gate after v1

- **Combined phrases sound musical** (the bet holds) → promote chooser to a C++
  `CombinationPhraseStrategy`; then add harmony and multi-phrase structure.
- **Specific patterns/placements sound bad** → adjust the pattern set and placement rules in
  Python (cheap) before any promotion.
- **Stitching itself sounds wrong regardless of pattern** → the `FigureConnector` bridging needs
  work; that becomes the focus instead of more patterns.

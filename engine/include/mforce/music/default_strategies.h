#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/figures.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>

namespace mforce {

// Locus is defined in locus.h (included via strategy.h).
// Composer is referenced only via Locus::composer — no forward decl needed here.

// ---------------------------------------------------------------------------
// DefaultFigureStrategy
//
// Wraps the pre-refactor ClassicalComposer::compose_figure /
// generate_figure / generate_shaped_figure / choose_shape / apply_transform
// code paths, so that a FigureTemplate routed through Composer::compose_figure
// produces byte-identical output compared to pre-refactor ClassicalComposer.
// ---------------------------------------------------------------------------
class DefaultFigureStrategy : public FigureStrategy {
public:
  std::string name() const override { return "default_figure"; }

  // compose_figure is DECLARED here, but DEFINED in composer.h below the
  // Composer class. Its body needs the full definition of Composer to call
  // ctx.composer->realized_motifs(), and composer.h includes this header —
  // breaking the cycle requires the out-of-line definition to live there.
  // Do NOT add an inline body for compose_figure in this file.
  MelodicFigure compose_figure(Locus locus, const FigureTemplate& figTmpl) override;

  // PUBLIC so Composer::realize_motifs_ can call generate_figure
  // directly, bypassing compose_figure's switch. The pre-refactor
  // ClassicalComposer::realize_motifs called the private generate_figure
  // directly, so preserving that call shape is required for bit-identical
  // output against the golden render.
  MelodicFigure generate_figure(const FigureTemplate& figTmpl, uint32_t seed);
  MelodicFigure apply_transform(const MelodicFigure& base, TransformOp op,
                                int param, uint32_t seed);

  // Static — called directly by DefaultPhraseStrategy (Task 6) to select a
  // shape under a MelodicFunction.
  static FigureShape choose_shape(MelodicFunction func, int posInPhrase,
                                  int totalFigures, uint32_t seed);
};

// ============================================================================
// Inline definitions — bodies lifted verbatim from classical_composer.h.
// ============================================================================

inline MelodicFigure DefaultFigureStrategy::generate_figure(
    const FigureTemplate& figTmpl, uint32_t seed) {
    StepGenerator sg(seed);

    float defaultPulse = (figTmpl.defaultPulse > 0) ? figTmpl.defaultPulse : 1.0f;

    auto generate_steps = [&](int noteCount) -> StepSequence {
      if (figTmpl.targetNet != 0) {
        return sg.targeted_sequence(noteCount, figTmpl.targetNet);
      } else if (figTmpl.preferStepwise) {
        return sg.no_skip_sequence(noteCount);
      } else {
        float skipProb = figTmpl.preferSkips ? 0.6f : 0.3f;
        return sg.random_sequence(noteCount, skipProb);
      }
    };

    auto clamp_steps = [&](StepSequence& ss) {
      if (figTmpl.maxStep > 0) {
        for (int i = 0; i < ss.count(); ++i) {
          if (ss.steps[i] > figTmpl.maxStep) ss.steps[i] = figTmpl.maxStep;
          else if (ss.steps[i] < -figTmpl.maxStep) ss.steps[i] = -figTmpl.maxStep;
        }
      }
    };

    if (figTmpl.totalBeats > 0) {
      int noteCount = int(figTmpl.totalBeats / defaultPulse);
      noteCount = std::clamp(noteCount, figTmpl.minNotes, figTmpl.maxNotes);

      StepSequence ss = generate_steps(noteCount);
      clamp_steps(ss);

      MelodicFigure fig = MelodicFigure::from_steps(ss, defaultPulse);
      if (!fig.units.empty()) fig.units[0].step = 0;

      Randomizer varyRng(seed + 2);
      if (varyRng.decide(0.4f)) {
        fig = figure_transforms::vary_rhythm(fig, varyRng);
      }
      return fig;
    }

    Randomizer countRng(seed + 3);
    int noteCount = countRng.int_range(figTmpl.minNotes, figTmpl.maxNotes);

    StepSequence ss = generate_steps(noteCount);
    clamp_steps(ss);

    MelodicFigure fig = MelodicFigure::from_steps(ss, defaultPulse);
    if (!fig.units.empty()) fig.units[0].step = 0;
    return fig;
}

inline MelodicFigure DefaultFigureStrategy::apply_transform(
    const MelodicFigure& base, TransformOp op, int param, uint32_t seed) {
    return figure_transforms::apply(base, op, param, seed);
}

inline FigureShape DefaultFigureStrategy::choose_shape(
    MelodicFunction func, int posInPhrase, int totalFigures, uint32_t seed) {
    Randomizer r(seed);
    bool isLast = (posInPhrase == totalFigures - 1);
    bool isFirst = (posInPhrase == 0);

    switch (func) {
      case MelodicFunction::Statement: {
        // Opening motif: clear, memorable shapes
        static const FigureShape opts[] = {
          FigureShape::RepeatedNote, FigureShape::TriadicOutline,
          FigureShape::ScalarRun, FigureShape::NeighborTone,
          FigureShape::Fanfare
        };
        if (isLast) return FigureShape::HeldNote;
        return opts[r.int_range(0, 4)];
      }

      case MelodicFunction::Development: {
        // Extension/variation: more complex shapes
        static const FigureShape opts[] = {
          FigureShape::Zigzag, FigureShape::ScalarReturn,
          FigureShape::LeapAndFill, FigureShape::Cambiata,
          FigureShape::ScalarRun
        };
        if (isLast) return FigureShape::ScalarRun;
        return opts[r.int_range(0, 4)];
      }

      case MelodicFunction::Transition: {
        static const FigureShape opts[] = {
          FigureShape::ScalarRun, FigureShape::LeapAndFill,
          FigureShape::Zigzag, FigureShape::ScalarReturn
        };
        return opts[r.int_range(0, 3)];
      }

      case MelodicFunction::Cadential: {
        if (isFirst || !isLast)
          return FigureShape::CadentialApproach;
        return FigureShape::HeldNote;
      }

      case MelodicFunction::Free:
      default:
        return FigureShape::Free;
    }
}

// ---------------------------------------------------------------------------
// DefaultPassageStrategy
//
// Walks the phrase list of a PassageTemplate and realizes each phrase via
// the Composer's phrase dispatcher. Mirrors pre-refactor
// ClassicalComposer::compose_passage (classical_composer.h:143-159) with
// the PitchReader still constructed locally — Phase 1a preserves the exact
// fallback semantics: reset to (octave 5, degree 0) for every phrase that
// doesn't provide its own startingPitch, because that's what the
// pre-refactor compose_phrase did at classical_composer.h:188-190.
//
// Body is DECLARED here but DEFINED in composer.h below the Composer class,
// because the body calls ctx.composer->compose_phrase(...) and Composer is
// only forward-declared in this file. Same pattern as DefaultFigureStrategy.
// ---------------------------------------------------------------------------
class DefaultPassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "default_passage"; }

  Passage compose_passage(Locus locus, const PassageTemplate& passTmpl) override;
};

// ---------------------------------------------------------------------------
// DefaultPhraseStrategy
//
// Walks a PhraseTemplate, dispatches each figure via
// ctx.composer->compose_figure(...), and applies end-of-phrase cadence
// adjustment.
//
// Mirrors pre-refactor ClassicalComposer::compose_phrase
// (classical_composer.h:181-224) with edits:
//   - rng.rng() becomes ctx.rng->rng()
//   - inlined compose_figure call becomes ctx.composer->compose_figure(...)
//   - choose_shape resolves through DefaultFigureStrategy::choose_shape
//
// RNG call order MUST be preserved. The pre-refactor path called rng.rng()
// inside the figure loop exactly once per figure that needed a shape
// auto-selected (inside the if (function != Free && source == Generate &&
// shape == Free) block). The new path does the same, via ctx.rng->rng(),
// and ctx.rng points at Composer::rng_ (set up in Task 7), so the sequence
// of rng() calls hits the same underlying generator in the same order.
// ---------------------------------------------------------------------------
class DefaultPhraseStrategy : public PhraseStrategy {
public:
  std::string name() const override { return "default_phrase"; }

  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
  static int degree_in_scale(const Pitch& pitch, const Scale& scale);
  // Adjusts ONLY the last figure of the phrase to land on cadenceTarget.
  // When a phrase's cadential tail spans multiple figures (e.g., K467 bars
  // 7-8 where bar 7's two figures approach and bar 8 arrives), the earlier
  // figures shape the approach trajectory but are NOT target-adjusted —
  // only the final arrival figure is. This matches classical cadence
  // structure: the "tail" is a sequence of figures (author's choice of how
  // many), and only the arrival needs target-landing logic.
  static void apply_cadence(Phrase& phrase, const PhraseTemplate& tmpl,
                            const Scale& scale);
};

// -- degree_in_scale: verbatim from classical_composer.h:167-175 -------------
inline int DefaultPhraseStrategy::degree_in_scale(const Pitch& pitch,
                                                  const Scale& scale) {
    float rel = std::fmod(float(pitch.pitchDef->offset - scale.offset()) + 12.0f, 12.0f);
    float accum = 0;
    for (int d = 0; d < scale.length(); ++d) {
      if (std::abs(accum - rel) < 0.5f) return d;
      accum += scale.ascending_step(d);
    }
    return 0; // fallback to root
}

// -- apply_cadence: role-aware. Skips trailing Connective / PostCadential
//    figures when locating the "cadence target" figure, because those are
//    pickups/extensions that don't belong to the phrase's cadential landing.
//    Net step calc and adjustment both use the effective phrase (up to and
//    including the last non-skipped figure).
inline void DefaultPhraseStrategy::apply_cadence(Phrase& phrase,
                                                 const PhraseTemplate& tmpl,
                                                 const Scale& scale) {
    // Walk backwards through tmpl.figures to find the last non-Connective,
    // non-PostCadential figure. That figure's phrase.figures index is where
    // the cadence target applies.
    int lastIdx = phrase.figure_count() - 1;
    while (lastIdx >= 0) {
      // Get the template's role for this figure position (if available).
      std::optional<MotifRole> role;
      if (lastIdx < (int)tmpl.figures.size()) {
        role = tmpl.figures[lastIdx].role;
      }
      if (role && (*role == MotifRole::Connective || *role == MotifRole::PostCadential)) {
        --lastIdx;
        continue;
      }
      break;
    }
    if (lastIdx < 0) return;  // entire phrase is Connective/PostCadential — no-op

    int startDeg = degree_in_scale(phrase.startingPitch, scale);
    int len = scale.length();

    int netSteps = 0;
    for (int f = 0; f <= lastIdx; ++f) {
      netSteps += phrase.figures[f]->net_step();
    }

    int landingDeg = ((netSteps + startDeg) % len + len) % len;
    int target = tmpl.cadenceTarget % len;

    if (landingDeg == target) return; // already correct

    int diff = target - landingDeg;
    if (diff > len / 2) diff -= len;
    if (diff < -len / 2) diff += len;

    auto& targetFig = *phrase.figures[lastIdx];
    if (targetFig.units.empty()) return;
    targetFig.units.back().step += diff;
}

// -- compose_phrase: DECLARED here, DEFINED in composer.h below Composer -----
// Body calls ctx.composer->compose_figure(...); Composer is forward-declared
// only in this file, so the definition must live after the full Composer class.

} // namespace mforce

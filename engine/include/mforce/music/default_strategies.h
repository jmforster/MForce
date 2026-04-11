#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>

namespace mforce {

// Forward — Composer is defined in composer.h and used via ctx.composer.
struct Composer;

// ---------------------------------------------------------------------------
// DefaultFigureStrategy
//
// Wraps the pre-refactor ClassicalComposer::realize_figure /
// generate_figure / generate_shaped_figure / choose_shape / apply_transform
// code paths, so that a FigureTemplate routed through Composer::realize_figure
// produces byte-identical output compared to pre-refactor ClassicalComposer.
// ---------------------------------------------------------------------------
class DefaultFigureStrategy : public Strategy {
public:
  std::string name() const override { return "default_figure"; }
  StrategyLevel level() const override { return StrategyLevel::Figure; }

  // realize_figure is DECLARED here, but DEFINED in composer.h below the
  // Composer class. Its body needs the full definition of Composer to call
  // ctx.composer->realized_seeds(), and composer.h includes this header —
  // breaking the cycle requires the out-of-line definition to live there.
  // Do NOT add an inline body for realize_figure in this file.
  MelodicFigure realize_figure(const FigureTemplate& figTmpl,
                               StrategyContext& ctx) override;

  // PUBLIC so Composer::realize_seeds_ (Task 7) can call generate_figure
  // directly, bypassing realize_figure's switch. The pre-refactor
  // ClassicalComposer::realize_seeds called the private generate_figure
  // directly, so preserving that call shape is required for bit-identical
  // output against the golden render.
  MelodicFigure generate_figure(const FigureTemplate& figTmpl, uint32_t seed);
  MelodicFigure generate_shaped_figure(const FigureTemplate& ft, uint32_t seed);
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
    FigureBuilder fb(seed + 1);

    fb.defaultPulse = (figTmpl.defaultPulse > 0) ? figTmpl.defaultPulse : 1.0f;

    if (figTmpl.totalBeats > 0) {
      // Build from total length
      int noteCount = int(figTmpl.totalBeats / fb.defaultPulse);
      noteCount = std::clamp(noteCount, figTmpl.minNotes, figTmpl.maxNotes);

      StepSequence ss;
      if (figTmpl.targetNet != 0) {
        ss = sg.targeted_sequence(noteCount - 1, figTmpl.targetNet);
      } else if (figTmpl.preferStepwise) {
        ss = sg.no_skip_sequence(noteCount - 1);
      } else {
        float skipProb = figTmpl.preferSkips ? 0.6f : 0.3f;
        ss = sg.random_sequence(noteCount - 1, skipProb);
      }

      MelodicFigure fig = fb.build(ss, fb.defaultPulse);

      // Apply random rhythm variation
      Randomizer varyRng(seed + 2);
      if (varyRng.decide(0.4f)) {
        fig = fb.vary_rhythm(fig);
      }

      return fig;
    }

    // No total beats specified — use note count range
    Randomizer countRng(seed + 3);
    int noteCount = countRng.int_range(figTmpl.minNotes, figTmpl.maxNotes);

    StepSequence ss;
    if (figTmpl.targetNet != 0) {
      ss = sg.targeted_sequence(noteCount - 1, figTmpl.targetNet);
    } else if (figTmpl.preferStepwise) {
      ss = sg.no_skip_sequence(noteCount - 1);
    } else {
      ss = sg.random_sequence(noteCount - 1);
    }

    return fb.build(ss, fb.defaultPulse);
}

inline MelodicFigure DefaultFigureStrategy::generate_shaped_figure(
    const FigureTemplate& ft, uint32_t seed) {
    FigureBuilder fb(seed);
    fb.defaultPulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
    int dir = ft.shapeDirection;
    int p1 = ft.shapeParam;
    int p2 = ft.shapeParam2;
    int count = (ft.maxNotes > ft.minNotes)
        ? Randomizer(seed + 99).int_range(ft.minNotes, ft.maxNotes)
        : (ft.minNotes > 0 ? ft.minNotes : 4);

    switch (ft.shape) {
      case FigureShape::ScalarRun:
        return fb.scalar_run(dir, count > 0 ? count : 4, fb.defaultPulse);

      case FigureShape::RepeatedNote:
        return fb.repeated_note(count > 0 ? count : 3, fb.defaultPulse);

      case FigureShape::HeldNote:
        return fb.held_note(ft.totalBeats > 0 ? ft.totalBeats : fb.defaultPulse * 2);

      case FigureShape::CadentialApproach:
        return fb.cadential_approach(dir < 0, p1 > 0 ? p1 : 3,
                                     fb.defaultPulse * 2, fb.defaultPulse);

      case FigureShape::TriadicOutline:
        return fb.triadic_outline(dir, p1 > 0, fb.defaultPulse);

      case FigureShape::NeighborTone:
        return fb.neighbor_tone(dir > 0, fb.defaultPulse);

      case FigureShape::LeapAndFill:
        return fb.leap_and_fill(p1 > 0 ? p1 : 4, dir > 0, p2, fb.defaultPulse);

      case FigureShape::ScalarReturn:
        return fb.scalar_return(dir, p1 > 0 ? p1 : 3, p2, fb.defaultPulse);

      case FigureShape::Anacrusis:
        return fb.anacrusis(count > 0 ? count : 2, dir,
                            fb.defaultPulse * 0.5f, fb.defaultPulse);

      case FigureShape::Zigzag:
        return fb.zigzag(dir, p1 > 0 ? p1 : 3, 2, 1, fb.defaultPulse);

      case FigureShape::Fanfare:
        return fb.fanfare({4, 3}, p1 > 0 ? p1 : 1, fb.defaultPulse);

      case FigureShape::Sigh:
        return fb.sigh(fb.defaultPulse);

      case FigureShape::Suspension:
        return fb.suspension(fb.defaultPulse * 2, fb.defaultPulse);

      case FigureShape::Cambiata:
        return fb.cambiata(dir, fb.defaultPulse);

      case FigureShape::Free:
      default:
        return generate_figure(ft, seed);
    }
}

inline MelodicFigure DefaultFigureStrategy::apply_transform(
    const MelodicFigure& base, TransformOp op, int param, uint32_t seed) {
    FigureBuilder fb(seed);

    switch (op) {
      case TransformOp::Invert:
        return fb.invert(base);

      case TransformOp::Reverse:
        return fb.reverse(base);

      case TransformOp::Stretch:
        return fb.stretch(base, param > 0 ? float(param) : 2.0f);

      case TransformOp::Compress:
        return fb.compress(base, param > 0 ? float(param) : 2.0f);

      case TransformOp::VaryRhythm:
        return fb.vary_rhythm(base);

      case TransformOp::VarySteps: {
        MelodicFigure copy = base;
        return fb.vary_steps(copy, std::max(1, param));
      }

      case TransformOp::NewSteps: {
        // Keep rhythm, generate new steps
        StepGenerator sg(seed);
        StepSequence newSS = sg.random_sequence(base.note_count() - 1);
        return fb.build(newSS, base.units[0].duration);
      }

      case TransformOp::NewRhythm: {
        // Keep steps, generate new rhythm — rebuild with random pulses
        MelodicFigure fig = base;
        Randomizer rr(seed);
        for (auto& u : fig.units) {
          u.duration *= (rr.decide(0.5f) ? 0.5f : 1.0f) * (rr.decide(0.3f) ? 1.5f : 1.0f);
        }
        return fig;
      }

      case TransformOp::Replicate: {
        int count = (param > 0) ? param : 2;
        Randomizer rr(seed);
        int step = rr.select_int({-2, -1, 1, 2});
        return fb.replicate(base, count, step);
      }

      case TransformOp::TransformGeneral: {
        // Composer picks a random transform
        Randomizer rr(seed);
        float choice = rr.value();
        if (choice < 0.25f) return fb.invert(base);
        if (choice < 0.50f) return fb.vary_rhythm(base);
        if (choice < 0.75f) return fb.reverse(base);
        return fb.stretch(base);
      }

      case TransformOp::None:
      default:
        return base;
    }
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

} // namespace mforce

#pragma once
#include "mforce/compose/strategy.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/piece_utils.h"
#include "mforce/compose/default_strategy_shared.h"
#include "mforce/compose/strategy_registry.h"
#include "mforce/music/figures.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/random_figure_builder.h"
#include "mforce/compose/pool_figure_builder.h"
#include "mforce/music/structure.h"
#include "mforce/compose/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include "mforce/compose/environment.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// DefaultFigureStrategy
//
// Wraps the pre-refactor ClassicalComposer::compose_figure /
// generate_figure / generate_shaped_figure / choose_shape / apply_transform
// code paths, so that a FigureTemplate routed through Composer::compose_figure
// produces byte-identical output compared to pre-refactor ClassicalComposer.
// ---------------------------------------------------------------------------
// Cached Markov atom pool, loaded once from lib/figures/markov_pool.json.
// Returns nullptr if the file is absent/unparseable, so callers fall back to
// procedural generation rather than failing.
inline PoolFigureBuilder* markov_pool_singleton() {
  static std::unique_ptr<PoolFigureBuilder> inst = []() -> std::unique_ptr<PoolFigureBuilder> {
    try {
      return std::make_unique<PoolFigureBuilder>(
          PoolFigureBuilder::load("lib/figures/markov_pool.json", 0u));
    } catch (...) {
      return nullptr;
    }
  }();
  return inst.get();
}

class DefaultFigureStrategy : public FigureStrategy {
public:
  static constexpr const char* kName = "default_figure";
  using Settings = NoSettings;

  std::string name() const override { return kName; }

  // Markov atom pool is the default source for generated figures; on a pool
  // no-match it falls back to procedural generation. Set false (or the env var
  // MFORCE_FIGURE_RANDOM) to force the procedural path always.
  inline static bool usePool = true;

  // compose_figure is DECLARED here and DEFINED at the end of this file.
  MelodicFigure compose_figure(Locus locus, const FigureTemplate& figTmpl) const override;

  // PUBLIC so realize_motifs can call generate_figure
  // directly, bypassing compose_figure's switch. The pre-refactor
  // ClassicalComposer::realize_motifs called the private generate_figure
  // directly, so preserving that call shape is required for bit-identical
  // output against the golden render.
  MelodicFigure generate_figure(const FigureTemplate& figTmpl, uint32_t seed) const;
  MelodicFigure apply_transform(const MelodicFigure& base, TransformOp op,
                                int param, uint32_t seed) const;
};

// ============================================================================
// Inline definitions — bodies lifted verbatim from classical_composer.h.
// ============================================================================

inline MelodicFigure DefaultFigureStrategy::generate_figure(
    const FigureTemplate& figTmpl, uint32_t seed) const {
    // Pool-first: draw an idiomatic Markov atom matching the template's beat/note
    // budget. On no-match (or pool unavailable, or override), fall through to the
    // procedural generator below. Contour is left unspecified for now.
    if (usePool && !environment_variable_is_set("MFORCE_FIGURE_RANDOM")) {
        if (PoolFigureBuilder* pool = markov_pool_singleton()) {
            Constraints pc;
            if (figTmpl.totalBeats > 0) {
                pc.length = figTmpl.totalBeats;
            } else {
                int lo = figTmpl.minNotes, hi = figTmpl.maxNotes;
                if (hi < lo) hi = lo;
                Randomizer pcRng(seed + 7);
                pc.count = pcRng.int_range(lo, hi);
            }
            try { return pool->build(pc, seed); }
            catch (const std::runtime_error&) { /* no pool match — procedural fallback */ }
        }
    }

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
      if (varyRng.decide(1.0f)) {  // TEMP: 1.0f forces vary_rhythm always (was 0.4f) — calibration test
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
    const MelodicFigure& base, TransformOp op, int param, uint32_t seed) const {
    return figure_transforms::apply(base, op, param, seed);
}



// ============================================================================
// Out-of-line definition of DefaultFigureStrategy::compose_figure. It looks
// up the shape strategies in the kit, which needs ComposeKit complete.
// ============================================================================

inline MelodicFigure DefaultFigureStrategy::compose_figure(
    Locus locus, const FigureTemplate& figTmpl) const {
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  // Use the figure's seed for reproducibility
  uint32_t figSeed = figTmpl.seed ? figTmpl.seed : locus.rng->rng();

  switch (figTmpl.source) {
    case FigureSource::Locked:
      if (figTmpl.lockedFigure) return *figTmpl.lockedFigure;
      {
        RandomFigureBuilder rfb(locus.rng->rng());
        Constraints fc; fc.length = 1.0f;
        return rfb.build_singleton(fc);  // fallback
      }

    case FigureSource::Literal: {
      // Convert the user-authored note list into a MelodicFigure. Each
      // LiteralNote becomes one FigureUnit whose `step` is the delta (in
      // scale degrees) from the previous note (or from the prior cursor
      // for the first note). Duration passes through.
      MelodicFigure fig;
      if (figTmpl.literalNotes.empty()) return fig;

      auto absoluteDeg = [&](const Pitch& p) {
        int d = degree_in_scale(p, scale);
        return p.octave * scale.length() + d;
      };

      const Pitch cursorPitch = ::mforce::piece_utils::pitch_before(locus);
      int prevDeg = absoluteDeg(cursorPitch);
      for (auto& ln : figTmpl.literalNotes) {
        FigureUnit u;
        u.duration = ln.duration;
        if (ln.rest) {
          u.rest = true;
          u.step = 0;
        } else {
          if (!ln.pitch) continue;
          int d = absoluteDeg(*ln.pitch);
          u.step = d - prevDeg;
          prevDeg = d;
        }
        fig.units.push_back(u);
      }
      return fig;
    }

    case FigureSource::Reference: {
      auto it = locus.plan->realized_motifs().find(figTmpl.motifName);
      if (it != locus.plan->realized_motifs().end()) return it->second;
      return generate_figure(figTmpl, figSeed);
    }

    case FigureSource::Transform: {
      auto it = locus.plan->realized_motifs().find(figTmpl.motifName);
      MelodicFigure base = (it != locus.plan->realized_motifs().end())
        ? it->second
        : generate_figure(figTmpl, figSeed);
      return apply_transform(base, figTmpl.transform, figTmpl.transformParam, figSeed);
    }

    case FigureSource::Generate:
    default: {
      MelodicFigure fig;
      if (figTmpl.shape != FigureShape::Free) {
        const char* shapeName = nullptr;
        switch (figTmpl.shape) {
          // Only shapes with real strategy logic dispatch here. Others
          // (ScalarRun, RepeatedNote, HeldNote, TriadicOutline, NeighborTone,
          // LeapAndFill, ScalarReturn, Anacrusis, Zigzag, Fanfare, Sigh,
          // Suspension, Cambiata) fall through to generate_figure — their
          // shape intent is available via shape_figures:: at call sites
          // that want it.
          case FigureShape::CadentialApproach: shapeName = "shape_cadential_approach"; break;
          case FigureShape::Skipping:          shapeName = "shape_skipping"; break;
          case FigureShape::Stepping:          shapeName = "shape_stepping"; break;
          default:                             shapeName = nullptr; break;
        }
        if (shapeName) {
          const FigureStrategy* s = locus.kit->strategies.find_figure(shapeName);
          if (s) {
            FigureTemplate shapeArg = figTmpl;
            shapeArg.seed = figSeed;
            fig = s->compose_figure(locus, shapeArg);
          }
        }
      }
      if (fig.units.empty()) {
        // Either shape was Free or the registry lookup failed — fall back.
        fig = generate_figure(figTmpl, figSeed);
      }

      // Phase 2 composition quality: proportional scaling to enforce totalBeats.
      // Applies only to Generate-path figures. Locked/Reference/Transform/Literal
      // paths take durations verbatim from the user.
      // Shapes that generate exact musical rhythms (Skipping, Stepping,
      // CadentialApproach) already sum to totalBeats — skip scaling for them
      // to preserve standard note durations.
      const bool exactRhythmShape =
          (figTmpl.shape == FigureShape::Skipping ||
           figTmpl.shape == FigureShape::Stepping ||
           figTmpl.shape == FigureShape::CadentialApproach);
      if (!exactRhythmShape && figTmpl.totalBeats > 0 && !fig.units.empty()) {
        float actual = 0;
        for (auto& u : fig.units) actual += u.duration;
        if (actual > 0 && std::abs(actual - figTmpl.totalBeats) > 0.001f) {
          float durationScale = figTmpl.totalBeats / actual;
          for (auto& u : fig.units) u.duration *= durationScale;
        }
      }
      return fig;
    }
  }
}

} // namespace mforce

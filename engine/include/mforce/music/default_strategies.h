#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/figures.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/pool_figure_builder.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <memory>

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
// Cached Markov atom pool, loaded once from lib/figures/markov_pool.json.
// Returns nullptr if the file is absent/unparseable, so callers fall back to
// procedural generation rather than failing.
inline PoolFigureBuilder* markov_pool_singleton() {
  static std::unique_ptr<PoolFigureBuilder> inst = []() -> std::unique_ptr<PoolFigureBuilder> {
    std::ifstream f("lib/figures/markov_pool.json");
    if (!f) return nullptr;
    try {
      nlohmann::json j; f >> j;
      return std::make_unique<PoolFigureBuilder>(j, 0u);
    } catch (...) {
      return nullptr;
    }
  }();
  return inst.get();
}

class DefaultFigureStrategy : public FigureStrategy {
public:
  std::string name() const override { return "default_figure"; }

  // Markov atom pool is the default source for generated figures; on a pool
  // no-match it falls back to procedural generation. Set false (or the env var
  // MFORCE_FIGURE_RANDOM) to force the procedural path always.
  inline static bool usePool = true;

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
    // Pool-first: draw an idiomatic Markov atom matching the template's beat/note
    // budget. On no-match (or pool unavailable, or override), fall through to the
    // procedural generator below. Contour is left unspecified for now.
    if (usePool && std::getenv("MFORCE_FIGURE_RANDOM") == nullptr) {
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

  // Cadential approach helpers (apply_cadence v2). Pure; unit-tested in test_figures.
  static int nearest_degree_index(int fromIdx, int targetDeg, int len);
  static std::vector<int> build_approach_steps(int headEndDeg, int targetDeg,
                                               int tailCount, int len);
  static std::vector<float> settle_tail(const std::vector<float>& tailDurs, float finalMin);
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
inline int DefaultPhraseStrategy::nearest_degree_index(int fromIdx, int targetDeg, int len) {
    int base = fromIdx - (((fromIdx % len) + len) % len);  // octave floor at/below fromIdx
    int best = base + targetDeg;
    for (int cand : {base + targetDeg - len, base + targetDeg, base + targetDeg + len}) {
        if (std::abs(cand - fromIdx) < std::abs(best - fromIdx)) best = cand;
    }
    return best;
}

inline std::vector<int> DefaultPhraseStrategy::build_approach_steps(
    int headEndDeg, int targetDeg, int tailCount, int len) {
    std::vector<int> steps(std::max(0, tailCount), 0);
    if (tailCount <= 0) return steps;

    int targetAbs = nearest_degree_index(headEndDeg, targetDeg, len);
    int delta = targetAbs - headEndDeg;

    if (delta == 0) {                       // on target: lower-neighbor escape -> resolve
        if (tailCount >= 2) {
            steps[tailCount - 2] = -1;       // down to leading-tone/neighbor
            steps[tailCount - 1] = +1;       // resolve up to target
        }                                    // tailCount==1: hold (step 0)
        return steps;
    }

    int dir  = (delta > 0) ? +1 : -1;
    int dist = std::abs(delta);
    if (dist <= tailCount) {                 // stepwise: single steps packed at the end
        for (int i = 0; i < dist; ++i) steps[tailCount - dist + i] = dir;
    } else {                                 // far: leap on first note, then step in
        int stepPortion = tailCount - 1;
        steps[0] = delta - dir * stepPortion;
        for (int i = 1; i < tailCount; ++i) steps[i] = dir;
    }
    return steps;
}

inline std::vector<float> DefaultPhraseStrategy::settle_tail(
    const std::vector<float>& tailDurs, float finalMin) {
    int n = int(tailDurs.size());
    if (n <= 1) return tailDurs;
    float total = 0; for (float d : tailDurs) total += d;

    int A = n;
    for (; A > 1; --A) {
        float lead = 0; for (int i = 0; i < A - 1; ++i) lead += tailDurs[i];
        if (total - lead >= finalMin) break;   // final note long enough with A notes
    }
    std::vector<float> out;
    float lead = 0;
    for (int i = 0; i < A - 1; ++i) { out.push_back(tailDurs[i]); lead += tailDurs[i]; }
    out.push_back(total - lead);                // final absorbs the elided remainder
    return out;
}

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

    int len = scale.length();
    int target = ((tmpl.cadenceTarget % len) + len) % len;

    // Degree at the END of the preserved head: start + all prior figures + this
    // figure's connector + this figure's head steps.
    int headEndDeg = degree_in_scale(phrase.startingPitch, scale);
    for (int f = 0; f < lastIdx; ++f) {
        if (f < int(phrase.connectors.size())) headEndDeg += phrase.connectors[f].leadStep;
        headEndDeg += phrase.figures[f]->net_step();
    }
    if (lastIdx < int(phrase.connectors.size())) headEndDeg += phrase.connectors[lastIdx].leadStep;

    auto& cf = *phrase.figures[lastIdx];
    int N = cf.note_count();
    if (N <= 0) return;
    int head = std::max(0, N - 3);
    for (int i = 0; i < head; ++i) headEndDeg += cf.units[i].step;

    // Original tail durations (total preserved so the figure keeps its beat budget).
    std::vector<float> tailDurs;
    for (int i = head; i < N; ++i) tailDurs.push_back(cf.units[i].duration);

    std::vector<float> newDurs = settle_tail(tailDurs, 1.0f);   // finalMin = 1.0 beat
    int A = int(newDurs.size());
    std::vector<int> appSteps = build_approach_steps(headEndDeg, target, A, len);

    cf.units.resize(head);                       // keep the head, drop the old tail
    for (int i = 0; i < A; ++i) {
        FigureUnit u;
        u.duration = newDurs[i];
        u.step = appSteps[i];
        cf.units.push_back(u);
    }
}

// -- compose_phrase: DECLARED here, DEFINED in composer.h below Composer -----
// Body calls ctx.composer->compose_figure(...); Composer is forward-declared
// only in this file, so the definition must live after the full Composer class.

} // namespace mforce

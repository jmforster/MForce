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
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>

namespace mforce {

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
  static constexpr const char* kName = "default_phrase";
  using Settings = NoSettings;

  std::string name() const override { return kName; }

  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) const override;

  // Cadential approach helpers (apply_cadence v2). Pure; unit-tested in test_figures.
  static int nearest_degree_index(int fromIdx, int targetDeg, int len);
  static std::vector<int> build_approach_steps(int headEndDeg, int targetDeg,
                                               int tailCount, int len);
  static std::vector<float> settle_tail(const std::vector<float>& tailDurs, float finalMin);
  // Rewrites the tail of ONE figure into an approach + settled arrival on
  // `targetDeg`. `degAtFigureStart` is the scale degree reached immediately
  // before this figure's first unit. Split out of apply_cadence so callers
  // that cannot use its degree accounting can supply the right starting
  // degree themselves: apply_cadence sums net_step() over the preceding
  // figures, which is only valid when every figure steps in SCALE degrees.
  // A chord-driven phrase (phrase_aware_figure) mixes ChordFigures, whose
  // steps are chord-tone indices, so it tracks the true pitch itself.
  static void rebuild_cadential_tail(Figure& cf, int degAtFigureStart,
                                     int targetDeg, int len, float finalMin);
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

    rebuild_cadential_tail(*phrase.figures[lastIdx], headEndDeg, target, len, 1.0f);
}

inline void DefaultPhraseStrategy::rebuild_cadential_tail(
    Figure& cf, int degAtFigureStart, int targetDeg, int len, float finalMin) {
    int N = cf.note_count();
    if (N <= 0) return;
    int headEndDeg = degAtFigureStart;
    int head = std::max(0, N - 3);
    for (int i = 0; i < head; ++i) headEndDeg += cf.units[i].step;

    // Original tail durations (total preserved so the figure keeps its beat budget).
    std::vector<float> tailDurs;
    for (int i = head; i < N; ++i) tailDurs.push_back(cf.units[i].duration);

    std::vector<float> newDurs = settle_tail(tailDurs, finalMin);
    int A = int(newDurs.size());
    std::vector<int> appSteps = build_approach_steps(headEndDeg, targetDeg, A, len);

    cf.units.resize(head);                       // keep the head, drop the old tail
    for (int i = 0; i < A; ++i) {
        FigureUnit u;
        u.duration = newDurs[i];
        u.step = appSteps[i];
        cf.units.push_back(u);
    }
}


// ============================================================================
// Out-of-line definition of DefaultPhraseStrategy::compose_phrase. It looks
// up the default figure strategy in the kit, which needs ComposeKit complete.
// ============================================================================

inline Phrase DefaultPhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) const {
  (void)settings_of<Settings>(phraseTmpl.strategy);  // refuses settings of another type
  Phrase phrase;

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  // Intra-phrase running cursor. Starts at the phrase's starting pitch
  // and advances through each figure's net_step() as figures realize.
  // Figure strategies will construct their own cursor via pitch_before(),
  // but since the phrase isn't published yet we pass a synthetic figure
  // template startingPitch for Literal path consumers.
  PitchReader runningReader(scale);
  runningReader.set_pitch(phrase.startingPitch);

  const int numFigs = int(phraseTmpl.figures.size());
  for (int i = 0; i < numFigs; ++i) {
    // Apply optional connector JOINING figures[i-1] → figures[i] before
    // generating this figure. Mutates the previously-added figure in
    // phrase.figures (elides trailing units and/or adjusts last unit's
    // duration). Does NOT modify the cursor for adjustment (adjustment is
    // a duration-only change). Elision does affect the cursor since it
    // removes step contributions — we rewind the runningReader accordingly.
    if (i > 0 && i < int(phraseTmpl.connectors.size())
        && phraseTmpl.connectors[i] && !phrase.figures.empty()) {
      const auto& conn = *phraseTmpl.connectors[i];
      auto& prevFig = *phrase.figures.back();
      if (conn.elideCount > 0 && !prevFig.units.empty()) {
        int elide = std::min(conn.elideCount, int(prevFig.units.size()));
        // Rewind cursor to reflect elided step contributions
        int netElidedSteps = 0;
        for (int e = 0; e < elide; ++e) {
          netElidedSteps += prevFig.units[prevFig.units.size() - 1 - e].step;
        }
        runningReader.step(-netElidedSteps);
        prevFig.units.resize(prevFig.units.size() - elide);
      }
      if (conn.adjustCount != 0 && !prevFig.units.empty()) {
        float newDur = prevFig.units.back().duration + conn.adjustCount;
        if (newDur < 0.0f) newDur = 0.0f;  // clamp rather than error
        prevFig.units.back().duration = newDur;
      }
    }

    // MelodicFunction-driven shape selection.
    FigureTemplate figTmpl = phraseTmpl.figures[i];
    if (phraseTmpl.function != MelodicFunction::Free
        && figTmpl.source == FigureSource::Generate
        && figTmpl.shape == FigureShape::Free) {
      figTmpl.shape = choose_shape(
          phraseTmpl.function, i, numFigs, locus.rng->rng(),
          phraseTmpl.cadentialArrival == "approach");
    }

    // Dispatch to the figure level. DefaultFigureStrategy's Literal path
    // reads the cursor via pitch_before(locus), which walks the phrase/
    // passage structure so far. The phrase being built is not in the piece
    // yet, so pitch_before would see an empty passage and fall back to the
    // passage template's startingPitch, which is wrong mid-phrase. A Literal
    // figure is therefore realized here against runningReader, the cursor
    // advanced by the figures before it. Other sources do not read the
    // cursor and go to the default figure strategy.
    Locus figLocus = locus.with_figure(i);
    const FigureStrategy* fs = &locus.kit->strategies.figure("default_figure");
    MelodicFigure fig;
    if (figTmpl.source == FigureSource::Literal) {
      // Inline cursor-aware Literal realization using the runningReader.
      const Pitch cursorPitch = runningReader.get_pitch();
      auto absoluteDeg = [&](const Pitch& p) {
        int d = degree_in_scale(p, scale);
        return p.octave * scale.length() + d;
      };
      int prevDeg = absoluteDeg(cursorPitch);
      for (auto& ln : figTmpl.literalNotes) {
        FigureUnit u;
        u.duration = ln.duration;
        if (ln.rest) { u.rest = true; u.step = 0; }
        else {
          if (!ln.pitch) continue;
          int d = absoluteDeg(*ln.pitch);
          u.step = d - prevDeg;
          prevDeg = d;
        }
        fig.units.push_back(u);
      }
    } else {
      fig = fs->compose_figure(figLocus, figTmpl);
    }

    // Pull this figure's connector from the template (if any). Default-
    // constructed FCs have leadStep=0/elide=0/adjust=0. Push to
    // phrase.connectors alongside the figure to keep the
    // figures.size() == connectors.size() invariant. The cursor advance
    // from leadStep is applied at realize time (see
    // realize_events), NOT here. Figures are not mutated.
    FigureConnector fc{};
    if (i < int(phraseTmpl.connectors.size()) && phraseTmpl.connectors[i]) {
      fc = *phraseTmpl.connectors[i];
    }

    // Advance running cursor by the FC's leadStep (used by the Literal-
    // figure path's pitch_before queries) and then by the figure's net
    // step. Under the new model, fig.units[0].step is 0 by convention, so
    // fig.net_step() no longer absorbs leadStep — we add it explicitly.
    runningReader.step(fc.leadStep);
    runningReader.step(fig.net_step());

    phrase.add_melodic_figure_with_connector(std::move(fig), fc);
  }

  // Cadence adjustment — skip for Literal and Locked source on the last
  // figure template. When the user provides exact notes (Literal) or an
  // explicit locked figure, re-pitching the last unit would clobber
  // the authored pitch. Reference and Transform continue to allow
  // cadence adjustment — motif intent doesn't dictate the closing pitch.
  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty() && !phraseTmpl.figures.empty()) {
    const FigureSource lastSource = phraseTmpl.figures.back().source;
    if (lastSource != FigureSource::Literal && lastSource != FigureSource::Locked) {
      apply_cadence(phrase, phraseTmpl, scale);
    }
  }

  return phrase;
}

} // namespace mforce

#pragma once
#include "mforce/compose/strategy.h"
#include "mforce/compose/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/compose/default_figure_strategy.h"
#include "mforce/compose/default_phrase_strategy.h"
#include "mforce/compose/piece_utils.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace mforce {

// ===========================================================================
// PeriodPhraseSettings — classical period (antecedent + consequent)
// ===========================================================================

struct PeriodPhraseSettings {
  MelodicFigure basicIdea;           // opens both sub-phrases (parallel)
  MelodicFigure antecedentTail;      // closes antecedent on halfCadenceTarget
  MelodicFigure consequentTail;      // closes consequent on authentic cadence
  int halfCadenceTarget{4};          // 0-indexed scale degree for half cadence
};

// ---------------------------------------------------------------------------
// PeriodPhraseStrategy — classical period (antecedent + consequent)
// ---------------------------------------------------------------------------
class PeriodPhraseStrategy : public PhraseStrategy {
public:
  static constexpr const char* kName = "period_phrase";
  using Settings = PeriodPhraseSettings;

  std::string name() const override { return kName; }
  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) const override;
};

// ============================================================================
// Inline definitions
// ============================================================================

inline Phrase PeriodPhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) const {
  Phrase phrase;

  if (!phraseTmpl.strategy.settings.has_value()) {
    throw std::runtime_error("period_phrase: given no settings");
  }
  const Settings cfg = settings_of<Settings>(phraseTmpl.strategy);

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  phrase.add_melodic_figure(cfg.basicIdea);
  phrase.add_melodic_figure(cfg.antecedentTail);

  // Half-cadence adjustment on the antecedent (figures 0 and 1)
  {
    int startDeg = degree_in_scale(phrase.startingPitch, scale);
    int len = scale.length();
    int netSteps = 0;
    for (int f = 0; f < 2; ++f) {
      netSteps += phrase.figures[f]->net_step();
    }
    int landingDeg = ((startDeg + netSteps) % len + len) % len;
    int target = cfg.halfCadenceTarget % len;
    if (landingDeg != target) {
      int diff = target - landingDeg;
      if (diff > len / 2) diff -= len;
      if (diff < -len / 2) diff += len;
      auto& lastFig = *phrase.figures.back();
      if (!lastFig.units.empty()) {
        lastFig.units.back().step += diff;
      }
    }
  }

  phrase.add_melodic_figure(cfg.basicIdea);
  phrase.add_melodic_figure(cfg.consequentTail);

  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    DefaultPhraseStrategy::apply_cadence(phrase, phraseTmpl, scale);
  }

  return phrase;
}

} // namespace mforce

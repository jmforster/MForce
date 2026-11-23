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
// SentencePhraseSettings — classical sentence (basic idea + repeat + continuation)
// ===========================================================================

struct SentencePhraseSettings {
  MelodicFigure basicIdea;
  int variationTransposition{0};     // scale-degree offset for the repetition
  MelodicFigure continuation;
};

// ---------------------------------------------------------------------------
// SentencePhraseStrategy — classical sentence (basic idea + repeat + continuation)
// ---------------------------------------------------------------------------
class SentencePhraseStrategy : public PhraseStrategy {
public:
  static constexpr const char* kName = "sentence_phrase";
  using Settings = SentencePhraseSettings;

  std::string name() const override { return kName; }
  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) const override;
};

// ============================================================================
// Inline definitions
// ============================================================================

inline Phrase SentencePhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) const {
  Phrase phrase;

  if (!phraseTmpl.strategy.settings.has_value()) {
    throw std::runtime_error("sentence_phrase: given no settings");
  }
  const Settings cfg = settings_of<Settings>(phraseTmpl.strategy);

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  phrase.add_melodic_figure(cfg.basicIdea);

  MelodicFigure repetition = cfg.basicIdea;
  if (!repetition.units.empty()) {
    repetition.units[0].step += cfg.variationTransposition;
  }
  phrase.add_melodic_figure(std::move(repetition));

  phrase.add_melodic_figure(cfg.continuation);

  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    DefaultPhraseStrategy::apply_cadence(phrase, phraseTmpl, scale);
  }

  return phrase;
}

} // namespace mforce

#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/piece_utils.h"
#include <cmath>
#include <iostream>

namespace mforce {

// ---------------------------------------------------------------------------
// PeriodPhraseStrategy — classical period (antecedent + consequent)
// ---------------------------------------------------------------------------
class PeriodPhraseStrategy : public PhraseStrategy {
public:
  std::string name() const override { return "period_phrase"; }
  Phrase realize_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

// ---------------------------------------------------------------------------
// SentencePhraseStrategy — classical sentence (basic idea + repeat + continuation)
// ---------------------------------------------------------------------------
class SentencePhraseStrategy : public PhraseStrategy {
public:
  std::string name() const override { return "sentence_phrase"; }
  Phrase realize_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

// ============================================================================
// Inline definitions
// ============================================================================

inline Phrase PeriodPhraseStrategy::realize_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  if (!phraseTmpl.periodConfig) {
    std::cerr << "PeriodPhraseStrategy: phraseTmpl.periodConfig is empty; returning empty phrase\n";
    return phrase;
  }
  const PeriodPhraseConfig& cfg = *phraseTmpl.periodConfig;

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
    int startDeg = DefaultPhraseStrategy::degree_in_scale(phrase.startingPitch, scale);
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

inline Phrase SentencePhraseStrategy::realize_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  if (!phraseTmpl.sentenceConfig) {
    std::cerr << "SentencePhraseStrategy: phraseTmpl.sentenceConfig is empty; returning empty phrase\n";
    return phrase;
  }
  const SentencePhraseConfig& cfg = *phraseTmpl.sentenceConfig;

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

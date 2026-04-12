#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/default_strategies.h"
#include <cmath>
#include <iostream>

namespace mforce {

struct Composer;  // forward declaration

// ---------------------------------------------------------------------------
// PeriodPhraseStrategy — classical period (antecedent + consequent)
// ---------------------------------------------------------------------------
class PeriodPhraseStrategy : public Strategy {
public:
  std::string name() const override { return "period_phrase"; }
  StrategyLevel level() const override { return StrategyLevel::Phrase; }
  Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                        StrategyContext& ctx) override;
};

// ---------------------------------------------------------------------------
// SentencePhraseStrategy — classical sentence (basic idea + repeat + continuation)
// ---------------------------------------------------------------------------
class SentencePhraseStrategy : public Strategy {
public:
  std::string name() const override { return "sentence_phrase"; }
  StrategyLevel level() const override { return StrategyLevel::Phrase; }
  Phrase realize_phrase(const PhraseTemplate& phraseTmpl,
                        StrategyContext& ctx) override;
};

// ============================================================================
// Inline definitions
// ============================================================================

inline Phrase PeriodPhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  if (!phraseTmpl.periodConfig) {
    std::cerr << "PeriodPhraseStrategy: phraseTmpl.periodConfig is empty; returning empty phrase\n";
    return phrase;
  }
  const PeriodPhraseConfig& cfg = *phraseTmpl.periodConfig;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.cursor;
  }

  // Figure 0: basicIdea (antecedent opening)
  phrase.add_melodic_figure(cfg.basicIdea);

  // Figure 1: antecedentTail
  phrase.add_melodic_figure(cfg.antecedentTail);

  // Half-cadence adjustment on the antecedent (figures 0 and 1)
  {
    int startDeg = DefaultPhraseStrategy::degree_in_scale(phrase.startingPitch, ctx.scale);
    int len = ctx.scale.length();
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
      auto& lastFig = *phrase.figures.back();  // antecedentTail
      if (!lastFig.units.empty()) {
        lastFig.units.back().step += diff;
      }
    }
  }

  // Figure 2: basicIdea (consequent opening, parallel)
  phrase.add_melodic_figure(cfg.basicIdea);

  // Figure 3: consequentTail
  phrase.add_melodic_figure(cfg.consequentTail);

  // Authentic cadence across the whole phrase
  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    DefaultPhraseStrategy::apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}

inline Phrase SentencePhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  if (!phraseTmpl.sentenceConfig) {
    std::cerr << "SentencePhraseStrategy: phraseTmpl.sentenceConfig is empty; returning empty phrase\n";
    return phrase;
  }
  const SentencePhraseConfig& cfg = *phraseTmpl.sentenceConfig;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.cursor;
  }

  // Figure 0: basicIdea
  phrase.add_melodic_figure(cfg.basicIdea);

  // Figure 1: basicIdea transposed via first-unit step offset
  MelodicFigure repetition = cfg.basicIdea;
  if (!repetition.units.empty()) {
    repetition.units[0].step += cfg.variationTransposition;
  }
  phrase.add_melodic_figure(std::move(repetition));

  // Figure 2: continuation
  phrase.add_melodic_figure(cfg.continuation);

  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    DefaultPhraseStrategy::apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}

} // namespace mforce

#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/figure_transforms.h"
#include <iostream>

namespace mforce {

// ---------------------------------------------------------------------------
// TwoFigurePhraseStrategy — builds figure 1 via RFB, derives figure 2 via
// figure_transforms::apply, returns a two-figure Phrase. Ignores connectors,
// cadence, and MelodicFunction. Expects a TwoFigurePhraseConfig on the
// PhraseTemplate.
// ---------------------------------------------------------------------------
class TwoFigurePhraseStrategy : public PhraseStrategy {
public:
  std::string name() const override { return "two_figure_phrase"; }
  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

inline Phrase TwoFigurePhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  if (!phraseTmpl.twoFigureConfig) {
    std::cerr << "TwoFigurePhraseStrategy: phraseTmpl.twoFigureConfig is empty; "
                 "returning empty phrase\n";
    return phrase;
  }
  if (!phraseTmpl.figures.empty()) {
    std::cerr << "TwoFigurePhraseStrategy: ignoring phraseTmpl.figures ("
              << phraseTmpl.figures.size()
              << " entries) — strategy uses twoFigureConfig\n";
  }

  const TwoFigurePhraseConfig& cfg = *phraseTmpl.twoFigureConfig;

  uint32_t seed = cfg.seed != 0 ? cfg.seed
                : phraseTmpl.seed != 0 ? phraseTmpl.seed
                : 0xF1F1F1F1u;
  RandomFigureBuilder rfb(seed);

  MelodicFigure fig1;
  switch (cfg.method) {
    case TwoFigurePhraseConfig::Method::ByCount:
      fig1 = rfb.build_by_count(cfg.count, cfg.constraints);
      break;
    case TwoFigurePhraseConfig::Method::ByLength:
      fig1 = rfb.build_by_length(cfg.length, cfg.constraints);
      break;
    case TwoFigurePhraseConfig::Method::Singleton:
      fig1 = rfb.build_singleton(cfg.constraints);
      break;
  }

  MelodicFigure fig2 = figure_transforms::apply(
      fig1, cfg.transform, cfg.transformParam, seed + 1);

  phrase.add_melodic_figure(std::move(fig1));
  phrase.add_melodic_figure(std::move(fig2));
  return phrase;
}

} // namespace mforce

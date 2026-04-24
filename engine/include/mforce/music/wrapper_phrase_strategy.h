#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include <iostream>

namespace mforce {

// ---------------------------------------------------------------------------
// WrapperPhraseStrategy — minimum-viable phrase strategy.
//
// Realizes `phraseTmpl.figures[0]` into a single-figure Phrase by dispatching
// through the registered `default_figure` strategy. Ignores connectors,
// cadence, and MelodicFunction. Intended as the strategy-driven entry point
// for figure-level testing (see step 2 of ComposerRefactor3).
// ---------------------------------------------------------------------------
class WrapperPhraseStrategy : public PhraseStrategy {
public:
  std::string name() const override { return "wrapper_phrase"; }
  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

inline Phrase WrapperPhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  if (phraseTmpl.figures.empty()) {
    std::cerr << "WrapperPhraseStrategy: no figures in template\n";
    return phrase;
  }
  if (phraseTmpl.figures.size() > 1) {
    std::cerr << "WrapperPhraseStrategy: ignoring figures beyond index 0 ("
              << phraseTmpl.figures.size() << " provided)\n";
  }

  FigureStrategy* fs = StrategyRegistry::instance().resolve_figure("default_figure");
  if (!fs) {
    std::cerr << "WrapperPhraseStrategy: default_figure strategy not registered\n";
    return phrase;
  }

  Locus figLocus = locus.with_figure(0);
  MelodicFigure fig = fs->compose_figure(figLocus, phraseTmpl.figures[0]);
  phrase.add_melodic_figure(std::move(fig));

  return phrase;
}

} // namespace mforce

#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/locus.h"
#include <string>

namespace mforce {

// Three typed peer bases. A strategy implements exactly one level.
// Registration and dispatch happen per-level via typed entries in
// StrategyRegistry. The Locus passed in names the strategy's position
// in the piece and carries refs to Piece + PieceTemplate (+ Composer
// for motif-pool access; that last field goes away once motifs move
// to PieceTemplate).

class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;
  virtual MelodicFigure realize_figure(Locus, const FigureTemplate&) = 0;
};

class PhraseStrategy {
public:
  virtual ~PhraseStrategy() = default;
  virtual std::string name() const = 0;
  virtual Phrase realize_phrase(Locus, const PhraseTemplate&) = 0;
};

class PassageStrategy {
public:
  virtual ~PassageStrategy() = default;
  virtual std::string name() const = 0;
  virtual Passage realize_passage(Locus, const PassageTemplate&) = 0;
};

} // namespace mforce

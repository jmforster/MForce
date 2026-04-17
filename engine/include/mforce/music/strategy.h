#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/locus.h"
#include <string>

namespace mforce {

// Three typed peer bases. A strategy implements exactly one level.
// Two-phase interface: plan_* transforms a seed template into a
// self-contained template (may mutate pieceTemplate's motif pool);
// compose_* realizes a self-contained template to concrete content
// (read-only on pool by contract).
//
// Default plan_* returns seed unchanged. Strategies with planning work
// (synthesizing motifs, resolving variants, filling unspecified fields)
// override it.

class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;

  virtual FigureTemplate plan_figure(Locus /*locus*/, FigureTemplate seed) {
    return seed;
  }
  virtual MelodicFigure compose_figure(Locus, const FigureTemplate&) = 0;
};

class PhraseStrategy {
public:
  virtual ~PhraseStrategy() = default;
  virtual std::string name() const = 0;

  virtual PhraseTemplate plan_phrase(Locus /*locus*/, PhraseTemplate seed) {
    return seed;
  }
  virtual Phrase compose_phrase(Locus, const PhraseTemplate&) = 0;
};

enum class StrategyScope { Melody, MelodyAndHarmony };

class PassageStrategy {
public:
  virtual ~PassageStrategy() = default;
  virtual std::string name() const = 0;
  virtual StrategyScope scope() const { return StrategyScope::Melody; }

  virtual PassageTemplate plan_passage(Locus /*locus*/, PassageTemplate seed) {
    return seed;
  }
  virtual Passage compose_passage(Locus, const PassageTemplate&) = 0;
};

} // namespace mforce

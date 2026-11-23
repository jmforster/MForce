#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/locus.h"
#include <string>

namespace mforce {

// Three typed peer bases. A strategy implements exactly one level.
//
// A passage strategy works in two steps: plan_passage turns the passage
// template into a self-contained one (it may add motifs to the plan's motif
// pool), and compose_passage realizes that template into a Passage. The
// default plan_passage returns its seed unchanged.

class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;
  virtual MelodicFigure compose_figure(Locus, const FigureTemplate&) = 0;
};

class PhraseStrategy {
public:
  virtual ~PhraseStrategy() = default;
  virtual std::string name() const = 0;
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

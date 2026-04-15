#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <string>

namespace mforce {

struct Composer;          // fwd

// Shared data bundle passed to every strategy call. Strategies are stateless
// singletons living in the registry; all per-call state lives here.
struct StrategyContext {
  Scale scale;
  Pitch cursor;                       // current PitchReader-equivalent cursor: passage sets it,
                                    // phrases may override, figures advance it via their step sequence.
  Composer* composer{nullptr};        // for dispatching to sub-levels
  Randomizer* rng{nullptr};           // shared RNG for this composition (migrating to mforce::rng singleton)

  // Harmony context
  const ChordProgression* chordProgression{nullptr};
};

// Three typed peer bases. A strategy implements exactly one level.
// Registration and dispatch happen per-level via typed entries in
// StrategyRegistry.

class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;
  virtual MelodicFigure realize_figure(const FigureTemplate&, StrategyContext&) = 0;
};

class PhraseStrategy {
public:
  virtual ~PhraseStrategy() = default;
  virtual std::string name() const = 0;
  virtual Phrase realize_phrase(const PhraseTemplate&, StrategyContext&) = 0;
};

class PassageStrategy {
public:
  virtual ~PassageStrategy() = default;
  virtual std::string name() const = 0;
  virtual Passage realize_passage(const PassageTemplate&, StrategyContext&) = 0;
};

} // namespace mforce

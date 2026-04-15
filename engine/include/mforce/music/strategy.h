#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <string>
#include <stdexcept>

namespace mforce {

struct Composer;          // fwd

enum class StrategyLevel { Figure, Phrase, Passage, Piece };

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

// Abstract base. Subclasses override exactly one of the realize_* methods,
// matching their level(). Calling a level that isn't overridden throws —
// that's a programming error (you asked a Figure strategy to realize a Phrase).
class Strategy {
public:
  virtual ~Strategy() = default;
  virtual std::string name() const = 0;
  virtual StrategyLevel level() const = 0;

  virtual MelodicFigure realize_figure(const FigureTemplate&, StrategyContext&) {
    throw std::logic_error("realize_figure not implemented for strategy: " + name());
  }
  virtual Phrase realize_phrase(const PhraseTemplate&, StrategyContext&) {
    throw std::logic_error("realize_phrase not implemented for strategy: " + name());
  }
  virtual Passage realize_passage(const PassageTemplate&, StrategyContext&) {
    throw std::logic_error("realize_passage not implemented for strategy: " + name());
  }
};

} // namespace mforce

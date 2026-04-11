#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <nlohmann/json.hpp>
#include <string>
#include <stdexcept>

namespace mforce {

struct PieceTemplate;     // fwd
struct Composer;          // fwd

enum class StrategyLevel { Figure, Phrase, Passage, Piece };

// Shared data bundle passed to every strategy call. Strategies are stateless
// singletons living in the registry; all per-call state lives here.
struct StrategyContext {
  Scale scale;
  Pitch startingPitch;                // initial pitch for the current unit of work
  float totalBeats{0.0f};             // target length of current unit (0 = unconstrained)
  Piece* piece{nullptr};              // in-progress piece (for seed/phrase lookup)
  const PieceTemplate* template_{nullptr};
  Composer* composer{nullptr};        // for dispatching to sub-levels
  nlohmann::json params;              // strategy-specific params from the template
  Randomizer* rng{nullptr};           // shared RNG for this composition
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

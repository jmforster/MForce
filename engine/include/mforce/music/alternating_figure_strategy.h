#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include <memory>
#include <stdexcept>

namespace mforce {

// ---------------------------------------------------------------------------
// AlternatingFigureStrategy (AFS) — Passage-level strategy.
//
// Reads the Section's ChordProgression from the Locus, takes two figure
// templates from the first PhraseTemplate of the PassageTemplate:
//   figures[0] = A template (chord-tone, even bars / ci == 0, 2, 4, ...)
//   figures[1] = B template (scalar, odd bars / ci == 1, 3, 5, ...)
//
// Alternates them one-per-chord: A over chord 0, B over chord 1, A over
// chord 2, B over chord 3, etc.  All figures are collected into a single
// Phrase.  A-figures are wrapped as ChordFigure (for chord-tone stepping);
// B-figures remain MelodicFigure (for scale-step movement).
//
// NOTE: compose_passage body is defined out-of-line in composer.h, after
// Composer is fully defined (same pattern as DefaultPassageStrategy).
// ---------------------------------------------------------------------------
class AlternatingFigureStrategy : public PassageStrategy {
public:
  std::string name() const override { return "alternating_figure"; }

  Passage compose_passage(Locus locus, const PassageTemplate& passTmpl) override;
};

} // namespace mforce

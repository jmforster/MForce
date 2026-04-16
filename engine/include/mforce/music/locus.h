#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

namespace mforce {

// Locus — a structural coordinate. Carries references to the Piece being
// built and the PieceTemplate driving the build, plus the index path to
// the current (Section, Part, Passage, Phrase, Figure) position.
//
// pieceTemplate is non-const: the plan phase (Plan B) may mutate the
// motif pool. Compose phase is read-only by contract, not by type.
struct Locus {
  const Piece* piece;
  PieceTemplate* pieceTemplate;
  int sectionIdx;
  int partIdx;
  int passageIdx{-1};
  int phraseIdx{-1};
  int figureIdx{-1};

  Locus with_passage(int p) const { Locus l = *this; l.passageIdx = p; l.phraseIdx = -1; l.figureIdx = -1; return l; }
  Locus with_phrase (int p) const { Locus l = *this; l.phraseIdx  = p; l.figureIdx = -1; return l; }
  Locus with_figure (int f) const { Locus l = *this; l.figureIdx  = f; return l; }
};

} // namespace mforce

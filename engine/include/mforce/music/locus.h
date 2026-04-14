#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

namespace mforce {

struct Locus {
  Piece& piece;
  PieceTemplate& pieceTemplate;
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

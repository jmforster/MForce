#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

namespace mforce {

struct Composer;  // fwd — Locus holds a Composer* for motif-pool access during
                  // the refactor. Once the motif pool moves to PieceTemplate
                  // (cleanup task), this field goes away.

struct Locus {
  const Piece* piece;
  const PieceTemplate* pieceTemplate;
  Composer* composer;           // TEMP: motif pool is still on Composer
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

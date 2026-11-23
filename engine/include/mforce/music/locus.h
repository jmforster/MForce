#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

namespace mforce {

// Locus — a structural coordinate. Carries the Piece being built, the plan
// (the working copy of the request that the strategies write to), the kit
// of registered strategies and voicings, and the random source every
// strategy draws from, plus the index path to the current (Section, Part,
// Passage, Phrase, Figure) position.
struct HarmonyTimeline;
struct ComposeKit;
struct Randomizer;

struct Locus {
  Piece* piece;
  PieceTemplate* plan;
  const ComposeKit* kit;
  Randomizer* rng;
  int sectionIdx;
  int partIdx;
  int passageIdx{-1};
  int phraseIdx{-1};
  int figureIdx{-1};
  const HarmonyTimeline* harmony{nullptr};

  Locus with_passage(int p) const { Locus l = *this; l.passageIdx = p; l.phraseIdx = -1; l.figureIdx = -1; return l; }
  Locus with_phrase (int p) const { Locus l = *this; l.phraseIdx  = p; l.figureIdx = -1; return l; }
  Locus with_figure (int f) const { Locus l = *this; l.figureIdx  = f; return l; }
};

} // namespace mforce

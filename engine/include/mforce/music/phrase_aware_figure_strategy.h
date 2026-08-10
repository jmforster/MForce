#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include <memory>
#include <stdexcept>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// PhraseAwareFigureStrategy — Passage-level strategy. Comp backlog #7.
//
// The chord-driven counterpart to AlternatingFigureStrategy that respects
// PHRASE BOUNDARIES. AFS alternates A/B strictly per chord across the whole
// progression and emits ONE Phrase, so a cadence lands on every other chord
// and pt.phrases[1..] are unreachable. Real phrases cadence only at their
// boundaries: K467's opening is 2+2+4+4 bars, which wants A-B / A-B /
// A-A-A-B / A-A-A-B, not A-B-A-B-A-B-...
//
// This strategy partitions the progression into one contiguous chord span per
// PhraseTemplate (by beat budget where the template states one, else evenly),
// alternates A/B on the position WITHIN the phrase, and gives the last chord
// of each phrase a melodic cadence figure driven by that phrase's own
// cadenceType. DefaultPhraseStrategy::apply_cadence then runs per phrase.
//
// See docs/superpowers/specs/2026-08-10-phrase-aware-cadence-design.md.
//
// NOTE: compose_passage body is defined out-of-line in composer.h, after
// Composer is fully defined (same pattern as AlternatingFigureStrategy).
// ---------------------------------------------------------------------------
class PhraseAwareFigureStrategy : public PassageStrategy {
public:
  std::string name() const override { return "phrase_aware_figure"; }

  Passage compose_passage(Locus locus, const PassageTemplate& passTmpl) override;

  // Partition `chordCount` chords into one contiguous span per phrase.
  // Returns span boundaries: out[p] = first chord index of phrase p, with a
  // trailing entry equal to the number of chords actually consumed. The
  // returned vector therefore has (phrasesUsed + 1) entries, and phrasesUsed
  // may be < budgets.size() when there are fewer chords than phrases.
  //
  // `budgets[p]` is the phrase's beat budget, or <= 0 when unknown. If ANY
  // budget is unknown the whole partition falls back to an even split, so
  // the result never mixes two conventions.
  //
  // Pure and header-visible so it can be unit-tested without a Piece.
  static std::vector<int> partition_chords(const std::vector<float>& budgets,
                                           const std::vector<float>& chordBeats);

  // Default cadence target degree for a cadence type when the template
  // leaves cadenceTarget at -1. 1 = half cadence -> degree 4 (V),
  // anything else -> degree 0 (I). Same mapping compose_figure already uses
  // for figureCadenceType (composer.h).
  static int default_cadence_target(int cadenceType) {
    return (cadenceType == 1) ? 4 : 0;
  }
};

// -- partition_chords -------------------------------------------------------
inline std::vector<int> PhraseAwareFigureStrategy::partition_chords(
    const std::vector<float>& budgets, const std::vector<float>& chordBeats) {

  const int nPhrases = int(budgets.size());
  const int nChords  = int(chordBeats.size());
  std::vector<int> bounds;
  if (nPhrases <= 0 || nChords <= 0) return bounds;

  // Fewer chords than phrases: one chord each until they run out. The
  // surplus phrases are dropped rather than emitted empty.
  if (nChords <= nPhrases) {
    for (int i = 0; i <= nChords; ++i) bounds.push_back(i);
    return bounds;
  }

  bool allBudgeted = true;
  for (float b : budgets) if (!(b > 0.0f)) { allBudgeted = false; break; }

  if (allBudgeted) {
    bounds.push_back(0);
    int ci = 0;
    for (int p = 0; p < nPhrases - 1; ++p) {
      // Leave at least one chord for each remaining phrase.
      const int maxEnd = nChords - (nPhrases - 1 - p);
      float accum = 0.0f;
      int end = ci;
      while (end < maxEnd && accum < budgets[p]) {
        accum += chordBeats[end];
        ++end;
      }
      if (end <= ci) end = ci + 1;        // never an empty span
      bounds.push_back(end);
      ci = end;
    }
    bounds.push_back(nChords);            // last phrase absorbs the remainder
    return bounds;
  }

  // Even split.
  for (int p = 0; p <= nPhrases; ++p) {
    bounds.push_back(int((int64_t)nChords * p / nPhrases));
  }
  // Repair any empty span the integer split produced (can't happen once
  // nChords > nPhrases, but the invariant is cheap to hold explicitly).
  for (int p = 1; p < int(bounds.size()); ++p) {
    if (bounds[p] <= bounds[p - 1]) bounds[p] = bounds[p - 1] + 1;
  }
  bounds.back() = nChords;
  return bounds;
}

} // namespace mforce

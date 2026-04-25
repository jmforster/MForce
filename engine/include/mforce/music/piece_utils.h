#pragma once
#include "mforce/music/locus.h"
#include "mforce/music/structure.h"
#include "mforce/music/pitch_reader.h"
#include <algorithm>

namespace mforce::piece_utils {

// Resolve the Passage at this Locus, given the parallel Section/Part indices.
// Returns nullptr if structure isn't populated yet (e.g., mid-composition
// before the passage has been inserted).
// Precondition: locus.piece != nullptr && locus.pieceTemplate != nullptr.
inline const Passage* passage_at(const Locus& locus) {
  if (locus.partIdx < 0 || locus.partIdx >= (int)locus.piece->parts.size()) return nullptr;
  if (locus.sectionIdx < 0 || locus.sectionIdx >= (int)locus.piece->sections.size()) return nullptr;
  const auto& part = locus.piece->parts[locus.partIdx];
  const auto& sectionName = locus.piece->sections[locus.sectionIdx].name;
  auto it = part.passages.find(sectionName);
  return it == part.passages.end() ? nullptr : &it->second;
}

// Pitch immediately preceding this Locus position.
//
// Matches DefaultPassageStrategy::realize_passage's threaded cursor exactly:
//   - Seed PitchReader ONCE from the passage template's startingPitch.
//   - Step through every unit of every figure in every phrase in order,
//     WITHOUT resetting between phrases (mirroring the continuous cursor).
//   - If phraseTmpl.startingPitch overrides exist in the realized passage,
//     they are already baked into each phrase's figures, so we simply step
//     through the data as laid down.
//
// Invariant assumed: phrases[0].startingPitch == passTmpl.startingPitch.
// DefaultPhraseStrategy / PeriodPhraseStrategy / SentencePhraseStrategy all
// guarantee this (the first phrase inherits the passage's seeded cursor).
//
// Precondition: locus.piece != nullptr && locus.pieceTemplate != nullptr.
inline Pitch pitch_before(const Locus& locus) {
  if (locus.sectionIdx < 0 || locus.sectionIdx >= (int)locus.piece->sections.size()) return Pitch{};
  if (locus.partIdx < 0 || locus.partIdx >= (int)locus.pieceTemplate->parts.size()) return Pitch{};

  const auto& sectionName = locus.piece->sections[locus.sectionIdx].name;

  // Resolve the PassageTemplate via PartTemplate.passages (unordered_map keyed by section name)
  const auto& partTmpl = locus.pieceTemplate->parts[locus.partIdx];
  auto tmplIt = partTmpl.passages.find(sectionName);
  if (tmplIt == partTmpl.passages.end()) return Pitch{};
  const PassageTemplate& passTmpl = tmplIt->second;

  const Passage* pass = passage_at(locus);

  if (locus.phraseIdx <= 0 && locus.figureIdx <= 0) {
    if (passTmpl.startingPitch) return *passTmpl.startingPitch;
  }

  if (pass != nullptr) {
    // Seed the PitchReader once from the passage template's startingPitch.
    // (Invariant: phrases[0].startingPitch == passTmpl.startingPitch.)
    Pitch seed = passTmpl.startingPitch ? *passTmpl.startingPitch
                 : (pass->phrases.empty() ? Pitch{} : pass->phrases[0].startingPitch);
    PitchReader pr(locus.piece->sections[locus.sectionIdx].scale);
    pr.set_pitch(seed);

    // Step through all prior phrases continuously — no reset between phrases.
    // Each figure boundary advances the cursor by its FC.leadStep before
    // walking that figure's units (post-foundation-refactor: leadStep no
    // longer baked into figure data).
    int endPhrase = locus.phraseIdx < 0 ? (int)pass->phrases.size() : locus.phraseIdx;
    for (int pi = 0; pi < endPhrase; ++pi) {
      const Phrase& ph = pass->phrases[pi];
      for (int fi = 0; fi < (int)ph.figures.size(); ++fi) {
        if (fi < (int)ph.connectors.size()) pr.step(ph.connectors[fi].leadStep);
        for (const auto& unit : ph.figures[fi]->units) {
          pr.step(unit.step);
        }
      }
    }
    // For the current phrase, step through figures 0..figureIdx-1.
    if (locus.phraseIdx >= 0 && locus.phraseIdx < (int)pass->phrases.size() && locus.figureIdx > 0) {
      const Phrase& ph = pass->phrases[locus.phraseIdx];
      int endFig = std::min(locus.figureIdx, (int)ph.figures.size());
      for (int fi = 0; fi < endFig; ++fi) {
        if (fi < (int)ph.connectors.size()) pr.step(ph.connectors[fi].leadStep);
        for (const auto& unit : ph.figures[fi]->units) {
          pr.step(unit.step);
        }
      }
    }
    return pr.get_pitch();
  }

  if (passTmpl.startingPitch) return *passTmpl.startingPitch;
  return Pitch{};
}

// Convenience: a PitchReader seeded to pitch_before(locus), with the current
// section's scale. Strategies that need to walk forward from the prior cursor
// use this instead of constructing a PitchReader manually.
inline PitchReader reader_before(const Locus& locus) {
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  PitchReader pr(scale);
  pr.set_pitch(pitch_before(locus));
  return pr;
}

// Pitch-range query. "Before" semantics: considers realized content up to
// (but not including) this Locus position, within the named scope.
//
// If no realized content precedes this Locus within the scope, `empty` is
// true and lowest/highest are default-constructed — callers must check
// `empty` before using the pitches.
struct PitchRange {
  Pitch lowest;
  Pitch highest;
  bool empty{true};
};

namespace detail {
// Advance a PitchReader across one figure's units, updating range. Walks
// using step semantics (matching DefaultPassageStrategy's cursor walk).
inline void update_range_from_figure(const Figure& fig, PitchReader& pr,
                                      PitchRange& r) {
  for (const auto& unit : fig.units) {
    pr.step(unit.step);
    if (unit.rest) continue;
    Pitch p = pr.get_pitch();
    float nn = p.note_number();
    if (r.empty) {
      r.lowest = p; r.highest = p; r.empty = false;
    } else {
      if (nn < r.lowest.note_number())  r.lowest  = p;
      if (nn > r.highest.note_number()) r.highest = p;
    }
  }
}
} // namespace detail

// Lowest / highest pitches realized within the CURRENT phrase, from figure 0
// up to (but not including) locus.figureIdx. If locus.figureIdx <= 0 or the
// current phrase has no realized content yet, returns empty=true.
inline PitchRange range_in_phrase_before(const Locus& locus) {
  PitchRange r;
  const Passage* pass = passage_at(locus);
  if (!pass) return r;
  if (locus.phraseIdx < 0 || locus.phraseIdx >= (int)pass->phrases.size()) return r;

  const Phrase& ph = pass->phrases[locus.phraseIdx];
  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  PitchReader pr(scale);
  pr.set_pitch(ph.startingPitch);

  int endFig = locus.figureIdx < 0 ? (int)ph.figures.size() : locus.figureIdx;
  for (int fi = 0; fi < endFig; ++fi) {
    if (fi < (int)ph.connectors.size()) pr.step(ph.connectors[fi].leadStep);
    detail::update_range_from_figure(*ph.figures[fi], pr, r);
  }
  return r;
}

// Lowest / highest pitches realized across the CURRENT passage up to (but
// not including) this Locus position. All phrases 0..phraseIdx-1 walked
// completely, plus phrase phraseIdx up to figure figureIdx.
inline PitchRange range_in_passage_before(const Locus& locus) {
  PitchRange r;
  const Passage* pass = passage_at(locus);
  if (!pass) return r;

  const auto& sectionName = locus.piece->sections[locus.sectionIdx].name;
  const auto& partTmpl = locus.pieceTemplate->parts[locus.partIdx];
  auto tmplIt = partTmpl.passages.find(sectionName);
  if (tmplIt == partTmpl.passages.end()) return r;
  const PassageTemplate& passTmpl = tmplIt->second;

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  PitchReader pr(scale);
  Pitch seed = passTmpl.startingPitch ? *passTmpl.startingPitch
               : (pass->phrases.empty() ? Pitch{} : pass->phrases[0].startingPitch);
  pr.set_pitch(seed);

  int endPhrase = locus.phraseIdx < 0 ? (int)pass->phrases.size() : locus.phraseIdx;
  for (int pi = 0; pi < endPhrase; ++pi) {
    const Phrase& ph = pass->phrases[pi];
    for (int fi = 0; fi < (int)ph.figures.size(); ++fi) {
      if (fi < (int)ph.connectors.size()) pr.step(ph.connectors[fi].leadStep);
      detail::update_range_from_figure(*ph.figures[fi], pr, r);
    }
  }
  if (locus.phraseIdx >= 0 && locus.phraseIdx < (int)pass->phrases.size() && locus.figureIdx > 0) {
    const Phrase& ph = pass->phrases[locus.phraseIdx];
    int endFig = std::min(locus.figureIdx, (int)ph.figures.size());
    for (int fi = 0; fi < endFig; ++fi) {
      if (fi < (int)ph.connectors.size()) pr.step(ph.connectors[fi].leadStep);
      detail::update_range_from_figure(*ph.figures[fi], pr, r);
    }
  }
  return r;
}

// Lowest / highest pitches realized within this Part across the whole Piece
// up to (but not including) this Locus position. Walks every prior section's
// passage for this Part fully, plus the current passage partially via
// range_in_passage_before.
inline PitchRange range_in_piece_before(const Locus& locus) {
  PitchRange r;
  if (locus.partIdx < 0 || locus.partIdx >= (int)locus.piece->parts.size()) return r;
  const Part& part = locus.piece->parts[locus.partIdx];

  for (int si = 0; si < locus.sectionIdx; ++si) {
    const auto& secName = locus.piece->sections[si].name;
    auto it = part.passages.find(secName);
    if (it == part.passages.end()) continue;
    const Scale& scale = locus.piece->sections[si].scale;
    PitchReader pr(scale);
    if (!it->second.phrases.empty()) pr.set_pitch(it->second.phrases[0].startingPitch);
    for (const auto& ph : it->second.phrases) {
      for (int fi = 0; fi < (int)ph.figures.size(); ++fi) {
        if (fi < (int)ph.connectors.size()) pr.step(ph.connectors[fi].leadStep);
        detail::update_range_from_figure(*ph.figures[fi], pr, r);
      }
    }
  }

  // Merge in current-passage partial.
  PitchRange partial = range_in_passage_before(locus);
  if (!partial.empty) {
    if (r.empty) { r = partial; }
    else {
      if (partial.lowest.note_number()  < r.lowest.note_number())  r.lowest  = partial.lowest;
      if (partial.highest.note_number() > r.highest.note_number()) r.highest = partial.highest;
    }
  }
  return r;
}

} // namespace mforce::piece_utils

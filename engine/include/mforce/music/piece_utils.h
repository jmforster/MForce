#pragma once
#include "mforce/music/locus.h"
#include "mforce/music/structure.h"
#include "mforce/music/pitch_reader.h"
#include <algorithm>

namespace mforce::piece_utils {

// Resolve the Passage at this Locus, given the parallel Section/Part indices.
// Returns nullptr if structure isn't populated yet (e.g., mid-composition
// before the passage has been inserted).
inline const Passage* passage_at(const Locus& locus) {
  if (locus.partIdx < 0 || locus.partIdx >= (int)locus.piece.parts.size()) return nullptr;
  if (locus.sectionIdx < 0 || locus.sectionIdx >= (int)locus.piece.sections.size()) return nullptr;
  const auto& part = locus.piece.parts[locus.partIdx];
  const auto& sectionName = locus.piece.sections[locus.sectionIdx].name;
  auto it = part.passages.find(sectionName);
  return it == part.passages.end() ? nullptr : &it->second;
}

// Pitch immediately preceding this Locus position.
// For a fresh figure within a phrase: the cursor after the prior figure's
// last note. For the first figure of a phrase: the phrase's startingPitch.
// For the first figure of the first phrase of a passage: the passage's
// startingPitch (read from the template, since the passage itself may not
// exist yet at the moment of the query).
inline Pitch pitch_before(const Locus& locus) {
  if (locus.sectionIdx < 0 || locus.sectionIdx >= (int)locus.piece.sections.size()) return Pitch{};
  if (locus.partIdx < 0 || locus.partIdx >= (int)locus.pieceTemplate.parts.size()) return Pitch{};

  const auto& sectionName = locus.piece.sections[locus.sectionIdx].name;

  // Resolve the PassageTemplate via PartTemplate.passages (unordered_map keyed by section name)
  const auto& partTmpl = locus.pieceTemplate.parts[locus.partIdx];
  auto tmplIt = partTmpl.passages.find(sectionName);
  if (tmplIt == partTmpl.passages.end()) return Pitch{};
  const PassageTemplate& passTmpl = tmplIt->second;

  const Passage* pass = passage_at(locus);

  if (locus.phraseIdx <= 0 && locus.figureIdx <= 0) {
    if (passTmpl.startingPitch) return *passTmpl.startingPitch;
  }

  if (pass != nullptr) {
    Pitch seed = passTmpl.startingPitch ? *passTmpl.startingPitch
                 : (pass->phrases.empty() ? Pitch{} : pass->phrases[0].startingPitch);
    PitchReader pr(locus.piece.sections[locus.sectionIdx].scale);
    pr.set_pitch(seed);
    int endPhrase = locus.phraseIdx < 0 ? (int)pass->phrases.size() : locus.phraseIdx;
    for (int pi = 0; pi < endPhrase; ++pi) {
      const Phrase& ph = pass->phrases[pi];
      pr.set_pitch(ph.startingPitch);
      for (const auto& fig : ph.figures) {
        for (const auto& unit : fig->units) {
          pr.step(unit.step);
        }
      }
    }
    if (locus.phraseIdx >= 0 && locus.phraseIdx < (int)pass->phrases.size() && locus.figureIdx > 0) {
      const Phrase& ph = pass->phrases[locus.phraseIdx];
      pr.set_pitch(ph.startingPitch);
      int endFig = std::min(locus.figureIdx, (int)ph.figures.size());
      for (int fi = 0; fi < endFig; ++fi) {
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

} // namespace mforce::piece_utils

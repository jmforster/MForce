#pragma once
#include "mforce/music/locus.h"
#include "mforce/music/structure.h"
#include "mforce/music/pitch_reader.h"
#include <algorithm>
#include <cassert>

namespace mforce {

// Thread-local selfcheck locus. Defined as inline thread_local (C++17) so a
// -DMFORCE_LOCUS_SELFCHECK build links without a separate TU definition.
inline thread_local Locus* g_selfcheck_locus = nullptr;

} // namespace mforce

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
    int endPhrase = locus.phraseIdx < 0 ? (int)pass->phrases.size() : locus.phraseIdx;
    for (int pi = 0; pi < endPhrase; ++pi) {
      const Phrase& ph = pass->phrases[pi];
      for (const auto& fig : ph.figures) {
        for (const auto& unit : fig->units) {
          pr.step(unit.step);
        }
      }
    }
    // For the current phrase, step through figures 0..figureIdx-1.
    if (locus.phraseIdx >= 0 && locus.phraseIdx < (int)pass->phrases.size() && locus.figureIdx > 0) {
      const Phrase& ph = pass->phrases[locus.phraseIdx];
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

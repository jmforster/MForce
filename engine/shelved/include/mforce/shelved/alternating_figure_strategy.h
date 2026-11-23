#pragma once
#include "mforce/music/pitch_reader.h"
#include "mforce/compose/piece_utils.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/chord_walk.h"
#include "mforce/compose/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/compose/templates.h"
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
// NOTE: compose_passage body is defined out-of-line below.
// ---------------------------------------------------------------------------
class AlternatingFigureStrategy : public PassageStrategy {
public:
  static constexpr const char* kName = "alternating_figure";
  using Settings = NoSettings;

  std::string name() const override { return kName; }

  Passage compose_passage(Locus locus, const PassageTemplate& passTmpl) const override;
};

// ---------------------------------------------------------------------------
// Out-of-line definition of AlternatingFigureStrategy::compose_passage.
//
// It looks up the default figure strategy in the kit, which needs
// ComposeKit complete.
// ---------------------------------------------------------------------------
inline Passage AlternatingFigureStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) const {
  (void)settings_of<Settings>(pt.strategy);  // refuses settings of another type

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  const ChordProgression chordProg =
      locus.piece->sections[locus.sectionIdx].harmonyTimeline.progression();
  if (chordProg.count() == 0) {
    throw std::runtime_error("AFS: no chord progression on section");
  }
  if (pt.phrases.empty() || pt.phrases[0].figures.size() < 2) {
    throw std::runtime_error(
        "AFS: need 1 phrase with at least 2 figure templates (A and B)");
  }

  const auto& figTemplateA = pt.phrases[0].figures[0];
  const auto& figTemplateB = pt.phrases[0].figures[1];

  Phrase phrase;
  if (pt.phrases[0].startingPitch) {
    phrase.startingPitch = *pt.phrases[0].startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus.with_phrase(0));
  }

  PitchReader runningReader(scale);
  runningReader.set_pitch(phrase.startingPitch);

  const FigureStrategy* fs = &locus.kit->strategies.figure("default_figure");

  for (int ci = 0; ci < chordProg.count(); ++ci) {
    bool isA = (ci % 2 == 0);
    const auto& ft = isA ? figTemplateA : figTemplateB;

    FigureTemplate adjusted = ft;
    adjusted.totalBeats = chordProg.pulses.get(ci);

    if (!isA && adjusted.figureCadenceType > 0) {
      int bIndex = ci / 2;
      adjusted.figureCadenceType = (bIndex % 2 == 0) ? 1 : 2;
    }

    MelodicFigure rawFig = fs->compose_figure(locus.with_phrase(0).with_figure(ci), adjusted);

    if (isA) {
      chord_walk::advance(runningReader, chordProg.chords.get(ci), scale,
                          4 /*kBaseOctave*/, rawFig);

      auto cf = std::make_unique<ChordFigure>();
      cf->units = rawFig.units;
      phrase.add_figure(std::move(cf));
    } else {
      runningReader.step(rawFig.net_step());
      phrase.add_melodic_figure(std::move(rawFig));
    }
  }

  Passage passage;
  passage.add_phrase(std::move(phrase));
  return passage;
}

} // namespace mforce

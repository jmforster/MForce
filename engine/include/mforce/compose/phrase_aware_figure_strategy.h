#pragma once
#include "mforce/music/pitch_reader.h"
#include "mforce/compose/piece_utils.h"
#include "mforce/compose/default_phrase_strategy.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/chord_walk.h"
#include "mforce/compose/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/compose/templates.h"
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
// NOTE: compose_passage body is defined out-of-line below.
// ---------------------------------------------------------------------------
class PhraseAwareFigureStrategy : public PassageStrategy {
public:
  static constexpr const char* kName = "phrase_aware_figure";
  using Settings = NoSettings;

  std::string name() const override { return kName; }

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
  // for figureCadenceType (shape_strategies.h).
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

// ---------------------------------------------------------------------------
// Out-of-line definition of PhraseAwareFigureStrategy::compose_passage.
//
// Comp backlog #7 — the chord-driven strategy that respects phrase
// boundaries. See docs/superpowers/specs/2026-08-10-phrase-aware-cadence-design.md.
// ---------------------------------------------------------------------------
inline Passage PhraseAwareFigureStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;

  // Passage-local progression wins over the section's timeline, so two
  // passages in one section can carry different harmony.
  const ChordProgression* prog = nullptr;
  ChordProgression sectionProg;
  if (pt.chordProgression && pt.chordProgression->count() > 0) {
    prog = &*pt.chordProgression;
  } else {
    sectionProg = locus.piece->sections[locus.sectionIdx].harmonyTimeline.progression();
    if (sectionProg.count() > 0) prog = &sectionProg;
  }
  if (!prog) {
    throw std::runtime_error(
        "phrase_aware_figure: no chord progression on passage or section");
  }

  // Collect the phrase templates we will actually use.
  std::vector<const PhraseTemplate*> phraseTmpls;
  for (const auto& ph : pt.phrases) {
    if (ph.locked) continue;
    if (ph.figures.empty()) {
      throw std::runtime_error(
          "phrase_aware_figure: phrase '" + ph.name + "' has no figure templates");
    }
    phraseTmpls.push_back(&ph);
  }
  if (phraseTmpls.empty()) {
    throw std::runtime_error("phrase_aware_figure: passage has no usable phrases");
  }

  // Beat budgets: explicit totalBeats, else the sum of the figures'.
  std::vector<float> budgets;
  for (const auto* ph : phraseTmpls) {
    float b = ph->totalBeats;
    if (!(b > 0.0f)) {
      b = 0.0f;
      for (const auto& ft : ph->figures) b += ft.totalBeats;
    }
    budgets.push_back(b);
  }
  std::vector<float> chordBeats;
  for (int ci = 0; ci < prog->count(); ++ci) chordBeats.push_back(prog->pulses.get(ci));

  const std::vector<int> bounds = partition_chords(budgets, chordBeats);
  const int phrasesUsed = int(bounds.size()) - 1;

  Passage passage;

  FigureStrategy* fs = &locus.kit->strategies.figure("default_figure");

  // Passage-level running cursor. Carried across phrases so a non-parallel
  // phrase continues where the previous one stopped — the same continuity
  // rule DefaultPassageStrategy uses.
  Pitch passageStart = pt.startingPitch
      ? *pt.startingPitch
      : ::mforce::piece_utils::pitch_before(locus.with_phrase(0));
  PitchReader runningReader(scale);
  runningReader.set_pitch(passageStart);

  for (int p = 0; p < phrasesUsed; ++p) {
    const PhraseTemplate& ph = *phraseTmpls[p];
    const int c0 = bounds[p];
    const int c1 = bounds[p + 1];

    Phrase phrase;
    if (ph.startingPitch) {
      phrase.startingPitch = *ph.startingPitch;
      runningReader.set_pitch(phrase.startingPitch);
    } else if (ph.parallel) {
      phrase.startingPitch = passageStart;
      runningReader.set_pitch(phrase.startingPitch);
    } else {
      phrase.startingPitch = runningReader.get_pitch();
    }

    const int cadType = ph.cadenceType;

    for (int ci = c0; ci < c1; ++ci) {
      const int j = ci - c0;                       // position WITHIN the phrase
      const bool isLast = (ci == c1 - 1);
      const bool isCadence = isLast && cadType > 0;

      // Body texture alternates on j, not on the global chord index. The
      // cadence figure is always melodic: apply_cadence accounts degrees by
      // summing net_step() in scale steps, so a chord-tone final figure
      // would desynchronise its arithmetic.
      const bool useA = !isCadence && (j % 2 == 0);
      const int tmplIdx = (useA || ph.figures.size() < 2) ? 0 : 1;

      FigureTemplate adjusted = ph.figures[std::min<size_t>(tmplIdx, ph.figures.size() - 1)];
      adjusted.totalBeats = prog->pulses.get(ci);
      if (isCadence) {
        adjusted.figureCadenceType = cadType;
        if (adjusted.shape == FigureShape::Free) {
          adjusted.shape = FigureShape::CadentialApproach;
        }
      } else {
        adjusted.figureCadenceType = 0;
      }

      // Degree the cursor has actually reached, tracked through both chord-
      // tone and scale-step figures. apply_cadence cannot recompute this
      // (it sums net_step() assuming every figure steps in scale degrees,
      // which a ChordFigure does not), so we hand it the real answer.
      const Pitch preFigurePitch = runningReader.get_pitch();
      const int preFigureDeg =
          DefaultPhraseStrategy::degree_in_scale(preFigurePitch, scale);

      MelodicFigure rawFig =
          fs->compose_figure(locus.with_phrase(p).with_figure(j), adjusted);

      if (isCadence) {
        // Land the arrival on the phrase's cadence target, rewriting the
        // figure's tail into approach + settled arrival.
        int target = ph.cadenceTarget >= 0 ? ph.cadenceTarget
                                           : default_cadence_target(cadType);
        const int len = scale.length();
        target = ((target % len) + len) % len;
        DefaultPhraseStrategy::rebuild_cadential_tail(rawFig, preFigureDeg,
                                                      target, len, 1.0f);
        runningReader.set_pitch(preFigurePitch);
        runningReader.step(rawFig.net_step());
        std::cerr << "phrase_aware_figure: phrase " << p
                  << " chords [" << c0 << "," << c1 << ") cadType " << cadType
                  << " target deg " << target
                  << " startDeg " << preFigureDeg
                  << " arrivalDeg " << (preFigureDeg + rawFig.net_step())
                  << " notes " << rawFig.note_count()
                  << " finalDur " << (rawFig.units.empty() ? 0.0f
                                      : rawFig.units.back().duration)
                  << "\n";
        phrase.add_melodic_figure(std::move(rawFig));
        continue;
      }

      if (useA) {
        // Chord-tone figure: walk the running pitch through the chord's
        // tones exactly as realization will (shared with AFS).
        chord_walk::advance(runningReader, prog->chords.get(ci), scale,
                            4 /*kBaseOctave*/, rawFig);

        auto cf = std::make_unique<ChordFigure>();
        cf->units = rawFig.units;
        phrase.add_figure(std::move(cf));
      } else {
        runningReader.step(rawFig.net_step());
        phrase.add_melodic_figure(std::move(rawFig));
      }
    }

    // NOTE: no post-loop apply_cadence call. The arrival is rewritten in
    // place above, with the cursor's true degree — apply_cadence would
    // recompute that degree by summing net_step() over the preceding
    // figures, which is wrong here because the body figures are
    // ChordFigures stepping in chord-tone indices, not scale degrees.

    passage.add_phrase(std::move(phrase));
  }

  return passage;
}

} // namespace mforce

#pragma once
//
// Anchor-driven passage strategies — backlog #6 (GOALS Wolfie G2), stage 1.
//
// Spec: docs/superpowers/specs/2026-07-31-passage-strategy-expansion-design.md
// Python prototype (rendered + measured first): corpus/mtd_seg/passage_strategies.py
//
// The prototype's central finding is that these passages are naturally
// authored as ANCHORS — "entry i starts on scale-degree a_i" — and that the
// FC.leadStep connectors the engine actually consumes are a mechanical
// derivation from that list. connectors_for() below is that derivation, and
// it is the piece both strategies here (and the stage-2/4 ones) share.
//
// Second shared piece: a passage-level RANGE GUARD. Anchor-driven passages
// walk further than phrases do, and without a guard both of these produced
// 23-24 semitone spans in the prototype. predicted_span() runs the same
// cursor math the engine runs, so the guard can reject a candidate before
// anything is composed.
//
// Stage 1 covers PedalBuildupStrategy + SequencePassageStrategy. The pedal
// itself stays a template-authored part: a PassageStrategy is passage-scoped
// and cannot add parts to the piece, so this emits the melody only.
//
// Registered as "pedal_buildup" and "sequence_passage".

#include "mforce/compose/strategy.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/compose/default_figure_strategy.h"
#include "mforce/compose/default_phrase_strategy.h"
#include "mforce/compose/random_figure_builder.h"
#include "mforce/compose/figure_constraints.h"
#include "mforce/core/randomizer.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace mforce {

// ===========================================================================
// Anchor-driven passage settings.
//
// The anchor-driven strategies (pedal_buildup_strategy.h,
// sequence_passage_strategy.h, connective_passage_strategy.h,
// wandering_passage_strategy.h) author their passage as "entry i starts on
// scale-degree anchors[i]" and derive FC.leadStep mechanically with the
// helpers here. Their settings' shared fields have the same meaning in each.
// ===========================================================================

// ===========================================================================
// Shared anchor plumbing
// ===========================================================================

namespace passage_anchors {

// connectors_for(figs, anchors) — leadStep connectors that place figure i's
// FIRST note on scale-degree anchors[i], measured relative to the passage
// start. Mirrors the engine's cursor math: the connector advances the cursor
// by leadStep, then units[0].step (0 by convention) leaves it there, so
//     leadStep_i = anchors[i] - cursor_after(i-1)
// and cursor_after(i) = anchors[i] + net_step(figs[i]).
// figs[0] anchors at 0 by definition and gets an absent connector.
inline std::vector<std::optional<FigureConnector>> connectors_for(
    const std::vector<const MelodicFigure*>& figs,
    const std::vector<int>& anchors) {
  std::vector<std::optional<FigureConnector>> conns;
  const int n = int(figs.size());
  conns.reserve(n);
  if (n == 0) return conns;
  conns.push_back(std::nullopt);
  int cursor = figs[0]->net_step();
  for (int i = 1; i < n; ++i) {
    FigureConnector fc;
    fc.leadStep = anchors[i] - cursor;
    conns.push_back(fc);
    cursor = anchors[i] + figs[i]->net_step();
  }
  return conns;
}

// predicted_span(scale, start, figs, conns) — widest semitone span the
// passage will occupy, computed WITHOUT composing anything. Runs the same
// walk the composer runs (connector leadStep, then each unit's step) through
// a PitchReader, so the number is the real one and not an estimate.
inline int predicted_span(const Scale& scale, const Pitch& start,
                          const std::vector<const MelodicFigure*>& figs,
                          const std::vector<std::optional<FigureConnector>>& conns) {
  PitchReader pr(scale);
  pr.set_pitch(start);
  float lo = pr.get_note_number(), hi = lo;
  for (int i = 0; i < int(figs.size()); ++i) {
    if (i < int(conns.size()) && conns[i]) pr.step(conns[i]->leadStep);
    for (const auto& u : figs[i]->units) {
      pr.step(u.step);
      lo = std::min(lo, pr.get_note_number());
      hi = std::max(hi, pr.get_note_number());
    }
  }
  return int(hi - lo);
}

// Resolve a strategy seed the same way LibraryPassageStrategy does.
inline uint32_t resolve_seed(const Locus& locus, uint32_t configSeed,
                             uint32_t salt) {
  if (configSeed != 0) return configSeed ^ salt;
  return (locus.plan->masterSeed
          ^ (uint32_t(locus.sectionIdx) * 1031u
             + uint32_t(locus.partIdx) * 17u)) ^ salt;
}

// sample_cell(rng, beats) — draw a short figure from RandomFigureBuilder with
// enough entropy to actually differ between takes.
//
// Measured while porting: RFB's build() picks a shape by coin-flip and most of
// the shape helpers (arc, zigzag, leap_fill) are DETERMINISTIC given (count,
// pulse, net) — only wander_ consumes the step RNG. So a bare
// build_by_length(3.0) with a fixed pulse returns the SAME cell for every
// seed: two pedal_buildup takes at seeds 7311 and 8311 rendered byte-identical
// note lists. Drawing the pulse (hence the note count) and the cell's net
// movement per candidate restores variety without touching RFB's contract.
inline MelodicFigure sample_cell(Randomizer& rng, float beats,
                                 const std::vector<float>& pulseChoices) {
  const float pulse = pulseChoices[rng.int_range(0, int(pulseChoices.size()) - 1)];
  const int count = std::max(1, int(std::lround(beats / pulse)));
  Constraints cons;
  cons.defaultPulse = pulse;
  // |net| must fit inside the cell's own span or the shape helpers throw.
  const int netLimit = std::max(0, count - 1);
  if (netLimit > 0) cons.net = std::clamp(rng.int_range(-2, 2), -netLimit, netLimit);
  RandomFigureBuilder rfb(rng.rng());
  MelodicFigure fig = rfb.build_by_length(beats, cons);
  if (!fig.units.empty()) fig.units[0].step = 0;
  return fig;
}

// Store a generated figure in the piece's motif pool under `name` (no-op if
// the name is already taken, so repeat compose calls stay idempotent).
inline void put_motif(Locus& locus, const std::string& name,
                      MelodicFigure fig, uint32_t seed) {
  if (locus.plan->realizedMotifs.count(name)) return;
  if (!fig.units.empty()) fig.units[0].step = 0;   // step[0]=0 convention
  Motif m;
  m.name = name;
  m.userProvided = false;
  m.content = std::move(fig);
  m.origin = MotifOrigin::Generated;
  m.generationSeed = seed;
  locus.plan->add_motif(std::move(m));
}

// prior_passage_tail(locus, units) — the last `units` FigureUnits of the
// previous section's passage for THIS part, or nullopt if there is no prior
// passage to harvest.
//
// Spec asked whether Locus needs a new "what came before" carrier. It does
// not: Composer::compose walks `for each part { for each section in order }`
// and assigns part->passages[sectionName] after composing, so by the time
// section i composes, sections 0..i-1 are already realized on the same Part
// and reachable through the Piece pointer Locus already holds.
inline std::optional<MelodicFigure> prior_passage_tail(const Locus& locus,
                                                       int units) {
  if (locus.sectionIdx <= 0 || locus.partIdx < 0) return std::nullopt;
  const auto& parts = locus.piece->parts;
  if (locus.partIdx >= int(parts.size())) return std::nullopt;
  const std::string& prevSection = locus.piece->sections[locus.sectionIdx - 1].name;
  auto it = parts[locus.partIdx].passages.find(prevSection);
  if (it == parts[locus.partIdx].passages.end()) return std::nullopt;

  const Passage& prev = it->second;
  for (int pi = prev.phrase_count() - 1; pi >= 0; --pi) {
    const Phrase& ph = prev.phrases[pi];
    for (int fi = int(ph.figures.size()) - 1; fi >= 0; --fi) {
      const Figure* f = ph.figures[fi].get();
      if (!f || f->units.empty()) continue;
      const int n = int(f->units.size());
      const int take = std::min(std::max(1, units), n);
      MelodicFigure tail;
      tail.units.assign(f->units.end() - take, f->units.end());
      tail.units[0].step = 0;             // a cell always starts anchored
      return tail;
    }
  }
  return std::nullopt;
}

// Assemble one PhraseTemplate out of motif names + anchors, then compose it
// via default_phrase. Both strategies end this way.
inline Phrase compose_anchored_phrase(
    Locus locus, const std::string& phraseName,
    const std::vector<std::string>& refs,
    const std::vector<std::optional<FigureConnector>>& conns,
    const std::optional<Pitch>& startingPitch) {
  PhraseTemplate pt;
  pt.name = phraseName;
  pt.startingPitch = startingPitch;
  pt.figures.reserve(refs.size());
  for (const auto& r : refs) {
    FigureTemplate ft;
    ft.source = FigureSource::Reference;
    ft.motifName = r;
    pt.figures.push_back(ft);
  }
  pt.connectors = conns;

  const PhraseStrategy* ps = &locus.kit->strategies.phrase("default_phrase");
  return ps->compose_phrase(locus.with_phrase(0), pt);
}

} // namespace passage_anchors

} // namespace mforce

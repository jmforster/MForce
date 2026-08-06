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

#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/figure_constraints.h"
#include "mforce/core/randomizer.h"

#include <algorithm>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace mforce {

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
  return (locus.pieceTemplate->masterSeed
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

// grid_complete(passage, grid) — quantize the passage's TOTAL length to the
// grid by adjusting the last note, and return the delta applied (comp #18).
//
// The phrase path has done this since run 13 — "a phrase stops at a barline,
// not mid-beat" — and the passage path never got the equivalent, so 17 of 23
// engine passage renders ended at things like 9.38 and 37.25. The cause is
// upstream and deliberate: sample_cell DRAWS its pulse per candidate (without
// that draw every take rendered byte-identical), and nothing downstream
// reconciled the accumulated total with the beat grid.
//
// NEAREST, not ceiling. Run 17 decomposed the phrase path's ending overshoot
// and found grid completion ceiling every phrase onto the NEXT barline was the
// dominant term, worth +0.25 beats at the median on top of an already-correct
// draw; `calib` fixed it by rounding to the nearest barline.
//
// Safe to mutate: Phrase's copy ctor deep-clones through Figure::clone(), so a
// composed Passage's units are its own and cannot reach realizedMotifs. The
// "figures are never mutated" convention is about the motif pool.
inline float grid_complete(Passage& passage, float grid) {
  if (grid <= 0.0f) return 0.0f;

  FigureUnit* last = nullptr;
  float total = 0.0f;
  for (auto& phrase : passage.phrases) {
    for (auto& fig : phrase.figures) {
      if (!fig) continue;
      for (auto& u : fig->units) {
        total += u.duration;
        last = &u;
      }
    }
  }
  if (!last || total <= 0.0f) return 0.0f;

  // A shrink that would leave the final note shorter than the tokenizer's own
  // duration grid is worse than the off-grid ending it fixes, so those round
  // up instead. Same for a target of zero.
  constexpr float kMinFinal = 0.25f;
  float target = std::round(total / grid) * grid;
  if (target <= 0.0f || last->duration + (target - total) < kMinFinal) {
    target = std::ceil(total / grid - 1e-6f) * grid;
  }
  const float delta = target - total;
  if (std::abs(delta) < 1e-3f) return 0.0f;

  last->duration += delta;
  return delta;
}

// Store a generated figure in the piece's motif pool under `name` (no-op if
// the name is already taken, so repeat compose calls stay idempotent).
inline void put_motif(Locus& locus, const std::string& name,
                      MelodicFigure fig, uint32_t seed) {
  if (locus.pieceTemplate->realizedMotifs.count(name)) return;
  if (!fig.units.empty()) fig.units[0].step = 0;   // step[0]=0 convention
  Motif m;
  m.name = name;
  m.userProvided = false;
  m.content = std::move(fig);
  m.origin = MotifOrigin::Generated;
  m.generationSeed = seed;
  locus.pieceTemplate->add_motif(std::move(m));
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

  PhraseStrategy* ps = StrategyRegistry::instance().resolve_phrase("default_phrase");
  if (!ps) {
    std::cerr << "passage_anchors: default_phrase not registered\n";
    return Phrase{};
  }
  return ps->compose_phrase(locus.with_phrase(0), pt);
}

} // namespace passage_anchors

// ===========================================================================
// PedalBuildupStrategy — "pedal_buildup"
// ===========================================================================
//
// Levels of EQUAL span with DOUBLING density: level j runs the base cell at
// 1/2^j the note values, restated 2^j times, so every level spans the same
// beats while the surface accelerates. (Equal span is what makes it read as a
// buildup; levels that also grow longer read as the opposite.) Each level
// climbs `climbStep` degrees; restatements inside a level stay at pitch.
//
// Strict restatement pins the interval sequence exactly periodic — the
// prototype measured selfsim 1.0, well past the corpus p95 of 0.77 — so
// alternate restatements use a rotation of the cell instead: same material,
// same density, different contour. The final entry gets a held last note.
class PedalBuildupStrategy : public PassageStrategy {
public:
  std::string name() const override { return "pedal_buildup"; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) override;
};

inline Passage PedalBuildupStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {
  Passage passage;
  const PedalBuildupConfig cfg =
      pt.pedalBuildupConfig ? *pt.pedalBuildupConfig : PedalBuildupConfig{};

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  Pitch start = pt.startingPitch ? *pt.startingPitch
                                 : Pitch::from_note_number(60.0f);
  uint32_t seed = passage_anchors::resolve_seed(locus, cfg.seed, 0x5045'4442u);

  // Candidate = one sampled base cell expanded into the level structure.
  // Kept as data (not motifs) until a candidate wins the range guard, so a
  // rejected sample leaves nothing behind in the motif pool.
  struct Candidate {
    std::vector<MelodicFigure> figs;    // one per entry, in order
    std::vector<int> anchors;
    std::vector<std::optional<FigureConnector>> conns;
    int span{0};
    int levels{0};
    int entries{0};
  };

  auto build_candidate = [&](uint32_t trySeed) {
    Randomizer rng(trySeed);
    Candidate c;
    c.levels = cfg.levels > 0 ? cfg.levels : rng.int_range(3, 4);

    MelodicFigure base;
    try {
      base = passage_anchors::sample_cell(rng, cfg.cellBeats,
                                          {0.5f, 0.75f, 1.0f});
    } catch (const std::exception& e) {
      std::cerr << "PedalBuildupStrategy: base cell build failed: "
                << e.what() << "\n";
      return c;
    }

    for (int j = 0; j < c.levels; ++j) {
      MelodicFigure lvl = (j == 0) ? base
                                   : figure_transforms::compress(base, float(1 << j));
      MelodicFigure var = figure_transforms::rotate(lvl, 1);
      // Rotation is a no-op on a cell whose steps are already uniform; fall
      // back to a step perturbation so the alternate restatement really
      // differs.
      bool sameContour = var.units.size() == lvl.units.size();
      for (size_t k = 0; sameContour && k < lvl.units.size(); ++k)
        sameContour = (var.units[k].step == lvl.units[k].step);
      if (sameContour) var = figure_transforms::vary_steps(lvl, rng, 1);

      const int restatements = 1 << j;
      for (int r = 0; r < restatements; ++r) {
        c.figs.push_back((r % 2 == 0) ? lvl : var);
        c.anchors.push_back(cfg.climbStep * j);
      }
    }
    if (c.figs.empty()) return c;

    // Arrival: the last entry's final note is held.
    MelodicFigure& last = c.figs.back();
    if (!last.units.empty()) {
      last.units.back().duration =
          std::max(cfg.holdBeats, last.units.back().duration);
    }

    std::vector<const MelodicFigure*> ptrs;
    ptrs.reserve(c.figs.size());
    for (const auto& f : c.figs) ptrs.push_back(&f);
    c.conns = passage_anchors::connectors_for(ptrs, c.anchors);
    c.span = passage_anchors::predicted_span(scale, start, ptrs, c.conns);
    c.entries = int(c.figs.size());
    return c;
  };

  Candidate best;
  const int maxTries = std::max(1, cfg.maxTries);
  for (int t = 0; t < maxTries; ++t) {
    Candidate c = build_candidate(seed + uint32_t(t) * 7919u);
    if (c.figs.empty()) continue;
    if (best.figs.empty() || c.span < best.span) best = std::move(c);
    if (cfg.rangeCap <= 0 || best.span <= cfg.rangeCap) break;
  }
  if (best.figs.empty()) {
    std::cerr << "PedalBuildupStrategy: no candidate built; empty passage\n";
    return passage;
  }

  std::vector<std::string> refs;
  refs.reserve(best.figs.size());
  for (int i = 0; i < int(best.figs.size()); ++i) {
    std::string mn = "pedbuild_" + std::to_string(locus.sectionIdx) + "_"
                   + std::to_string(i);
    passage_anchors::put_motif(locus, mn, best.figs[i], seed);
    refs.push_back(mn);
  }

  std::cerr << "pedal_buildup: levels=" << best.levels
            << " entries=" << best.entries
            << " anchor_span=" << (best.anchors.back() - best.anchors.front())
            << " pred_range=" << best.span << "\n";

  passage.add_phrase(passage_anchors::compose_anchored_phrase(
      locus, "pedal_buildup", refs, best.conns, pt.startingPitch));
  return passage;
}

// ===========================================================================
// SequencePassageStrategy — "sequence_passage"
// ===========================================================================
//
// One cell restated on anchors that alternate stepA / stepB. The default
// (-4 / +3) is the diatonic circle-of-fifths trip: down a fifth, up a fourth,
// giving I-IV-vii-iii-vi-ii-V-I in scale-degree space. Other pairs give other
// sequences, which is the point of generalizing it rather than hard-coding
// the fifths walk.
//
// NOTE: this is a DIATONIC sequence, not a modulating one. Section
// keyContexts are inert in melody realization today (probed in run 12:
// Section::active_scale_at has zero callers), so a real modulating trip is
// stage 3 of the port, not something this class can express.
class SequencePassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "sequence_passage"; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) override;
};

inline Passage SequencePassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {
  Passage passage;
  const SequencePassageConfig cfg =
      pt.sequenceConfig ? *pt.sequenceConfig : SequencePassageConfig{};

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  Pitch start = pt.startingPitch ? *pt.startingPitch
                                 : Pitch::from_note_number(60.0f);
  uint32_t seed = passage_anchors::resolve_seed(locus, cfg.seed, 0x5345'5145u);

  const int n = std::max(2, cfg.entries);
  std::vector<int> anchors;
  anchors.reserve(n);
  anchors.push_back(0);
  for (int j = 1, a = 0; j < n; ++j) {
    a += (j % 2 == 1) ? cfg.stepA : cfg.stepB;
    anchors.push_back(a);
  }

  MelodicFigure bestCell;
  std::vector<std::optional<FigureConnector>> bestConns;
  int bestSpan = 0;
  const int maxTries = std::max(1, cfg.maxTries);
  for (int t = 0; t < maxTries; ++t) {
    Randomizer rng(seed + uint32_t(t) * 6151u);
    MelodicFigure cell;
    try {
      cell = passage_anchors::sample_cell(rng, cfg.cellBeats,
                                          {0.25f, 0.375f, 0.5f});
    } catch (const std::exception& e) {
      std::cerr << "SequencePassageStrategy: cell build failed: "
                << e.what() << "\n";
      continue;
    }
    if (cell.units.empty()) continue;

    std::vector<const MelodicFigure*> ptrs(n, &cell);
    auto conns = passage_anchors::connectors_for(ptrs, anchors);
    int span = passage_anchors::predicted_span(scale, start, ptrs, conns);
    if (bestCell.units.empty() || span < bestSpan) {
      bestCell = cell; bestConns = conns; bestSpan = span;
    }
    if (cfg.rangeCap <= 0 || bestSpan <= cfg.rangeCap) break;
  }
  if (bestCell.units.empty()) {
    std::cerr << "SequencePassageStrategy: no cell built; empty passage\n";
    return passage;
  }

  const std::string mn = "seqcell_" + std::to_string(locus.sectionIdx);
  passage_anchors::put_motif(locus, mn, bestCell, seed);
  std::vector<std::string> refs(n, mn);

  // Report the diatonic roots the anchors land on — the audible claim this
  // strategy makes ("it walks the fifths") is checkable from this line.
  static const char* DEG[] = {"I", "ii", "iii", "IV", "V", "vi", "vii"};
  std::string roots;
  const int len = scale.length() > 0 ? scale.length() : 7;
  for (int j = 0; j < n; ++j) {
    int d = ((anchors[j] % len) + len) % len;
    roots += (j ? "-" : "");
    roots += (d < 7 ? DEG[d] : std::to_string(d));
  }
  std::cerr << "sequence_passage: entries=" << n
            << " steps=" << cfg.stepA << "/" << cfg.stepB
            << " roots=" << roots
            << " pred_range=" << bestSpan << "\n";

  passage.add_phrase(passage_anchors::compose_anchored_phrase(
      locus, "sequence", refs, bestConns, pt.startingPitch));
  return passage;
}

// ===========================================================================
// ConnectivePassageStrategy — "connective_passage"  (stage 2)
// ===========================================================================
//
// The bridge between two thematic passages. Musically it is not new material:
// it takes the TAIL of what just finished, sequences it toward a destination,
// and broadens on arrival. The prototype's measurable claim was that it lands
// exactly on the requested degree (3/3 takes), and that is preserved here by
// backing the last anchor off by the cell's own net motion:
//     anchors.back() = targetDegree - net_step(finalCell)
// so anchors.back() + net_step(finalCell) == targetDegree by construction.
//
// This is the stage that needed "what came before". prior_passage_tail()
// above answers it without extending Locus.
class ConnectivePassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "connective_passage"; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) override;
};

inline Passage ConnectivePassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {
  Passage passage;
  const ConnectivePassageConfig cfg =
      pt.connectiveConfig ? *pt.connectiveConfig : ConnectivePassageConfig{};

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  Pitch start = pt.startingPitch ? *pt.startingPitch
                                 : Pitch::from_note_number(60.0f);
  uint32_t seed = passage_anchors::resolve_seed(locus, cfg.seed, 0x434F'4E4Eu);

  const int n = std::max(2, cfg.entries);

  // Anchors interpolate linearly from the passage start toward the target,
  // with the final entry backed off by its own net motion.
  auto anchors_for = [&](const MelodicFigure& finalCell) {
    std::vector<int> a;
    a.reserve(n);
    for (int j = 0; j < n; ++j)
      a.push_back(int(std::lround(double(cfg.targetDegree) * j / (n - 1))));
    a.back() = cfg.targetDegree - finalCell.net_step();
    return a;
  };

  MelodicFigure bestCell;
  std::vector<int> bestAnchors;
  std::vector<std::optional<FigureConnector>> bestConns;
  int bestSpan = 0;
  bool harvested = false;

  const int maxTries = std::max(1, cfg.maxTries);
  for (int t = 0; t < maxTries; ++t) {
    Randomizer rng(seed + uint32_t(t) * 5387u);
    MelodicFigure cell;
    if (auto tail = passage_anchors::prior_passage_tail(locus, cfg.tailUnits)) {
      cell = *tail;
      harvested = true;
    } else {
      // No prior passage (first section, or the part has nothing before it).
      // Fall back to a sampled cell so the strategy still produces a bridge
      // rather than silence — reported, not silently substituted.
      try {
        cell = passage_anchors::sample_cell(rng, cfg.cellBeats,
                                            {0.5f, 0.75f, 1.0f});
      } catch (const std::exception& e) {
        std::cerr << "ConnectivePassageStrategy: fallback cell failed: "
                  << e.what() << "\n";
        continue;
      }
    }
    if (cell.units.empty()) continue;

    MelodicFigure finalCell =
        figure_transforms::stretch(cell, std::max(0.01f, cfg.finalAugment));
    auto anchors = anchors_for(finalCell);

    std::vector<const MelodicFigure*> ptrs;
    ptrs.reserve(n);
    for (int j = 0; j < n - 1; ++j) ptrs.push_back(&cell);
    ptrs.push_back(&finalCell);
    auto conns = passage_anchors::connectors_for(ptrs, anchors);
    int span = passage_anchors::predicted_span(scale, start, ptrs, conns);

    if (bestCell.units.empty() || span < bestSpan) {
      bestCell = cell; bestAnchors = anchors; bestConns = conns; bestSpan = span;
    }
    // A harvested tail is FIXED — resampling cannot change it, so one pass is
    // all there is. Only the fallback path benefits from retries.
    if (harvested || cfg.rangeCap <= 0 || bestSpan <= cfg.rangeCap) break;
  }
  if (bestCell.units.empty()) {
    std::cerr << "ConnectivePassageStrategy: no cell; empty passage\n";
    return passage;
  }

  MelodicFigure finalCell =
      figure_transforms::stretch(bestCell, std::max(0.01f, cfg.finalAugment));
  const std::string base = "connect_" + std::to_string(locus.sectionIdx);
  passage_anchors::put_motif(locus, base + "_cell", bestCell, seed);
  passage_anchors::put_motif(locus, base + "_end", finalCell, seed);
  std::vector<std::string> refs(n - 1, base + "_cell");
  refs.push_back(base + "_end");

  const int landed = bestAnchors.back() + finalCell.net_step();
  std::cerr << "connective_passage: entries=" << n
            << " source=" << (harvested ? "prior-tail" : "sampled-fallback")
            << " cell_units=" << bestCell.units.size()
            << " target=" << cfg.targetDegree
            << " landed=" << landed
            << " pred_range=" << bestSpan << "\n";

  passage.add_phrase(passage_anchors::compose_anchored_phrase(
      locus, "connective", refs, bestConns, pt.startingPitch));
  return passage;
}

// ===========================================================================
// WanderingPassageStrategy — "wandering_passage"  (stage 4)
// ===========================================================================
//
// The discursive passage: every entry is a DISTINCT figure, the cursor runs
// continuously, and nothing returns. Drift is bounded by a soft register
// pull — when the cursor strays past +-pullThreshold degrees the next entry
// is nudged back — so it wanders without running off the instrument.
//
// It sits at the bottom of the repetition screen on purpose (the prototype
// measured selfsim 0.27, the lowest of the four). Whether "discursive" should
// still return to something is a taste question, queued rather than tuned.
class WanderingPassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "wandering_passage"; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) override;
};

inline Passage WanderingPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) {
  Passage passage;
  const WanderingPassageConfig cfg =
      pt.wanderingConfig ? *pt.wanderingConfig : WanderingPassageConfig{};

  const Scale& scale = locus.piece->sections[locus.sectionIdx].scale;
  Pitch start = pt.startingPitch ? *pt.startingPitch
                                 : Pitch::from_note_number(60.0f);
  uint32_t seed = passage_anchors::resolve_seed(locus, cfg.seed, 0x5741'4E44u);

  struct Candidate {
    std::vector<MelodicFigure> figs;
    std::vector<int> anchors;
    std::vector<std::optional<FigureConnector>> conns;
    int span{0};
  };

  auto build_candidate = [&](uint32_t trySeed) {
    Randomizer rng(trySeed);
    Candidate c;
    const int n = cfg.entries > 0 ? cfg.entries : rng.int_range(5, 7);
    int cursor = 0;
    for (int j = 0; j < n; ++j) {
      // Retry the CELL rather than abandoning the whole candidate. Every
      // entry is an independent draw and RandomFigureBuilder fails
      // occasionally, so a single-strike rule makes the failure probability
      // compound with the entry count: at cfg.entries=24 all 8 candidate
      // tries died and the passage rendered SILENT (peak=0). Found by the
      // outlier case, which is what outliers are for.
      MelodicFigure fig;
      for (int attempt = 0; attempt < 4 && fig.units.empty(); ++attempt) {
        try {
          fig = passage_anchors::sample_cell(rng, cfg.cellBeats,
                                             {0.375f, 0.5f, 0.75f});
        } catch (const std::exception& e) {
          if (attempt == 3) {
            std::cerr << "WanderingPassageStrategy: cell build failed after "
                      << "4 attempts: " << e.what() << "\n";
            return Candidate{};
          }
        }
      }
      if (fig.units.empty()) return Candidate{};
      if (j == 0) {
        c.anchors.push_back(0);
        cursor = fig.net_step();
      } else {
        int pull = (cursor > cfg.pullThreshold)  ? -cfg.pullAmount
                 : (cursor < -cfg.pullThreshold) ?  cfg.pullAmount
                                                 : rng.select_int({-1, 0, 1});
        c.anchors.push_back(cursor + pull);
        cursor = c.anchors.back() + fig.net_step();
      }
      c.figs.push_back(std::move(fig));
    }
    std::vector<const MelodicFigure*> ptrs;
    ptrs.reserve(c.figs.size());
    for (const auto& f : c.figs) ptrs.push_back(&f);
    c.conns = passage_anchors::connectors_for(ptrs, c.anchors);
    c.span = passage_anchors::predicted_span(scale, start, ptrs, c.conns);
    return c;
  };

  Candidate best;
  const int maxTries = std::max(1, cfg.maxTries);
  for (int t = 0; t < maxTries; ++t) {
    Candidate c = build_candidate(seed + uint32_t(t) * 4211u);
    if (c.figs.empty()) continue;
    if (best.figs.empty() || c.span < best.span) best = std::move(c);
    if (cfg.rangeCap <= 0 || best.span <= cfg.rangeCap) break;
  }
  if (best.figs.empty()) {
    std::cerr << "WanderingPassageStrategy: no candidate; empty passage\n";
    return passage;
  }

  std::vector<std::string> refs;
  refs.reserve(best.figs.size());
  for (int i = 0; i < int(best.figs.size()); ++i) {
    std::string mn = "wander_" + std::to_string(locus.sectionIdx) + "_"
                   + std::to_string(i);
    passage_anchors::put_motif(locus, mn, best.figs[i], seed);
    refs.push_back(mn);
  }

  const int lo = *std::min_element(best.anchors.begin(), best.anchors.end());
  const int hi = *std::max_element(best.anchors.begin(), best.anchors.end());
  std::cerr << "wandering_passage: entries=" << best.figs.size()
            << " distinct=" << refs.size()
            << " anchor_span=" << (hi - lo)
            << " pred_range=" << best.span << "\n";

  passage.add_phrase(passage_anchors::compose_anchored_phrase(
      locus, "wandering", refs, best.conns, pt.startingPitch));
  return passage;
}

} // namespace mforce

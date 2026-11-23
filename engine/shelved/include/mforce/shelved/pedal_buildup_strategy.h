#pragma once
#include "mforce/shelved/passage_anchors.h"

namespace mforce {

// Settings for PedalBuildupStrategy ("pedal_buildup"). The melody half of a
// pedal-point buildup: levels of EQUAL span with DOUBLING density, climbing
// `climbStep` scale-degrees per level, arrival note held. The pedal itself
// stays a template-authored part (a passage strategy cannot add parts).
struct PedalBuildupSettings {
    int levels{0};            // 0 = pick 3 or 4 at random
    int climbStep{2};         // degrees climbed per level (2 = a third)
    float cellBeats{3.0f};    // target length of the level-0 cell
    float holdBeats{2.0f};    // minimum duration of the arrival note
    int rangeCap{19};         // semitone span guard (corpus range_p90); 0 = off
    int maxTries{8};          // resamples allowed while over rangeCap
    uint32_t seed{0};         // 0 = derive from masterSeed + locus
};

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
  static constexpr const char* kName = "pedal_buildup";
  using Settings = PedalBuildupSettings;

  std::string name() const override { return kName; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) const override;
};

inline Passage PedalBuildupStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) const {
  Passage passage;
  const Settings cfg = settings_of<Settings>(pt.strategy);

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

} // namespace mforce

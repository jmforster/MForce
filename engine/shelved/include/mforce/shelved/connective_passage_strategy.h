#pragma once
#include "mforce/shelved/passage_anchors.h"

namespace mforce {

// Settings for ConnectivePassageStrategy ("connective_passage"). A bridge: the
// PREVIOUS passage's tail becomes the link cell, sequenced toward a target
// degree that the final note is guaranteed to land on.
struct ConnectivePassageSettings {
    int entries{3};           // sequenced restatements of the link cell
    int targetDegree{-2};     // degree the FINAL note lands on, rel. to the
                              // passage's startingPitch
    int tailUnits{3};         // units harvested from the prior passage's end
    float finalAugment{1.5f}; // the last entry broadens as it arrives
    float cellBeats{2.0f};    // fallback cell length when there is no prior
                              // passage to harvest (e.g. first section)
    int rangeCap{19};
    int maxTries{8};
    uint32_t seed{0};
};

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
  static constexpr const char* kName = "connective_passage";
  using Settings = ConnectivePassageSettings;

  std::string name() const override { return kName; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) const override;
};

inline Passage ConnectivePassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) const {
  Passage passage;
  const Settings cfg = settings_of<Settings>(pt.strategy);

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

} // namespace mforce

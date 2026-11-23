#pragma once
#include "mforce/shelved/passage_anchors.h"

namespace mforce {

// Settings for WanderingPassageStrategy ("wandering_passage"). The discursive
// passage: distinct figures, cursor-continuous, no motif returns. It sits
// DELIBERATELY at the bottom of the repetition screen — that is the shape,
// not a defect, and it is reported rather than tuned away.
struct WanderingPassageSettings {
    int entries{0};           // 0 = pick 5..7 at random
    int pullThreshold{5};     // cursor drift past +-this nudges the next entry
    int pullAmount{2};        // ...by this many degrees, back toward centre
    float cellBeats{2.0f};
    int rangeCap{19};
    int maxTries{8};
    uint32_t seed{0};
};

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
  static constexpr const char* kName = "wandering_passage";
  using Settings = WanderingPassageSettings;

  std::string name() const override { return kName; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) const override;
};

inline Passage WanderingPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) const {
  Passage passage;
  const Settings cfg = settings_of<Settings>(pt.strategy);

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

#pragma once
#include "mforce/shelved/passage_anchors.h"

namespace mforce {

// Settings for SequencePassageStrategy ("sequence_passage"). One cell restated
// on anchors that alternate stepA / stepB. The circle-of-fifths default
// (-4 / +3) walks I-IV-vii-iii-vi-ii-V-I in diatonic space; other pairs give
// other sequences (e.g. -1/0 = a descending stepwise sequence).
struct SequencePassageSettings {
    int entries{8};           // number of cell entries
    int stepA{-4};            // anchor delta applied on odd entries (down a 5th)
    int stepB{3};             // ...and on even entries (up a 4th)
    float cellBeats{1.5f};    // target length of the cell
    int rangeCap{19};
    int maxTries{8};
    uint32_t seed{0};
};

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
  static constexpr const char* kName = "sequence_passage";
  using Settings = SequencePassageSettings;

  std::string name() const override { return kName; }
  StrategyScope scope() const override { return StrategyScope::Melody; }

  Passage compose_passage(Locus locus, const PassageTemplate& pt) const override;
};

inline Passage SequencePassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& pt) const {
  Passage passage;
  const Settings cfg = settings_of<Settings>(pt.strategy);

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

} // namespace mforce

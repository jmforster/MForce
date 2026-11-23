#pragma once
#include "mforce/compose/strategy.h"
#include "mforce/compose/templates.h"
#include "mforce/music/structure.h"
#include "mforce/compose/piece_utils.h"
#include "mforce/compose/random_figure_builder.h"
#include "mforce/compose/figure_transforms.h"
#include <iostream>
#include <stdexcept>

namespace mforce {

// ===========================================================================
// TwoFigurePhraseSettings — base + transform phrase
// ===========================================================================

struct TwoFigurePhraseSettings {
    enum class Method { ByCount, ByLength, Singleton };
    Method method{Method::ByCount};
    int   count{4};        // used when method == ByCount
    float length{4.0f};    // used when method == ByLength

    Constraints constraints;

    // 0 means "use phraseTmpl.seed, else a deterministic default"
    uint32_t seed{0};

    // How figure 2 is derived from figure 1.
    TransformOp transform{TransformOp::Invert};
    int transformParam{0};
};

// ---------------------------------------------------------------------------
// TwoFigurePhraseStrategy — builds figure 1 via RFB, derives figure 2 via
// figure_transforms::apply, returns a two-figure Phrase. Ignores connectors,
// cadence, and MelodicFunction. Expects a TwoFigurePhraseConfig on the
// PhraseTemplate.
// ---------------------------------------------------------------------------
class TwoFigurePhraseStrategy : public PhraseStrategy {
public:
  static constexpr const char* kName = "two_figure_phrase";
  using Settings = TwoFigurePhraseSettings;

  std::string name() const override { return kName; }
  Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

inline Phrase TwoFigurePhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
  Phrase phrase;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
  }

  if (!phraseTmpl.strategy.settings.has_value()) {
    throw std::runtime_error("two_figure_phrase: given no settings");
  }
  if (!phraseTmpl.figures.empty()) {
    std::cerr << "TwoFigurePhraseStrategy: ignoring phraseTmpl.figures ("
              << phraseTmpl.figures.size()
              << " entries) — strategy uses its settings\n";
  }

  const Settings cfg = settings_of<Settings>(phraseTmpl.strategy);

  uint32_t seed = cfg.seed != 0 ? cfg.seed
                : phraseTmpl.seed != 0 ? phraseTmpl.seed
                : 0xF1F1F1F1u;
  RandomFigureBuilder rfb(seed);

  MelodicFigure fig1;
  switch (cfg.method) {
    case TwoFigurePhraseSettings::Method::ByCount:
      fig1 = rfb.build_by_count(cfg.count, cfg.constraints);
      break;
    case TwoFigurePhraseSettings::Method::ByLength:
      fig1 = rfb.build_by_length(cfg.length, cfg.constraints);
      break;
    case TwoFigurePhraseSettings::Method::Singleton:
      fig1 = rfb.build_singleton(cfg.constraints);
      break;
  }

  MelodicFigure fig2 = figure_transforms::apply(
      fig1, cfg.transform, cfg.transformParam, seed + 1);

  phrase.add_melodic_figure(std::move(fig1));
  phrase.add_melodic_figure(std::move(fig2));
  return phrase;
}

} // namespace mforce

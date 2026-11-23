#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/core/randomizer.h"
#include <iostream>
#include <stdexcept>

namespace mforce {

// ===========================================================================
// ElaboratedPhraseSettings — RFB-built (or literal) skeleton + per-anchor
// random Leave/Generate elaboration.
// ===========================================================================

struct ElaboratedPhraseSettings {
    // Optional literal skeleton. If absent, strategy builds via RFB.
    std::optional<MelodicFigure> skeleton;

    // RFB build spec (used when skeleton is absent).
    enum class Method { ByCount, ByLength, Singleton };
    Method buildMethod{Method::ByCount};
    int   buildCount{4};       // ByCount
    float buildLength{4.0f};   // ByLength
    Constraints buildConstraints;

    // Per-anchor choice mode. Random is the production default; AllLeave /
    // AllGenerate exist for testability and direct authoring.
    enum class ChoiceMode { Random, AllLeave, AllGenerate };
    ChoiceMode choiceMode{ChoiceMode::Random};

    // Constraints for RFB-generated elaboration figures (Generate path).
    Constraints generateConstraints;

    // 0 means "use phraseTmpl.seed, else a deterministic default".
    uint32_t seed{0};
};

// ---------------------------------------------------------------------------
// ElaboratedPhraseStrategy — Phase 1 of recursive elaboration.
//
// Builds a skeleton MelodicFigure (via RFB unless config supplies a literal),
// walks each anchor, and per anchor randomly chooses Leave (single-note
// figure) or Generate (RFB-built figure of matching duration). Populates
// phrase.figures and phrase.connectors per the leadStep math:
//   FC[0].leadStep = 0
//   FC[i>0].leadStep = skeleton.units[i].step − E_{i-1}.net_step
//
// Figures are NEVER mutated. The cursor advance happens at realize time via
// the FC.leadStep field carried on phrase.connectors.
//
// Spec: docs/superpowers/specs/2026-04-24-elaborated-phrase-strategy-design.md
// ---------------------------------------------------------------------------
class ElaboratedPhraseStrategy : public PhraseStrategy {
public:
    static constexpr const char* kName = "elaborated_phrase";
    using Settings = ElaboratedPhraseSettings;

    std::string name() const override { return kName; }
    Phrase compose_phrase(Locus locus, const PhraseTemplate& phraseTmpl) override;
};

inline Phrase ElaboratedPhraseStrategy::compose_phrase(
    Locus locus, const PhraseTemplate& phraseTmpl) {
    Phrase phrase;

    if (phraseTmpl.startingPitch) {
        phrase.startingPitch = *phraseTmpl.startingPitch;
    } else {
        phrase.startingPitch = ::mforce::piece_utils::pitch_before(locus);
    }

    if (!phraseTmpl.strategy.settings.has_value()) {
        throw std::runtime_error("elaborated_phrase: given no settings");
    }
    if (!phraseTmpl.figures.empty()) {
        std::cerr << "ElaboratedPhraseStrategy: ignoring phraseTmpl.figures ("
                  << phraseTmpl.figures.size()
                  << " entries) — strategy uses its settings\n";
    }

    const Settings cfg = settings_of<Settings>(phraseTmpl.strategy);

    uint32_t seed = cfg.seed != 0 ? cfg.seed
                  : phraseTmpl.seed != 0 ? phraseTmpl.seed
                  : 0xE1AB0AAFu;
    RandomFigureBuilder rfb(seed);
    Randomizer choiceRng(seed ^ 0xCAFEu);

    // Resolve skeleton: literal override beats RFB build spec.
    MelodicFigure skeleton;
    if (cfg.skeleton) {
        skeleton = *cfg.skeleton;
    } else {
        switch (cfg.buildMethod) {
            case ElaboratedPhraseSettings::Method::ByCount:
                skeleton = rfb.build_by_count(cfg.buildCount, cfg.buildConstraints);
                break;
            case ElaboratedPhraseSettings::Method::ByLength:
                skeleton = rfb.build_by_length(cfg.buildLength, cfg.buildConstraints);
                break;
            case ElaboratedPhraseSettings::Method::Singleton:
                skeleton = rfb.build_singleton(cfg.buildConstraints);
                break;
        }
    }

    if (skeleton.units.empty()) {
        std::cerr << "ElaboratedPhraseStrategy: skeleton is empty; returning "
                     "empty phrase\n";
        return phrase;
    }

    // Walk skeleton anchors. For each, decide Leave or Generate, build the
    // elaboration figure, compute the bridging FC.leadStep, append both.
    int prev_net_step = 0;  // E_{i-1}.net_step; 0 before any elaboration

    for (size_t i = 0; i < skeleton.units.size(); ++i) {
        const FigureUnit& anchor = skeleton.units[i];

        bool generate;
        switch (cfg.choiceMode) {
            case ElaboratedPhraseSettings::ChoiceMode::AllLeave:
                generate = false;
                break;
            case ElaboratedPhraseSettings::ChoiceMode::AllGenerate:
                generate = true;
                break;
            case ElaboratedPhraseSettings::ChoiceMode::Random:
            default:
                generate = choiceRng.decide(0.5f);
                break;
        }

        MelodicFigure E_i;
        if (generate) {
            // RFB.build_by_length throws if constraints.length is set; pass
            // a copy without that field. defaultPulse must be set for
            // resolve_count_ to compute count = length/defaultPulse and
            // produce a figure whose total duration matches anchor.duration.
            // Fall back to anchor.duration / 4 if author didn't supply one
            // (gives a 4-unit figure of the right total duration).
            Constraints gc = cfg.generateConstraints;
            gc.length.reset();
            if (!gc.defaultPulse) gc.defaultPulse = anchor.duration / 4.0f;
            E_i = rfb.build_by_length(anchor.duration, gc);
            // RFB output convention: units[0].step is 0. Defensive: ensure it.
            if (!E_i.units.empty()) E_i.units[0].step = 0;
        } else {
            // Leave: single-unit figure at anchor pitch (step=0 within figure).
            FigureUnit u{anchor.duration, 0};
            E_i.units.push_back(u);
        }

        // Compute leadStep: FC[0]=0; FC[i>0] = skel.step[i] − prev_net_step.
        int leadStep = (i == 0)
                       ? 0
                       : (skeleton.units[i].step - prev_net_step);

        FigureConnector fc{};
        fc.leadStep = leadStep;

        // Compute this elaboration's interior_net_step BEFORE moving E_i.
        int E_i_net_step = 0;
        for (size_t k = 0; k < E_i.units.size(); ++k) {
            E_i_net_step += E_i.units[k].step;
        }
        prev_net_step = E_i_net_step;

        phrase.add_melodic_figure_with_connector(std::move(E_i), fc);
    }

    return phrase;
}

} // namespace mforce

#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/piece_utils.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/core/randomizer.h"
#include <iostream>

namespace mforce {

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
    std::string name() const override { return "elaborated_phrase"; }
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

    if (!phraseTmpl.elaboratedConfig) {
        std::cerr << "ElaboratedPhraseStrategy: phraseTmpl.elaboratedConfig is "
                     "empty; returning empty phrase\n";
        return phrase;
    }
    if (!phraseTmpl.figures.empty()) {
        std::cerr << "ElaboratedPhraseStrategy: ignoring phraseTmpl.figures ("
                  << phraseTmpl.figures.size()
                  << " entries) — strategy uses elaboratedConfig\n";
    }

    const ElaboratedPhraseConfig& cfg = *phraseTmpl.elaboratedConfig;

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
            case ElaboratedPhraseConfig::Method::ByCount:
                skeleton = rfb.build_by_count(cfg.buildCount, cfg.buildConstraints);
                break;
            case ElaboratedPhraseConfig::Method::ByLength:
                skeleton = rfb.build_by_length(cfg.buildLength, cfg.buildConstraints);
                break;
            case ElaboratedPhraseConfig::Method::Singleton:
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
            case ElaboratedPhraseConfig::ChoiceMode::AllLeave:
                generate = false;
                break;
            case ElaboratedPhraseConfig::ChoiceMode::AllGenerate:
                generate = true;
                break;
            case ElaboratedPhraseConfig::ChoiceMode::Random:
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

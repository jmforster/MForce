#pragma once
//
// LibraryPassageStrategy — passage-level strategy that deploys a Pattern
// from a loaded PatternLibrary set.
//
// 1. Resolves a Pattern (by name from config, or by length).
// 2. Generates one RFB motif per unique slot letter; stores in piece motif pool.
// 3. Walks the Pattern's passage line: per step, builds a PhraseTemplate
//    referencing the slot motifs with synthesized FC values for `~` (random
//    leadStep) and dash-elision (`-`) markers, applies cadence per the
//    step's marker, composes the phrase via default_phrase, appends.
//
// Spec: docs/superpowers/specs/2026-04-27-pattern-library-design.md
// Registered as "library_passage".

#include "mforce/music/strategy.h"
#include "mforce/music/strategy_registry.h"
#include "mforce/music/templates.h"
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/default_strategies.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/figure_constraints.h"
#include "mforce/music/pattern_library.h"
#include "mforce/core/randomizer.h"

#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace mforce {

class LibraryPassageStrategy : public PassageStrategy {
public:
    std::string name() const override { return "library_passage"; }
    StrategyScope scope() const override { return StrategyScope::Melody; }

    Passage compose_passage(Locus locus, const PassageTemplate& pt) override;
};

namespace library_passage_detail {

// Per-position figure-unit count under the Q2 "last-of-three doubles" rule.
inline int unit_count_for(int figureIndex, int phraseFigCount) {
    if (phraseFigCount == 3 && figureIndex == 2) return 2;
    return 1;
}

inline std::vector<pattern_library::PatternLibrary>& cached_libraries() {
    static std::vector<pattern_library::PatternLibrary> libs =
        pattern_library::load_libraries("lib/json");
    return libs;
}

} // namespace library_passage_detail

inline Passage LibraryPassageStrategy::compose_passage(
    Locus locus, const PassageTemplate& passTmpl) {
    Passage passage;

    if (!passTmpl.libraryConfig) {
        std::cerr << "LibraryPassageStrategy: passTmpl.libraryConfig is empty; "
                     "returning empty passage\n";
        return passage;
    }
    const LibraryPassageConfig& cfg = *passTmpl.libraryConfig;

    auto& libs = library_passage_detail::cached_libraries();
    if (libs.empty()) {
        std::cerr << "LibraryPassageStrategy: no libraries loaded from lib/json\n";
        return passage;
    }

    // Resolve seed.
    uint32_t seed = cfg.seed != 0
        ? cfg.seed
        : (locus.pieceTemplate->masterSeed
           ^ (uint32_t(locus.sectionIdx) * 1031u
              + uint32_t(locus.partIdx) * 17u));

    // Resolve Pattern.
    Randomizer pickRng(seed ^ 0x4C49'4250u);  // 'LIBP'
    const pattern_library::Pattern* pattern = nullptr;
    if (!cfg.patternName.empty()) {
        pattern = pattern_library::find_pattern_by_name(libs, cfg.patternName);
    } else if (cfg.barsHint > 0) {
        pattern = pattern_library::pick_pattern_by_length(libs, cfg.barsHint, pickRng);
    }
    if (!pattern) {
        std::cerr << "LibraryPassageStrategy: no pattern matched ("
                  << (cfg.patternName.empty() ? "by length" : cfg.patternName)
                  << ")\n";
        return passage;
    }

    // Compute beat-per-unit budget.
    int totalUnits = pattern->total_units();
    if (totalUnits <= 0 || pattern->bars <= 0) {
        std::cerr << "LibraryPassageStrategy: pattern has zero units/bars\n";
        return passage;
    }
    const float beat_per_unit = float(pattern->bars * 4) / float(totalUnits);

    // Walk all phrase definitions to discover unique slots and their unit
    // counts (taken from first occurrence). Slots with inconsistent unit
    // counts across positions get their first-seen value (warned only).
    std::set<char> slots;
    std::map<char, int> slotUnits;
    bool warnedPrime = false;
    for (const auto& pdef : pattern->phrases) {
        const int n = int(pdef.figures.size());
        for (int i = 0; i < n; ++i) {
            const auto& tok = pdef.figures[i];
            if (tok.modifiedDepth > 0 && !warnedPrime) {
                std::cerr << "LibraryPassageStrategy: prime tokens (modifiedDepth>0) "
                             "treated as ~ for Phase 1; actual modification deferred\n";
                warnedPrime = true;
            }
            char s = tok.slot;
            if (!slots.count(s)) {
                slots.insert(s);
                slotUnits[s] = library_passage_detail::unit_count_for(i, n);
            }
        }
    }

    // Generate slot motifs (idempotent if already in pool — guards against
    // multiple compose calls reusing the same motif names).
    //
    // Generation: RandomFigureBuilder.build_by_length with a randomly-chosen
    // pulseHint. PulseGenerator (under RFB) produces non-uniform standard
    // durations summing to the target length. Different pulseHints across
    // motifs gives real density variety (motif lengths from 2 to ~8 notes).
    //
    // Bias toward shorter / less-busy cells: pulseHint drawn from
    // {1.0, 1.5, 2.0} which on a 4-beat budget yields ~2-4 notes per cell,
    // and on an 8-beat budget yields ~4-8 notes. Avoids the "8 sixteenth
    // notes is a melody, not a motif" failure mode.
    Randomizer slotRng(seed ^ 0x4C53'4C54u);  // 'LSLT'
    static constexpr float PULSE_HINTS[] = {1.0f, 1.5f, 2.0f};
    for (char s : slots) {
        std::string motifName = pattern->name + "_" + std::string(1, s);
        if (locus.pieceTemplate->realizedMotifs.count(motifName)) continue;

        uint32_t motifSeed = slotRng.rng();
        Randomizer perMotifRng(motifSeed);
        float pulseHint = PULSE_HINTS[perMotifRng.int_range(0, 2)];

        Constraints constraints;
        constraints.defaultPulse = pulseHint;

        RandomFigureBuilder rfb(perMotifRng.rng());
        float length = float(slotUnits[s]) * beat_per_unit;
        MelodicFigure fig;
        try {
            fig = rfb.build_by_length(length, constraints);
        } catch (const std::exception& e) {
            std::cerr << "LibraryPassageStrategy: RFB build_by_length failed for slot "
                      << s << " (length=" << length << ", pulse=" << pulseHint
                      << "): " << e.what() << "\n";
            continue;
        }
        // Defensive: enforce step[0]=0 convention.
        if (!fig.units.empty()) fig.units[0].step = 0;

        Motif m;
        m.name = motifName;
        m.userProvided = false;
        m.content = fig;
        m.origin = MotifOrigin::Generated;
        m.generationSeed = motifSeed;
        locus.pieceTemplate->add_motif(std::move(m));
    }

    // Walk passage and build phrases.
    Randomizer anchorRng(seed ^ 0x4C414E43u);  // 'LANC'
    int phraseIdx = 0;
    for (const auto& step : pattern->passage) {
        const pattern_library::PhraseDef* pdef =
            pattern->phrase_by_index(step.phraseIndex);
        if (!pdef) {
            std::cerr << "LibraryPassageStrategy: phrase index "
                      << step.phraseIndex << " not found in pattern '"
                      << pattern->name << "'\n";
            continue;
        }

        PhraseTemplate phraseTmpl;
        phraseTmpl.name = pattern->name + "_p" + std::to_string(phraseIdx);
        const int n = int(pdef->figures.size());
        phraseTmpl.figures.reserve(n);
        phraseTmpl.connectors.reserve(n);

        for (int i = 0; i < n; ++i) {
            const auto& tok = pdef->figures[i];

            FigureTemplate ft;
            ft.source = FigureSource::Reference;
            ft.motifName = pattern->name + "_" + std::string(1, tok.slot);
            ft.totalBeats =
                float(library_passage_detail::unit_count_for(i, n)) * beat_per_unit;
            phraseTmpl.figures.push_back(ft);

            // Connector i is the connector AT figure i (the join INTO it).
            // - newAnchor or modifiedDepth>0 (Phase 1: same effect): random leadStep.
            // - elideAfter on the PREVIOUS figure → this connector elides the
            //   previous figure's last `elideAfter` units.
            FigureConnector fc;
            if (i > 0 && (tok.newAnchor || tok.modifiedDepth > 0)) {
                fc.leadStep = anchorRng.select_int({-3, -2, -1, 1, 2, 3});
            }
            if (i > 0) {
                int prev_elide = pdef->figures[i - 1].elideAfter;
                if (prev_elide > 0) fc.elideCount = prev_elide;
            }
            phraseTmpl.connectors.push_back(fc);
        }

        // Apply cadence per the step's marker.
        if (step.cadence == pattern_library::CadenceMarker::Half) {
            phraseTmpl.cadenceType = 1;
            phraseTmpl.cadenceTarget = 4;  // V
        } else if (step.cadence == pattern_library::CadenceMarker::Full) {
            phraseTmpl.cadenceType = 2;
            phraseTmpl.cadenceTarget = 0;  // I
        }

        // Compose this phrase via default_phrase.
        Locus phraseLocus = locus.with_phrase(phraseIdx);
        PhraseStrategy* ps =
            StrategyRegistry::instance().resolve_phrase("default_phrase");
        if (!ps) {
            std::cerr << "LibraryPassageStrategy: default_phrase not registered\n";
            return passage;
        }
        Phrase ph = ps->compose_phrase(phraseLocus, phraseTmpl);
        passage.add_phrase(std::move(ph));
        ++phraseIdx;
    }

    return passage;
}

} // namespace mforce

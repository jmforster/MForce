#pragma once
//
// pattern_library — engine-side types for compiled Passage Pattern Libraries.
//
// PatternLibrary owns a list of Patterns, each describing a passage-level
// structural recipe (phrase definitions, deployment sequence, cadence
// markers). Loaded from lib/json/*.json — never directly from .ppl files;
// the standalone tools/shelved/ppl_to_json tool handles .ppl parsing offline.
//
// Spec: docs/superpowers/specs/2026-04-27-pattern-library-design.md

#include "mforce/core/randomizer.h"
#include <string>
#include <vector>

namespace mforce::pattern_library {

enum class CadenceMarker { None, Half, Full };

struct FigureToken {
    char slot{'A'};            // 'A'..'Z'
    bool newAnchor{false};     // ~ marker → random leadStep at compose time
    int  modifiedDepth{0};     // 0 = unchanged, 1 = ', 2 = '' (Phase 1: warned + treated as newAnchor)
    int  elideAfter{0};        // dash count → FC.elideCount on the connector AFTER this figure
};

struct PhraseDef {
    int                       index{1};
    std::vector<FigureToken>  figures;
};

struct PassageStep {
    int            phraseIndex{1};
    CadenceMarker  cadence{CadenceMarker::None};
};

struct Pattern {
    std::string                 name;
    int                         bars{0};
    std::vector<std::string>    roles;     // Phase 1: ignored
    std::vector<PhraseDef>      phrases;
    std::vector<PassageStep>    passage;

    // Convenience: find a phrase definition by its 1-based index.
    const PhraseDef* phrase_by_index(int idx) const {
        for (const auto& p : phrases) if (p.index == idx) return &p;
        return nullptr;
    }

    // Compute the figure-unit count for a phrase, applying the last-of-three
    // doubling rule (Q2 option D from the spec).
    static int phrase_units(const PhraseDef& p) {
        int n = int(p.figures.size());
        if (n == 3) return 4;  // 1 + 1 + 2
        return n;
    }

    // Total figure-units across all passage deployments.
    int total_units() const {
        int total = 0;
        for (const auto& step : passage) {
            const PhraseDef* p = phrase_by_index(step.phraseIndex);
            if (!p) continue;
            total += phrase_units(*p);
        }
        return total;
    }
};

struct PatternLibrary {
    std::string                 name;
    std::vector<std::string>    genres;    // Phase 1: ignored
    std::vector<std::string>    roles;     // Phase 1: ignored
    std::vector<Pattern>        patterns;
};


// Load all *.json files from a directory; merge into one library list.
// Errors on individual files are reported to stderr but don't abort the whole
// load. Defined in pattern_library_json.cpp.
std::vector<PatternLibrary> load_libraries(const std::string& dirPath);

// Find a Pattern by name across all loaded libraries (first match wins).
inline const Pattern* find_pattern_by_name(
        const std::vector<PatternLibrary>& libs, const std::string& name) {
    for (const auto& lib : libs) {
        for (const auto& p : lib.patterns) {
            if (p.name == name) return &p;
        }
    }
    return nullptr;
}

// Pick a Pattern by length (bars) — random across matching patterns. Returns
// nullptr if none match.
inline const Pattern* pick_pattern_by_length(
        const std::vector<PatternLibrary>& libs, int bars, Randomizer& rng) {
    std::vector<const Pattern*> matches;
    for (const auto& lib : libs) {
        for (const auto& p : lib.patterns) {
            if (p.bars == bars) matches.push_back(&p);
        }
    }
    if (matches.empty()) return nullptr;
    int idx = rng.int_range(0, int(matches.size()) - 1);
    return matches[idx];
}

} // namespace mforce::pattern_library

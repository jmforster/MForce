#pragma once
//
// pattern_library — engine-side types for compiled Passage Pattern Libraries.
//
// PatternLibrary owns a list of Patterns, each describing a passage-level
// structural recipe (phrase definitions, deployment sequence, cadence
// markers). Loaded from lib/json/*.json — never directly from .ppl files;
// the standalone tools/ppl_to_json tool handles .ppl parsing offline.
//
// Spec: docs/superpowers/specs/2026-04-27-pattern-library-design.md

#include "mforce/core/randomizer.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
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

// -----------------------------------------------------------------------------
// JSON loader
// -----------------------------------------------------------------------------

inline FigureToken figure_token_from_json(const nlohmann::json& j) {
    FigureToken t;
    if (j.contains("slot")) {
        std::string s = j.at("slot").get<std::string>();
        if (!s.empty()) t.slot = s[0];
    }
    t.newAnchor     = j.value("newAnchor", false);
    t.modifiedDepth = j.value("modifiedDepth", 0);
    t.elideAfter    = j.value("elideAfter", 0);
    return t;
}

inline PhraseDef phrase_def_from_json(const nlohmann::json& j) {
    PhraseDef p;
    p.index = j.value("index", 1);
    if (j.contains("figures")) {
        for (const auto& fj : j.at("figures")) {
            p.figures.push_back(figure_token_from_json(fj));
        }
    }
    return p;
}

inline PassageStep passage_step_from_json(const nlohmann::json& j) {
    PassageStep s;
    s.phraseIndex = j.value("phrase", 1);
    std::string cad = j.value("cadence", std::string("none"));
    if      (cad == "half") s.cadence = CadenceMarker::Half;
    else if (cad == "full") s.cadence = CadenceMarker::Full;
    else                    s.cadence = CadenceMarker::None;
    return s;
}

inline Pattern pattern_from_json(const nlohmann::json& j) {
    Pattern p;
    p.name = j.value("name", std::string(""));
    p.bars = j.value("bars", 0);
    if (j.contains("roles") && j.at("roles").is_array()) {
        for (const auto& r : j.at("roles")) p.roles.push_back(r.get<std::string>());
    }
    if (j.contains("phrases") && j.at("phrases").is_array()) {
        for (const auto& pj : j.at("phrases")) p.phrases.push_back(phrase_def_from_json(pj));
    }
    if (j.contains("passage") && j.at("passage").is_array()) {
        for (const auto& sj : j.at("passage")) p.passage.push_back(passage_step_from_json(sj));
    }
    return p;
}

inline PatternLibrary library_from_json(const nlohmann::json& j) {
    PatternLibrary lib;
    lib.name = j.value("name", std::string(""));
    if (j.contains("genres") && j.at("genres").is_array()) {
        for (const auto& g : j.at("genres")) lib.genres.push_back(g.get<std::string>());
    }
    if (j.contains("roles") && j.at("roles").is_array()) {
        for (const auto& r : j.at("roles")) lib.roles.push_back(r.get<std::string>());
    }
    if (j.contains("patterns") && j.at("patterns").is_array()) {
        for (const auto& pj : j.at("patterns")) lib.patterns.push_back(pattern_from_json(pj));
    }
    return lib;
}

// Load all *.json files from a directory; merge into one library list.
// Errors on individual files are reported to stderr but don't abort the whole load.
inline std::vector<PatternLibrary> load_libraries(const std::string& dirPath) {
    std::vector<PatternLibrary> out;
    namespace fs = std::filesystem;
    if (!fs::exists(dirPath) || !fs::is_directory(dirPath)) return out;
    for (const auto& ent : fs::directory_iterator(dirPath)) {
        if (!ent.is_regular_file()) continue;
        if (ent.path().extension() != ".json") continue;
        try {
            std::ifstream in(ent.path());
            nlohmann::json j;
            in >> j;
            out.push_back(library_from_json(j));
        } catch (const std::exception& e) {
            std::cerr << "pattern_library: failed to load " << ent.path()
                      << ": " << e.what() << "\n";
        }
    }
    return out;
}

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

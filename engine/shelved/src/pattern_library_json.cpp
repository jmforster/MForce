// The pattern library's file form: lib/json/*.json, compiled offline from
// .ppl files by tools/shelved/ppl_to_json.
#include "mforce/shelved/pattern_library.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace mforce::pattern_library {

namespace {

// -----------------------------------------------------------------------------
// JSON loader
// -----------------------------------------------------------------------------

FigureToken figure_token_from_json(const nlohmann::json& j) {
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

PhraseDef phrase_def_from_json(const nlohmann::json& j) {
    PhraseDef p;
    p.index = j.value("index", 1);
    if (j.contains("figures")) {
        for (const auto& fj : j.at("figures")) {
            p.figures.push_back(figure_token_from_json(fj));
        }
    }
    return p;
}

PassageStep passage_step_from_json(const nlohmann::json& j) {
    PassageStep s;
    s.phraseIndex = j.value("phrase", 1);
    std::string cad = j.value("cadence", std::string("none"));
    if      (cad == "half") s.cadence = CadenceMarker::Half;
    else if (cad == "full") s.cadence = CadenceMarker::Full;
    else                    s.cadence = CadenceMarker::None;
    return s;
}

Pattern pattern_from_json(const nlohmann::json& j) {
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

PatternLibrary library_from_json(const nlohmann::json& j) {
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

} // namespace

std::vector<PatternLibrary> load_libraries(const std::string& dirPath) {
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

} // namespace mforce::pattern_library

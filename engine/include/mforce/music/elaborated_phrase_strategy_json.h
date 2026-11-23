#pragma once
// The elaborated phrase strategy's settings, to and from JSON.
#include "mforce/music/elaborated_phrase_strategy.h"
#include "mforce/music/templates_json.h"

namespace mforce {

// ===========================================================================
// ElaboratedPhraseSettings
// ===========================================================================

inline void to_json(json& j, ElaboratedPhraseSettings::Method m) {
    switch (m) {
        case ElaboratedPhraseSettings::Method::ByCount:   j = "by_count";   break;
        case ElaboratedPhraseSettings::Method::ByLength:  j = "by_length";  break;
        case ElaboratedPhraseSettings::Method::Singleton: j = "singleton";  break;
    }
}

inline void from_json(const json& j, ElaboratedPhraseSettings::Method& m) {
    const std::string s = j.get<std::string>();
    if      (s == "by_count")  m = ElaboratedPhraseSettings::Method::ByCount;
    else if (s == "by_length") m = ElaboratedPhraseSettings::Method::ByLength;
    else if (s == "singleton") m = ElaboratedPhraseSettings::Method::Singleton;
    else throw std::runtime_error("Unknown ElaboratedPhraseSettings::Method: " + s);
}

inline void to_json(json& j, ElaboratedPhraseSettings::ChoiceMode m) {
    switch (m) {
        case ElaboratedPhraseSettings::ChoiceMode::Random:      j = "random";       break;
        case ElaboratedPhraseSettings::ChoiceMode::AllLeave:    j = "all_leave";    break;
        case ElaboratedPhraseSettings::ChoiceMode::AllGenerate: j = "all_generate"; break;
    }
}

inline void from_json(const json& j, ElaboratedPhraseSettings::ChoiceMode& m) {
    const std::string s = j.get<std::string>();
    if      (s == "random")       m = ElaboratedPhraseSettings::ChoiceMode::Random;
    else if (s == "all_leave")    m = ElaboratedPhraseSettings::ChoiceMode::AllLeave;
    else if (s == "all_generate") m = ElaboratedPhraseSettings::ChoiceMode::AllGenerate;
    else throw std::runtime_error("Unknown ElaboratedPhraseSettings::ChoiceMode: " + s);
}

inline void to_json(json& j, const ElaboratedPhraseSettings& c) {
    j = json::object();
    if (c.skeleton) j["skeleton"] = *c.skeleton;
    j["buildMethod"]      = c.buildMethod;
    if (c.buildCount != 4)     j["buildCount"]  = c.buildCount;
    if (c.buildLength != 4.0f) j["buildLength"] = c.buildLength;
    j["buildConstraints"] = c.buildConstraints;
    j["choiceMode"]       = c.choiceMode;
    j["generateConstraints"] = c.generateConstraints;
    if (c.seed != 0) j["seed"] = c.seed;
}

inline void from_json(const json& j, ElaboratedPhraseSettings& c) {
    if (j.contains("skeleton")) {
        MelodicFigure mf;
        from_json(j.at("skeleton"), mf);
        c.skeleton = std::move(mf);
    }
    if (j.contains("buildMethod"))         c.buildMethod = j.at("buildMethod").get<ElaboratedPhraseSettings::Method>();
    c.buildCount  = j.value("buildCount", 4);
    c.buildLength = j.value("buildLength", 4.0f);
    if (j.contains("buildConstraints"))    from_json(j.at("buildConstraints"), c.buildConstraints);
    if (j.contains("choiceMode"))          c.choiceMode = j.at("choiceMode").get<ElaboratedPhraseSettings::ChoiceMode>();
    if (j.contains("generateConstraints")) from_json(j.at("generateConstraints"), c.generateConstraints);
    c.seed = j.value("seed", 0u);
}

} // namespace mforce

#pragma once
// The two-figure phrase strategy's settings, to and from JSON.
#include "mforce/music/two_figure_phrase_strategy.h"
#include "mforce/music/templates_json.h"

namespace mforce {

// ===========================================================================
// TwoFigurePhraseSettings
// ===========================================================================

inline void to_json(json& j, const TwoFigurePhraseSettings& c) {
    switch (c.method) {
        case TwoFigurePhraseSettings::Method::ByCount:   j["method"] = "by_count";   break;
        case TwoFigurePhraseSettings::Method::ByLength:  j["method"] = "by_length";  break;
        case TwoFigurePhraseSettings::Method::Singleton: j["method"] = "singleton";  break;
    }
    if (c.count != 4)     j["count"]  = c.count;
    if (c.length != 4.0f) j["length"] = c.length;
    j["constraints"] = c.constraints;
    if (c.seed != 0) j["seed"] = c.seed;
    j["transform"] = c.transform;
    if (c.transformParam != 0) j["transformParam"] = c.transformParam;
}

inline void from_json(const json& j, TwoFigurePhraseSettings& c) {
    auto methodStr = j.value("method", std::string("by_count"));
    if      (methodStr == "by_count")   c.method = TwoFigurePhraseSettings::Method::ByCount;
    else if (methodStr == "by_length")  c.method = TwoFigurePhraseSettings::Method::ByLength;
    else if (methodStr == "singleton")  c.method = TwoFigurePhraseSettings::Method::Singleton;
    else                                c.method = TwoFigurePhraseSettings::Method::ByCount;
    c.count  = j.value("count", 4);
    c.length = j.value("length", 4.0f);
    if (j.contains("constraints")) from_json(j.at("constraints"), c.constraints);
    c.seed = j.value("seed", 0u);
    if (j.contains("transform")) c.transform = j.at("transform").get<TransformOp>();
    c.transformParam = j.value("transformParam", 0);
}

} // namespace mforce

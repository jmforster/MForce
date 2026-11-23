#pragma once
// The rhythm_pattern realization strategy's settings, to and from JSON.
#include "mforce/compose/realization_strategy.h"
#include <nlohmann/json.hpp>

namespace mforce {

// ===========================================================================
// RhythmPattern
// ===========================================================================

inline void to_json(nlohmann::json& jrp, const RhythmPattern& rp) {
    jrp = nlohmann::json::object();
    jrp["defaultPattern"] = rp.defaultPattern;
    if (!rp.overrides.empty()) {
        nlohmann::json jOvs = nlohmann::json::array();
        for (const auto& ov : rp.overrides) {
            nlohmann::json jOv;
            jOv["bars"] = ov.bars;
            jOv["pattern"] = ov.pattern;
            jOvs.push_back(std::move(jOv));
        }
        jrp["overrides"] = jOvs;
    }
}

inline void from_json(const nlohmann::json& jrp, RhythmPattern& rp) {
    if (jrp.contains("defaultPattern")) {
        rp.defaultPattern.clear();
        for (auto& v : jrp["defaultPattern"]) rp.defaultPattern.push_back(v.get<float>());
    }
    if (jrp.contains("overrides")) {
        for (auto& ov : jrp["overrides"]) {
            RhythmPattern::BarOverride bo;
            for (auto& b : ov["bars"]) bo.bars.push_back(b.get<int>());
            for (auto& v : ov["pattern"]) bo.pattern.push_back(v.get<float>());
            rp.overrides.push_back(std::move(bo));
        }
    }
}

} // namespace mforce

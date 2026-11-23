#pragma once
// The library passage strategy's settings, to and from JSON.
#include "mforce/music/library_passage_strategy.h"
#include "mforce/music/templates_json.h"
#include <string>

namespace mforce {

// ===========================================================================
// LibraryPassageSettings
// ===========================================================================

inline void to_json(json& j, const LibraryPassageSettings& c) {
    j = json::object();
    if (!c.patternName.empty()) j["patternName"] = c.patternName;
    if (c.barsHint != 0) j["barsHint"] = c.barsHint;
    if (c.seed != 0) j["seed"] = c.seed;
}

inline void from_json(const json& j, LibraryPassageSettings& c) {
    c.patternName = j.value("patternName", std::string(""));
    c.barsHint    = j.value("barsHint", 0);
    c.seed        = j.value("seed", 0u);
}

} // namespace mforce

#pragma once
// The wandering passage strategy's settings, to and from JSON. Every field is
// written; a field absent when read keeps its default.
#include "mforce/shelved/wandering_passage_strategy.h"
#include "mforce/compose/templates_json.h"

namespace mforce {

inline void to_json(json& j, const WanderingPassageSettings& c) {
    j = json{
        {"entries", c.entries}, {"pullThreshold", c.pullThreshold},
        {"pullAmount", c.pullAmount}, {"cellBeats", c.cellBeats},
        {"rangeCap", c.rangeCap}, {"maxTries", c.maxTries},
        {"seed", c.seed}};
}

inline void from_json(const json& jc, WanderingPassageSettings& c) {
    c.entries       = jc.value("entries", c.entries);
    c.pullThreshold = jc.value("pullThreshold", c.pullThreshold);
    c.pullAmount    = jc.value("pullAmount", c.pullAmount);
    c.cellBeats     = jc.value("cellBeats", c.cellBeats);
    c.rangeCap      = jc.value("rangeCap", c.rangeCap);
    c.maxTries      = jc.value("maxTries", c.maxTries);
    c.seed          = jc.value("seed", c.seed);
}

} // namespace mforce

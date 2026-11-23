#pragma once
// The pedal buildup strategy's settings, to and from JSON. Every field is
// written; a field absent when read keeps its default.
#include "mforce/shelved/pedal_buildup_strategy.h"
#include "mforce/compose/templates_json.h"

namespace mforce {

inline void to_json(json& j, const PedalBuildupSettings& c) {
    j = json{
        {"levels", c.levels}, {"climbStep", c.climbStep},
        {"cellBeats", c.cellBeats}, {"holdBeats", c.holdBeats},
        {"rangeCap", c.rangeCap}, {"maxTries", c.maxTries},
        {"seed", c.seed}};
}

inline void from_json(const json& jc, PedalBuildupSettings& c) {
    c.levels    = jc.value("levels", c.levels);
    c.climbStep = jc.value("climbStep", c.climbStep);
    c.cellBeats = jc.value("cellBeats", c.cellBeats);
    c.holdBeats = jc.value("holdBeats", c.holdBeats);
    c.rangeCap  = jc.value("rangeCap", c.rangeCap);
    c.maxTries  = jc.value("maxTries", c.maxTries);
    c.seed      = jc.value("seed", c.seed);
}

} // namespace mforce

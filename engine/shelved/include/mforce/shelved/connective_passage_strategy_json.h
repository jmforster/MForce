#pragma once
// The connective passage strategy's settings, to and from JSON. Every field is
// written; a field absent when read keeps its default.
#include "mforce/shelved/connective_passage_strategy.h"
#include "mforce/compose/templates_json.h"

namespace mforce {

inline void to_json(json& j, const ConnectivePassageSettings& c) {
    j = json{
        {"entries", c.entries}, {"targetDegree", c.targetDegree},
        {"tailUnits", c.tailUnits}, {"finalAugment", c.finalAugment},
        {"cellBeats", c.cellBeats}, {"rangeCap", c.rangeCap},
        {"maxTries", c.maxTries}, {"seed", c.seed}};
}

inline void from_json(const json& jc, ConnectivePassageSettings& c) {
    c.entries      = jc.value("entries", c.entries);
    c.targetDegree = jc.value("targetDegree", c.targetDegree);
    c.tailUnits    = jc.value("tailUnits", c.tailUnits);
    c.finalAugment = jc.value("finalAugment", c.finalAugment);
    c.cellBeats    = jc.value("cellBeats", c.cellBeats);
    c.rangeCap     = jc.value("rangeCap", c.rangeCap);
    c.maxTries     = jc.value("maxTries", c.maxTries);
    c.seed         = jc.value("seed", c.seed);
}

} // namespace mforce

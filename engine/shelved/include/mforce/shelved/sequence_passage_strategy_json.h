#pragma once
// The sequence passage strategy's settings, to and from JSON. Every field is
// written; a field absent when read keeps its default.
#include "mforce/shelved/sequence_passage_strategy.h"
#include "mforce/compose/templates_json.h"

namespace mforce {

inline void to_json(json& j, const SequencePassageSettings& c) {
    j = json{
        {"entries", c.entries}, {"stepA", c.stepA}, {"stepB", c.stepB},
        {"cellBeats", c.cellBeats}, {"rangeCap", c.rangeCap},
        {"maxTries", c.maxTries}, {"seed", c.seed}};
}

inline void from_json(const json& jc, SequencePassageSettings& c) {
    c.entries   = jc.value("entries", c.entries);
    c.stepA     = jc.value("stepA", c.stepA);
    c.stepB     = jc.value("stepB", c.stepB);
    c.cellBeats = jc.value("cellBeats", c.cellBeats);
    c.rangeCap  = jc.value("rangeCap", c.rangeCap);
    c.maxTries  = jc.value("maxTries", c.maxTries);
    c.seed      = jc.value("seed", c.seed);
}

} // namespace mforce

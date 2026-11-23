#pragma once
// The anchor-driven passage strategies' settings, to and from JSON.
#include "mforce/music/passage_strategies.h"
#include "mforce/music/templates_json.h"

namespace mforce {

// ===========================================================================
// PedalBuildupSettings, SequencePassageSettings, ConnectivePassageSettings,
// WanderingPassageSettings. Every field is written; a field absent when
// read keeps its default.
// ===========================================================================

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

#include "mforce/testing/check.h"
#include "mforce/voicing/voicing_json.h"
#include "mforce/music/music_json.h"
#include "mforce/voicing/drift_voicing_profile_selector.h"
#include "mforce/voicing/random_voicing_profile_selector.h"
#include "mforce/voicing/scripted_voicing_profile_selector.h"
#include "mforce/voicing/voicing_profile.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <vector>

using namespace mforce;


// The profile `p` as the capture printed it: priority, then the two lists.
static bool profile_is(const VoicingProfile& p, float priority, std::vector<int> inv,
                       std::vector<int> spr) {
    return p.priority == priority && p.allowedInversions == inv && p.allowedSpreads == spr;
}

void run_profile_selector_capture_tests() {
    std::printf("profile selectors: the profiles captured before typed settings\n");
    using V = std::vector<int>;
    RandomVoicingProfileSelector random(json::parse(
        R"({"priorityMin": 0.2, "priorityMax": 0.8, "inversionProfiles": [[0], [0, 1], [1, 2]], "spreadProfiles": [[0], [0, 1]]})")
        .get<RandomProfileSettings>());
    DriftVoicingProfileSelector drift(json::parse(
        R"({"priorityMin": 0.8, "priorityMax": 0.1, "priorityStepMax": 0.3, "profileTransitionProb": 0.5, "inversionProfiles": [[0], [0, 1], [1, 2]], "spreadProfiles": [[0], [0, 1]]})")
        .get<DriftProfileSettings>());
    ScriptedVoicingProfileSelector scripted(json::parse(
        R"({"sequence": [{"priority": 0.1, "allowedInversions": [0, 1], "allowedSpreads": [0]}, {"priority": 0.6, "allowedInversions": [2]}, {"priority": 0.9}]})")
        .get<ScriptedProfileSettings>());
    struct Want { float priority; V inv, spr; };
    const Want wantRandom[8] = {
        {0.757769644f, {1, 2}, {0}}, {0.278424382f, {0}, {0}}, {0.322736174f, {1, 2}, {0, 1}},
        {0.519246757f, {0, 1}, {0, 1}}, {0.778708696f, {0, 1}, {0, 1}}, {0.754242897f, {1, 2}, {0}},
        {0.592141926f, {0}, {0, 1}}, {0.73540622f, {1, 2}, {0}}};
    const Want wantDrift[8] = {
        {0.538384855f, {0, 1}, {0, 1}}, {0.800000012f, {1, 2}, {0, 1}}, {0.487242162f, {1, 2}, {0, 1}},
        {0.408894598f, {1, 2}, {0, 1}}, {0.248676211f, {1, 2}, {0}}, {0.427068502f, {1, 2}, {0, 1}},
        {0.1878957f, {0}, {0}}, {0.352164149f, {0}, {0}}};
    const Want wantScripted[3] = {{0.100000001f, {0, 1}, {0}}, {0.600000024f, {2}, {}},
                                  {0.899999976f, {}, {}}};
    random.reset(12345u);
    drift.reset(12345u);
    scripted.reset(12345u);
    bool same = true;
    for (int c = 0; c < 8; ++c) {
        const Want& r = wantRandom[c];
        const Want& d = wantDrift[c];
        const Want& s = wantScripted[c % 3];
        same = same && profile_is(random.profile_for_chord(c, 0.0f, float(c)), r.priority, r.inv, r.spr)
                    && profile_is(drift.profile_for_chord(c, 0.0f, float(c)), d.priority, d.inv, d.spr)
                    && profile_is(scripted.profile_for_chord(c, 0.0f, float(c)), s.priority, s.inv, s.spr);
    }
    CHECK(same);
}

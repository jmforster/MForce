#pragma once
// Genre melody profile: the note map, the
// phrase critic's weights and the best-of-N search sizes, read from the
// "melody" block of styles/<name>.json. Strict: every key is required and a
// missing one throws by name. The file IS the ruleset; there are no
// in-code defaults to drift from it.
#include <map>
#include <stdexcept>
#include <string>

namespace mforce {

struct TendencyRow {
    std::map<std::string, double> weights;   // motion -> weight; "other" = fallback
    double total() const {
        double t = 0.0;
        for (const auto& [k, w] : weights) t += w;
        return t;
    }
    // Odds per 100 of `motion` (falls back to "other"; 0 when neither).
    double odds_per_100(const std::string& motion) const {
        const double tot = total();
        if (tot <= 0.0) return 0.0;
        auto it = weights.find(motion);
        if (it == weights.end()) it = weights.find("other");
        return it == weights.end() ? 0.0 : 100.0 * it->second / tot;
    }
};

struct NctOdds {
    double passing{0}, neighbor{0}, suspension{0}, anticipation{0},
           unresolvedSuspension{0}, appoggiatura{0}, escape{0}, free{0},
           accentedFactor{0}, longOdds{0};
};

struct Placement { double chordTone{0}, barFinal{0}, figureFinal{0}, longNote{0}; };

struct MelodyCritic {
    int departureBudget{0};
    double rangeMaxSteps{0}, rangeCostPerStep{0}, repAcrossHarmony{0},
           repMaxBeats{0}, motionRepeatFrac{0}, motionCost{0}, gapFill{0},
           leapEarly{0}, leapMid{0}, leapLate{0}, regressSlack{0},
           regressPerStep{0}, penultEqualsFinal{0};
};

struct MelodySearch { int phraseCandidates{1}, topK{1}, passageCandidates{1}; };

struct MelodyProfile {
    std::string name;
    std::map<std::string, TendencyRow> tendencies;
    NctOdds nct;
    Placement placement;
    double departureBelow{0};
    MelodyCritic critic;
    MelodySearch search;

    // The profile in the "melody" block of styles/<name>.json; throws when
    // the file cannot be opened or a key is missing. Defined in
    // melody_profile_json.cpp; parse_melody_profile (melody_profile_json.h)
    // reads one from a JSON value.
    static MelodyProfile load_by_name(const std::string& name);
};

} // namespace mforce

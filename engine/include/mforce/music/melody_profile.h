#pragma once
// Genre melody profile (walk3 spec 2026-09-22 §1-3): the note map, the
// phrase critic's weights and the best-of-N search sizes, read from the
// "melody" block of styles/<name>.json. Strict: every key is required and a
// missing one throws by name. The file IS the ruleset; there are no
// in-code defaults to drift from it.
#include <nlohmann/json.hpp>
#include <fstream>
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

    static MelodyProfile parse_json(const nlohmann::json& file) {
        auto req = [](const nlohmann::json& j, const char* key,
                      const std::string& where) -> const nlohmann::json& {
            if (!j.contains(key))
                throw std::runtime_error("melody profile: missing '" +
                                         std::string(key) + "' in " + where);
            return j.at(key);
        };
        MelodyProfile p;
        p.name = file.value("name", std::string(""));
        const auto& m = req(file, "melody", "file");
        for (auto& [key, row] : req(m, "tendencies", "melody").items()) {
            TendencyRow r;
            for (auto& [mot, w] : row.items()) r.weights[mot] = w.get<double>();
            p.tendencies[key] = std::move(r);
        }
        const auto& n = req(m, "nct", "melody");
        p.nct.passing              = req(n, "passing", "nct").get<double>();
        p.nct.neighbor             = req(n, "neighbor", "nct").get<double>();
        p.nct.suspension           = req(n, "suspension", "nct").get<double>();
        p.nct.anticipation         = req(n, "anticipation", "nct").get<double>();
        p.nct.unresolvedSuspension = req(n, "unresolvedSuspension", "nct").get<double>();
        p.nct.appoggiatura         = req(n, "appoggiatura", "nct").get<double>();
        p.nct.escape               = req(n, "escape", "nct").get<double>();
        p.nct.free                 = req(n, "free", "nct").get<double>();
        p.nct.accentedFactor       = req(n, "accentedFactor", "nct").get<double>();
        p.nct.longOdds             = req(n, "long", "nct").get<double>();
        const auto& pl = req(m, "placement", "melody");
        p.placement.chordTone   = req(pl, "chordTone", "placement").get<double>();
        p.placement.barFinal    = req(pl, "barFinal", "placement").get<double>();
        p.placement.figureFinal = req(pl, "figureFinal", "placement").get<double>();
        p.placement.longNote    = req(pl, "long", "placement").get<double>();
        p.departureBelow = req(m, "departureBelow", "melody").get<double>();
        const auto& c = req(m, "critic", "melody");
        p.critic.departureBudget   = req(c, "departureBudget", "critic").get<int>();
        p.critic.rangeMaxSteps     = req(c, "rangeMaxSteps", "critic").get<double>();
        p.critic.rangeCostPerStep  = req(c, "rangeCostPerStep", "critic").get<double>();
        p.critic.repAcrossHarmony  = req(c, "repAcrossHarmony", "critic").get<double>();
        p.critic.repMaxBeats       = req(c, "repMaxBeats", "critic").get<double>();
        p.critic.motionRepeatFrac  = req(c, "motionRepeatFrac", "critic").get<double>();
        p.critic.motionCost        = req(c, "motionCost", "critic").get<double>();
        p.critic.gapFill           = req(c, "gapFill", "critic").get<double>();
        p.critic.leapEarly         = req(c, "leapEarly", "critic").get<double>();
        p.critic.leapMid           = req(c, "leapMid", "critic").get<double>();
        p.critic.leapLate          = req(c, "leapLate", "critic").get<double>();
        p.critic.regressSlack      = req(c, "regressSlack", "critic").get<double>();
        p.critic.regressPerStep    = req(c, "regressPerStep", "critic").get<double>();
        p.critic.penultEqualsFinal = req(c, "penultEqualsFinal", "critic").get<double>();
        const auto& s = req(m, "search", "melody");
        p.search.phraseCandidates  = req(s, "phraseCandidates", "search").get<int>();
        p.search.topK              = req(s, "topK", "search").get<int>();
        p.search.passageCandidates = req(s, "passageCandidates", "search").get<int>();
        return p;
    }

    static MelodyProfile load_by_name(const std::string& name) {
        const std::string path = "styles/" + name + ".json";
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("melody profile: cannot open " + path);
        return parse_json(nlohmann::json::parse(f));
    }
};

} // namespace mforce

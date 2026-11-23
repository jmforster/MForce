// The melody profile's file form: the "melody" block of styles/<name>.json.
#include "mforce/compose/melody_profile_json.h"
#include <fstream>
#include <stdexcept>
#include <string>

namespace mforce {

MelodyProfile parse_melody_profile(const nlohmann::json& file) {
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

MelodyProfile MelodyProfile::load_by_name(const std::string& name) {
    const std::string path = "styles/" + name + ".json";
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("melody profile: cannot open " + path);
    return parse_melody_profile(nlohmann::json::parse(f));
}

} // namespace mforce

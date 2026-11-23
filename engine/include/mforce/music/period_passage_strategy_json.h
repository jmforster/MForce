#pragma once
// The period passage strategy's settings, to and from JSON. A period's
// phrases may name phrase strategies, so these take the codecs.
#include "mforce/music/period_passage_strategy.h"
#include "mforce/music/templates_json.h"

namespace mforce {

// ===========================================================================
// PeriodVariant / PeriodSpec
// ===========================================================================

inline void to_json(json& j, PeriodVariant v) {
    switch (v) {
        case PeriodVariant::Parallel:    j = "Parallel"; break;
        case PeriodVariant::Modified:    j = "Modified"; break;
        case PeriodVariant::Contrasting: j = "Contrasting"; break;
    }
}
inline void from_json(const json& j, PeriodVariant& v) {
    auto s = j.get<std::string>();
    if (s == "Parallel")         v = PeriodVariant::Parallel;
    else if (s == "Modified")    v = PeriodVariant::Modified;
    else if (s == "Contrasting") v = PeriodVariant::Contrasting;
    else throw std::runtime_error("Unknown PeriodVariant: " + s);
}

inline void to_json(json& j, const PeriodSpec& ps, const SettingsCodecs& codecs) {
    j = json::object();
    j["variant"] = ps.variant;
    j["bars"] = ps.bars;
    j["antecedent"] = write_phrase_template(ps.antecedent, codecs);
    j["consequent"] = write_phrase_template(ps.consequent, codecs);
    if (ps.consequentTransform) j["consequentTransform"] = *ps.consequentTransform;
    if (ps.consequentTransformParam != 0) j["consequentTransformParam"] = ps.consequentTransformParam;
    if (ps.leadingConnective) j["leadingConnective"] = *ps.leadingConnective;
}
inline void from_json(const json& j, PeriodSpec& ps, const SettingsCodecs& codecs) {
    ps = PeriodSpec{};
    ps.variant = j.value("variant", PeriodVariant::Parallel);
    ps.bars = j.value("bars", 4.0f);
    // The antecedent/consequent PhraseTemplates inside a PeriodSpec need
    // neither figures nor a startingPitch in JSON — placement comes from the
    // enclosing PassageTemplate's startingPitch + period flow. So each
    // phrase's fields are read here rather than by read_phrase_template,
    // which requires figures.
    auto loadPhrase = [&codecs](const json& pj) {
        refuse_old_phrase_keys(pj);
        PhraseTemplate ph;
        if (pj.contains("figures")) {
            for (auto& fj : pj.at("figures")) {
                FigureTemplate ft; from_json(fj, ft);
                ph.figures.push_back(std::move(ft));
            }
        }
        ph.name = pj.value("name", std::string(""));
        if (pj.contains("startingPitch")) {
            Pitch p; from_json(pj.at("startingPitch"), p);
            ph.startingPitch = p;
        }
        ph.parallel = pj.value("parallel", false);
        ph.totalBeats = pj.value("totalBeats", 0.0f);
        ph.cadenceType = pj.value("cadenceType", 0);
        ph.cadenceTarget = pj.value("cadenceTarget", -1);
        ph.cadentialArrival = pj.value("cadentialArrival", std::string());
        if (pj.contains("function")) from_json(pj.at("function"), ph.function);
        ph.seed = pj.value("seed", 0u);
        ph.locked = pj.value("locked", false);
        ph.strategy = pj.contains("strategy")
            ? codecs.read(pj.at("strategy"), StrategyLevel::Phrase) : StrategyChoice{};
        if (pj.contains("connectors")) ph.connectors = read_connectors(pj.at("connectors"));
        return ph;
    };
    ps.antecedent = loadPhrase(j.at("antecedent"));
    ps.consequent = loadPhrase(j.at("consequent"));
    if (j.contains("consequentTransform") && !j.at("consequentTransform").is_null()) {
        ps.consequentTransform = j.at("consequentTransform").get<TransformOp>();
    }
    ps.consequentTransformParam = j.value("consequentTransformParam", 0);
    if (j.contains("leadingConnective") && !j.at("leadingConnective").is_null()) {
        ps.leadingConnective = j.at("leadingConnective").get<std::string>();
    }
}

// ===========================================================================
// PeriodPassageSettings: {"periods": [...]}
// ===========================================================================

inline void to_json(json& j, const PeriodPassageSettings& s, const SettingsCodecs& codecs) {
    json periods = json::array();
    for (const auto& ps : s.periods) {
        json pj;
        to_json(pj, ps, codecs);
        periods.push_back(std::move(pj));
    }
    j = json{{"periods", std::move(periods)}};
}

inline void from_json(const json& j, PeriodPassageSettings& s, const SettingsCodecs& codecs) {
    s.periods.clear();
    if (j.contains("periods") && j.at("periods").is_array()) {
        for (const auto& pj : j.at("periods")) {
            PeriodSpec ps; from_json(pj, ps, codecs);
            s.periods.push_back(std::move(ps));
        }
    }
}

} // namespace mforce

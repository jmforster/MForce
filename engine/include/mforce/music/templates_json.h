#pragma once
#include "mforce/music/templates.h"
#include "mforce/music/music_json.h"
#include <nlohmann/json.hpp>

namespace mforce {

using json = nlohmann::json;

// ===========================================================================
// Enum serialization
// ===========================================================================

inline void to_json(json& j, FigureSource s) {
    switch (s) {
        case FigureSource::Generate:  j = "generate";  break;
        case FigureSource::Reference: j = "reference"; break;
        case FigureSource::Transform: j = "transform"; break;
        case FigureSource::Locked:    j = "locked";    break;
    }
}
inline void from_json(const json& j, FigureSource& s) {
    auto str = j.get<std::string>();
    if (str == "generate")  s = FigureSource::Generate;
    else if (str == "reference") s = FigureSource::Reference;
    else if (str == "transform") s = FigureSource::Transform;
    else if (str == "locked")    s = FigureSource::Locked;
    else s = FigureSource::Generate;
}

inline void to_json(json& j, TransformOp t) {
    switch (t) {
        case TransformOp::None:             j = "none"; break;
        case TransformOp::Invert:           j = "invert"; break;
        case TransformOp::Reverse:          j = "reverse"; break;
        case TransformOp::Stretch:          j = "stretch"; break;
        case TransformOp::Compress:         j = "compress"; break;
        case TransformOp::VaryRhythm:       j = "vary_rhythm"; break;
        case TransformOp::VarySteps:        j = "vary_steps"; break;
        case TransformOp::NewSteps:         j = "new_steps"; break;
        case TransformOp::NewRhythm:        j = "new_rhythm"; break;
        case TransformOp::Replicate:        j = "replicate"; break;
        case TransformOp::TransformGeneral: j = "general"; break;
    }
}
inline void from_json(const json& j, TransformOp& t) {
    auto str = j.get<std::string>();
    if (str == "none")          t = TransformOp::None;
    else if (str == "invert")        t = TransformOp::Invert;
    else if (str == "reverse")       t = TransformOp::Reverse;
    else if (str == "stretch")       t = TransformOp::Stretch;
    else if (str == "compress")      t = TransformOp::Compress;
    else if (str == "vary_rhythm")   t = TransformOp::VaryRhythm;
    else if (str == "vary_steps")    t = TransformOp::VarySteps;
    else if (str == "new_steps")     t = TransformOp::NewSteps;
    else if (str == "new_rhythm")    t = TransformOp::NewRhythm;
    else if (str == "replicate")     t = TransformOp::Replicate;
    else if (str == "general")       t = TransformOp::TransformGeneral;
    else t = TransformOp::None;
}

inline void to_json(json& j, PartRole r) {
    switch (r) {
        case PartRole::Melody:        j = "melody"; break;
        case PartRole::Bass:          j = "bass"; break;
        case PartRole::Harmony:       j = "harmony"; break;
        case PartRole::Drums:         j = "drums"; break;
        case PartRole::Countermelody: j = "countermelody"; break;
        case PartRole::Pad:           j = "pad"; break;
    }
}
inline void from_json(const json& j, PartRole& r) {
    auto str = j.get<std::string>();
    if (str == "melody")             r = PartRole::Melody;
    else if (str == "bass")          r = PartRole::Bass;
    else if (str == "harmony")       r = PartRole::Harmony;
    else if (str == "drums")         r = PartRole::Drums;
    else if (str == "countermelody") r = PartRole::Countermelody;
    else if (str == "pad")           r = PartRole::Pad;
    else r = PartRole::Melody;
}

// ===========================================================================
// FigureTemplate
// ===========================================================================

inline void to_json(json& j, const FigureTemplate& ft) {
    j = json{{"source", ft.source}};

    if (ft.source == FigureSource::Generate) {
        if (ft.minNotes != 2) j["minNotes"] = ft.minNotes;
        if (ft.maxNotes != 8) j["maxNotes"] = ft.maxNotes;
        if (ft.totalBeats != 0.0f) j["totalBeats"] = ft.totalBeats;
        if (ft.defaultPulse != 0.0f) j["defaultPulse"] = ft.defaultPulse;
        if (ft.preferStepwise) j["stepwise"] = true;
        if (ft.preferSkips) j["skips"] = true;
        if (ft.targetNet != 0) j["targetNet"] = ft.targetNet;
    }

    if (ft.source == FigureSource::Reference || ft.source == FigureSource::Transform) {
        j["seedName"] = ft.seedName;
    }
    if (ft.source == FigureSource::Transform) {
        j["transform"] = ft.transform;
        if (ft.transformParam != 0) j["transformParam"] = ft.transformParam;
    }

    if (ft.source == FigureSource::Locked && ft.lockedFigure) {
        j["lockedFigure"] = *ft.lockedFigure;
    }

    if (ft.seed != 0) j["seed"] = ft.seed;
    if (ft.locked) j["locked"] = true;
}

inline void from_json(const json& j, FigureTemplate& ft) {
    from_json(j.at("source"), ft.source);

    ft.minNotes = j.value("minNotes", 2);
    ft.maxNotes = j.value("maxNotes", 8);
    ft.totalBeats = j.value("totalBeats", 0.0f);
    ft.defaultPulse = j.value("defaultPulse", 0.0f);
    ft.preferStepwise = j.value("stepwise", false);
    ft.preferSkips = j.value("skips", false);
    ft.targetNet = j.value("targetNet", 0);

    ft.seedName = j.value("seedName", std::string(""));
    if (j.contains("transform")) from_json(j.at("transform"), ft.transform);
    ft.transformParam = j.value("transformParam", 0);

    if (j.contains("lockedFigure")) {
        MelodicFigure mf;
        from_json(j.at("lockedFigure"), mf);
        ft.lockedFigure = std::move(mf);
    }

    ft.seed = j.value("seed", 0u);
    ft.locked = j.value("locked", false);
}

// ===========================================================================
// Seed
// ===========================================================================

inline void to_json(json& j, const Seed& s) {
    j = json{{"name", s.name}};
    if (!s.figure.units.empty()) j["figure"] = s.figure;
    if (s.userProvided) j["userProvided"] = true;
    if (s.generationSeed != 0) j["generationSeed"] = s.generationSeed;
    if (s.constraints) j["constraints"] = *s.constraints;
}
inline void from_json(const json& j, Seed& s) {
    s.name = j.at("name").get<std::string>();
    if (j.contains("figure")) {
        from_json(j.at("figure"), s.figure);
    }
    s.userProvided = j.value("userProvided", false);
    s.generationSeed = j.value("generationSeed", 0u);
    if (j.contains("constraints")) {
        FigureTemplate ft;
        from_json(j.at("constraints"), ft);
        s.constraints = std::move(ft);
    }
}

// ===========================================================================
// PhraseTemplate
// ===========================================================================

inline void to_json(json& j, const PhraseTemplate& pt) {
    j = json{{"figures", pt.figures}};
    if (!pt.name.empty()) j["name"] = pt.name;
    if (pt.startingPitch) j["startingPitch"] = *pt.startingPitch;
    if (!pt.connectors.empty()) j["connectors"] = pt.connectors;
    if (pt.totalBeats != 0.0f) j["totalBeats"] = pt.totalBeats;
    if (pt.cadenceType != 0) j["cadenceType"] = pt.cadenceType;
    if (pt.cadenceTarget != -1) j["cadenceTarget"] = pt.cadenceTarget;
    if (pt.seed != 0) j["seed"] = pt.seed;
    if (pt.locked) j["locked"] = true;
}

inline void from_json(const json& j, PhraseTemplate& pt) {
    pt.figures.clear();
    for (auto& fj : j.at("figures")) {
        FigureTemplate ft;
        from_json(fj, ft);
        pt.figures.push_back(std::move(ft));
    }
    pt.name = j.value("name", std::string(""));
    if (j.contains("startingPitch")) {
        Pitch p; from_json(j.at("startingPitch"), p);
        pt.startingPitch = p;
    }
    if (j.contains("connectors")) {
        pt.connectors.clear();
        for (auto& cj : j.at("connectors")) {
            FigureConnector fc; from_json(cj, fc);
            pt.connectors.push_back(fc);
        }
    }
    pt.totalBeats = j.value("totalBeats", 0.0f);
    pt.cadenceType = j.value("cadenceType", 0);
    pt.cadenceTarget = j.value("cadenceTarget", -1);
    pt.seed = j.value("seed", 0u);
    pt.locked = j.value("locked", false);
}

// ===========================================================================
// PassageTemplate
// ===========================================================================

inline void to_json(json& j, const PassageTemplate& pt) {
    j = json{{"phrases", pt.phrases}};
    if (!pt.name.empty()) j["name"] = pt.name;
    if (!pt.character.empty()) j["character"] = pt.character;
    if (!pt.fromKey.empty()) j["fromKey"] = pt.fromKey;
    if (!pt.toKey.empty()) j["toKey"] = pt.toKey;
    if (pt.seed != 0) j["seed"] = pt.seed;
    if (pt.locked) j["locked"] = true;
}

inline void from_json(const json& j, PassageTemplate& pt) {
    pt.phrases.clear();
    for (auto& pj : j.at("phrases")) {
        PhraseTemplate ph; from_json(pj, ph);
        pt.phrases.push_back(std::move(ph));
    }
    pt.name = j.value("name", std::string(""));
    pt.character = j.value("character", std::string(""));
    pt.fromKey = j.value("fromKey", std::string(""));
    pt.toKey = j.value("toKey", std::string(""));
    pt.seed = j.value("seed", 0u);
    pt.locked = j.value("locked", false);
}

// ===========================================================================
// PartTemplate
// ===========================================================================

inline void to_json(json& j, const PartTemplate& pt) {
    j = json{{"name", pt.name}, {"role", pt.role}};
    if (!pt.instrumentPatch.empty()) j["instrumentPatch"] = pt.instrumentPatch;
    if (!pt.passages.empty()) {
        json passagesJ;
        for (auto& [secName, passageTmpl] : pt.passages)
            passagesJ[secName] = passageTmpl;
        j["passages"] = passagesJ;
    }
}

inline void from_json(const json& j, PartTemplate& pt) {
    pt.name = j.at("name").get<std::string>();
    from_json(j.at("role"), pt.role);
    pt.instrumentPatch = j.value("instrumentPatch", std::string(""));
    if (j.contains("passages")) {
        for (auto& [secName, passageJ] : j.at("passages").items()) {
            PassageTemplate pass; from_json(passageJ, pass);
            pt.passages[secName] = std::move(pass);
        }
    }
}

// ===========================================================================
// PieceTemplate
// ===========================================================================

inline void to_json(json& j, const PieceTemplate::SectionDef& sd) {
    j = json{{"name", sd.name}, {"beats", sd.beats}};
    if (!sd.scaleOverride.empty()) j["scaleOverride"] = sd.scaleOverride;
}
inline void from_json(const json& j, PieceTemplate::SectionDef& sd) {
    sd.name = j.at("name").get<std::string>();
    sd.beats = j.at("beats").get<float>();
    sd.scaleOverride = j.value("scaleOverride", std::string(""));
}

inline void to_json(json& j, const PieceTemplate& pt) {
    j = json{
        {"keyName", pt.keyName},
        {"scaleName", pt.scaleName},
        {"meter", pt.meter},
        {"bpm", pt.bpm},
        {"sections", pt.sections},
        {"parts", pt.parts}
    };
    if (!pt.seeds.empty()) j["seeds"] = pt.seeds;
    if (!pt.form.empty()) j["form"] = pt.form;
    if (pt.harmonySeeds) j["harmony"] = *pt.harmonySeeds;
    if (pt.masterSeed != 0) j["masterSeed"] = pt.masterSeed;
}

inline void from_json(const json& j, PieceTemplate& pt) {
    pt.keyName = j.value("keyName", std::string("C"));
    pt.scaleName = j.value("scaleName", std::string("Major"));
    if (j.contains("meter")) from_json(j.at("meter"), pt.meter);
    pt.bpm = j.value("bpm", 120.0f);

    if (j.contains("seeds")) {
        pt.seeds.clear();
        for (auto& sj : j.at("seeds")) {
            Seed s; from_json(sj, s);
            pt.seeds.push_back(std::move(s));
        }
    }

    pt.sections.clear();
    if (j.contains("sections")) {
        for (auto& sj : j.at("sections")) {
            PieceTemplate::SectionDef sd; from_json(sj, sd);
            pt.sections.push_back(std::move(sd));
        }
    }

    pt.form = j.value("form", std::string(""));

    pt.parts.clear();
    if (j.contains("parts")) {
        for (auto& pj : j.at("parts")) {
            PartTemplate p; from_json(pj, p);
            pt.parts.push_back(std::move(p));
        }
    }

    if (j.contains("harmony")) {
        ChordProgression cp; from_json(j.at("harmony"), cp);
        pt.harmonySeeds = std::move(cp);
    }

    pt.masterSeed = j.value("masterSeed", 0u);
}

} // namespace mforce

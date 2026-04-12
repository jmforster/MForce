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
        case FigureSource::Literal:   j = "literal";   break;
    }
}
inline void from_json(const json& j, FigureSource& s) {
    auto str = j.get<std::string>();
    if (str == "generate")        s = FigureSource::Generate;
    else if (str == "reference")  s = FigureSource::Reference;
    else if (str == "transform")  s = FigureSource::Transform;
    else if (str == "locked")     s = FigureSource::Locked;
    else if (str == "literal")    s = FigureSource::Literal;
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
// MelodicFunction
// ===========================================================================

inline void to_json(json& j, MelodicFunction f) {
    switch (f) {
        case MelodicFunction::Free:        j = "free"; break;
        case MelodicFunction::Statement:   j = "statement"; break;
        case MelodicFunction::Development: j = "development"; break;
        case MelodicFunction::Transition:  j = "transition"; break;
        case MelodicFunction::Cadential:   j = "cadential"; break;
    }
}
inline void from_json(const json& j, MelodicFunction& f) {
    auto s = j.get<std::string>();
    if (s == "statement")        f = MelodicFunction::Statement;
    else if (s == "development") f = MelodicFunction::Development;
    else if (s == "transition")  f = MelodicFunction::Transition;
    else if (s == "cadential")   f = MelodicFunction::Cadential;
    else f = MelodicFunction::Free;
}

// ===========================================================================
// FigureShape
// ===========================================================================

inline void to_json(json& j, FigureShape s) {
    switch (s) {
        case FigureShape::Free:              j = "free"; break;
        case FigureShape::ScalarRun:         j = "scalar_run"; break;
        case FigureShape::RepeatedNote:      j = "repeated_note"; break;
        case FigureShape::HeldNote:          j = "held_note"; break;
        case FigureShape::CadentialApproach: j = "cadential_approach"; break;
        case FigureShape::TriadicOutline:    j = "triadic_outline"; break;
        case FigureShape::NeighborTone:      j = "neighbor_tone"; break;
        case FigureShape::LeapAndFill:       j = "leap_and_fill"; break;
        case FigureShape::ScalarReturn:      j = "scalar_return"; break;
        case FigureShape::Anacrusis:         j = "anacrusis"; break;
        case FigureShape::Zigzag:            j = "zigzag"; break;
        case FigureShape::Fanfare:           j = "fanfare"; break;
        case FigureShape::Sigh:              j = "sigh"; break;
        case FigureShape::Suspension:        j = "suspension"; break;
        case FigureShape::Cambiata:          j = "cambiata"; break;
        case FigureShape::Skipping:          j = "skipping"; break;
        case FigureShape::Stepping:          j = "stepping"; break;
    }
}
inline void from_json(const json& j, FigureShape& s) {
    auto str = j.get<std::string>();
    if (str == "scalar_run")              s = FigureShape::ScalarRun;
    else if (str == "repeated_note")      s = FigureShape::RepeatedNote;
    else if (str == "held_note")          s = FigureShape::HeldNote;
    else if (str == "cadential_approach") s = FigureShape::CadentialApproach;
    else if (str == "triadic_outline")    s = FigureShape::TriadicOutline;
    else if (str == "neighbor_tone")      s = FigureShape::NeighborTone;
    else if (str == "leap_and_fill")      s = FigureShape::LeapAndFill;
    else if (str == "scalar_return")      s = FigureShape::ScalarReturn;
    else if (str == "anacrusis")          s = FigureShape::Anacrusis;
    else if (str == "zigzag")             s = FigureShape::Zigzag;
    else if (str == "fanfare")            s = FigureShape::Fanfare;
    else if (str == "sigh")               s = FigureShape::Sigh;
    else if (str == "suspension")         s = FigureShape::Suspension;
    else if (str == "cambiata")           s = FigureShape::Cambiata;
    else if (str == "skipping")           s = FigureShape::Skipping;
    else if (str == "stepping")           s = FigureShape::Stepping;
    else s = FigureShape::Free;
}

// ===========================================================================
// FigureDirection
// ===========================================================================

inline void to_json(json& j, FigureDirection d) {
    switch (d) {
        case FigureDirection::Ascending:             j = "ascending"; break;
        case FigureDirection::Descending:            j = "descending"; break;
        case FigureDirection::TurnaroundAscending:   j = "turnaround_ascending"; break;
        case FigureDirection::TurnaroundDescending:  j = "turnaround_descending"; break;
        case FigureDirection::AscendingArc:          j = "ascending_arc"; break;
        case FigureDirection::DescendingArc:         j = "descending_arc"; break;
        case FigureDirection::SineAscending:         j = "sine_ascending"; break;
        case FigureDirection::SineDescending:        j = "sine_descending"; break;
        case FigureDirection::Random:                j = "random"; break;
    }
}
inline void from_json(const json& j, FigureDirection& d) {
    auto str = j.get<std::string>();
    if (str == "ascending")                  d = FigureDirection::Ascending;
    else if (str == "descending")            d = FigureDirection::Descending;
    else if (str == "turnaround_ascending")  d = FigureDirection::TurnaroundAscending;
    else if (str == "turnaround_descending") d = FigureDirection::TurnaroundDescending;
    else if (str == "ascending_arc")         d = FigureDirection::AscendingArc;
    else if (str == "descending_arc")        d = FigureDirection::DescendingArc;
    else if (str == "sine_ascending")        d = FigureDirection::SineAscending;
    else if (str == "sine_descending")       d = FigureDirection::SineDescending;
    else if (str == "random")                d = FigureDirection::Random;
    else d = FigureDirection::Ascending;
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
        j["motifName"] = ft.motifName;
    }
    if (ft.source == FigureSource::Transform) {
        j["transform"] = ft.transform;
        if (ft.transformParam != 0) j["transformParam"] = ft.transformParam;
    }

    if (ft.source == FigureSource::Locked && ft.lockedFigure) {
        j["lockedFigure"] = *ft.lockedFigure;
    }

    if (!ft.literalNotes.empty()) {
        json arr = json::array();
        for (auto& ln : ft.literalNotes) {
            json jn;
            if (ln.rest) {
                jn["rest"] = true;
            } else if (ln.pitch) {
                jn["pitch"] = *ln.pitch;
            }
            jn["duration"] = ln.duration;
            arr.push_back(std::move(jn));
        }
        j["literalNotes"] = std::move(arr);
    }

    if (ft.shape != FigureShape::Free) j["shape"] = ft.shape;
    if (ft.shapeDirection != 1) j["shapeDirection"] = ft.shapeDirection;
    if (ft.shapeParam != 0) j["shapeParam"] = ft.shapeParam;
    if (ft.shapeParam2 != 0) j["shapeParam2"] = ft.shapeParam2;
    if (ft.direction != FigureDirection::Ascending) j["direction"] = ft.direction;

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

    ft.motifName = j.value("motifName", std::string(""));
    if (j.contains("transform")) from_json(j.at("transform"), ft.transform);
    ft.transformParam = j.value("transformParam", 0);

    if (j.contains("shape")) from_json(j.at("shape"), ft.shape);
    ft.shapeDirection = j.value("shapeDirection", 1);
    ft.shapeParam = j.value("shapeParam", 0);
    ft.shapeParam2 = j.value("shapeParam2", 0);
    if (j.contains("direction")) from_json(j.at("direction"), ft.direction);

    if (j.contains("lockedFigure")) {
        MelodicFigure mf;
        from_json(j.at("lockedFigure"), mf);
        ft.lockedFigure = std::move(mf);
    }

    if (j.contains("literalNotes")) {
        ft.literalNotes.clear();
        for (auto& jn : j.at("literalNotes")) {
            FigureTemplate::LiteralNote ln;
            ln.rest = jn.value("rest", false);
            if (!ln.rest && jn.contains("pitch")) {
                Pitch p;
                from_json(jn.at("pitch"), p);
                ln.pitch = p;
            }
            ln.duration = jn.value("duration", 1.0f);
            ft.literalNotes.push_back(std::move(ln));
        }
    }

    ft.seed = j.value("seed", 0u);
    ft.locked = j.value("locked", false);
}

// ===========================================================================
// Motif
// ===========================================================================

inline void to_json(json& j, const Motif& m) {
    j = json{{"name", m.name}};
    if (!m.figure.units.empty()) j["figure"] = m.figure;
    if (m.userProvided) j["userProvided"] = true;
    if (m.generationSeed != 0) j["generationSeed"] = m.generationSeed;
    if (m.constraints) j["constraints"] = *m.constraints;
}
inline void from_json(const json& j, Motif& m) {
    m.name = j.at("name").get<std::string>();
    if (j.contains("figure")) {
        from_json(j.at("figure"), m.figure);
    }
    m.userProvided = j.value("userProvided", false);
    m.generationSeed = j.value("generationSeed", 0u);
    if (j.contains("constraints")) {
        FigureTemplate ft;
        from_json(j.at("constraints"), ft);
        m.constraints = std::move(ft);
    }
}

// ===========================================================================
// PeriodPhraseConfig
// ===========================================================================

inline void to_json(json& j, const PeriodPhraseConfig& c) {
  j["basicIdea"] = c.basicIdea;
  j["antecedentTail"] = c.antecedentTail;
  j["consequentTail"] = c.consequentTail;
  if (c.halfCadenceTarget != 4) j["halfCadenceTarget"] = c.halfCadenceTarget;
}

inline void from_json(const json& j, PeriodPhraseConfig& c) {
  from_json(j.at("basicIdea"), c.basicIdea);
  from_json(j.at("antecedentTail"), c.antecedentTail);
  from_json(j.at("consequentTail"), c.consequentTail);
  c.halfCadenceTarget = j.value("halfCadenceTarget", 4);
}

// ===========================================================================
// SentencePhraseConfig
// ===========================================================================

inline void to_json(json& j, const SentencePhraseConfig& c) {
  j["basicIdea"] = c.basicIdea;
  j["continuation"] = c.continuation;
  if (c.variationTransposition != 0) j["variationTransposition"] = c.variationTransposition;
}

inline void from_json(const json& j, SentencePhraseConfig& c) {
  from_json(j.at("basicIdea"), c.basicIdea);
  from_json(j.at("continuation"), c.continuation);
  c.variationTransposition = j.value("variationTransposition", 0);
}

// ===========================================================================
// PhraseTemplate
// ===========================================================================

inline void to_json(json& j, const PhraseTemplate& pt) {
    j = json{{"figures", pt.figures}};
    if (!pt.name.empty()) j["name"] = pt.name;
    if (pt.startingPitch) j["startingPitch"] = *pt.startingPitch;
    if (pt.totalBeats != 0.0f) j["totalBeats"] = pt.totalBeats;
    if (pt.cadenceType != 0) j["cadenceType"] = pt.cadenceType;
    if (pt.cadenceTarget != -1) j["cadenceTarget"] = pt.cadenceTarget;
    if (pt.function != MelodicFunction::Free) j["function"] = pt.function;
    if (pt.seed != 0) j["seed"] = pt.seed;
    if (pt.locked) j["locked"] = true;
    if (!pt.strategy.empty()) j["strategy"] = pt.strategy;
    if (pt.periodConfig) j["periodConfig"] = *pt.periodConfig;
    if (pt.sentenceConfig) j["sentenceConfig"] = *pt.sentenceConfig;
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

    pt.totalBeats = j.value("totalBeats", 0.0f);
    pt.cadenceType = j.value("cadenceType", 0);
    pt.cadenceTarget = j.value("cadenceTarget", -1);
    if (j.contains("function")) from_json(j.at("function"), pt.function);
    pt.seed = j.value("seed", 0u);
    pt.locked = j.value("locked", false);
    pt.strategy = j.value("strategy", std::string(""));
    if (j.contains("periodConfig")) {
        PeriodPhraseConfig c;
        from_json(j.at("periodConfig"), c);
        pt.periodConfig = c;
    }
    if (j.contains("sentenceConfig")) {
        SentencePhraseConfig c;
        from_json(j.at("sentenceConfig"), c);
        pt.sentenceConfig = c;
    }
}

// ===========================================================================
// PassageTemplate
// ===========================================================================

inline void to_json(json& j, const PassageTemplate& pt) {
    j = json{{"phrases", pt.phrases}};
    if (!pt.name.empty()) j["name"] = pt.name;
    if (pt.startingPitch) j["startingPitch"] = *pt.startingPitch;
    if (!pt.character.empty()) j["character"] = pt.character;
    if (!pt.fromKey.empty()) j["fromKey"] = pt.fromKey;
    if (!pt.toKey.empty()) j["toKey"] = pt.toKey;
    if (pt.seed != 0) j["seed"] = pt.seed;
    if (pt.locked) j["locked"] = true;
}

inline void from_json(const json& j, PassageTemplate& pt) {
    if (!j.contains("startingPitch")) {
        throw std::runtime_error(
            "PassageTemplate '" + j.value("name", std::string("<unnamed>")) +
            "' is missing required field 'startingPitch'");
    }
    Pitch p;
    from_json(j.at("startingPitch"), p);
    pt.startingPitch = p;

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
    if (!pt.motifs.empty()) j["motifs"] = pt.motifs;
    if (!pt.form.empty()) j["form"] = pt.form;
    if (pt.harmonySeeds) j["harmony"] = *pt.harmonySeeds;
    if (pt.masterSeed != 0) j["masterSeed"] = pt.masterSeed;
}

inline void from_json(const json& j, PieceTemplate& pt) {
    pt.keyName = j.value("keyName", std::string("C"));
    pt.scaleName = j.value("scaleName", std::string("Major"));
    if (j.contains("meter")) from_json(j.at("meter"), pt.meter);
    pt.bpm = j.value("bpm", 120.0f);

    if (j.contains("motifs")) {
        pt.motifs.clear();
        for (auto& mj : j.at("motifs")) {
            Motif m; from_json(mj, m);
            pt.motifs.push_back(std::move(m));
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

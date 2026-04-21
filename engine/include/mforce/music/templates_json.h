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
        case TransformOp::RhythmTail:       j = "rhythm_tail"; break;
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
    else if (str == "rhythm_tail")   t = TransformOp::RhythmTail;
    else t = TransformOp::None;
}

// MotifRole and MotifOrigin enum JSON helpers (2026-04-15 umbrella spec).
inline void to_json(json& j, MotifRole r) {
    switch (r) {
        case MotifRole::Thematic:      j = "Thematic"; break;
        case MotifRole::Cadential:     j = "Cadential"; break;
        case MotifRole::PostCadential: j = "PostCadential"; break;
        case MotifRole::Discursive:    j = "Discursive"; break;
        case MotifRole::Climactic:     j = "Climactic"; break;
        case MotifRole::Connective:    j = "Connective"; break;
        case MotifRole::Ornamental:    j = "Ornamental"; break;
    }
}
inline void from_json(const json& j, MotifRole& r) {
    auto s = j.get<std::string>();
    if (s == "Thematic")           r = MotifRole::Thematic;
    else if (s == "Cadential")     r = MotifRole::Cadential;
    else if (s == "PostCadential") r = MotifRole::PostCadential;
    else if (s == "Discursive")    r = MotifRole::Discursive;
    else if (s == "Climactic")     r = MotifRole::Climactic;
    else if (s == "Connective")    r = MotifRole::Connective;
    else if (s == "Ornamental")    r = MotifRole::Ornamental;
    else throw std::runtime_error("Unknown MotifRole: " + s);
}

inline void to_json(json& j, MotifOrigin o) {
    switch (o) {
        case MotifOrigin::User:      j = "User"; break;
        case MotifOrigin::Generated: j = "Generated"; break;
        case MotifOrigin::Derived:   j = "Derived"; break;
        case MotifOrigin::Extracted: j = "Extracted"; break;
    }
}
inline void from_json(const json& j, MotifOrigin& o) {
    auto s = j.get<std::string>();
    if (s == "User")           o = MotifOrigin::User;
    else if (s == "Generated") o = MotifOrigin::Generated;
    else if (s == "Derived")   o = MotifOrigin::Derived;
    else if (s == "Extracted") o = MotifOrigin::Extracted;
    else throw std::runtime_error("Unknown MotifOrigin: " + s);
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
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) lower.push_back((char)std::tolower((unsigned char)c));
    if (lower == "statement")        f = MelodicFunction::Statement;
    else if (lower == "development") f = MelodicFunction::Development;
    else if (lower == "transition")  f = MelodicFunction::Transition;
    else if (lower == "cadential")   f = MelodicFunction::Cadential;
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

    if (!ft.rhythmMotifName.empty()) j["rhythmMotifName"] = ft.rhythmMotifName;
    if (!ft.contourMotifName.empty()) j["contourMotifName"] = ft.contourMotifName;
    if (!ft.rhythmTransform.empty()) j["rhythmTransform"] = ft.rhythmTransform;
    if (ft.rhythmTransformParam != 0) j["rhythmTransformParam"] = ft.rhythmTransformParam;
    if (!ft.contourTransform.empty()) j["contourTransform"] = ft.contourTransform;
    if (ft.contourTransformParam != 0) j["contourTransformParam"] = ft.contourTransformParam;

    if (ft.maxStep != 0) j["maxStep"] = ft.maxStep;
    if (ft.stepMode != StepMode::Scale) j["stepMode"] = "chordTone";

    if (ft.figureCadenceType != 0) j["figureCadenceType"] = ft.figureCadenceType;
    if (!ft.perfect) j["perfect"] = false;

    if (ft.seed != 0) j["seed"] = ft.seed;
    if (ft.locked) j["locked"] = true;
    if (ft.role) j["role"] = *ft.role;
    if (ft.function != MelodicFunction::Free) j["function"] = ft.function;
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

    ft.rhythmMotifName = j.value("rhythmMotifName", std::string(""));
    ft.contourMotifName = j.value("contourMotifName", std::string(""));
    ft.rhythmTransform = j.value("rhythmTransform", std::string(""));
    ft.rhythmTransformParam = j.value("rhythmTransformParam", 0.0f);
    ft.contourTransform = j.value("contourTransform", std::string(""));
    ft.contourTransformParam = j.value("contourTransformParam", 0.0f);

    if (j.contains("stepMode")) {
      auto sm = j["stepMode"].get<std::string>();
      if (sm == "chordTone") ft.stepMode = StepMode::ChordTone;
      else ft.stepMode = StepMode::Scale;
    }

    ft.maxStep = j.value("maxStep", 0);

    ft.figureCadenceType = j.value("figureCadenceType", 0);
    ft.perfect = j.value("perfect", true);

    ft.seed = j.value("seed", 0u);
    ft.locked = j.value("locked", false);

    if (j.contains("role") && !j.at("role").is_null()) {
        ft.role = j.at("role").get<MotifRole>();
    }

    if (j.contains("function")) from_json(j.at("function"), ft.function);
}

// ===========================================================================
// Motif
// ===========================================================================

inline void to_json(json& j, const Motif& m) {
    j["name"] = m.name;
    if (m.is_figure()) {
        j["type"] = "figure";
        const auto& fig = m.figure();
        if (!fig.units.empty()) j["figure"] = fig;
    } else if (m.is_rhythm()) {
        j["type"] = "rhythm";
        j["rhythm"] = m.rhythm();
    } else if (m.is_contour()) {
        j["type"] = "contour";
        j["contour"] = m.contour();
    }
    if (m.userProvided) j["userProvided"] = true;
    if (m.generationSeed != 0) j["generationSeed"] = m.generationSeed;
    if (m.constraints) j["constraints"] = *m.constraints;

    // New metadata — emit only when non-default for terse JSON.
    if (!m.roles.empty()) {
        j["roles"] = json::array();
        for (auto r : m.roles) j["roles"].push_back(r);
    }
    if (m.origin != MotifOrigin::User) j["origin"] = m.origin;
    if (m.derivedFrom) j["derivedFrom"] = *m.derivedFrom;
    if (m.transform) j["transform"] = *m.transform;
    if (m.transformParam != 0) j["transformParam"] = m.transformParam;
}
inline void from_json(const json& j, Motif& m) {
    m.name = j.at("name").get<std::string>();
    std::string type = j.value("type", std::string("figure"));
    if (type == "rhythm") {
        PulseSequence ps;
        from_json(j.at("rhythm"), ps);
        m.content = std::move(ps);
    } else if (type == "contour") {
        StepSequence ss;
        from_json(j.at("contour"), ss);
        m.content = std::move(ss);
    } else {
        // Default: figure (backward compatible)
        if (j.contains("figure")) {
            MelodicFigure fig;
            from_json(j.at("figure"), fig);
            m.content = std::move(fig);
        } else {
            m.content = MelodicFigure{};  // empty figure if no content
        }
    }
    m.userProvided = j.value("userProvided", false);
    m.generationSeed = j.value("generationSeed", 0u);
    if (j.contains("constraints")) {
        FigureTemplate ft;
        from_json(j.at("constraints"), ft);
        m.constraints = std::move(ft);
    }

    // New metadata (2026-04-15). Defaults: empty roles, User origin, no derivation.
    if (j.contains("roles") && j.at("roles").is_array()) {
        for (const auto& r : j.at("roles")) {
            m.roles.insert(r.get<MotifRole>());
        }
    }
    m.origin = j.value("origin", MotifOrigin::User);
    if (j.contains("derivedFrom") && !j.at("derivedFrom").is_null()) {
        m.derivedFrom = j.at("derivedFrom").get<std::string>();
    }
    if (j.contains("transform") && !j.at("transform").is_null()) {
        m.transform = j.at("transform").get<TransformOp>();
    }
    m.transformParam = j.value("transformParam", 0);
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

    // Connectors: only emit if any are present
    bool anyConnectors = false;
    for (const auto& c : pt.connectors) { if (c) { anyConnectors = true; break; } }
    if (anyConnectors) {
        json connArr = json::array();
        for (const auto& c : pt.connectors) {
            if (!c) { connArr.push_back(nullptr); }
            else {
                json cj = json::object();
                if (c->elideCount != 0) cj["elide"] = c->elideCount;
                if (c->adjustCount != 0) cj["adjust"] = c->adjustCount;
                if (c->leadStep != 0) cj["leadStep"] = c->leadStep;
                connArr.push_back(cj);
            }
        }
        j["connectors"] = connArr;
    }
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

    // Connectors: optional parallel vector; when absent, leave empty.
    // Accepts null (default connector), an integer (bare leadStep shorthand),
    // or a full object with elide/adjust/leadStep fields.
    if (j.contains("connectors")) {
        pt.connectors.clear();
        for (const auto& cj : j.at("connectors")) {
            if (cj.is_null()) {
                pt.connectors.push_back(std::nullopt);
            } else if (cj.is_number_integer()) {
                FigureConnector fc;
                fc.leadStep = cj.get<int>();
                pt.connectors.push_back(fc);
            } else {
                FigureConnector fc;
                fc.elideCount = cj.value("elide", 0);
                fc.adjustCount = cj.value("adjust", 0.0f);
                fc.leadStep = cj.value("leadStep", 0);
                pt.connectors.push_back(fc);
            }
        }
    }
}

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

inline void to_json(json& j, const PeriodSpec& ps) {
    j = json::object();
    j["variant"] = ps.variant;
    j["bars"] = ps.bars;
    j["antecedent"] = ps.antecedent;
    j["consequent"] = ps.consequent;
    if (ps.consequentTransform) j["consequentTransform"] = *ps.consequentTransform;
    if (ps.consequentTransformParam != 0) j["consequentTransformParam"] = ps.consequentTransformParam;
    if (ps.leadingConnective) j["leadingConnective"] = *ps.leadingConnective;
}
inline void from_json(const json& j, PeriodSpec& ps) {
    ps = PeriodSpec{};
    ps.variant = j.value("variant", PeriodVariant::Parallel);
    ps.bars = j.value("bars", 4.0f);
    // The antecedent/consequent PhraseTemplates inside a PeriodSpec don't
    // require their own startingPitch in JSON — placement comes from the
    // enclosing PassageTemplate's startingPitch + period flow. So we do
    // not use from_json(PhraseTemplate) directly (which enforces startingPitch);
    // instead we read each phrase's fields manually here.
    auto loadPhrase = [](const json& pj) {
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
        ph.totalBeats = pj.value("totalBeats", 0.0f);
        ph.cadenceType = pj.value("cadenceType", 0);
        ph.cadenceTarget = pj.value("cadenceTarget", -1);
        if (pj.contains("function")) from_json(pj.at("function"), ph.function);
        ph.seed = pj.value("seed", 0u);
        ph.locked = pj.value("locked", false);
        ph.strategy = pj.value("strategy", std::string(""));
        if (pj.contains("connectors")) {
            ph.connectors.clear();
            for (const auto& cj : pj.at("connectors")) {
                if (cj.is_null()) {
                    ph.connectors.push_back(std::nullopt);
                } else if (cj.is_number_integer()) {
                    FigureConnector fc;
                    fc.leadStep = cj.get<int>();
                    ph.connectors.push_back(fc);
                } else {
                    FigureConnector fc;
                    fc.elideCount = cj.value("elide", 0);
                    fc.adjustCount = cj.value("adjust", 0.0f);
                    fc.leadStep = cj.value("leadStep", 0);
                    ph.connectors.push_back(fc);
                }
            }
        }
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
// PassageTemplate
// ===========================================================================

inline void to_json(json& j, const PassageTemplate& pt) {
    j = json{{"phrases", pt.phrases}};
    if (!pt.name.empty()) j["name"] = pt.name;
    if (pt.startingPitch) j["startingPitch"] = *pt.startingPitch;
    if (!pt.character.empty()) j["character"] = pt.character;
    if (!pt.fromKey.empty()) j["fromKey"] = pt.fromKey;
    if (!pt.toKey.empty()) j["toKey"] = pt.toKey;
    if (!pt.strategy.empty()) j["strategy"] = pt.strategy;
    if (pt.seed != 0) j["seed"] = pt.seed;
    if (pt.locked) j["locked"] = true;
    if (!pt.periods.empty()) j["periods"] = pt.periods;
    if (!pt.voicingSelector.empty()) j["voicingSelector"] = pt.voicingSelector;
    if (pt.voicingPriority != 0.0f) j["voicingPriority"] = pt.voicingPriority;
    if (!pt.voicingDictionary.empty()) j["voicingDictionary"] = pt.voicingDictionary;
}

inline void from_json(const json& j, ChordAccompanimentConfig& cc) {
    if (j.contains("defaultPattern")) {
        cc.defaultPattern.clear();
        for (auto& v : j["defaultPattern"]) cc.defaultPattern.push_back(v.get<float>());
    }
    if (j.contains("overrides")) {
        for (auto& ov : j["overrides"]) {
            ChordAccompanimentConfig::BarOverride bo;
            for (auto& b : ov["bars"]) bo.bars.push_back(b.get<int>());
            for (auto& v : ov["pattern"]) bo.pattern.push_back(v.get<float>());
            cc.overrides.push_back(std::move(bo));
        }
    }
    cc.octave = j.value("octave", 3);
    cc.inversion = j.value("inversion", 0);
    cc.spread = j.value("spread", 0);
}

inline void from_json(const json& j, PassageTemplate& pt) {
    if (j.contains("startingPitch")) {
        Pitch p;
        from_json(j.at("startingPitch"), p);
        pt.startingPitch = p;
    }

    // phrases[] may be empty when periods[] drives the passage.
    pt.phrases.clear();
    if (j.contains("phrases") && j.at("phrases").is_array()) {
        for (auto& pj : j.at("phrases")) {
            PhraseTemplate ph; from_json(pj, ph);
            pt.phrases.push_back(std::move(ph));
        }
    }
    pt.name = j.value("name", std::string(""));
    pt.character = j.value("character", std::string(""));
    pt.fromKey = j.value("fromKey", std::string(""));
    pt.toKey = j.value("toKey", std::string(""));
    pt.strategy = j.value("strategy", std::string(""));
    pt.seed = j.value("seed", 0u);
    pt.locked = j.value("locked", false);

    // Optional period scaffolding.
    if (j.contains("periods") && j.at("periods").is_array()) {
        pt.periods.clear();
        for (const auto& pj : j.at("periods")) {
            PeriodSpec ps; from_json(pj, ps);
            pt.periods.push_back(std::move(ps));
        }
    }

    // Optional chord accompaniment config.
    if (j.contains("chordConfig")) {
        ChordAccompanimentConfig cc;
        from_json(j["chordConfig"], cc);
        pt.chordConfig = cc;
    }

    pt.voicingSelector = j.value("voicingSelector", std::string(""));
    pt.voicingPriority = j.value("voicingPriority", 0.0f);
    pt.voicingDictionary = j.value("voicingDictionary", std::string(""));
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

inline void to_json(json& j, const PieceTemplate::SectionTemplate& sd) {
    j = json{{"name", sd.name}, {"beats", sd.beats}};
    if (!sd.scaleOverride.empty()) j["scaleOverride"] = sd.scaleOverride;
    if (!sd.progressionName.empty()) j["progressionName"] = sd.progressionName;
    if (sd.chordProgression) j["chordProgression"] = *sd.chordProgression;
    if (!sd.keyContexts.empty()) {
        json arr = json::array();
        for (const auto& kc : sd.keyContexts) {
            json jkc;
            jkc["beat"] = kc.beat;
            jkc["key"] = kc.key.to_string();
            if (kc.scaleOverride) {
                jkc["scaleOverride"] = *kc.scaleOverride;  // uses to_json(Scale)
            }
            arr.push_back(std::move(jkc));
        }
        j["keyContexts"] = std::move(arr);
    }
}
inline void from_json(const json& j, PieceTemplate::SectionTemplate& sd) {
    sd.name = j.at("name").get<std::string>();
    sd.beats = j.at("beats").get<float>();
    sd.scaleOverride = j.value("scaleOverride", std::string(""));

    // progressionName
    if (j.contains("progressionName")) {
        sd.progressionName = j["progressionName"].get<std::string>();
    }

    // Inline chord progression (overrides progressionName)
    if (j.contains("chordProgression")) {
        ChordProgression prog;
        for (const auto& entry : j["chordProgression"]) {
            int degree = entry["degree"].get<int>();
            std::string quality = entry.value("quality", "Major");
            float beats = entry["beats"].get<float>();
            prog.add(degree, quality, beats);
        }
        sd.chordProgression = prog;
    }

    // Key contexts
    if (j.contains("keyContexts")) {
        for (const auto& kc : j["keyContexts"]) {
            KeyContext ctx;
            ctx.beat = kc["beat"].get<float>();
            std::string keyName = kc["key"].get<std::string>();
            ctx.key = Key::get(keyName);
            if (kc.contains("scaleOverride")) {
                Scale s;
                from_json(kc.at("scaleOverride"), s);
                ctx.scaleOverride = s;
            }
            sd.keyContexts.push_back(ctx);
        }
    }

    if (j.contains("styleName")) {
        sd.styleName = j["styleName"].get<std::string>();
    }
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
            PieceTemplate::SectionTemplate sd; from_json(sj, sd);
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

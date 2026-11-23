#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/realization_strategy.h"
#include "mforce/compose/register_builtin.h"
#include "mforce/compose/strategy_choice.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/music/basics.h"
#include "mforce/music/music_json.h"
#include "mforce/music/structure.h"
#include "mforce/shelved/pedal_buildup_strategy.h"
#include "mforce/shelved/register_shelved.h"
#include "mforce/shelved/sequence_passage_strategy.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


void run_settings_of_tests() {
    std::printf("settings_of: defaults when empty, the value when present, a named throw\n");
    const StrategyChoice empty{"pedal_buildup", {}};
    CHECK(settings_of<PedalBuildupSettings>(empty).climbStep == 2);
    PedalBuildupSettings given;
    given.levels = 3;
    CHECK(settings_of<PedalBuildupSettings>(StrategyChoice{"pedal_buildup", given}).levels == 3);
    bool named = false;
    try {
        (void)settings_of<SequencePassageSettings>(StrategyChoice{"pedal_buildup", given});
    } catch (const std::exception& e) {
        const std::string m = e.what();
        named = m.find("pedal_buildup") != std::string::npos
             && m.find("PedalBuildupSettings") != std::string::npos
             && m.find("SequencePassageSettings") != std::string::npos;
    }
    CHECK(named);
}

// True when every member of `want` is in `got` with an equal value (numbers
// compared by value): what a codec read and wrote back keeps of an input.
static bool json_holds(const json& want, const json& got) {
    if (want.is_object()) {
        if (!got.is_object()) return false;
        for (const auto& [k, v] : want.items())
            if (!got.contains(k) || !json_holds(v, got.at(k))) return false;
        return true;
    }
    if (want.is_array()) {
        if (!got.is_array() || got.size() != want.size()) return false;
        for (size_t i = 0; i < want.size(); ++i)
            if (!json_holds(want[i], got[i])) return false;
        return true;
    }
    if (want.is_number() && got.is_number()) return want.get<double>() == got.get<double>();
    return want == got;
}

// The settings of `name` at `level` read from `text` (every field set to a
// value that is not its default) and written back hold every value of the
// input, and a second round trip writes the same.
static bool settings_round_trip(const SettingsCodecs& codecs, StrategyLevel level,
                                const char* name, const std::string& text) {
    const json in = json{{"name", name}, {"settings", json::parse(text)}};
    const json once = codecs.write(codecs.read(in, level), level);
    const json twice = codecs.write(codecs.read(once, level), level);
    return json_holds(in["settings"], once["settings"]) && once == twice;
}

// Every settings type, every field set to a value that is not its default.
static std::vector<std::pair<StrategyLevel, std::pair<const char*, std::string>>> settings_cases() {
    const std::string fig = R"({"units": [{"duration": 1.5, "step": 0}, {"duration": 0.5, "step": 2}]})";
    const std::string cons = R"({"count": 5, "length": 2.5, "net": 1, "ceiling": 4, "floor": -3,
        "defaultPulse": 0.5, "minPulse": 0.25, "maxPulse": 1.5})";
    const std::string phrase = R"({"name": "a", "figures": [{"source": "generate", "totalBeats": 4.0}],
        "cadenceType": 1, "cadenceTarget": 4, "seed": 3, "strategy": "default_phrase",
        "connectors": [null, {"leadStep": 1}]})";
    using L = StrategyLevel;
    return {
        {L::Phrase, {"period_phrase", R"({"basicIdea": )" + fig + R"(, "antecedentTail": )" + fig +
            R"(, "consequentTail": )" + fig + R"(, "halfCadenceTarget": 3})"}},
        {L::Phrase, {"sentence_phrase", R"({"basicIdea": )" + fig + R"(, "continuation": )" + fig +
            R"(, "variationTransposition": 2})"}},
        {L::Phrase, {"two_figure_phrase", R"({"method": "by_length", "count": 6, "length": 3.0,
            "constraints": )" + cons + R"(, "seed": 9, "transform": "reverse", "transformParam": 2})"}},
        {L::Phrase, {"elaborated_phrase", R"({"skeleton": )" + fig + R"(, "buildMethod": "by_length",
            "buildCount": 5, "buildLength": 6.0, "buildConstraints": )" + cons +
            R"(, "choiceMode": "all_leave", "generateConstraints": )" + cons + R"(, "seed": 4})"}},
        {L::Passage, {"library_passage", R"({"patternName": "BillyLife", "barsHint": 8, "seed": 2})"}},
        {L::Passage, {"pedal_buildup", R"({"levels": 3, "climbStep": 1, "cellBeats": 2.5,
            "holdBeats": 1.5, "rangeCap": 12, "maxTries": 3, "seed": 7})"}},
        {L::Passage, {"sequence_passage", R"({"entries": 6, "stepA": -1, "stepB": 1,
            "cellBeats": 2.5, "rangeCap": 12, "maxTries": 3, "seed": 7})"}},
        {L::Passage, {"connective_passage", R"({"entries": 2, "targetDegree": 3, "tailUnits": 2,
            "finalAugment": 2.5, "cellBeats": 1.5, "rangeCap": 12, "maxTries": 3, "seed": 7})"}},
        {L::Passage, {"wandering_passage", R"({"entries": 5, "pullThreshold": 3, "pullAmount": 1,
            "cellBeats": 1.5, "rangeCap": 12, "maxTries": 3, "seed": 7})"}},
        {L::Passage, {"period_passage", R"({"periods": [{"variant": "Modified", "bars": 8.0,
            "antecedent": )" + phrase + R"(, "consequent": )" + phrase + R"(,
            "consequentTransform": "invert", "consequentTransformParam": 2,
            "leadingConnective": "link"}]})"}},
        {L::Realization, {"rhythm_pattern", R"({"defaultPattern": [2.0, -1.0, 1.0],
            "overrides": [{"bars": [2], "pattern": [4.0]}]})"}},
        {L::ProfileSelector, {"random", R"({"priorityMin": 0.25, "priorityMax": 0.75,
            "inversionProfiles": [[0], [1, 2]], "spreadProfiles": [[1]]})"}},
        {L::ProfileSelector, {"drift", R"({"priorityMin": 0.25, "priorityMax": 0.75,
            "priorityStepMax": 0.5, "profileTransitionProb": 0.5,
            "inversionProfiles": [[2]], "spreadProfiles": [[0, 1]]})"}},
        {L::ProfileSelector, {"scripted", R"({"sequence": [{"priority": 0.5,
            "allowedInversions": [1], "allowedSpreads": [0, 2]}]})"}},
    };
}

void run_settings_codec_tests() {
    std::printf("every settings type through its codec: every field read and written back\n");
    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    register_shelved(kit, codecs);
    bool all = true;
    for (const auto& [level, c] : settings_cases()) {
        const bool ok = settings_round_trip(codecs, level, c.first, c.second);
        if (!ok) std::printf("  round trip failed: %s\n", c.first);
        all = all && ok;
    }
    CHECK(all);
}

// True when reading `j` throws and the message holds every one of `parts`.
static bool read_throws_naming(const json& j, std::initializer_list<const char*> parts) {
    try {
        (void)template_from(j);
    } catch (const std::exception& e) {
        const std::string m = e.what();
        for (const char* p : parts) if (m.find(p) == std::string::npos) return false;
        return true;
    }
    return false;
}

void run_template_refusal_tests() {
    std::printf("templates: each old key is refused by name; an unregistered strategy too\n");
    const json probe = compose_probe_json();
    bool all = true;
    for (const char* key : {"libraryConfig", "pedalBuildupConfig", "sequenceConfig",
                            "connectiveConfig", "wanderingConfig", "periods",
                            "realizationStrategy", "rhythmPattern",
                            "voicingProfileSelectorConfig"}) {
        json j = probe;
        j["parts"][0]["passages"]["Main"][key] = json::object();
        all = all && read_throws_naming(j, {key, "tools/convert_template.py"});
    }
    for (const char* key : {"periodConfig", "sentenceConfig", "twoFigureConfig",
                            "elaboratedConfig"}) {
        json j = probe;
        j["parts"][0]["passages"]["Main"]["phrases"][0][key] = json::object();
        all = all && read_throws_naming(j, {key, "tools/convert_template.py"});
    }
    CHECK(all);
    json j = probe;
    j["parts"][0]["passages"]["Main"]["strategy"] = "no_such_passage";
    CHECK(read_throws_naming(j, {"no_such_passage"}));
    j = probe;
    j["parts"][1]["passages"]["Main"]["realization"] = "no_such_realization";
    CHECK(read_throws_naming(j, {"no_such_realization"}));
    j = probe;
    j["parts"][0]["passages"]["Main"]["strategy"] = json{{"name", "default_passage"},
                                                         {"settings", json::object()}};
    CHECK(read_throws_naming(j, {"default_passage", "takes no settings"}));
}

void run_settings_required_tests() {
    std::printf("the five strategies that need settings throw without them\n");
    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    register_shelved(kit, codecs);
    const PieceTemplate probe = template_from(compose_probe_json());
    bool all = true;
    for (const char* name : {"period_phrase", "sentence_phrase", "two_figure_phrase",
                             "elaborated_phrase"}) {
        PieceTemplate t = probe;
        t.parts[0].passages["Main"].phrases[0].strategy.name = name;
        all = all && compose_throws_naming(t, kit, std::string(name) + ": given no settings");
    }
    PieceTemplate t = probe;
    t.parts[0].passages["Main"].strategy.name = "library_passage";
    all = all && compose_throws_naming(t, kit, "library_passage: given no settings");
    CHECK(all);
}


// The kit register_builtin fills knows none of the eleven shelved strategies
// and composes a template that names no strategy; register_shelved adds all
// eleven.
void run_shelved_registration_tests() {
    std::printf("registration: register_builtin alone lacks the shelved eleven; register_shelved adds them\n");
    const char* phrases[] = {"period_phrase", "sentence_phrase", "two_figure_phrase",
                             "elaborated_phrase"};
    const char* passages[] = {"alternating_figure", "period_passage", "library_passage",
                              "pedal_buildup", "sequence_passage", "connective_passage",
                              "wandering_passage"};
    auto throws_naming = [](auto lookup, const std::string& name) {
        try { lookup(); } catch (const std::exception& e) {
            return std::string(e.what()).find(name) != std::string::npos;
        }
        return false;
    };
    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    bool unknown = true;
    for (const char* n : phrases)
        unknown = unknown && throws_naming([&] { (void)kit.strategies.phrase(n); }, n);
    for (const char* n : passages)
        unknown = unknown && throws_naming([&] { (void)kit.strategies.passage(n); }, n);
    CHECK(unknown);
    CHECK(!compose(template_from(compose_probe_json()), kit).piece.parts.empty());
    register_shelved(kit, codecs);
    bool known = true;
    for (const char* n : phrases) known = known && kit.strategies.phrase(n).name() == n;
    for (const char* n : passages) known = known && kit.strategies.passage(n).name() == n;
    CHECK(known);
}

// Part 0 a harmony part with a chordConfig (octave 5, inversion 1) in both
// sections; part 1 a melody with a passage for the second section only.
static json missing_passage_json() {
    return json::parse(R"({
      "keyName": "C", "scaleName": "Major", "bpm": 120, "masterSeed": 5,
      "sections": [
        {"name": "A", "beats": 8, "chordProgression": [
          {"degree": 0, "beats": 4}, {"degree": 4, "beats": 4}]},
        {"name": "B", "beats": 8, "chordProgression": [
          {"degree": 3, "beats": 4}, {"degree": 0, "beats": 4}]}],
      "parts": [
        {"name": "chords", "role": "harmony", "passages": {
          "A": {"chordConfig": {"octave": 5, "inversion": 1}},
          "B": {"chordConfig": {"octave": 5, "inversion": 1}}}},
        {"name": "melody", "role": "melody", "passages": {
          "B": {"startingPitch": {"octave": 5, "pitch": "C"},
                "phrases": [{"name": "p", "startingPitch": {"octave": 5, "pitch": "C"},
                  "figures": [{"source": "generate", "totalBeats": 4},
                              {"source": "generate", "totalBeats": 4}]}]}}}]
    })");
}

void run_default_passage_place_tests() {
    std::printf("a part's default passage is composed and recorded at its own part and section\n");
    const PieceTemplate request = template_from(missing_passage_json());
    const Composition c = compose_builtin(request);
    const ChordDef* major = &ChordDef::get("Major");
    const Chord want = ScaleChord{0, 0, major}.resolve(Scale::get("C", "Major"), 5, 4.0f, 1, 0);
    const auto& els = c.piece.parts[0].elementSequence.elements;
    bool voiced = !els.empty() && els[0].is_chord()
        && els[0].chord().pitches.size() == want.pitches.size();
    for (size_t k = 0; voiced && k < want.pitches.size(); ++k)
        voiced = els[0].chord().pitches[k].note_number() == want.pitches[k].note_number();
    CHECK(voiced);
    CHECK(json_of(c.plan)["parts"][0] == json_of(request)["parts"][0]);
    CHECK(request.parts[1].passages.count("A") == 0);
    CHECK(c.plan.parts[1].passages.count("A") == 1);
}

void run_kit_reuse_tests() {
    std::printf("one kit composes a request twice alike, and two seeds as fresh kits do\n");
    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    register_shelved(kit, codecs);
    const PieceTemplate request = template_from(compose_probe_json());
    const json a = compose(request, kit).piece;
    const json b = compose(request, kit).piece;
    CHECK(a == b);
    PieceTemplate seven = request, eight = request;
    seven.masterSeed = 7;
    eight.masterSeed = 8;
    const json sevenShared = compose(seven, kit).piece;
    const json eightShared = compose(eight, kit).piece;
    CHECK(sevenShared == json(compose_builtin(seven).piece));
    CHECK(eightShared == json(compose_builtin(eight).piece));
    CHECK(sevenShared != eightShared);
}

void run_wrong_pairing_tests() {
    std::printf("settings of another type given to a NoSettings strategy throw; a choice holds name and settings only\n");
    ComposeKit kit;
    SettingsCodecs codecs;
    register_builtin(kit, codecs);
    const PieceTemplate probe = template_from(compose_probe_json());
    PieceTemplate t = probe;
    t.parts[0].passages["Main"].strategy = StrategyChoice{"default_passage", PedalBuildupSettings{}};
    CHECK(compose_throws_naming(t, kit, "default_passage"));
    t = probe;
    t.parts[0].passages["Main"].phrases[0].strategy = StrategyChoice{"default_phrase", RhythmPattern{}};
    CHECK(compose_throws_naming(t, kit, "default_phrase"));
    t = probe;
    t.parts[1].passages["Main"].realization = StrategyChoice{"block", RhythmPattern{}};
    CHECK(compose_throws_naming(t, kit, "block"));
    t = probe;
    t.parts[1].passages["Main"].voicingSelector = "smooth";
    t.parts[1].passages["Main"].voicingProfileSelector = StrategyChoice{"static", RhythmPattern{}};
    CHECK(compose_throws_naming(t, kit, "static"));

    json j = compose_probe_json();
    j["parts"][0]["passages"]["Main"]["strategy"] =
        json{{"name", "default_passage"}, {"setings", json::object()}};
    CHECK(read_throws_naming(j, {"setings"}));
    j = compose_probe_json();
    j["parts"][1]["passages"]["Main"]["realization"] = json{{"name", "block"}, {"why", 1}};
    CHECK(read_throws_naming(j, {"why"}));
}

void run_period_phrase_refusal_tests() {
    std::printf("an old phrase key inside a period's antecedent or consequent is refused\n");
    bool all = true;
    for (const char* side : {"antecedent", "consequent"}) {
        for (const char* key : {"periodConfig", "sentenceConfig", "twoFigureConfig",
                                "elaboratedConfig"}) {
            json period = json::parse(R"({"variant": "Parallel", "antecedent": {}, "consequent": {}})");
            period[side][key] = json::object();
            json j = compose_probe_json();
            j["parts"][0]["passages"]["Main"]["strategy"] =
                json{{"name", "period_passage"}, {"settings", {{"periods", json::array({period})}}}};
            all = all && read_throws_naming(j, {key, "tools/convert_template.py"});
        }
    }
    CHECK(all);
}

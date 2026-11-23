#include "mforce/perform/embedded_score.h"
#include "mforce/music/basics.h"
#include "mforce/perform/conductor.h"
#include "mforce/music/music_json.h"
#include "mforce/perform/time_map.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace mforce {

namespace {

// What the reader says once per score: a "hold" on an instrument that does
// not sustain, and each onset name outside the instrument's vocabulary.
struct ScoreWarnings {
    std::vector<std::string>& out;
    bool notSustaining{false};
    std::set<std::string> onsets;

    void hold_ignored() {
        if (notSustaining) return;
        notSustaining = true;
        out.push_back("[score] \"hold\" ignored: this patch's instrument block is not marked \"sustaining\"");
    }
    void unknown_onset(const std::string& name) {
        if (onsets.insert(name).second)
            out.push_back("[onset] name '" + name + "' is not in this instrument's onsets[] vocabulary "
                          "— it will never fire");
    }
};

// The onset id of a score note's name. A name outside the instrument's
// vocabulary is warned once; an instrument with no vocabulary maps every name
// to 0 without a word (lines still play, gestures do not exist there).
int onset_of(const InstrumentClass& cls, const std::string& name, ScoreWarnings& warn)
{
    const int id = cls.onset_id(name);
    if (id == 0 && !name.empty() && !cls.onsets.empty()) warn.unknown_onset(name);
    return id;
}

// An unphrased note: its articulation and ornament, as the score gives them.
Note unphrased_note(const json& noteJson, float note, float velocity, float duration)
{
    Articulation art = articulations::Default{};
    if (noteJson.contains("articulation"))
        from_json(noteJson.at("articulation"), art);
    Ornament orn{};
    if (noteJson.contains("ornament"))
        from_json(noteJson.at("ornament"), orn);
    return Note{note, velocity, duration, art, orn};
}

// The embedded score as one performance, in score order: a NotePerformer at
// 60 bpm (beats are seconds, as the score's "time" and "duration" are), the
// keys "hold" and "onset", articulation and ornament on unphrased notes.
// A phrased note first concludes the note the performer holds pending.
//
// The score's keys: every note is atomic and carries its own two facts — "onset" (a name from the instrument's
// onsets[] vocabulary: how it begins) and "hold" (does the excitation
// continue past its end). There is no phrase object, no grouping pass and no
// lookahead: the engine derives continuation from its own state. A note with
// no "time" starts where its predecessor ended, which is what makes a held
// line sequential.
Performance perform_notes(const InstrumentClass& cls, const json& score, ScoreWarnings& warn)
{
    NotePerformer performer;
    const TimeMap time(60.0f);
    Performance out;
    bool lineOpen = false;
    float prevEnd = 0.0f;
    for (const auto& noteJson : score) {
        float note     = noteJson.at("note").get<float>();
        float velocity = noteJson.value("velocity", 0.8f);
        float duration = noteJson.at("duration").get<float>();
        float start    = noteJson.contains("time")
                       ? noteJson["time"].get<float>() : prevEnd;
        bool hold = noteJson.value("hold", false);
        std::string onset = noteJson.value("onset", std::string());
        // The piano rule: a non-sustaining instrument never receives hold,
        // so a phrase-marked score on it emits ordinary independent notes.
        if (hold && !cls.sustaining) {
            warn.hold_ignored();
            hold = false;
        }
        prevEnd = start + duration;

        if (hold || lineOpen || !onset.empty()) {
            if (noteJson.contains("articulation") || noteJson.contains("ornament"))
                throw std::runtime_error("score: articulation/ornament are unsupported on phrased notes");
            performer.conclude(out);
            out.notes.push_back({.start = start, .duration = duration,
                                 .noteNumber = note, .velocity = velocity,
                                 .onset = onset_of(cls, onset, warn), .hold = hold});
            lineOpen = hold;
            continue;
        }
        performer.perform_note(unphrased_note(noteJson, note, velocity, duration),
                               time.seconds_at(start), time.bpm_at(start), out);
    }
    performer.conclude(out);
    return out;
}

// The score's end in seconds, in double from the JSON values. A note with no
// "time" starts where its predecessor ended — the same rule the reader above
// uses.
double score_end(const json& score)
{
    double maxEnd = 0;
    double prevEnd = 0;
    for (const auto& noteJson : score) {
        double d = noteJson.at("duration").get<double>();
        double t = noteJson.contains("time")
            ? noteJson["time"].get<double>()
            : prevEnd;
        prevEnd = t + d;
        maxEnd = std::max(maxEnd, prevEnd);
    }
    return maxEnd;
}

} // namespace

EmbeddedScore perform_embedded_score(const std::string& scoreJson, const InstrumentClass& cls)
{
    const json score = json::parse(scoreJson);
    EmbeddedScore result;
    ScoreWarnings warn{result.warnings};
    result.performance = perform_notes(cls, score, warn);
    result.endSeconds = score_end(score);
    return result;
}

} // namespace mforce

#include "mforce/render/embedded_score.h"
#include "mforce/render/instrument.h"
#include "mforce/music/basics.h"
#include "mforce/music/conductor.h"
#include "mforce/music/music_json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

using json = nlohmann::json;

namespace mforce {

// The embedded score, delivered to the instrument the way the pre-render
// does it: a NotePerformer at 60 bpm (beats are seconds, as the score's
// "time" and "duration" are), the v2 keys "hold" and "onset", articulation
// and ornament on unphrased notes, the open line finished at the end.
static void perform_notes(PitchedInstrument& inst, const json& score)
{
    NotePerformer performer;
    const float bpm = 60.0f;
    // v2 score keys (spec 2026-09-20-note-onsets-v2 §3): every
    // note is atomic and carries its own two facts — "onset" (a
    // name from the instrument's onsets[] vocabulary: how it
    // begins) and "hold" (does the excitation continue past its
    // end). There is no phrase object, no grouping pass and no
    // lookahead: the engine derives continuation from its own
    // state. A note with no "time" starts where its predecessor
    // ended, which is what makes a held line sequential. These
    // scores play Performer for themselves — a real Performer
    // stamps the same two facts from phrase notation.
    bool lineOpen = false;
    bool warnedNotSustaining = false;
    float prevEnd = 0.0f;
    for (size_t i = 0; i < score.size(); ++i) {
        const auto& noteJson = score[i];
        float note     = noteJson.at("note").get<float>();
        float velocity = noteJson.value("velocity", 0.8f);
        float duration = noteJson.at("duration").get<float>();
        float start    = noteJson.contains("time")
                       ? noteJson["time"].get<float>() : prevEnd;
        bool hold = noteJson.value("hold", false);
        std::string onset = noteJson.value("onset", std::string());
        // The piano invariant (§2): a non-sustaining instrument
        // never receives hold, so a phrase-marked score on it
        // emits ordinary independent notes.
        if (hold && !inst.sustaining) {
            if (!warnedNotSustaining) {
                warnedNotSustaining = true;
                std::fprintf(stderr, "[score] \"hold\" ignored: this "
                    "patch's instrument block is not marked "
                    "\"sustaining\"\n");
            }
            hold = false;
        }

        if (hold || lineOpen || !onset.empty()) {
            if (noteJson.contains("articulation") ||
                noteJson.contains("ornament"))
                throw std::runtime_error("score: articulation/"
                    "ornament are unsupported on phrased notes "
                    "(spec 2026-09-20 §3)");
            inst.play_note({note, velocity, duration,
                             inst.onset_id(onset), hold}, start);
            lineOpen = hold;
            prevEnd = start + duration;
            continue;
        }

        Articulation art = articulations::Default{};
        if (noteJson.contains("articulation"))
            from_json(noteJson.at("articulation"), art);

        Ornament orn{};
        if (noteJson.contains("ornament"))
            from_json(noteJson.at("ornament"), orn);

        Note n{note, velocity, duration, art, orn};
        performer.perform_note(n, start, bpm, inst);
        prevEnd = start + duration;
    }
    inst.finish_open_lines();   // §4.3 end-of-score safety
    performer.conclude(bpm, inst);
}

// The score's end in seconds, in double from the JSON values. A note with no
// "time" starts where its predecessor ended — the same rule the delivery
// loop above uses.
static double score_end(const json& score)
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

double play_embedded_score(PitchedInstrument& inst, const std::string& scoreText)
{
    const json score = json::parse(scoreText);
    perform_notes(inst, score);
    return score_end(score);
}

} // namespace mforce

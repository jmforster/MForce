#include "mforce/testing/check.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/music/music_json.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/perform/embedded_score.h"
#include "mforce/play/play.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/pitched_instrument.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;


// An instrument as the tests hold it: the build, the patch's rate and what
// the build had to say.
struct LoadedInstrument {
    std::unique_ptr<PitchedInstrument> instrument;
    int sampleRate{48000};
    std::vector<std::string> warnings;
};

// The instrument `json` describes, read through the codec and built with at
// least `minPolyphony` voices.
static LoadedInstrument load_inst(const std::string& json, int minPolyphony = 0) {
    const mforce::Patch patch = patch_door::read_text(json);
    LoadedInstrument loaded;
    loaded.instrument = build_instrument(patch, minPolyphony, &loaded.warnings);
    loaded.sampleRate = patch.sampleRate;
    return loaded;
}


// The frame count play gives a patch file; -1, with the reason printed, when
// the load throws.
static int frames_of(const char* path, const char* text) {
    ScratchPatch file(path, text);
    int frames = -1;
    try { frames = play(load_patch(file.path)).frames; }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return frames;
}

// The score performed for `inst`'s class and played on it; its end in seconds.
static double play_score(PitchedInstrument& inst, const std::string& score) {
    EmbeddedScore performed = perform_embedded_score(score, inst.instrument_class());
    inst.play(performed.performance);
    return performed.endSeconds;
}

// The score a patch file carries under "score": performed and its end
// returned in seconds; play turns the end into the frame count.
void run_embedded_score_tests() {
    std::printf("the embedded score: its end, hold, the phrased refusal, the frames\n");
    const char* plain = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* sustaining = R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    // A fresh instrument per score.
    auto end_of = [](const char* patch, const char* score) {
        auto ip = load_inst(patch);
        return play_score(*ip.instrument, score);
    };
    // A note with no "time" starts where its predecessor ended.
    CHECK(end_of(plain, R"([{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}])") == 1.75);
    // The end is the maximum, not the last note's.
    CHECK(end_of(plain, R"([{"note":60,"time":0,"duration":3},{"note":62,"time":1,"duration":0.5}])") == 3.0);
    CHECK(end_of(plain, "[]") == 0.0);
    // "hold" on a non-sustaining instrument is ignored and the end still
    // counted: no line is open after the first note, so the second takes its
    // articulation. The same score on a sustaining instrument is refused,
    // because there the line is open.
    {
        const char* heldThenArticulated = R"([{"note":60,"time":0,"duration":1,"hold":true},
          {"note":62,"duration":1,"articulation":{"type":"Staccato"}}])";
        bool threw = false;
        double end = -1.0;
        try { end = end_of(plain, heldThenArticulated); }
        catch (const std::exception&) { threw = true; }
        CHECK(!threw);
        CHECK(end == 2.0);
        bool named = false;
        try { (void)end_of(sustaining, heldThenArticulated); }
        catch (const std::exception& e) { named = std::string(e.what()).find("phrased") != std::string::npos; }
        CHECK(named);
    }
    // Articulation on a phrased note is refused, naming "phrased".
    {
        bool named = false;
        try { (void)end_of(sustaining, R"([{"note":60,"time":0,"duration":1,"hold":true,"articulation":"staccato"}])"); }
        catch (const std::exception& e) { named = std::string(e.what()).find("phrased") != std::string::npos; }
        CHECK(named);
    }
    // play's frame arithmetic: the render covers at least the score, a
    // larger "seconds" extends it.
    CHECK(frames_of("renders/scratch/_embedded_score_noseconds.json",
        R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}]})") == 84000);
    CHECK(frames_of("renders/scratch/_embedded_score_seconds5.json",
        R"({"sampleRate":48000,"seconds":5,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}]})") == 240000);
    CHECK(frames_of("renders/scratch/_embedded_score_seconds1.json",
        R"({"sampleRate":48000,"seconds":1,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0.5,"duration":1.0},{"note":62,"duration":0.25}]})") == 84000);
}

// The score's end is double arithmetic on the JSON values, and play
// rounds it to frames; a file with neither a score nor "seconds" renders five
// seconds.
void run_embedded_score_end_tests() {
    std::printf("the embedded score's end: double, rounded to frames, the five-second default\n");
    const char* plain = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    {
        auto ip = load_inst(plain);
        // 0.1 + 0.2 in double is not 0.1f + 0.2f widened.
        CHECK(play_score(*ip.instrument, R"([{"note":60,"time":0.1,"duration":0.2}])") == 0.1 + 0.2);
    }
    // 1.0000125 s at 48000 is 48000.6 frames: rounded, not truncated.
    CHECK(frames_of("renders/scratch/_embedded_score_rounding.json",
        R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]},
          "score":[{"note":60,"time":0,"duration":1.0000125}]})") == 48001);
    CHECK(frames_of("renders/scratch/_embedded_score_none.json", plain) == 240000);
}

// What the embedded score delivers to the instrument, observed on it: where
// each note starts, the velocity a note without one gets, which notes are
// phrased, and the open line finished at the end.
void run_embedded_score_delivery_tests() {
    std::printf("the embedded score's delivery: starts, the velocity default, onset-only, the open line\n");
    const char* plain = R"({"sampleRate":48000,"instrument":{"polyphony":3},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    // A note with no "time" starts where its predecessor ended.
    {
        auto ip = load_inst(plain);
        (void)play_score(*ip.instrument,
            R"([{"note":60,"time":0.25,"duration":0.5},{"note":62,"duration":0.25},{"note":64,"duration":0.5}])");
        const auto& notes = ip.instrument->rendered();
        const int sr = ip.instrument->render_context().sampleRate;
        CHECK(notes.size() == 3);
        CHECK(notes.size() == 3 && notes[0].startSample == int(0.25 * sr)
              && notes[1].startSample == int(0.75 * sr) && notes[2].startSample == int(1.0 * sr));
    }
    // A note with no "velocity" is played at 0.8.
    {
        auto delivered = [&](const char* score) {
            auto ip = load_inst(plain);
            (void)play_score(*ip.instrument, score);
            const auto& notes = ip.instrument->rendered();
            return notes.size() == 1 ? notes[0].samples : std::vector<float>{};
        };
        const auto unstated = delivered(R"([{"note":60,"time":0,"duration":0.25}])");
        CHECK(!unstated.empty());
        CHECK(unstated == delivered(R"([{"note":60,"time":0,"duration":0.25,"velocity":0.8}])"));
        CHECK(unstated != delivered(R"([{"note":60,"time":0,"duration":0.25,"velocity":0.4}])"));
    }
    // A note with an onset and no hold is phrased: articulation is refused on
    // it, and without the articulation it is delivered.
    {
        const char* plucked = R"({"sampleRate":48000,"instrument":{"polyphony":1,"onsets":["pluck"]},"graph":{"output":"osc","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
        bool named = false;
        try {
            auto ip = load_inst(plucked);
            (void)play_score(*ip.instrument,
                R"([{"note":60,"time":0,"duration":0.5,"onset":"pluck","articulation":{"type":"Staccato"}}])");
        }
        catch (const std::exception& e) { named = std::string(e.what()).find("phrased") != std::string::npos; }
        CHECK(named);
        auto ip = load_inst(plucked);
        (void)play_score(*ip.instrument, R"([{"note":60,"time":0,"duration":0.5,"onset":"pluck"}])");
        CHECK(ip.instrument->rendered().size() == 1);
    }
    // A score ending on a held note leaves no line open: the line reaches the
    // timeline at the end.
    {
        const char* sustaining = R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true},"graph":{"output":"osc","nodes":[
          {"id":"env","type":"Envelope","params":{"startVal":[0.0,1.0,1.0],"endVal":[1.0,1.0,0.0],
            "power":[0.0,0.0,0.0],"type":["Linear","Linear","Linear"],"percent":[0.05,0.0,0.25],
            "minSec":[0.0,0.0,0.0],"maxSec":[0.0,0.0,0.0],"nominal":[0.0,0.0,0.0]}},
          {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"env"}}}]}})";
        auto ip = load_inst(sustaining);
        (void)play_score(*ip.instrument,
            R"([{"note":60,"time":0,"duration":0.5},{"note":62,"duration":0.5,"hold":true}])");
        CHECK(ip.instrument->rendered().size() == 2);
    }
}

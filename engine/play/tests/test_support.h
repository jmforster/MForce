#pragma once
// Helpers the play tests share, and the suites one file runs from another.
#include "mforce/testing/check.h"
#include "render/tests/fixtures.h"
#include "mforce/contract/instrument_class.h"
#include "mforce/contract/performance.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/music/music_json.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/patch_json.h"
#include "mforce/play/play.h"
#include "mforce/render/instrument.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;


// An instrument that keeps each performance it is handed and renders each
// note as one sample of `level` at the note's start.
struct RecordingInstrument final : Instrument {
    std::vector<Performance> played;
    InstrumentClass cls;
    float level{0.5f};
    RecordingInstrument() { prepare(RenderContext{48000}); }
    const InstrumentClass& instrument_class() const override { return cls; }
    void play(const Performance& part) override {
        played.push_back(part);
        for (const auto& n : part.notes) { float s = level; add_rendered(n.start, &s, 1); }
    }
};

// A patch file in the scratch folder played, its stereo frames into `buf`.
// False, with the reason printed, when the load or the render throws.
inline bool render_scratch_patch(const char* path, const std::string& text, std::vector<float>& buf) {
    ScratchPatch file(path, text);
    bool rendered = false;
    try {
        buf = play(load_patch(file.path)).stereo;
        rendered = true;
    }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return rendered;
}

// Whether a mixer signal is a constant of `v` (prepared, not yet rendered:
// its current value is the constant's).
inline bool is_constant_of(const std::shared_ptr<ValueSource>& s, float v) {
    return s && std::string(s->type_name()) == "Constant" && s->current() == v;
}
namespace plain_parts {
struct Case { const char* name; nlohmann::json signals; std::vector<Chan> chans; nlohmann::json mix; };
inline std::vector<Case> cases() {
    const auto osc   = node("osc", "SineSource", {{"frequency", 220.0}, {"amplitude", 0.5}});
    const auto saw   = node("saw", "SawSource", {{"frequency", 330.0}, {"amplitude", 0.4}});
    const auto tri   = node("tri", "TriangleSource", {{"frequency", 440.0}, {"amplitude", 0.3}});
    const auto noise = node("noise", "WhiteNoiseSource", {{"amplitude", 0.2}, {"seed", 12345}});
    const auto lfo   = node("lfo", "SineSource", {{"frequency", 3.0}, {"amplitude", 0.4}});
    using J = nlohmann::json;
    return {
        {"one channel, all four numbers", J::array({osc}), {{"ch1", "osc", {{"volume", 0.7}, {"pan", -0.3}}}},
         {{"gainL", 0.9}, {"gainR", 0.8}}},
        {"no numbers on either part", J::array({osc}), {{"ch1", "osc", J()}}, J()},
        {"two channels", J::array({osc, noise}),
         {{"chA", "osc", {{"volume", 0.3}, {"pan", -0.7}}}, {"chB", "noise", {{"volume", 0.9}, {"pan", 0.4}}}},
         {{"gainL", 0.8}, {"gainR", 0.6}}},
        {"three channels", J::array({osc, saw, tri}),
         {{"chA", "osc", {{"volume", 0.6}, {"pan", -0.5}}}, {"chB", "saw", {{"volume", 0.7}, {"pan", 0.5}}}, {"chC", "tri", J()}},
         {{"gainL", 0.9}, {"gainR", 0.7}}},
        {"volume wired", J::array({lfo, osc}), {{"ch1", "osc", {{"volume", ref("lfo")}, {"pan", 0.2}}}},
         {{"gainL", 1.0}, {"gainR", 1.0}}},
        {"pan wired", J::array({lfo, osc}), {{"ch1", "osc", {{"volume", 0.8}, {"pan", ref("lfo")}}}},
         {{"gainL", 1.0}, {"gainR", 1.0}}},
        {"gainL wired", J::array({lfo, osc}), {{"ch1", "osc", {{"volume", 0.8}, {"pan", 0.0}}}},
         {{"gainL", ref("lfo")}, {"gainR", 0.7}}},
    };
}
// Plain sounds whose pull loops share no node: two channels on two sources,
// a wired pan on a node of its own, and a tap from one loop into another.
inline std::vector<std::string> sharing_sounds_that_load() {
    using J = nlohmann::json;
    const auto all = cases();
    const J tapAcross = J::array({node("oscA", "SineSource", {{"frequency", 220.0}, {"amplitude", 0.4}}),
                                  node("oscB", "SawSource", {{"frequency", 330.0}, {"amplitude", 0.4}}),
                                  node("comb", "CombinedSource", {{"operation", 0}, {"gainAdj", 0.0}, {"source1", ref("oscA")},
                                                                  {"source2", J{{"tap", "oscB"}}}})});
    return {sound(all[2].signals, all[2].chans, all[2].mix), sound(all[5].signals, all[5].chans, all[5].mix),
            sound(tapAcross, {{"chA", "comb", J()}, {"chB", "oscB", J()}}, J())};
}
}  // namespace plain_parts

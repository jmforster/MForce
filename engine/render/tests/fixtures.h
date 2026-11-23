#pragma once
// Helpers the render tests share with the test programs of the modules above it.
#include "patch/tests/fixtures.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/built_sound.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

// The sound the patch file at `path` describes.
inline BuiltSound load_sound(const std::string& path) {
    return build_sound(load_patch(path));
}
// The sentence a read or a build of `json` refuses with: an instrument as
// load_inst builds it, a plain sound as a sound; empty when it loads.
inline std::string load_refused(const std::string& json) {
    try {
        const mforce::Patch patch = patch_door::read_text(json);
        if (patch.instrument) (void)build_instrument(patch);
        else (void)build_sound(patch);
    } catch (const std::exception& e) { return e.what(); }
    return {};
}

// A patch file in the scratch folder for the tests that read a file: written when made (the folder too, since a fresh clone has
// none), removed when it goes out of scope.
struct ScratchPatch {
    std::string path;
    ScratchPatch(const char* p, const std::string& text) : path(p) {
        std::filesystem::create_directories("renders/scratch");
        std::ofstream f(path);
        f << text;
    }
    // The error_code overload: a destructor must not throw, and a file that
    // will not go away is not this test's failure.
    ~ScratchPatch() { std::error_code ec; std::filesystem::remove(path, ec); }
    ScratchPatch(const ScratchPatch&) = delete;
    ScratchPatch& operator=(const ScratchPatch&) = delete;
};

// A load refuses a per-note push into a structural setting — the wiring
// format's dynamic pin — naming the node, the setting and the reason; a value
// setting still takes one.
// A load that fails for the reason the needle names; another reason is
// printed, so a test that expected one refusal and got another says so.
inline bool load_fails_naming(const char* patch, const char* needle) {
    const std::string msg = load_refused(patch);
    if (msg.empty()) return false;
    if (msg.find(needle) == std::string::npos)
        std::printf("  load failed for another reason: %s\n", msg.c_str());
    return msg.find(needle) != std::string::npos;
}

// Whether build_sound refuses a patch file with a message holding
// `needle`; a refusal for another reason is printed.
inline bool file_fails_naming(const char* path, const char* text, const char* needle) {
    ScratchPatch file(path, text);
    bool named = false;
    try { (void)load_sound(file.path); }
    catch (const std::exception& e) {
        const std::string msg = e.what();
        if (msg.find(needle) == std::string::npos) std::printf("  load failed for another reason: %s\n", msg.c_str());
        named = msg.find(needle) != std::string::npos;
    }
    return named;
}
namespace plain_parts {
// One channel: its id, the id of the node its source holds, and the params
// beside the source (null: none).
struct Chan { std::string id, source; nlohmann::json params; };
inline nlohmann::json node(const char* id, const char* type, nlohmann::json params) {
    return nlohmann::json{{"id", id}, {"type", type}, {"params", std::move(params)}};
}
inline nlohmann::json ref(const char* id) { return nlohmann::json{{"ref", id}}; }
// A plain sound of 0.25 s at 48 kHz: the signals, each channel, then the
// mixer `mix` (`mixParams` null: no gains) as the output.
inline std::string sound(const nlohmann::json& signals, const std::vector<Chan>& chans,
                         const nlohmann::json& mixParams) {
    nlohmann::json nodes = signals;
    nlohmann::json ids = nlohmann::json::array();
    for (const auto& c : chans) {
        nlohmann::json p = c.params.is_null() ? nlohmann::json::object() : c.params;
        p["source"] = nlohmann::json{{"ref", c.source}};
        nodes.push_back(nlohmann::json{{"id", c.id}, {"type", "SoundChannel"}, {"params", p}});
        ids.push_back(nlohmann::json{{"ref", c.id}});
    }
    nlohmann::json mp = mixParams.is_null() ? nlohmann::json::object() : mixParams;
    mp["channels"] = ids;
    nodes.push_back(nlohmann::json{{"id", "mix"}, {"type", "StereoMixer"}, {"params", mp}});
    return nlohmann::json{{"sampleRate", 48000}, {"seconds", 0.25},
                          {"graph", {{"output", "mix"}, {"nodes", nodes}}}}.dump();
}
// The refusal of a patch file read and built as a sound, after the path its
// message begins with; empty when it loads.
inline std::string file_refusal(const char* path, const std::string& text) {
    ScratchPatch file(path, text);
    try { (void)load_sound(file.path); }
    catch (const std::exception& e) {
        std::string msg = e.what();
        if (msg.rfind(file.path + ": ", 0) == 0) msg.erase(0, file.path.size() + 2);
        return msg;
    }
    return {};
}
// The same sound with the mixer's channel list reversed, the file's node order kept.
inline std::string reversed_channels(const std::string& text) {
    auto doc = nlohmann::json::parse(text);
    for (auto& n : doc["graph"]["nodes"]) {
        if (n["type"] != "StereoMixer") continue;
        auto& list = n["params"]["channels"];
        std::reverse(list.begin(), list.end());
    }
    return doc.dump();
}
// A plain sound whose pull loops share a node, and the sentence that refuses it.
struct Shared { std::string what, text, sentence; };
inline std::vector<Shared> shared_cases() {
    using J = nlohmann::json;
    const std::string tail = ": each pulls its nodes on its own clock, so a node belongs to one";
    struct Shape { const char* what; J signals; std::vector<Chan> chans; std::string sentence; };
    const Shape shapes[] = {
        {"two channels on one source", J::array({node("osc", "SineSource", {{"frequency", 220.0}})}),
         {{"ch1", "osc", J()}, {"ch2", "osc", J()}}, "'osc' is reached from channel 'ch1' and from channel 'ch2'" + tail},
        {"one node two levels down", J::array({node("lfo", "SineSource", {{"frequency", 2.0}}),
                                               node("mid", "SineSource", {{"frequency", 5.0}, {"amplitude", ref("lfo")}}),
                                               node("srcA", "SineSource", {{"frequency", 220.0}, {"amplitude", ref("mid")}}),
                                               node("srcB", "SawSource", {{"frequency", 330.0}, {"amplitude", ref("lfo")}})}),
         {{"chA", "srcA", J()}, {"chB", "srcB", J()}}, "'lfo' is reached from channel 'chA' and from channel 'chB'" + tail},
        {"a source and a pan on one node", J::array({node("X", "SineSource", {{"frequency", 0.5}}),
                                                     node("src", "SineSource", {{"frequency", 220.0}, {"amplitude", ref("X")}})}),
         {{"ch1", "src", {{"pan", ref("X")}}}}, "'X' is reached from channel 'ch1' and from the mixer's gains, volumes and pans" + tail},
        {"through a list pin", J::array({node("s1", "SineSource", {{"frequency", 220.0}}), node("s2", "SawSource", {{"frequency", 330.0}}),
                                         node("multi", "MultiSource", {{"source", J::array({ref("s1"), ref("s2")})}})}),
         {{"chA", "multi", J()}, {"chB", "s2", J()}}, "'s2' is reached from channel 'chA' and from channel 'chB'" + tail},
    };
    std::vector<Shared> out;
    for (const auto& s : shapes) out.push_back({s.what, sound(s.signals, s.chans, J()), s.sentence});
    // the loops in the mixer's channel order, not the file's node order
    out.push_back({"the first, its channels reversed", reversed_channels(out[0].text),
                   "'osc' is reached from channel 'ch2' and from channel 'ch1'" + tail});
    // a gain on a node a channel's source reads: the mixer's loop is named last
    out.push_back({"a gain on the source's node", sound(shapes[2].signals, {{"ch1", "src", J()}}, J{{"gainL", ref("X")}}),
                   "'X' is reached from channel 'ch1' and from the mixer's gains, volumes and pans" + tail});
    return out;
}
}  // namespace plain_parts

#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/music/music_json.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/mixer.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace plain_parts {
// The FNV-1a of a sound's stereo render; 0 when the load or the render throws.
static uint64_t render_hash(const std::string& text) {
    std::vector<float> buf;
    if (!render_scratch_patch("renders/scratch/_plain_parts.json", text, buf)) return 0;
    return node_twins::fnv(buf);
}
// The mixer build_sound builds from a patch file; null, with the
// refusal printed, when the load throws.
static std::unique_ptr<Mixer> built_mixer(const std::string& text) {
    ScratchPatch file("renders/scratch/_plain_built.json", text);
    try { return std::move(load_sound(file.path).mixer); }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return nullptr;
}
}  // namespace plain_parts

// Each plain sound renders the same bytes as the base patch.
void run_plain_part_twin_tests() {
    using namespace plain_parts;
    std::printf("the plain-sound build: each sound against the base's render\n");
    // Each sound's hash, held.
    const uint64_t kBase[] = {
        0xbd803f232d11c7d6ull,   // one channel, all four numbers
        0x428f79ca36025021ull,   // no numbers on either part
        0x6a4b98ef60321f25ull,   // two channels
        0x6da350faa0c174a3ull,   // three channels
        0x8f96948dd16fb3c3ull,   // volume wired
        0xbe932bc542335611ull,   // pan wired
        0x94a4b14bfce0b145ull,   // gainL wired
    };
    const auto all = cases();
    CHECK(all.size() == std::size(kBase));
    for (size_t k = 0; k < all.size() && k < std::size(kBase); ++k) {
        const uint64_t h = render_hash(sound(all[k].signals, all[k].chans, all[k].mix));
        if (h != kBase[k]) std::printf("  %s: 0x%016llx\n", all[k].name, (unsigned long long)h);
        CHECK(h == kBase[k]);
    }
}

// What the build reads from the parts: the channels in the mixer's order, a
// number on a pin as given, a pin the file leaves out at its declared
// constant; an empty channel list renders silence.
void run_plain_part_build_tests() {
    using namespace plain_parts;
    std::printf("the plain-sound build: the channels' order, each pin's number or default\n");
    const auto all = cases();
    const auto three = sound(all[3].signals, all[3].chans, all[3].mix);
    auto m = built_mixer(three);
    CHECK(m && m->channels.size() == 3);
    if (m && m->channels.size() == 3)
        CHECK(is_constant_of(m->channels[0].volume, 0.6f) && is_constant_of(m->channels[1].volume, 0.7f)
              && is_constant_of(m->channels[2].volume, 1.0f) && is_constant_of(m->channels[0].pan, -0.5f)
              && is_constant_of(m->channels[1].pan, 0.5f) && is_constant_of(m->channels[2].pan, 0.0f)
              && is_constant_of(m->gainL, 0.9f) && is_constant_of(m->gainR, 0.7f));
    auto r = built_mixer(reversed_channels(three));                        // the mixer's list, not the file's node order
    CHECK(r && r->channels.size() == 3);
    if (r && r->channels.size() == 3)
        CHECK(is_constant_of(r->channels[0].volume, 1.0f) && is_constant_of(r->channels[1].volume, 0.7f)
              && is_constant_of(r->channels[2].volume, 0.6f));
    auto bare = built_mixer(sound(all[1].signals, all[1].chans, all[1].mix));
    CHECK(bare && bare->channels.size() == 1);
    if (bare && bare->channels.size() == 1)
        CHECK(is_constant_of(bare->gainL, 1.0f) && is_constant_of(bare->gainR, 1.0f)
              && is_constant_of(bare->channels[0].volume, 1.0f) && is_constant_of(bare->channels[0].pan, 0.0f));
    std::vector<float> buf;
    CHECK(render_scratch_patch("renders/scratch/_plain_empty.json", sound(all[1].signals, {}, nlohmann::json()), buf));
    bool silent = !buf.empty();
    for (float s : buf) silent = silent && s == 0.0f;
    CHECK(silent);
}

// An instrument may hold a mixer and a channel beside its voice: they are
// built and play no part. A mixer as the instrument's output is a part where
// a signal is required.
void run_instrument_holding_parts_tests() {
    std::printf("the output parts in an instrument: built beside the voice, not its output\n");
    const std::string head = R"({"sampleRate":48000,"instrument":{"polyphony":1},
      "score":[{"note":60,"time":0,"duration":0.05}],"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":0.5}})";
    std::vector<float> plain, withParts;
    CHECK(render_scratch_patch("renders/scratch/_instrument_alone.json", head + "]}}", plain));
    CHECK(render_scratch_patch("renders/scratch/_instrument_parts_params.json", head
        + R"(,{"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":0.5,"pan":0.3}},)"
        + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":0.25,"gainR":0.5}}]}})", withParts));
    CHECK(!plain.empty() && plain.size() == withParts.size()
          && std::memcmp(plain.data(), withParts.data(), plain.size() * sizeof(float)) == 0);
    CHECK(load_fails_naming(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"mix","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
      {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"}}},
      {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})", "'mix' is a StereoMixer, a part, not a signal"));
}
void run_plain_sharing_load_tests() {
    using namespace plain_parts;
    std::printf("the plain sound's pull loops: what loads\n");
    for (const auto& t : sharing_sounds_that_load()) {
        std::vector<float> buf;
        CHECK(render_scratch_patch("renders/scratch/_plain_loads.json", t, buf));
        bool sound_ = false;
        for (float s : buf) sound_ = sound_ || s != 0.0f;
        CHECK(sound_);
    }
}
void run_plain_sharing_multiplex_tests() {
    using namespace plain_parts;
    using J = nlohmann::json;
    std::printf("the plain sound's pull loops: a Multiplex's template is not walked\n");
    const std::string tail = ": each pulls its nodes on its own clock, so a node belongs to one";
    const auto mux = [](const char* id, int count) {
        return node(id, "MultiplexSource", {{"count", count}, {"source", ref("t")}});
    };
    const J lfo = node("lfo", "SineSource", {{"frequency", 2.0}, {"amplitude", 0.5}});
    const J saw = node("b", "SawSource", {{"frequency", 330.0}, {"amplitude", ref("lfo")}});
    const std::string texts[] = {
        sound(J::array({node("t", "SineSource", {{"frequency", 220.0}, {"amplitude", 0.3}}), mux("m1", 2), mux("m2", 3)}),
              {{"c1", "m1", J()}, {"c2", "m2", J()}}, J()),
        sound(J::array({lfo, node("t", "SineSource", {{"frequency", 220.0}, {"amplitude", ref("lfo")}}), mux("m1", 2), saw}),
              {{"c1", "m1", J()}, {"c2", "b", J()}}, J()),
    };
    for (const auto& t : texts) {
        std::vector<float> buf;
        CHECK(render_scratch_patch("renders/scratch/_plain_mux_loads.json", t, buf));
        bool sound_ = false;
        for (float s : buf) sound_ = sound_ || s != 0.0f;
        CHECK(sound_);
    }
    const J comb = node("comb", "CombinedSource", {{"operation", 0}, {"gainAdj", 0.0}, {"source1", ref("m1")},
                                                   {"source2", ref("lfo")}});
    const std::string got = file_refusal("renders/scratch/_plain_mux_shared.json",
        sound(J::array({lfo, node("t", "SineSource", {{"frequency", 220.0}}), mux("m1", 2), comb, saw}),
              {{"c1", "comb", J()}, {"c2", "b", J()}}, J()));
    if (got != "'lfo' is reached from channel 'c1' and from channel 'c2'" + tail) std::printf("  beside a Multiplex: %s\n", got.c_str());
    CHECK(got == "'lfo' is reached from channel 'c1' and from channel 'c2'" + tail);
}

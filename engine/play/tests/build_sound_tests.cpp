#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/patch/patch.h"
#include "mforce/play/play.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/built_sound.h"
#include "mforce/render/fault_report.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iterator>
#include <string>
#include <vector>

using namespace mforce;

namespace patch_door {
// A sound's first `frames` (all of it for -1) through its mixer, stereo interleaved.
static std::vector<float> render_sound(BuiltSound& s, int frames = -1) {
    const int n = frames < 0 ? s.frames : std::min(frames, s.frames);
    std::vector<float> buf(2 * size_t(n), 0.0f);
    s.mixer->render(buf.data(), n);
    return buf;
}
}  // namespace patch_door
namespace patch_codec {
// A patch read from JSON text through the codec; throws as from_json does.
static Patch load_text(const std::string& text) { return patch_door::read_text(text); }
}  // namespace patch_codec
namespace build_tests {
using patch_codec::load_text;
using patch_door::render_sound;
// A plain sound around `nodes`, `output` its output, a quarter second long.
static std::string plain(const std::string& nodes, const char* output) {
    return std::string(R"({"seconds":0.25,"graph":{"output":")") + output + R"(","nodes":[)" + nodes + "]}}";
}
}  // namespace build_tests

// build_sound: the base's seven plain-sound hashes; the sharing refusal's
// shapes with their sentences and the shapes that load; the bare output; the
// instrument form and the frame count's three cases; a tail in a plain sound
// and a faulting node, each named by id.
void run_build_sound_tests() {
    using namespace build_tests;
    std::printf("build_sound: the base's plain sounds, the sharing walk, the three forms, the frames\n");
    const uint64_t kBase[] = {0xbd803f232d11c7d6ull, 0x428f79ca36025021ull, 0x6a4b98ef60321f25ull, 0x6da350faa0c174a3ull,
                              0x8f96948dd16fb3c3ull, 0xbe932bc542335611ull, 0x94a4b14bfce0b145ull};
    const auto all = plain_parts::cases();
    CHECK(all.size() == std::size(kBase));
    for (size_t k = 0; k < all.size() && k < std::size(kBase); ++k) {
        BuiltSound s = build_sound(load_text(plain_parts::sound(all[k].signals, all[k].chans, all[k].mix)));
        const uint64_t h = node_twins::fnv(render_sound(s));
        if (h != kBase[k]) std::printf("  %s: 0x%016llx\n", all[k].name, (unsigned long long)h);
        CHECK(h == kBase[k]);
    }
    const auto refusal = [](const std::string& text) {
        try { (void)build_sound(load_text(text)); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    for (const auto& c : plain_parts::shared_cases()) {
        const std::string got = refusal(c.text);
        if (got != c.sentence) std::printf("  %s: %s\n", c.what.c_str(), got.c_str());
        CHECK(got == c.sentence);
    }
    for (const auto& text : plain_parts::sharing_sounds_that_load()) {
        BuiltSound s = build_sound(load_text(text));
        const auto buf = render_sound(s);
        CHECK(std::any_of(buf.begin(), buf.end(), [](float v) { return v != 0.0f; }));
    }
    BuiltSound bare = build_sound(load_text(plain(R"({"id":"osc","type":"SineSource","params":{"frequency":220}})", "osc")));
    CHECK(bare.mixer->channels.size() == 1 && bare.frames == 12000 && bare.mixer->signals.size() == 1 &&
          bare.mixer->signals[0].id == "osc");
    // The frames play gives an instrument patch: its score, its seconds, or five seconds.
    const std::string head = R"({"instrument":{"polyphony":1},)";
    const std::string tail = R"("graph":{"output":"osc","nodes":[{"id":"osc","type":"SineSource","params":{"frequency":220}}]}})";
    const std::string score = R"("score":[{"note":60,"time":0,"duration":0.5}],)";
    const std::string forms[] = {head + tail, head + score + tail, head + score + R"("seconds":0.25,)" + tail,
                                 head + score + R"("seconds":2,)" + tail, head + R"("seconds":0.25,)" + tail};
    const int frames[] = {240000, 24000, 24000, 96000, 12000};
    for (size_t k = 0; k < std::size(forms); ++k) {
        const PlayResult mod = play(load_text(forms[k]));
        if (mod.frames != frames[k]) std::printf("  form %zu: %d frames, want %d\n", k, mod.frames, frames[k]);
        CHECK(mod.sampleRate == 48000 && mod.frames == frames[k]);
    }
    BuiltSound tailed = build_sound(load_text(plain(R"({"id":"T","type":"SawSource"},)"
        R"({"id":"out","type":"CombinedSource","params":{"source1":{"tap":"T"}}})", "out")));
    CHECK(tailed.warnings.size() == 1 && tailed.warnings[0].rfind("node 'T' is read only through a tap", 0) == 0);
    BuiltSound faulty = build_sound(load_text(R"J({"sampleRate":48000,"graph":{"nodes":[{"id":"zero","type":"SawSource","params":{"amplitude":0.3,"frequency":0.0,"phase":0.0}},{"id":"tone","type":"SineSource","params":{"amplitude":0.3,"frequency":220.0,"phase":0.0}},{"id":"chZ","type":"SoundChannel","params":{"volume":0.8,"pan":-0.5,"source":{"ref":"zero"}}},{"id":"chT","type":"SoundChannel","params":{"volume":0.8,"pan":0.5,"source":{"ref":"tone"}}},{"id":"mix","type":"StereoMixer","params":{"gainL":1.0,"gainR":1.0,"channels":[{"ref":"chZ"},{"ref":"chT"}]}}],"output":"mix"}})J"));
    (void)render_sound(faulty, 4800);
    std::vector<FaultReport> faults;
    faulty.mixer->collect_faults(faults);
    CHECK(faults.size() == 1 && faults[0].nodeId == "zero");
    if (!faults.empty()) std::printf("  the faulting node: %s\n", fault_report_text(faults[0]).c_str());
}

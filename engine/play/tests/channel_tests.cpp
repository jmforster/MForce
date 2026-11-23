#include "mforce/testing/check.h"
#include "test_support.h"
#include "render/tests/fixtures.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

using namespace mforce;


// A channel's source is any signal the graph builds: each of six signal types
// feeds the one SoundChannel of a StereoMixer and renders, every sample finite.
void run_channel_source_tests() {
    std::printf("channel sources: six signal types feed a SoundChannel and render\n");
    auto through_channel = [](const char* node, const char* id) {
        return std::string(R"({"sampleRate":48000,"seconds":0.05,"graph":{"output":"mix","nodes":[)") + node
            + R"(,{"id":"ch1","type":"SoundChannel","params":{"source":{"ref":")" + id
            + R"("},"volume":1.0,"pan":0.0}},)"
            + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})";
    };
    auto all_finite = [](const std::vector<float>& b) {
        for (float s : b) if (!std::isfinite(s)) return false;
        return !b.empty();
    };
    struct Case { const char* path; const char* node; const char* id; };
    const Case cases[] = {
        {"renders/scratch/_channel_envelope.json",
         R"({"id":"env","type":"Envelope","params":{"preset":"ar","attack":0.2}})", "env"},
        {"renders/scratch/_channel_curve.json",
         R"({"id":"cv","type":"CurveNode","params":{"x":[0.0,1.0],"y":[0.25,0.75],"source":0.5}})", "cv"},
        {"renders/scratch/_channel_range.json",
         R"({"id":"rng","type":"RangeSource","params":{"min":0.0,"max":1.0,"var":0.5}})", "rng"},
        {"renders/scratch/_channel_vibrato.json",
         R"({"id":"vib","type":"Vibrato","params":{"frequency":220.0}})", "vib"},
        // No vocabulary in a mixer-mode patch: the gate never fires.
        {"renders/scratch/_channel_name_gate.json",
         R"({"id":"gate","type":"NameGate","params":{"name":"pluck"}})", "gate"},
    };
    for (const auto& c : cases) {
        std::vector<float> buf;
        CHECK(render_scratch_patch(c.path, through_channel(c.node, c.id), buf));
        CHECK(all_finite(buf));
    }
    // A constant signal renders as one non-zero value on every left sample:
    // the adapter really pulls the node.
    std::vector<float> buf;
    CHECK(render_scratch_patch("renders/scratch/_channel_var.json",
        through_channel(R"({"id":"var","type":"VarSource","params":{"val":0.5,"var":0.0}})", "var"), buf));
    CHECK(all_finite(buf));
    bool constant = !buf.empty() && buf[0] != 0.0f;
    for (size_t i = 0; i < buf.size(); i += 2) constant = constant && buf[i] == buf[0];
    CHECK(constant);
}

// The channel adapter is its graph's pull loop: it gives the graph a clock, so
// a node two consumers pull computes once a sample, and it starts the graph
// over the buffer. Two channels cannot share a source.
void run_channel_clock_tests() {
    std::printf("the channel adapter: its clock, its start, one source per channel\n");
    auto product_of = [](const char* nodes, const char* a, const char* b) {
        return std::string(R"({"sampleRate":48000,"seconds":0.05,"graph":{"output":"mix","nodes":[)") + nodes
            + R"(,{"id":"prod","type":"CombinedSource","params":{"operation":1,"gainAdj":0.0,"source1":{"ref":")" + a
            + R"("},"source2":{"ref":")" + b + R"("}}},)"
            + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"prod"},"volume":1.0,"pan":0.0}},)"
            + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})";
    };
    // One envelope on both inputs renders as two identical envelopes do.
    std::vector<float> shared, twin;
    CHECK(render_scratch_patch("renders/scratch/_channel_shared_env.json",
        product_of(R"({"id":"env","type":"Envelope","params":{"preset":"ar","attack":0.2}})", "env", "env"), shared));
    CHECK(render_scratch_patch("renders/scratch/_channel_twin_env.json",
        product_of(R"({"id":"env1","type":"Envelope","params":{"preset":"ar","attack":0.2}},)"
                   R"({"id":"env2","type":"Envelope","params":{"preset":"ar","attack":0.2}})", "env1", "env2"), twin));
    CHECK(!shared.empty() && shared.size() == twin.size()
          && std::memcmp(shared.data(), twin.data(), shared.size() * sizeof(float)) == 0);
    // The envelope moves across the buffer: it was started over it.
    bool moves = false;
    for (size_t i = 2; i < twin.size(); i += 2) moves = moves || twin[i] != twin[0];
    CHECK(moves);
    // Two channels naming one source are refused.
    CHECK(file_fails_naming("renders/scratch/_channels_one_source.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":1.0,"pan":0.0}},
          {"id":"ch2","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":1.0,"pan":0.0}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"},{"ref":"ch2"}],"gainL":1.0,"gainR":1.0}}]}})",
        "'osc' is reached from channel 'ch1' and from channel 'ch2'"));
}

// The instrument form's mixer is one unity channel: a node named "mix" that
// nothing consumes puts no gain on it.
void run_instrument_mixer_unity_tests() {
    std::printf("the instrument form's mixer: unity, whatever a node named mix says\n");
    const std::string head = R"({"sampleRate":48000,"instrument":{"polyphony":1},
      "score":[{"note":60,"time":0,"duration":0.05}],"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":0.5}})";
    std::vector<float> plain, withMix;
    CHECK(render_scratch_patch("renders/scratch/_instrument_no_mix.json", head + "]}}", plain));
    CHECK(render_scratch_patch("renders/scratch/_instrument_quarter_mix.json",
        head + R"(,{"id":"mix","type":"StereoMixer","params":{"channels":[],"gainL":0.25,"gainR":0.25}}]}})",
        withMix));
    CHECK(!plain.empty() && plain.size() == withMix.size()
          && std::memcmp(plain.data(), withMix.data(), plain.size() * sizeof(float)) == 0);
    bool silent = true;
    for (float s : plain) if (s != 0.0f) silent = false;
    CHECK(!silent);
    // The mixer itself: one channel, unity gains and volume, centre pan.
    ScratchPatch file("renders/scratch/_instrument_unity.json", head + "]}}");
    bool unity = false;
    try {
        auto built = load_sound(file.path);
        const auto& m = *built.mixer;
        unity = m.channels.size() == 1
             && is_constant_of(m.gainL, 1.0f) && is_constant_of(m.gainR, 1.0f)
             && is_constant_of(m.channels[0].volume, 1.0f) && is_constant_of(m.channels[0].pan, 0.0f);
    }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    CHECK(unity);
}

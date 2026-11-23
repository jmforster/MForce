#include "mforce/testing/check.h"
#include "render/tests/fixtures.h"
#include <cstdio>
#include <string>

using namespace mforce;


// The plain sound's refusals, each with its sentence.
void run_plain_part_refusal_tests() {
    using plain_parts::file_refusal;
    std::printf("the plain-sound build: its refusals\n");
    const std::string osc = R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}},)";
    CHECK(file_refusal("renders/scratch/_plain_no_source.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"volume":1.0}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})").ends_with("node 'ch1' (SoundChannel): 'source' is not wired; a channel the mixer lists needs a source"));
    CHECK(file_refusal("renders/scratch/_plain_signal_channel.json",
                       osc + R"({"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"osc"}]}}]}})").ends_with("node 'mix' (StereoMixer): 'channels' does not accept a SineSource; 'channels' takes a SoundChannel"));
    CHECK(file_fails_naming("renders/scratch/_plain_part_source.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"parts"}}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})",
        "node 'ch1' (SoundChannel): 'source' does not accept a FullPartials, a part, not a signal"));
    CHECK(file_refusal("renders/scratch/_plain_source_nowhere.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"nowhere"}}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})").ends_with("node 'ch1' (SoundChannel): 'source' reads 'nowhere', which names no node"));
    CHECK(file_refusal("renders/scratch/_plain_channel_output.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"ch1","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"}}}]}})").ends_with("'ch1' is a SoundChannel, a part, not a signal"));
    CHECK(file_refusal("renders/scratch/_plain_channel_key.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"bogus":1}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}]}}]}})").ends_with("node 'ch1' (SoundChannel): unknown key 'bogus'"));
    CHECK(file_refusal("renders/scratch/_plain_mixer_key.json", osc
        + R"({"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"}}},
             {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"bogus":1}}]}})").ends_with("node 'mix' (StereoMixer): unknown key 'bogus'"));
}

void run_plain_sharing_refusal_tests() {
    using namespace plain_parts;
    std::printf("the plain sound's pull loops: a node two loops reach is refused\n");
    for (const auto& c : shared_cases()) {
        const std::string got = file_refusal("renders/scratch/_plain_shared.json", c.text);
        if (got != c.sentence) std::printf("  %s: %s\n", c.what.c_str(), got.c_str());
        CHECK(got == c.sentence);
    }
}

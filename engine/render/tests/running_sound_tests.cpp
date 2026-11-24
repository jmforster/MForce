// A sound that runs until it is let go: the patch's envelopes held as data,
// the mixer's running form equal to its one-call render, a running fill that
// allocates nothing, and a sound that goes on past where its envelope ended.
#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/running_sound.h"
#include "mforce/testing/alloc_probe.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace mforce;

namespace {

std::vector<Patch> measured_plain_sounds() {
  std::vector<std::string> paths;
  for (const char* root : {"patches/library", "patches/baselines"})
    for (const auto& e : std::filesystem::recursive_directory_iterator(root))
      if (e.path().extension() == ".json") paths.push_back(e.path().generic_string());
  std::sort(paths.begin(), paths.end());
  std::vector<Patch> out;
  for (const auto& p : paths) {
    Patch patch = load_patch(p);
    if (!patch.instrument) out.push_back(std::move(patch));
  }
  return out;
}

// An enveloped sine, the envelope's form given, as a plain sound.
std::string plain_enveloped(const char* envelope) {
  return std::string(R"({"sampleRate":48000,"seconds":1.0,"graph":{"output":"osc","nodes":[
      {"id":"env","type":"Envelope","params":)") + envelope + R"(},
      {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"env"}}}]}})";
}

const Node& node(const Patch& p, const std::string& id) {
  return *std::find_if(p.graph.nodes.begin(), p.graph.nodes.end(), [&](const Node& n) { return n.id == id; });
}

}  // namespace

// held_patch: every Envelope keeps time in seconds; one with no stage of 0
// percent gains a last stage holding its final level; one that has such a
// stage keeps its rows; nothing else changes.
void run_held_patch_tests() {
  std::printf("held_patch: every Envelope in seconds, a hold stage where it has none\n");
  const Patch ar = patch_door::read_text(plain_enveloped(R"({"startVal":[0.0,1.0],"endVal":[1.0,0.5],"power":[0.0,0.0],
      "type":["Linear","Linear"],"percent":[0.3,0.7],"minSec":[0.0,0.0],"maxSec":[0.0,0.0],"nominal":[0.0,0.0]})"));
  const Patch adsr = patch_door::read_text(plain_enveloped(R"({"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.5,"release":0.1})"));
  const Patch arHeld = held_patch(ar), adsrHeldPatch = held_patch(adsr);
  const Node& before = node(ar, "env");
  const Node& after = node(arHeld, "env");
  const size_t rows = before.arrays.at("percent").size();
  CHECK(after.settings.at("timeMode") == 1.0f);
  CHECK(after.arrays.at("percent").size() == rows + 1 && after.arrays.at("percent").back() == 0.0f);
  CHECK(after.arrays.at("startVal").back() == before.arrays.at("endVal").back() &&
        after.arrays.at("endVal").back() == before.arrays.at("endVal").back());
  for (const char* c : {"power", "type", "minSec", "maxSec", "nominal"})
    CHECK(after.arrays.at(c).size() == rows + 1 && after.arrays.at(c).back() == 0.0f);
  const Node& adsrHeld = node(adsrHeldPatch, "env");
  CHECK(adsrHeld.arrays == node(adsr, "env").arrays && adsrHeld.settings.at("timeMode") == 1.0f);
  CHECK(node(arHeld, "osc") == node(ar, "osc"));
}

// The mixer's running form: over every measured plain sound, held (the form
// that runs), blocks of uneven sizes after one start equal one render over
// the same length, sample for sample. A quarter second crosses every block
// size many times.
void run_mixer_running_tests() {
  std::printf("the mixer: rendered block after block equals one render, over the measured plain sounds\n");
  const auto t0 = std::chrono::steady_clock::now();
  const int frames = 12000;
  const int sizes[] = {1, 100, 511, 512, 513, 700, 64, 2048, 3};
  int sounds = 0, differ = 0;
  for (const Patch& plain : measured_plain_sounds()) {
    const Patch patch = held_patch(plain);
    BuiltSound whole = build_sound(patch), blocks = build_sound(patch);
    std::vector<float> one(size_t(frames) * 2), many(size_t(frames) * 2);
    whole.mixer->render(one.data(), frames);
    blocks.mixer->start_running(frames, 512);
    for (int done = 0, k = 0; done < frames; ++k) {
      const int n = std::min(sizes[k % 9], frames - done);
      blocks.mixer->render_running(many.data() + 2 * done, n);
      done += n;
    }
    ++sounds;
    if (!patch_door::same_samples(one, many)) {
      ++differ;
      std::printf("  differs: %s\n", patch_door::difference(one, many).c_str());
    }
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  %d held plain sounds, %d differ, %.1f s\n", sounds, differ, secs);
  CHECK(sounds > 0 && differ == 0);
}

// A running sound's fill allocates nothing, over every measured plain sound;
// and a sound whose envelope releases at the end of its one-second length
// keeps its stages' times and is still sounding, at its sustain, three
// seconds in.
void run_running_sound_tests() {
  std::printf("a running sound: fill allocates nothing; it outlives its envelope\n");
  std::vector<float> buf(2 * 1000);
  int sounds = 0;
  for (const Patch& plain : measured_plain_sounds()) {
    RunningSound running(plain, 48000);
    alloc_probe::reset();
    alloc_probe::arm();
    for (int k = 0; k < 20; ++k) running.fill(buf.data(), k % 2 ? 1000 : 256);
    alloc_probe::disarm();
    ++sounds;
    if (alloc_probe::count()) std::printf("  [alloc] %zu allocation(s) in a running fill\n", alloc_probe::count());
    CHECK(alloc_probe::count() == 0);
  }
  CHECK(sounds > 0);
  const Patch adsr = patch_door::read_text(plain_enveloped(R"({"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.5,"release":0.1})"));
  std::vector<float> once(48000 * 2);
  build_sound(adsr).mixer->render(once.data(), 48000);
  CHECK(std::fabs(once[once.size() - 2]) < 1e-3f);                  // the plain render has released by its end
  // Its stages keep their time in seconds: the first half second equals the
  // held patch's own one-second render, the expand being the only stage the
  // length moves and the release coming after.
  std::vector<float> heldOnce(48000 * 2), first(24000 * 2);
  build_sound(held_patch(adsr)).mixer->render(heldOnce.data(), 48000);
  RunningSound running(adsr, 48000);
  running.fill(first.data(), 24000);
  CHECK(std::equal(first.begin(), first.end(), heldOnce.begin()));
  for (int k = 0; k < 3 * 48 - 24; ++k) running.fill(buf.data(), 1000);   // to three seconds
  float loudest = 0.0f;
  for (float s : buf) loudest = std::max(loudest, std::fabs(s));
  CHECK(loudest > 0.3f && loudest < 0.6f);
}

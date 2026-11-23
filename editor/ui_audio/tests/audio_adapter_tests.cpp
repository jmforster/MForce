// AudioAdapter, all through fill() and never open(): silence before a
// player, a player's note on both channels, the fade of a swap, the newest
// of several swaps, no allocation in fill, the peak, and a player refused for
// its rate.
#include "mforce/testing/check.h"
#include "mforce/testing/alloc_probe.h"
#include "mforce/patch/patch.h"
#include "mforce/render/live_player.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_audio/audio_adapter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

constexpr int kFrames = 256;
constexpr int kFade = 240;                               // 5 ms at 48 kHz

// A sine instrument, its amplitude a constant, at `rate`.
Patch sine(int rate = 48000) {
  Node pf;
  pf.id = "pf";
  pf.type = "PerformNode";
  pf.settings["field"] = 0.0f;
  Node osc;
  osc.id = "osc";
  osc.type = "SineSource";
  osc.wires["frequency"] = {Wire{"pf"}};
  osc.values["amplitude"] = 0.5f;
  Patch p;
  p.graph.nodes = {pf, osc};
  p.graph.output = "osc";
  p.instrument = InstrumentBlock{};
  p.instrument->polyphony = 1;
  p.sampleRate = rate;
  return p;
}

PerformedNote long_note(float noteNumber = 60.0f) {
  PerformedNote n;
  n.duration = 2.0f;
  n.noteNumber = noteNumber;
  return n;
}

// One stereo buffer from the adapter, as its two channels.
struct Stereo { std::vector<float> left, right; };
Stereo fill_once(AudioAdapter& a, int frames = kFrames) {
  std::vector<float> interleaved(size_t(2 * frames));
  a.fill(interleaved.data(), frames);
  Stereo s;
  for (int i = 0; i < frames; ++i) {
    s.left.push_back(interleaved[size_t(2 * i)]);
    s.right.push_back(interleaved[size_t(2 * i + 1)]);
  }
  return s;
}

std::vector<float> mono_once(LivePlayer& p, int frames = kFrames) {
  std::vector<float> out(static_cast<size_t>(frames));
  p.fill(out.data(), frames);
  return out;
}

bool silent(const std::vector<float>& v, size_t from = 0) {
  return std::all_of(v.begin() + std::ptrdiff_t(from), v.end(), [](float s) { return s == 0.0f; });
}

}  // namespace

void run_adapter_silence_and_note_tests() {
  register_all_sources();
  AudioAdapter a;
  CHECK(a.sample_rate() == 48000);
  CHECK(a.player() == nullptr);
  CHECK(!a.error().empty());                             // no device was opened
  const Stereo before = fill_once(a);
  CHECK(silent(before.left) && silent(before.right));
  CHECK(a.peak() == 0.0f);

  a.play(std::make_unique<LivePlayer>(sine(), 1));
  LivePlayer twin(sine(), 1);
  REQUIRE(a.player() != nullptr);
  CHECK(a.player()->note_on(long_note()) != 0);
  CHECK(twin.note_on(long_note()) != 0);
  for (int k = 0; k < 4; ++k) {
    const Stereo s = fill_once(a);
    const std::vector<float> m = mono_once(twin);
    CHECK(s.left == m && s.right == m);
    float most = 0.0f;
    for (float x : m) most = std::max(most, std::fabs(x));
    CHECK(a.peak() == most);
    CHECK(most > 0.0f);
  }
}

// The player sounding when a silent one is handed over fades to nothing
// within 5 ms; then the new one's note sounds as its twin's does.
void run_adapter_swap_tests() {
  AudioAdapter a;
  a.play(std::make_unique<LivePlayer>(sine(), 1));
  CHECK(a.player()->note_on(long_note()) != 0);
  (void)fill_once(a);
  a.play(std::make_unique<LivePlayer>(sine(), 1));
  const Stereo swap = fill_once(a);
  CHECK(!silent(std::vector<float>(swap.left.begin(), swap.left.begin() + 16)));   // the fade begins at full sound
  CHECK(silent(swap.left, kFade));                       // and is over within 5 ms
  float nearEnd = 0.0f;
  for (int i = kFade - 40; i < kFade; ++i) nearEnd = std::max(nearEnd, std::fabs(swap.left[size_t(i)]));
  CHECK(nearEnd < 0.1f);                                 // near nothing as it ends
  LivePlayer twin(sine(), 1);
  CHECK(a.player()->note_on(long_note(67.0f)) != 0);
  CHECK(twin.note_on(long_note(67.0f)) != 0);
  CHECK(fill_once(a).left == mono_once(twin));
}

// Three players handed over between two buffers: the last one sounds.
void run_adapter_three_swaps_tests() {
  AudioAdapter a;
  a.play(std::make_unique<LivePlayer>(sine(), 1));
  (void)fill_once(a);
  for (float pitch : {50.0f, 55.0f, 62.0f}) {
    a.play(std::make_unique<LivePlayer>(sine(), 1));
    CHECK(a.player()->note_on(long_note(pitch)) != 0);
  }
  LivePlayer twin(sine(), 1);
  CHECK(twin.note_on(long_note(62.0f)) != 0);
  // The first player was silent, so its fade adds nothing: the newest alone.
  CHECK(fill_once(a).left == mono_once(twin));
  CHECK(fill_once(a).left == mono_once(twin));
  a.play(nullptr);                                       // silence: the last one fades out
  const Stereo out = fill_once(a);
  CHECK(silent(out.left, kFade));
  CHECK(a.player() == nullptr);
}

void run_adapter_allocation_tests() {
  AudioAdapter a;
  a.play(std::make_unique<LivePlayer>(sine(), 1));
  CHECK(a.player()->note_on(long_note()) != 0);
  std::vector<float> buf(2 * 1024);
  a.fill(buf.data(), 1024);
  alloc_probe::reset();
  alloc_probe::arm();
  a.fill(buf.data(), 1024);                              // two chunks, no swap
  alloc_probe::disarm();
  CHECK(alloc_probe::count() == 0);
  a.play(std::make_unique<LivePlayer>(sine(), 1));       // the control side may allocate
  a.play(std::make_unique<LivePlayer>(sine(), 1));
  alloc_probe::reset();
  alloc_probe::arm();
  a.fill(buf.data(), 1024);                              // the swap and the fade
  a.fill(buf.data(), 1024);
  alloc_probe::disarm();
  if (alloc_probe::count()) std::printf("  %zu allocation(s) in fill\n", alloc_probe::count());
  CHECK(alloc_probe::count() == 0);
}

void run_adapter_rate_tests() {
  AudioAdapter a;
  a.play(std::make_unique<LivePlayer>(sine(), 1));
  LivePlayer* before = a.player();
  bool refused = false;
  try {
    a.play(std::make_unique<LivePlayer>(sine(22050), 1));
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  CHECK(refused);
  CHECK(a.player() == before);
}

// AudioAdapter, all through fill() and never open(), over a stand-in sound
// of the test's own: silence, a line's sound on both channels, the two lines
// summed, a sum past 1 clipped and both peaks, the fade of a swap, many
// swaps on both lines losing and leaking nothing, never destroying inside a
// fill and never filling a sound after it was destroyed, and no allocation
// in fill.
#include "mforce/testing/check.h"
#include "mforce/testing/alloc_probe.h"
#include "mforce/render/limiter.h"
#include "mforce/ui_audio/audio_adapter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

constexpr int kFrames = 256;
constexpr int kFade = 240;                               // 5 ms at 48 kHz
using Line = AudioAdapter::Line;

// What the stand-in sounds did: how many are alive, and whether one was
// destroyed inside a fill or filled after it was destroyed.
struct Ledger {
  bool inFill{false};
  int alive{0};
  bool destroyedInFill{false};
  bool filledAfterDestroy{false};
  std::vector<int> fills;                                // by sound id
  std::vector<bool> destroyed;                           // by sound id
};

// A constant level on both channels; counts its fills in the ledger.
class Level final : public Sound {
public:
  Level(Ledger& ledger, float level) : ledger_(ledger), level_(level), id_(int(ledger.fills.size())) {
    ledger.fills.push_back(0);
    ledger.destroyed.push_back(false);
    ++ledger.alive;
  }
  ~Level() override {
    --ledger_.alive;
    if (ledger_.inFill) ledger_.destroyedInFill = true;
    ledger_.destroyed[size_t(id_)] = true;
  }
  void fill(float* stereo, int frames) noexcept override {
    if (ledger_.destroyed[size_t(id_)]) ledger_.filledAfterDestroy = true;
    ++ledger_.fills[size_t(id_)];
    std::fill(stereo, stereo + 2 * frames, level_);
  }
  int id() const { return id_; }

private:
  Ledger& ledger_;
  float level_;
  int id_;
};

std::vector<float> fill_once(AudioAdapter& a, Ledger& ledger, int frames = kFrames) {
  std::vector<float> out(size_t(2 * frames));
  ledger.inFill = true;
  a.fill(out.data(), frames);
  ledger.inFill = false;
  return out;
}

bool all_are(const std::vector<float>& v, float x) {
  return std::all_of(v.begin(), v.end(), [x](float s) { return s == x; });
}

}  // namespace

// Silence before any sound; a sound on one line on both channels; the two
// lines summed; a sum past 1 clipped, with the peak before and after.
void run_adapter_lines_tests() {
  std::printf("the adapter: silence, one line, two lines summed, the sum clipped\n");
  Ledger ledger;
  AudioAdapter a;
  CHECK(all_are(fill_once(a, ledger), 0.0f) && a.peak() == 0.0f && a.peak_before_clip() == 0.0f);
  CHECK(a.playing(Line::Keys) == nullptr && a.playing(Line::Playback) == nullptr);
  a.play(Line::Keys, std::make_unique<Level>(ledger, 0.25f));
  CHECK(all_are(fill_once(a, ledger), 0.25f) && a.peak() == 0.25f && a.peak_before_clip() == 0.25f);
  a.play(Line::Playback, std::make_unique<Level>(ledger, 0.5f));
  CHECK(all_are(fill_once(a, ledger), 0.75f) && a.peak() == 0.75f);
  CHECK(a.playing(Line::Keys) != nullptr && a.playing(Line::Playback) != a.playing(Line::Keys));
  a.play(Line::Keys, std::make_unique<Level>(ledger, 0.8f));
  a.play(Line::Playback, std::make_unique<Level>(ledger, 0.7f));
  (void)fill_once(a, ledger);                            // the fades of both swaps
  const auto out = fill_once(a, ledger);
  CHECK(all_are(out, soft_clip(1.5f)) && out[0] < 1.0f);
  CHECK(std::fabs(a.peak_before_clip() - 1.5f) < 1e-6f && a.peak() == soft_clip(1.5f));
}

// A swap: the sound before fades over 5 ms from the next buffer's top while
// the new one sounds whole; then the old one is filled no more, and the next
// control call destroys it.
void run_adapter_swap_tests() {
  std::printf("the adapter: a swap fades the sound before in 5 ms, then destroys it\n");
  Ledger ledger;
  AudioAdapter a;
  a.play(Line::Keys, std::make_unique<Level>(ledger, 0.5f));
  (void)fill_once(a, ledger);
  a.play(Line::Keys, std::make_unique<Level>(ledger, 0.25f));
  const auto out = fill_once(a, ledger);
  bool faded = true;
  for (int i = 0; i < kFrames; ++i) {
    const float want = i < kFade ? 0.25f + 0.5f * (float(kFade - i) / float(kFade)) : 0.25f;
    faded = faded && out[size_t(2 * i)] == want && out[size_t(2 * i + 1)] == want;
  }
  CHECK(faded);
  const int oldFills = ledger.fills[0];
  (void)fill_once(a, ledger);
  CHECK(ledger.fills[0] == oldFills && ledger.alive == 2);   // handed back, not yet destroyed
  (void)a.playing(Line::Keys);
  CHECK(ledger.alive == 1 && ledger.destroyed[0] && !ledger.destroyedInFill);
}

// Many swaps on both lines, some with no fill between (the sound offered and
// never taken is destroyed at once), in a seeded order; then silence on both
// and the fades played out: every sound destroyed, none inside a fill, none
// filled after it was destroyed.
void run_adapter_many_swaps_tests() {
  std::printf("the adapter: many swaps on both lines lose and leak nothing\n");
  Ledger ledger;
  std::mt19937 rng(7u);
  {
    AudioAdapter a;
    for (int k = 0; k < 2000; ++k) {
      const unsigned r = rng() % 10;
      if (r < 3) a.play(Line::Keys, std::make_unique<Level>(ledger, 0.1f));
      else if (r < 6) a.play(Line::Playback, std::make_unique<Level>(ledger, 0.2f));
      else if (r == 6) a.play(rng() % 2 ? Line::Keys : Line::Playback, nullptr);
      else (void)fill_once(a, ledger, 1 + int(rng() % 600));
    }
    a.play(Line::Keys, nullptr);
    a.play(Line::Playback, nullptr);
    for (int k = 0; k < 4; ++k) (void)fill_once(a, ledger);
    (void)a.playing(Line::Keys);
    (void)a.playing(Line::Playback);
    CHECK(ledger.alive == 0);
  }
  CHECK(ledger.alive == 0 && !ledger.destroyedInFill && !ledger.filledAfterDestroy && ledger.fills.size() > 500);
}

// fill allocates nothing: two lines sounding, and through a swap's fade.
void run_adapter_allocation_tests() {
  std::printf("the adapter: fill allocates nothing\n");
  Ledger ledger;
  ledger.fills.reserve(8);
  ledger.destroyed.reserve(8);
  AudioAdapter a;
  std::vector<float> out(2 * 1500);
  a.play(Line::Keys, std::make_unique<Level>(ledger, 0.3f));
  a.play(Line::Playback, std::make_unique<Level>(ledger, 0.3f));
  a.fill(out.data(), 256);
  a.play(Line::Keys, std::make_unique<Level>(ledger, 0.4f));   // the control side may allocate
  alloc_probe::reset();
  alloc_probe::arm();
  a.fill(out.data(), 1500);                                     // longer than a chunk: filled in pieces
  a.fill(out.data(), 256);
  alloc_probe::disarm();
  if (alloc_probe::count()) std::printf("  %zu allocation(s) in fill\n", alloc_probe::count());
  CHECK(alloc_probe::count() == 0);
}

// An adapter never asked to open: the heartbeat counts each fill; it is not
// running, names no device, sees no change of device, and a restart does
// nothing but say so.
void run_adapter_unopened_tests() {
  std::printf("adapter: never asked to open: the heartbeat counts fills, a restart does nothing\n");
  AudioAdapter a;
  std::vector<float> out(2 * kFrames);
  CHECK(a.heartbeat() == 0);
  a.fill(out.data(), kFrames);
  a.fill(out.data(), kFrames);
  CHECK(a.heartbeat() == 2);
  CHECK(!a.asked_to_open() && !a.running() && a.device_name().empty() && !a.default_device_changed());
  CHECK(a.restart() == "no sound device was ever opened" && !a.asked_to_open() && a.sample_rate() == 48000);
  CHECK(a.error() == "no sound device is open");
}

// A fade under way when its length is made shorter (as a restart at a lower
// rate does, with no callback running) goes on within the new length: the
// sound fading out never comes back louder than it was.
void run_line_fade_shortened_tests() {
  std::printf("a line: a fade made shorter while under way never passes 1\n");
  Ledger ledger;
  SoundLine line;
  line.set_fade(1000);
  std::vector<float> out(2 * 16);
  line.play(std::make_unique<Level>(ledger, 1.0f));
  line.begin_buffer();
  line.fill_chunk(out.data(), 16);
  line.play(std::make_unique<Level>(ledger, 0.0f));
  line.begin_buffer();
  line.fill_chunk(out.data(), 16);
  CHECK(out[0] <= 1.0f && out[0] > 0.9f);                // the old sound fading from full
  line.set_fade(100);
  line.fill_chunk(out.data(), 16);
  CHECK(*std::max_element(out.begin(), out.end()) <= 1.0f);
}

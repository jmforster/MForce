// The live player's stop, its list of the notes sounding, its faults, its
// guard; the line's carry onset and 3 ms cut; and a seeded scramble of every
// control call.
#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/contract/performance.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/live_player.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace mforce;

namespace {

constexpr int kBuffer = 256;

std::vector<float> buffers(LivePlayer& player, int count) {
  std::vector<float> out, buf(kBuffer);
  for (int k = 0; k < count; ++k) {
    player.fill(buf.data(), kBuffer);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

std::vector<float> until_quiet(LivePlayer& player) {
  std::vector<float> out, buf(kBuffer);
  const int most = 60 * player.sample_rate() / kBuffer;
  for (int k = 0; k < most && player.sounding() > 0; ++k) {
    player.fill(buf.data(), kBuffer);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

PerformedNote held(float noteNumber, float seconds = 2.0f) { return note_at(0.0f, seconds, noteNumber, 0.5f, true); }

std::string enveloped_json(int polyphony, bool sustaining, const char* onsets = "[]") {
  char text[1400];
  std::snprintf(text, sizeof text, R"({"sampleRate":48000,"instrument":{"polyphony":%d,"sustaining":%s,"onsets":%s},"graph":{"output":"sum","nodes":[
    {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
    {"id":"on","type":"PerformNode","params":{"field":"onset"}},
    {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.5,"release":0.1}},
    {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"env"}}},
    {"id":"tone","type":"SineSource","params":{"frequency":300.0,"amplitude":{"ref":"on"}}},
    {"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"osc"},"source2":{"ref":"tone"},"operation":0}}]}})",
                polyphony, sustaining ? "true" : "false", onsets);
  return text;
}

// A held note, then a second held note carried onto the line with `onset`.
std::vector<float> carried(const Patch& patch, int onset) {
  LivePlayer player(patch, 1);
  (void)player.note_on(held(60.0f));
  auto out = buffers(player, 8);
  PerformedNote next = held(64.0f);
  next.onset = onset;
  CHECK(player.note_on(next) != 0 && player.sounding() == 1);
  const auto more = buffers(player, 8);
  out.insert(out.end(), more.begin(), more.end());
  return out;
}

}  // namespace

// stop: every voice, held, struck, and one started just before it, fades in
// 5 ms and comes back; nothing sounds after the fade.
void run_live_stop_tests() {
  std::printf("the live player: stop fades every voice in 5 ms\n");
  const Patch piano = load_patch("patches/library/keys/acoustic_piano/piano_default.json");
  LivePlayer player(piano, 8);
  for (int k = 0; k < 4; ++k) (void)player.note_on(held(float(60 + k)));
  (void)player.note_on(note_at(0.0f, 3.0f, 70.0f, 0.5f));
  (void)buffers(player, 20);
  CHECK(player.note_on(held(72.0f)) != 0);
  player.stop();
  const int fade = int(0.005f * 48000.0f);
  const auto out = buffers(player, fade / kBuffer + 2);
  CHECK(player.sounding() == 0);
  CHECK(std::all_of(out.begin() + fade, out.end(), [](float s) { return s == 0.0f; }));
  CHECK(std::any_of(out.begin(), out.begin() + fade, [](float s) { return s != 0.0f; }));
  CHECK(player.note_on(held(60.0f)) != 0);
}

// sounding_notes: lowest first; a short span takes the lowest; ringing
// voices count until they come back.
void run_live_sounding_notes_tests() {
  std::printf("the live player: sounding_notes, lowest first\n");
  LivePlayer player(patch_door::read_text(enveloped_json(4, false)), 4);
  for (const float n : {67.0f, 60.0f, 72.0f, 64.0f}) (void)player.note_on(held(n));
  std::array<int, 8> all{};
  CHECK(player.sounding_notes(all) == 4 && all[0] == 60 && all[1] == 64 && all[2] == 67 && all[3] == 72);
  std::array<int, 2> two{};
  CHECK(player.sounding_notes(two) == 2 && two[0] == 60 && two[1] == 64);
  CHECK(player.sounding_notes(std::span<int>()) == 0);
  (void)player.note_on(note_at(0.0f, 0.1f, 59.6f, 0.5f));
  CHECK(player.sounding_notes(all) == 5 && all[0] == 60 && all[1] == 60);   // 59.6 rounds to 60
}

// collect_faults: the instrument's wording and merging, cleared as read.
void run_live_faults_tests() {
  std::printf("the live player: collect_faults by node id, cleared as read\n");
  const Patch patch = patch_door::read_text(R"({"sampleRate":48000,"instrument":{"polyphony":2},
      "graph":{"output":"sum","nodes":[
        {"id":"b","type":"SineSource","params":{"frequency":0.0,"amplitude":1.0}},
        {"id":"a","type":"SineSource","params":{"frequency":0.0,"amplitude":1.0}},
        {"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"b"},"source2":{"ref":"a"},"operation":3}}]}})");
  LivePlayer player(patch, 2);
  (void)player.note_on(note_at(0.0f, 0.1f, 60.0f, 0.8f));
  (void)player.note_on(note_at(0.0f, 0.1f, 64.0f, 0.8f));
  (void)buffers(player, 4);
  std::vector<FaultReport> faults;
  player.collect_faults(faults);
  CHECK(faults.size() == 2 && faults[0].nodeId == "a" && faults[1].nodeId == "b");
  CHECK(faults.size() == 2 && (faults[0].faults & fault_bit(Fault::FrequencyClamped)) != 0);
  faults.clear();
  player.collect_faults(faults);
  CHECK(faults.empty());
}

// The guard off: a loud chord's samples pass 1.0; with it on they do not.
void run_live_guard_tests() {
  std::printf("the live player: with the guard off the output is not clipped\n");
  const Patch patch = patch_door::read_text(R"({"sampleRate":48000,"instrument":{"polyphony":4},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":1.0}}]}})");
  const auto chord = [&](bool guard) {
    LivePlayer player(patch, 4, guard);
    for (const float n : {60.0f, 64.0f, 67.0f, 72.0f}) (void)player.note_on(note_at(0.0f, 0.5f, n, 1.0f));
    return buffers(player, 40);
  };
  const auto on = chord(true), off = chord(false);
  CHECK(std::all_of(on.begin(), on.end(), [](float s) { return std::fabs(s) <= 0.999f; }));
  CHECK(std::any_of(off.begin(), off.end(), [](float s) { return std::fabs(s) > 1.0f; }));
}

// The line: a carry naming no onset takes "slur" when the vocabulary has it,
// on a patch that sounds its onset; on phrase_smoke_phrased, opening a line
// while a release rings cuts it in 3 ms, and an overlap still carries.
void run_live_line_refinement_tests() {
  std::printf("the live player: the carry's slur, the 3 ms cut, the overlap\n");
  const Patch phrased = load_patch("patches/baselines/phrase_smoke_phrased.json");
  CHECK(phrased.instrument->sustaining);
  const Patch sounded = patch_door::read_text(enveloped_json(1, true, R"(["breath","slur"])"));
  const Patch plain = patch_door::read_text(enveloped_json(1, true, R"(["breath"])"));
  CHECK(patch_door::same_samples(carried(sounded, 0), carried(sounded, 2)));
  CHECK(!patch_door::same_samples(carried(sounded, 0), carried(plain, 0)));
  // The cut: a line released and ringing, then a new line; against the new
  // line alone on a player whose rotation puts it on the same voice.
  const int cut = int(0.003f * 48000.0f);
  LivePlayer a(phrased, 2), b(phrased, 2);
  const LivePlayer::NoteId first = a.note_on(held(60.0f));
  (void)b.note_on(note_at(0.0f, 0.01f, 60.0f, 0.0f));
  (void)buffers(a, 20);
  (void)until_quiet(b);
  a.note_off(first);
  (void)buffers(a, 2);
  CHECK(a.sounding() == 1);
  CHECK(a.note_on(held(67.0f)) != 0 && b.note_on(held(67.0f)) != 0);
  const auto withCut = buffers(a, 4), alone = buffers(b, 4);
  CHECK(withCut.size() == alone.size());
  CHECK(std::equal(withCut.begin() + cut, withCut.end(), alone.begin() + cut));
  CHECK(!std::equal(withCut.begin(), withCut.begin() + cut, alone.begin()));
  CHECK(a.sounding() == 1);
  CHECK(a.note_on(held(69.0f)) != 0 && a.sounding() == 1);   // the overlap carries
}

// A seeded scramble of note_on (held and not), note_off, stop and fills of
// random lengths, on an instrument that sustains and one that does not; then
// every key let go and the fills played out.
void run_live_scramble_tests() {
  const std::uint32_t seed = 20261123u;
  std::printf("the live player: a scramble of control calls, seed %u\n", seed);
  for (const bool sustaining : {true, false}) {
    LivePlayer player(patch_door::read_text(enveloped_json(4, sustaining)), 4);
    std::mt19937 rng(seed);
    std::vector<LivePlayer::NoteId> keys;
    std::vector<float> buf(2048);
    int ons = 0, sent = 0, offs = 0, stops = 0, fills = 0, refusedWhenQuiet = 0;
    bool finite = true;
    for (int i = 0; i < 4000; ++i) {
      const unsigned r = rng() % 100;
      if (r < 40) {
        const bool hold = rng() % 2 == 0;
        if (player.sounding() == 0 && player.note_on(note_at(0.0f, 0.05f, 60.0f, 0.0f)) == 0) ++refusedWhenQuiet;
        const LivePlayer::NoteId id = player.note_on(note_at(0.0f, 0.02f + float(rng() % 100) * 0.001f,
                                                             float(48 + rng() % 24), 0.3f, hold));
        ++ons;
        if (id) { ++sent; if (hold) keys.push_back(id); }
      } else if (r < 70 && !keys.empty()) {
        const size_t k = rng() % keys.size();
        player.note_off(keys[k]);
        keys.erase(keys.begin() + std::ptrdiff_t(k));
        ++offs;
      } else if (r < 72) {
        player.stop();
        ++stops;
      } else {
        const int frames = 1 + int(rng() % 2048);
        player.fill(buf.data(), frames);
        for (int f = 0; f < frames; ++f) finite = finite && std::isfinite(buf[size_t(f)]);
        ++fills;
      }
    }
    for (const LivePlayer::NoteId id : keys) player.note_off(id);
    (void)until_quiet(player);
    std::printf("  sustaining %d: %d note_on (%d sent), %d note_off, %d stop, %d fill; %d sounding at the end\n",
                int(sustaining), ons, sent, offs, stops, fills, player.sounding());
    CHECK(player.sounding() == 0 && finite && refusedWhenQuiet == 0 && sent > 0);
  }
}

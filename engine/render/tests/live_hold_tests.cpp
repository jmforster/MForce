// The live player's held notes: a held note on an instrument that does not
// sustain holds a voice of its own until its note_off; note_off by id; stop;
// the notes sounding; the faults; the guard off; and the line's carry onset
// and 3 ms cut on an instrument that sustains.
#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/contract/performance.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/live_player.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;

namespace {

constexpr int kBuffer = 256;
const char* const kPiano = "patches/library/keys/acoustic_piano/piano_default.json";
const char* const kPhrased = "patches/baselines/phrase_smoke_phrased.json";

// `buffers` buffers of the player's output.
std::vector<float> buffers(LivePlayer& player, int count) {
  std::vector<float> out, buf(kBuffer);
  for (int k = 0; k < count; ++k) {
    player.fill(buf.data(), kBuffer);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

// Buffers until no voice sounds (at most a minute of them); the output.
std::vector<float> until_quiet(LivePlayer& player) {
  std::vector<float> out, buf(kBuffer);
  const int most = 60 * player.sample_rate() / kBuffer;
  for (int k = 0; k < most && player.sounding() > 0; ++k) {
    player.fill(buf.data(), kBuffer);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

std::vector<int> notes_of(LivePlayer& player) {
  std::array<int, 64> into{};
  const int n = player.sounding_notes(into);
  return std::vector<int>(into.begin(), into.begin() + n);
}

PerformedNote held(float noteNumber, float seconds = 2.0f) { return note_at(0.0f, seconds, noteNumber, 0.5f, true); }

// An enveloped sine whose pitch reaches the oscillator, `polyphony` voices,
// marked sustaining or not.
std::string enveloped_json(int polyphony, bool sustaining) {
  char text[900];
  std::snprintf(text, sizeof text, R"({"sampleRate":48000,"instrument":{"polyphony":%d,"sustaining":%s},"graph":{"output":"osc","nodes":[
    {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
    {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.5,"release":0.1}},
    {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"env"}}}]}})",
                polyphony, sustaining ? "true" : "false");
  return text;
}

// A sustaining patch whose output carries the note's onset id as the
// amplitude of a second sine, so a carry's onset is heard.
std::string onset_json(const char* onsets) {
  char text[1400];
  std::snprintf(text, sizeof text, R"({"sampleRate":48000,"instrument":{"polyphony":1,"sustaining":true,"onsets":%s},"graph":{"output":"sum","nodes":[
    {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
    {"id":"on","type":"PerformNode","params":{"field":"onset"}},
    {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.5,"release":0.1}},
    {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"env"}}},
    {"id":"tone","type":"SineSource","params":{"frequency":300.0,"amplitude":{"ref":"on"}}},
    {"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"osc"},"source2":{"ref":"tone"},"operation":0}}]}})",
                onsets);
  return text;
}

}  // namespace

// Eight held at once on the piano: all eight sound; released in a scrambled
// order, each release takes that note and no other out once it has rung out.
void run_live_hold_eight_tests() {
  std::printf("the live player: eight held notes on an instrument that does not sustain\n");
  const Patch piano = load_patch(kPiano);
  CHECK(!piano.instrument->sustaining);
  LivePlayer player(piano, 8);
  std::vector<LivePlayer::NoteId> ids;
  for (int k = 0; k < 8; ++k) ids.push_back(player.note_on(held(float(60 + 2 * k))));
  CHECK(std::all_of(ids.begin(), ids.end(), [](LivePlayer::NoteId id) { return id != 0; }));
  (void)buffers(player, 40);
  std::vector<int> left{60, 62, 64, 66, 68, 70, 72, 74};
  CHECK(notes_of(player) == left);
  const int order[] = {5, 1, 7, 0, 3, 6, 2, 4};
  const int most = 10 * player.sample_rate() / kBuffer;
  for (int k : order) {
    player.note_off(ids[size_t(k)]);
    left.erase(std::find(left.begin(), left.end(), 60 + 2 * k));
    for (int b = 0; b < most && player.sounding() > int(left.size()); ++b) (void)buffers(player, 1);
    if (notes_of(player) != left) std::printf("  after releasing %d: %zu notes\n", 60 + 2 * k, notes_of(player).size());
    CHECK(notes_of(player) == left);
  }
}

// A ninth held note steals as a ninth unheld one does: the same victim, the
// same samples while the ninth plays its first second.
void run_live_hold_ninth_tests() {
  std::printf("the live player: a ninth held note steals as a ninth struck note does\n");
  const Patch piano = load_patch(kPiano);
  const auto run = [&](bool hold) {
    LivePlayer player(piano, 8);
    for (int k = 0; k < 8; ++k) (void)player.note_on(held(float(60 + k)));
    auto out = buffers(player, 20);
    CHECK(player.note_on(note_at(0.0f, 5.0f, 72.0f, 0.5f, hold)) != 0);
    const auto more = buffers(player, 190);
    out.insert(out.end(), more.begin(), more.end());
    const std::vector<int> after{61, 62, 63, 64, 65, 66, 67, 72};
    CHECK(notes_of(player) == after);
    return out;
  };
  const auto heldNinth = run(true), struckNinth = run(false);
  if (!patch_door::same_samples(heldNinth, struckNinth)) std::printf("  %s\n", patch_door::difference(heldNinth, struckNinth).c_str());
  CHECK(patch_door::same_samples(heldNinth, struckNinth));
}

// A held note is the struck note of its length until it is let go.
void run_live_hold_struck_tests() {
  std::printf("the live player: a held note equals a long struck note until its release\n");
  const Patch piano = load_patch(kPiano);
  LivePlayer a(piano, 8), b(piano, 8);
  CHECK(a.note_on(note_at(0.0f, 10.0f, 57.0f, 0.6f, true)) != 0);
  CHECK(b.note_on(note_at(0.0f, 10.0f, 57.0f, 0.6f)) != 0);
  const auto heldOut = buffers(a, 200), struckOut = buffers(b, 200);
  if (!patch_door::same_samples(heldOut, struckOut)) std::printf("  %s\n", patch_door::difference(heldOut, struckOut).c_str());
  CHECK(patch_door::same_samples(heldOut, struckOut));
  CHECK(std::any_of(heldOut.begin(), heldOut.end(), [](float s) { return s != 0.0f; }));
}

// note_off that names nothing sounding and held changes nothing: a stolen
// note's id, 0, an id never given, a release twice, a note given back, a
// struck note.
void run_live_hold_note_off_nothing_tests() {
  std::printf("the live player: note_off that names nothing changes nothing\n");
  const Patch patch = patch_door::read_text(enveloped_json(1, false));
  const auto run = [&](bool noisy) {
    LivePlayer player(patch, 1);
    const LivePlayer::NoteId a = player.note_on(held(60.0f));
    auto out = buffers(player, 4);
    const LivePlayer::NoteId b = player.note_on(held(64.0f));     // steals a
    if (noisy) { player.note_off(a); player.note_off(0); player.note_off(987654); }
    const auto mid = buffers(player, 10);
    player.note_off(b);
    if (noisy) player.note_off(b);
    const auto tail = until_quiet(player);
    if (noisy) { player.note_off(b); player.note_off(a); }
    const LivePlayer::NoteId d = player.note_on(note_at(0.0f, 0.2f, 67.0f, 0.5f));
    if (noisy) player.note_off(d);
    const auto last = until_quiet(player);
    for (const auto* part : {&mid, &tail, &last}) out.insert(out.end(), part->begin(), part->end());
    CHECK(a != 0 && b != 0 && d != 0 && player.sounding() == 0);
    return out;
  };
  const auto quiet = run(false), noisy = run(true);
  CHECK(!quiet.empty() && patch_door::same_samples(quiet, noisy));
}

// The queue full: with no fill, more notes and key-ups than the queue holds;
// then the fills; every key-up arrives and every voice comes back. Again with
// stop() among the key-ups.
void run_live_hold_queue_full_tests() {
  std::printf("the live player: key-ups and a stop the full queue cannot take wait and are sent\n");
  const Patch patch = patch_door::read_text(enveloped_json(1, false));
  for (const bool stopping : {false, true}) {
    LivePlayer player(patch, 100);
    std::vector<LivePlayer::NoteId> ids;
    for (int k = 0; k < 300; ++k)
      if (const LivePlayer::NoteId id = player.note_on(held(float(40 + k % 40)))) ids.push_back(id);
    CHECK(ids.size() > 100);
    for (size_t k = 0; k < ids.size(); ++k) {
      if (stopping && k == ids.size() / 2) player.stop();
      player.note_off(ids[k]);
    }
    CHECK(player.note_on(held(50.0f)) == 0);                     // a key-up or the stop waits
    (void)until_quiet(player);
    if (player.sounding() != 0) std::printf("  stop %d: %d still sound\n", int(stopping), player.sounding());
    CHECK(player.sounding() == 0);
    CHECK(player.note_on(held(50.0f)) != 0);
  }
}

// The three measured instruments that cannot hold: a held note plays its
// length, as a struck note does, and its note_off does nothing.
void run_live_hold_cannot_tests() {
  std::printf("the live player: on a voice that cannot hold, a held note is a struck note\n");
  for (const char* path : {"patches/baselines/feedback/loop_tap_counter.json",
                           "patches/baselines/feedback/loop_tap_starved.json",
                           "patches/library/percussion/hi_hat1.json"}) {
    const Patch patch = load_patch(path);
    LivePlayer a(patch, 8), b(patch, 8);
    const LivePlayer::NoteId id = a.note_on(note_at(0.0f, 0.3f, 60.0f, 0.6f, true));
    CHECK(id != 0 && b.note_on(note_at(0.0f, 0.3f, 60.0f, 0.6f)) != 0);
    auto heldOut = buffers(a, 5), struckOut = buffers(b, 5);
    a.note_off(id);
    const auto heldTail = until_quiet(a), struckTail = until_quiet(b);
    heldOut.insert(heldOut.end(), heldTail.begin(), heldTail.end());
    struckOut.insert(struckOut.end(), struckTail.begin(), struckTail.end());
    if (!patch_door::same_samples(heldOut, struckOut)) std::printf("  %s: %s\n", path, patch_door::difference(heldOut, struckOut).c_str());
    CHECK(patch_door::same_samples(heldOut, struckOut));
  }
}

// A patch played offline from a performance: tied to the embedded-score path
// over every measured instrument; a strip is the node's own output; the
// normalised level; a plain sound refused; a drum kit with a gap.
#include "test_support.h"
#include "mforce/perform/embedded_score.h"
#include "mforce/perform/text_performance.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace {

std::vector<Patch> measured_instruments() {
  std::vector<std::string> paths;
  for (const char* root : {"patches/library", "patches/baselines"})
    for (const auto& e : std::filesystem::recursive_directory_iterator(root))
      if (e.path().extension() == ".json") paths.push_back(e.path().generic_string());
  std::sort(paths.begin(), paths.end());
  std::vector<Patch> out;
  for (const auto& p : paths) {
    Patch patch = load_patch(p);
    if (patch.instrument) out.push_back(std::move(patch));
  }
  return out;
}

// `notes` as an embedded score's text: each note at its start, written so
// that it reads back as the same floats.
std::string score_of(const Performance& p) {
  nlohmann::json score = nlohmann::json::array();
  for (const auto& n : p.notes)
    score.push_back({{"note", double(n.noteNumber)}, {"velocity", double(n.velocity)},
                     {"time", double(n.start)}, {"duration", double(n.duration)}});
  return score.dump();
}

// The two renders agree, sample for sample, over the frames both have.
bool agree(const PlayResult& a, const PlayResult& b, long long& compared) {
  const size_t n = std::min(a.stereo.size(), b.stereo.size());
  compared += (long long)(n / 2);
  return n > 0 && std::memcmp(a.stereo.data(), b.stereo.data(), n * sizeof(float)) == 0;
}

}  // namespace

// One short note through play(patch, performance, ...) equals play(copy),
// the copy holding that note as its embedded score, over every measured
// instrument; and an unmarked passage of three notes on the first few.
void run_offline_play_proof_tests() {
  std::printf("play with a performance: equal to the embedded-score path over the measured instruments\n");
  const auto t0 = std::chrono::steady_clock::now();
  const std::vector<Patch> instruments = measured_instruments();
  long long compared = 0;
  int differ = 0, passages = 0;
  for (size_t k = 0; k < instruments.size(); ++k) {
    const Patch& patch = instruments[k];
    const auto first = first_score_note(patch.score);
    const TextPerformance note = perform_note(first ? first->note : 60.0f, first ? first->velocity : 0.8f, 0.5f);
    Patch copy = patch;
    copy.score = score_of(note.performance);
    if (!agree(play(patch, note.performance, note.seconds, Level::Guarded), play(copy), compared)) {
      ++differ;
      std::printf("  differs: note on patch %zu\n", k);
    }
    if (k % 20 != 0) continue;
    const TextPerformance passage = perform_passage("Cq Eq Gq", 4, 120.0f, 0.7f, Onsets::Auto,
                                                    InstrumentClass{patch.instrument->sustaining, patch.instrument->onsets});
    copy.score = score_of(passage.performance);
    ++passages;
    if (!agree(play(patch, passage.performance, passage.seconds, Level::Guarded), play(copy), compared)) {
      ++differ;
      std::printf("  differs: passage on patch %zu\n", k);
    }
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  %zu instruments, %d passages, %lld frames compared, %d differ, %.1f s\n", instruments.size(), passages,
              compared, differ, secs);
  CHECK(instruments.size() > 0 && differ == 0);
}

// A strip is the node's own output: on the baseline sine, the output node's
// strip times the velocity is the instrument's render alone (normalised, at
// a peak under 0.95, so unscaled), and times the volume too through the mixer.
void run_offline_strip_tests() {
  std::printf("play with a performance: a captured strip is the node's raw output\n");
  const Patch sine = load_patch("patches/baselines/BaselineSIN.json");
  const TextPerformance note = perform_note(60.0f, 0.5f, 0.3f);
  const PlayResult alone = play(sine, note.performance, note.seconds, Level::Normalised, {sine.graph.output, "nothing"});
  CHECK(alone.strips.size() == 2 && alone.scale == 1.0f);
  if (alone.strips.size() != 2) return;
  const auto& strip = alone.strips[0];
  bool exact = int(strip.size()) == alone.frames, sounds = false;
  for (int i = 0; exact && i < alone.frames; ++i) {
    exact = alone.stereo[size_t(i) * 2] == strip[size_t(i)] * 0.5f;
    sounds = sounds || strip[size_t(i)] != 0.0f;
  }
  CHECK(exact && sounds);
  CHECK(std::all_of(alone.strips[1].begin(), alone.strips[1].end(), [](float s) { return s == 0.0f; }));
  const PlayResult mixed = play(sine, note.performance, note.seconds, Level::Guarded, {sine.graph.output});
  float worst = 0.0f;
  for (int i = 0; i < mixed.frames; ++i)
    worst = std::max(worst, std::fabs(mixed.stereo[size_t(i) * 2] - mixed.strips[0][size_t(i)] * 0.5f * sine.instrument->volume));
  CHECK(worst < 1e-6f);
}

// Normalised: a chord that sums past 1 comes out with its peak at 0.95 and
// its shape kept (the quiet chord's, scaled); a quiet chord as it went in.
void run_offline_normalised_tests() {
  std::printf("play with a performance: the normalised level\n");
  const Patch patch = patch_door::read_text(R"({"sampleRate":48000,"instrument":{"polyphony":4,"volume":0.5},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":1.0}}]}})");
  const auto chord = [&](float velocity) {
    Performance p;
    for (const float n : {60.0f, 64.0f, 67.0f, 72.0f}) p.notes.push_back({.start = 0.0f, .duration = 0.3f, .noteNumber = n, .velocity = velocity});
    return play(patch, p, 0.5, Level::Normalised);
  };
  const PlayResult loud = chord(1.0f), quiet = chord(0.1f);
  CHECK(loud.mixPeak > 1.0f && std::fabs(loud.peak - 0.95f) < 1e-6f);
  CHECK(quiet.scale == 1.0f && quiet.mixPeak < 0.95f && quiet.peak == quiet.mixPeak);
  float worst = 0.0f;
  for (size_t i = 0; i < loud.stereo.size(); ++i)
    worst = std::max(worst, std::fabs(loud.stereo[i] - quiet.stereo[i] * 10.0f * loud.scale));
  CHECK(worst < 1e-5f);
  bool refused = false;
  try {
    (void)play(load_patch("patches/baselines/delay_test.json"), Performance{}, 1.0, Level::Guarded);
  } catch (const std::invalid_argument& e) {
    refused = std::string(e.what()).find("plain sound") != std::string::npos;
  }
  CHECK(refused);
}

// A kit of two library percussion patches on drums 0 and 2, a gap at 1, and
// a hit on each of 0, 1, 2 and 7: the hits on 1 and 7 are ignored, the others
// sound from their starts.
void run_offline_drums_tests() {
  std::printf("play_drums: a gap and a number past the last are ignored\n");
  const std::vector<std::pair<int, Patch>> kit = {{0, load_patch("patches/library/percussion/hi_hat1.json")},
                                                  {2, load_patch("patches/library/percussion/kick_studio_dry.json")}};
  const auto hit = [](int drum, float start) { return PerformedHit{.start = start, .duration = 0.1f, .drumNumber = drum, .velocity = 1.0f}; };
  const Performance all{{}, {hit(0, 1.0f), hit(1, 2.0f), hit(2, 3.0f), hit(7, 3.5f)}};
  const Performance sounding{{}, {hit(0, 1.0f), hit(2, 3.0f)}};
  const PlayResult a = play_drums(kit, all, 4.5, 48000), b = play_drums(kit, sounding, 4.5, 48000);
  CHECK(a.stereo == b.stereo && a.frames == 4.5 * 48000);
  const auto any_between = [&](double from, double to) {
    for (int i = int(from * 48000); i < int(to * 48000); ++i)
      if (a.stereo[size_t(i) * 2] != 0.0f) return true;
    return false;
  };
  CHECK(!any_between(0.0, 1.0) && any_between(1.0, 1.1) && !any_between(1.2, 3.0) && any_between(3.0, 3.1));
}

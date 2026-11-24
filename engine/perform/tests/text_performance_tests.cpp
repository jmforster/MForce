// The transport's texts made into performances: the passage's phrase table,
// chords through the chord performer, a drum pattern's hits, the refusals;
// and the embedded score's first note as written.
#include "mforce/testing/check.h"
#include "mforce/contract/instrument_class.h"
#include "mforce/contract/performance.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/structure.h"
#include "mforce/perform/conductor.h"
#include "mforce/perform/embedded_score.h"
#include "mforce/perform/text_performance.h"
#include "mforce/perform/time_map.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace mforce;

namespace {

// The words a call throws; empty when it throws nothing.
std::string thrown(const std::function<void()>& call) {
  try {
    call();
  } catch (const std::exception& e) {
    return e.what();
  }
  return {};
}

// One row of the passage table: the text, the instrument, the onset mode,
// and what each note's start, hold and onset id must be.
struct PassageCase {
  const char* why;
  const char* text;
  bool sustaining;
  bool vocabulary;
  Onsets onsets;
  std::vector<float> starts;
  std::vector<int> holds;
  std::vector<int> onsetIds;
  bool told;
};

// The vocabulary's ids: breath 1, tongue 2, slur 3.
constexpr int B = 1, T = 2, S = 3;

// The old editor's passage: the schedule (old:7559-7614: a cursor, rests as
// gaps, hold = not the last of its phrase, where a note is last when the
// next element is a rest, begins a phrase, or there is none), stamp_passage
// (old:4014-4035: run only when the text has a '|'; all breath, or an
// instrument that does not sustain, makes every note plain, the second said;
// otherwise a phrase's first note is a breath and the rest follow the mode,
// Auto tongues a repeated pitch and slurs a new one) and the engine call's
// hold && sustaining (old:4105-4111). parse_passage begins a phrase at every
// note of a text with no '|' and after every rest (parse_util.h:264-292).
// At 60 bpm a quarter is one second.
std::vector<PassageCase> passage_table() {
  return {
      {"no marks, sustaining", "Cq Dq Eq", true, true, Onsets::Auto, {0, 1, 2}, {0, 0, 0}, {0, 0, 0}, false},
      {"no marks, not sustaining", "Cq Dq Eq", false, true, Onsets::Auto, {0, 1, 2}, {0, 0, 0}, {0, 0, 0}, false},
      {"marks, sustaining, auto", "Cq Dq Dq | Eq Fq", true, true, Onsets::Auto, {0, 1, 2, 3, 4},
       {1, 1, 0, 1, 0}, {B, S, T, B, S}, false},
      {"marks, not sustaining", "Cq Dq Dq | Eq Fq", false, true, Onsets::Auto, {0, 1, 2, 3, 4},
       {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, true},
      {"marks, all tongue", "Cq Dq Dq | Eq Fq", true, true, Onsets::AllTongue, {0, 1, 2, 3, 4},
       {1, 1, 0, 1, 0}, {B, T, T, B, T}, false},
      {"marks, all slur", "Cq Dq Dq | Eq Fq", true, true, Onsets::AllSlur, {0, 1, 2, 3, 4},
       {1, 1, 0, 1, 0}, {B, S, S, B, S}, false},
      {"marks, all breath", "Cq Dq Dq | Eq Fq", true, true, Onsets::AllBreath, {0, 1, 2, 3, 4},
       {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, false},
      {"marks, all breath, not sustaining (not told)", "Cq Dq | Eq", false, true, Onsets::AllBreath, {0, 1, 2},
       {0, 0, 0}, {0, 0, 0}, false},
      {"a rest inside a phrase ends it", "Cq Dq Rq Eq Eq | Fq", true, true, Onsets::Auto, {0, 1, 3, 4, 5},
       {1, 0, 1, 0, 0}, {B, S, B, T, B}, false},
      {"marks, no vocabulary: holds stamped, ids 0", "Cq Dq Dq | Eq Fq", true, false, Onsets::Auto,
       {0, 1, 2, 3, 4}, {1, 1, 0, 1, 0}, {0, 0, 0, 0, 0}, false},
  };
}

bool passage_matches(const PassageCase& c, const TextPerformance& t) {
  const auto& n = t.performance.notes;
  if (n.size() != c.starts.size()) return false;
  for (size_t i = 0; i < n.size(); ++i)
    if (n[i].start != c.starts[i] || int(n[i].hold) != c.holds[i] || n[i].onset != c.onsetIds[i] ||
        n[i].duration != 1.0f || n[i].velocity != 0.6f)
      return false;
  return t.notes.size() == (c.told ? 1u : 0u) && t.seconds == double(n.back().start) + 1.0 + double(kVoiceTailSeconds);
}

}  // namespace

void run_text_passage_tests() {
  std::printf("text performance: the passage table, each onset mode, rests, the refusals\n");
  for (const PassageCase& c : passage_table()) {
    InstrumentClass cls{c.sustaining, {}};
    if (c.vocabulary) cls.onsets = {"breath", "tongue", "slur"};
    const TextPerformance t = perform_passage(c.text, 4, 60.0f, 0.6f, c.onsets, cls);
    if (!passage_matches(c, t)) std::printf("  differs: %s\n", c.why);
    CHECK(passage_matches(c, t));
  }
  const InstrumentClass cls{true, {}};
  CHECK(thrown([&] { (void)perform_passage("", 4, 60.0f, 0.8f, Onsets::Auto, cls); }) ==
        "No notes parsed from passage string");
  CHECK(thrown([&] { (void)perform_passage("Rq Rh", 4, 60.0f, 0.8f, Onsets::Auto, cls); }) ==
        "Passage contains only rests");
  CHECK(thrown([&] { (void)perform_passage("Xq", 4, 60.0f, 0.8f, Onsets::Auto, cls); }).find("must start with A-G") !=
        std::string::npos);
  const TextPerformance note = perform_note(57.0f, 0.7f, 0.5f);
  CHECK(note.performance.notes.size() == 1 && note.performance.notes[0].noteNumber == 57.0f &&
        note.performance.notes[0].duration == 0.5f && !note.performance.notes[0].hold);
  CHECK(note.seconds == 0.5 + double(kVoiceTailSeconds));
}

// Chords: the performer's notes at the starts bpm gives, with a figure; the
// delay reaches a block chord; the refusals.
void run_text_chords_tests() {
  std::printf("text performance: chords through the performer, the delay, the refusals\n");
  const TextPerformance figured = perform_chords("C_q G_q", 4, 120.0f, "", "Josie", 15.0f);
  const auto parsed = parse_chord_string("C_q G_q", 4, "Default", "Josie");
  Part part;
  for (const auto& pc : parsed) part.add_chord(pc.chord);
  Conductor conductor;
  conductor.chordPerformer.defaultSpreadMs = 15.0f;
  conductor.chordPerformer.register_josie_figures();
  const Performance want = conductor.perform(part, TimeMap(120.0f));
  bool same = want.notes.size() == figured.performance.notes.size() && !want.notes.empty();
  for (size_t i = 0; same && i < want.notes.size(); ++i)
    same = want.notes[i].start == figured.performance.notes[i].start &&
           want.notes[i].noteNumber == figured.performance.notes[i].noteNumber &&
           want.notes[i].duration == figured.performance.notes[i].duration;
  CHECK(same);
  CHECK(std::any_of(figured.performance.notes.begin(), figured.performance.notes.end(),
                    [](const PerformedNote& n) { return n.start == 0.5f; }));   // the second chord at one beat
  CHECK(figured.seconds == 2.0 * 60.0 / 120.0 + 1.0);
  const TextPerformance block = perform_chords("C_q", 4, 120.0f, "", "", 40.0f);
  const auto& b = block.performance.notes;
  CHECK(b.size() == 3);
  CHECK(b.size() == 3 && b[0].start == 0.0f && std::fabs(b[1].start - 0.04f) < 1e-6f && std::fabs(b[2].start - 0.08f) < 1e-6f);
  CHECK(std::all_of(b.begin(), b.end(), [](const PerformedNote& n) { return n.velocity == 1.0f; }));
  CHECK(thrown([] { (void)perform_chords("", 4, 120.0f, "", "", 15.0f); }) == "No chords parsed from string");
  CHECK(thrown([] { (void)perform_chords("C", 4, 120.0f, "", "", 15.0f); }).find("missing duration") != std::string::npos);
}

// Drums: each hit's start, its length and its floor, the whole's seconds;
// the refusals.
void run_text_drums_tests() {
  std::printf("text performance: drum hits, their lengths and the floor, the refusals\n");
  const TextPerformance t = perform_drums("KK1,3;SN2/4/1", 120.0f);
  const auto& h = t.performance.hits;
  CHECK(h.size() == 3);
  CHECK(h.size() == 3 && h[0].drumNumber == 0 && h[0].start == 0.5f && h[1].start == 1.5f &&
        h[2].drumNumber == 1 && h[2].start == 1.0f);
  CHECK(std::all_of(h.begin(), h.end(), [](const PerformedHit& x) { return x.duration == 0.1f * 60.0f / 120.0f && x.velocity == 1.0f; }));
  CHECK(t.seconds == 4.0 * 60.0 / 120.0 + 1.0);
  const TextPerformance fast = perform_drums("KK1/4/2", 600.0f);
  CHECK(fast.performance.hits.size() == 2 && fast.performance.hits[1].start == 5.0f * 60.0f / 600.0f);
  CHECK(std::all_of(fast.performance.hits.begin(), fast.performance.hits.end(),
                    [](const PerformedHit& x) { return x.duration == 0.02f; }));
  CHECK(thrown([] { (void)perform_drums("KK,/4/1", 120.0f); }) == "No hits parsed from drum pattern");
  CHECK(thrown([] { (void)perform_drums("", 120.0f); }) == "Empty drum pattern");
}

// The first score note as written, over the measured set: it is what a JSON
// read of score[0] gives, the score's text being what the patch reader keeps
// (the array's dump); none where there is no score. And the default score.
void run_first_score_note_tests() {
  std::printf("the embedded score: the first note as written over the measured set; the default\n");
  int scored = 0, unscored = 0;
  for (const char* root : {"patches/library", "patches/baselines"})
    for (const auto& e : std::filesystem::recursive_directory_iterator(root)) {
      if (e.path().extension() != ".json") continue;
      std::ifstream in(e.path());
      const nlohmann::json patch = nlohmann::json::parse(in);
      if (!patch.contains("score")) {
        ++unscored;
        CHECK(!first_score_note("").has_value());
        continue;
      }
      ++scored;
      const nlohmann::json& n0 = patch["score"][0];
      const auto got = first_score_note(patch["score"].dump());
      CHECK(got.has_value() && got->note == n0.value("note", 60.0f) && got->velocity == n0.value("velocity", 0.8f) &&
            got->duration == n0.value("duration", 2.0f));
    }
  std::printf("  %d scored, %d without a score\n", scored, unscored);
  CHECK(scored > 0 && unscored > 0);
  CHECK(!first_score_note("[]").has_value() && !first_score_note("{}").has_value() && !first_score_note("[3]").has_value());
  const auto plain = first_score_note(R"([{"time":0.0}])");
  CHECK(plain && plain->note == 60.0f && plain->velocity == 0.8f && plain->duration == 2.0f);
  CHECK(default_embedded_score() == R"([{"duration":2.0,"note":60,"time":0.0,"velocity":0.8}])");
}

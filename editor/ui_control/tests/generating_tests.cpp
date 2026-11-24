// Generate on each tab, its refusals in their words, an error that stays;
// Play's order and its rule per tab; playing through a take's end; the WAV,
// the texts; the seed over the measured set and a new patch's score.
#include "mforce/testing/check.h"
#include "transport_fixture.h"
#include "mforce/perform/embedded_score.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/wav_reader.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace transport_fixture;

namespace {

using Mode = Transport::Mode;

// The session after a Generate on `mode` with `edit` applied to its
// transport first.
template <class Edit>
void generate_on(Session& s, Mode mode, Edit edit) {
  s.transport.mode = mode;
  edit(s.transport);
  generate(s);
}

std::string scratch(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }

}  // namespace

// Each tab renders the patch as it is, at its own rate; a plain sound its
// own length; and the preconditions.
void run_generate_tabs_tests() {
  std::printf("generate: each tab, a plain sound, the patch's rate, the preconditions\n");
  Session s;
  generate(s);
  CHECK(s.status == "No patch loaded — open a patch first" && s.take.stereo.empty());
  open_file(s, kOboe);
  generate_on(s, Mode::Note, [](Transport& t) { t.note = "C4"; });
  CHECK(s.status == "Generated note" && s.take.sampleRate == s.open.patch.sampleRate && s.take.frames > 0);
  CHECK(!s.take.stripIds.empty() && s.take.strips.size() == s.take.stripIds.size() && s.lastNote.note == 48);
  open_file(s, kPiano);
  generate_on(s, Mode::Passage, [](Transport& t) { t.passage = "Cq Eq"; });
  CHECK(s.status == "Generated 2 notes" && !s.take.strips.empty());
  generate_on(s, Mode::Passage, [](Transport& t) { t.passage = "Cq | Eq"; });
  CHECK(s.status == "patch is not marked sustaining — phrase marks ignored" && !s.take.stereo.empty());
  generate_on(s, Mode::Chords, [](Transport& t) { t.chords = "C_q G_q"; });
  CHECK(s.status == "Generated 2 chords" && s.take.strips.empty() && !s.take.stereo.empty());
  CHECK(std::all_of(s.take.stereo.begin(), s.take.stereo.end(), [](float x) { return std::fabs(x) <= 0.95f + 1e-6f; }));
  generate_on(s, Mode::Drums, [](Transport& t) { t.drumPattern = "KK1;HH2/4/1"; });
  CHECK(s.status == "Generated 2 hits" && s.take.sampleRate == 48000 && s.take.strips.empty());
  open_file(s, kSlowRate);
  generate_on(s, Mode::Note, [](Transport&) {});
  CHECK(s.take.sampleRate == 22050);
  open_file(s, kPlain);
  generate_on(s, Mode::Chords, [](Transport&) {});
  CHECK(s.status.rfind("Generated the plain sound", 0) == 0 && s.take.frames == sound_frames(s.open.patch));
  new_file(s);
  generate(s);
  CHECK(s.status == "Output node has no source connected — wire a node into Output");
}

// Each refusal in its words, staying on screen, with no take.
void run_generate_refusal_tests() {
  std::printf("generate: the refusals, each in its words, the take empty\n");
  Session s;
  open_file(s, kPiano);
  const auto refused = [&](Mode mode, auto edit, const std::string& words) {
    generate_on(s, mode, edit);
    const bool ok = s.status.find(words) != std::string::npos && s.take.stereo.empty();
    if (!ok) std::printf("  status: %s\n", s.status.c_str());
    return ok;
  };
  CHECK(refused(Mode::Passage, [](Transport& t) { t.passage = "Xq"; }, "must start with A-G"));
  CHECK(refused(Mode::Passage, [](Transport& t) { t.passage = ""; }, "No notes parsed from passage string"));
  CHECK(refused(Mode::Passage, [](Transport& t) { t.passage = "Rq"; }, "Passage contains only rests"));
  CHECK(refused(Mode::Chords, [](Transport& t) { t.chords = ""; }, "No chords parsed from string"));
  CHECK(refused(Mode::Drums, [](Transport& t) { t.drumPattern = "KK,/4/1"; }, "No hits parsed from drum pattern"));
  CHECK(refused(Mode::Drums, [](Transport& t) { t.drumPattern = "KK1/4/1"; t.drumMap = ""; },
                "No drum patches specified"));
  CHECK(refused(Mode::Drums, [](Transport& t) { t.drumMap = "KK=nowhere/kick.json"; }, "Drum patch not found: nowhere/kick.json"));
  CHECK(refused(Mode::Drums, [](Transport& t) { t.drumMap = "SN=nowhere/snare.json"; }, "No drum patches could be loaded"));
}

// Play: everything stops first, keys too, then the take plays; a key after
// sounds over it; the rule per tab; playing through a take's end; a failed
// Generate plays nothing; a plain sound runs until Stop.
void run_play_tests() {
  std::printf("play: the order, the rule per tab, a take's end, a failure, a plain sound\n");
  Session s;
  open_file(s, kOboe);
  s.keyboard.seconds = 0.3f;
  key_down(s, 1, 60);
  (void)run(s, 4);
  play(s);
  CHECK(until_quiet(s, 1.0f) && s.audio.playing(AudioAdapter::Line::Playback) != nullptr);
  key_down(s, 2, 62);
  (void)run(s, 2);
  CHECK(sounding_voices(s) > 0 && playing(s));             // both lines sound
  key_up(s, 2);
  const std::vector<float> noteTake = s.take.stereo;
  play(s);
  CHECK(s.take.stereo == noteTake);                       // nothing changed: not rendered again
  s.keyboard.velocity = 0.5f;
  play(s);
  CHECK(s.take.velocity == 0.5f);                         // stale: rendered again
  for (int k = 0; k < 400 && playing(s); ++k) {
    (void)run(s, 1);
    keep_up(s);
  }
  CHECK(!playing(s) && s.audio.playing(AudioAdapter::Line::Playback) == nullptr);

  s.transport.mode = Mode::Passage;
  s.transport.passage = "Cq";
  generate(s);
  const std::vector<float> passageTake = s.take.stereo;
  s.transport.passage = "Cq Dq Eq";
  play(s);
  CHECK(s.take.stereo == passageTake);                    // the A/B: played as it is
  s.transport.mode = Mode::Chords;
  s.transport.chords = "C_q";
  play(s);
  CHECK(s.take.mode == Mode::Chords);                     // rendered every time
  s.transport.mode = Mode::Passage;
  s.transport.passage = "Xq";
  generate(s);
  play(s);
  CHECK(s.take.stereo.empty() && s.audio.playing(AudioAdapter::Line::Playback) == nullptr);

  open_file(s, kPlain);
  play(s);
  CHECK(playing(s) && run(s, 20) > 0.0f);
  stop(s);
  CHECK(!playing(s));
}

// The WAV read back: its rate, its frames, its samples to 16 bits; the
// texts round trip; a load's strip rule; the words.
void run_transport_files_tests() {
  std::printf("the transport's files: the WAV read back, the texts, the words\n");
  Session s;
  save_wav(s, scratch("mforce_take.wav"));
  CHECK(s.status == "Nothing to save — generate first");
  open_file(s, kSlowRate);
  generate(s);
  const std::string wav = scratch("mforce_take.wav");
  save_wav(s, wav);
  CHECK(s.status == "Saved: " + wav + " (" + std::to_string(s.take.frames) + " frames)");
  std::vector<float> back;
  int rate = 0;
  CHECK(read_wav_16le_stereo(wav, back, rate) && rate == 22050 && back.size() == s.take.stereo.size());
  float worst = 0.0f;
  for (size_t i = 0; i < back.size() && back.size() == s.take.stereo.size(); ++i)
    worst = std::max(worst, std::fabs(back[i] - s.take.stereo[i]));
  CHECK(worst <= 1.0f / 32767.0f + 1e-6f);
  save_wav(s, scratch("no_such_folder/x.wav"));
  CHECK(s.status.rfind("Failed to save: ", 0) == 0);

  const std::string text = scratch("mforce_passage.psg");
  s.transport.mode = Mode::Passage;
  s.transport.passage = "Cq Eq | Gq";
  save_text(s, text);
  CHECK(s.status == "Saved: " + text);
  s.transport.passage.clear();
  load_text(s, text);
  CHECK(s.transport.passage == "Cq Eq | Gq" && s.status == "Loaded: " + text);
  { std::ofstream(text, std::ios::binary) << "Cq\r\nEq \n \n"; }
  s.transport.mode = Mode::Chords;
  load_text(s, text);
  CHECK(s.transport.chords == "Cq\nEq" && s.transport.passage == "Cq Eq | Gq");
  load_text(s, scratch("no_such_file.chd"));
  CHECK(s.status.rfind("Failed to open: ", 0) == 0);
  s.transport.mode = Mode::Note;
  save_text(s, text);
  CHECK(s.status == "only the Passage and Chords tabs have a text to save");
}

// The seed over the measured set: a file with a score sets the four numbers
// as the rule says, one without leaves them; a new patch's score and length.
void run_seed_tests() {
  std::printf("the seed: the measured set's scores into the transport; a new patch's score\n");
  int seeded = 0, left = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (entry.path().extension() != ".json") continue;
      Session s;
      s.transport.note = "X";
      s.keyboard = Keyboard{3, 0.123f, 7.5f, true};
      open_file(s, entry.path().generic_string());
      const auto first = first_score_note(s.open.patch.score);
      if (!first) {
        ++left;
        CHECK(s.transport.note == "X" && s.keyboard.velocity == 0.123f && s.keyboard.seconds == 7.5f && s.keyboard.octave == 3);
        continue;
      }
      ++seeded;
      char want[32];
      std::snprintf(want, sizeof want, "%g", double(first->note));
      CHECK(s.transport.note == want && s.keyboard.velocity == first->velocity &&
            s.keyboard.seconds == std::clamp(first->duration, 0.05f, 30.0f) &&
            s.keyboard.octave == std::clamp(int(first->note) / 12, 0, 8));
    }
  std::printf("  %d seeded, %d left as they were\n", seeded, left);
  CHECK(seeded > 0 && left > 0);
  Session s;
  new_file(s);
  CHECK(s.open.patch.seconds && *s.open.patch.seconds == 3.0 && s.open.patch.score == default_embedded_score());
}

// Play's status is its own each time: three Plays of a take at another rate
// than the output's say the rate once.
void run_play_status_tests() {
  std::printf("play: the rate sentence said once, however often Play is pressed\n");
  Session s;
  open_file(s, kSlowRate);
  for (int k = 0; k < 3; ++k) play(s);
  const std::string sentence = "the render is at 22050 Hz";
  const size_t first = s.status.find(sentence);
  CHECK(first != std::string::npos && s.status.find(sentence, first + 1) == std::string::npos);
}

// A plain sound running follows the patch, with the program's loop turning
// (keep_up once a frame): a setting edit leaves another sound on the line; a
// drag of a number leaves the same one until the drag ends, then another;
// with nothing running, an edit builds nothing.
void run_plain_follows_tests() {
  std::printf("a running plain sound follows the patch: a setting at once, a number at the drag's end\n");
  Session s;
  open_file(s, kPlain);
  edit_setting(s, "ampEnv", "timeScale", 2.0f);
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == nullptr);
  play(s);
  const Sound* first = s.audio.playing(AudioAdapter::Line::Playback);
  REQUIRE(first != nullptr);
  keep_up(s);
  edit_setting(s, "ampEnv", "timeScale", 1.5f);
  const Sound* second = s.audio.playing(AudioAdapter::Line::Playback);
  CHECK(second != nullptr && second != first && playing(s));
  keep_up(s);
  edit_value(s, "fm", "frequency", 450.0f);
  keep_up(s);
  edit_value(s, "fm", "frequency", 460.0f);
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == second);
  keep_up(s);
  end_gesture(s);
  const Sound* third = s.audio.playing(AudioAdapter::Line::Playback);
  CHECK(third != nullptr && third != second && run(s, 10) > 0.0f);
  stop(s);
  edit_setting(s, "ampEnv", "timeScale", 1.0f);
  CHECK(s.audio.playing(AudioAdapter::Line::Playback) == nullptr);
}

// What a person relies on that the slice's parity check found unproven:
// faults reach the status line, a new patch's score round-trips through its
// file into the Note tab, and the Note tab's pitch and octave compose its
// text.
#include "mforce/testing/check.h"
#include "transport_fixture.h"
#include "mforce/perform/embedded_score.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace transport_fixture;

namespace {

std::string scratch(const char* name) { return (std::filesystem::temp_directory_path() / name).string(); }

// Two sines at frequency 0, summed: each clamps its frequency and reports it.
std::string faulting_patch() {
  const std::string path = scratch("mforce_faulting.json");
  std::ofstream(path) << R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"sum","nodes":[
    {"id":"b","type":"SineSource","params":{"frequency":0.0,"amplitude":0.3}},
    {"id":"a","type":"SineSource","params":{"frequency":0.0,"amplitude":0.3}},
    {"id":"sum","type":"CombinedSource","params":{"source1":{"ref":"b"},"source2":{"ref":"a"},"operation":3}}]}})";
  return path;
}

}  // namespace

// After Generate on a patch whose nodes report a fault, the status is the
// engine's faults line; a fault a live voice reports reaches the status
// through keep_up.
void run_faults_to_status_tests() {
  std::printf("faults: a render's and a live voice's reach the status line\n");
  const std::string words = "faults: a (SineSource): frequency clamped; b (SineSource): frequency clamped";
  Session s;
  open_file(s, faulting_patch());
  REQUIRE(s.live.player() != nullptr);
  generate(s);
  CHECK(s.status == words && !s.take.stereo.empty());
  s.status.clear();
  key_down(s, 1, 60);
  (void)run(s, 4);
  keep_up(s);
  CHECK(s.status == words);
  s.status.clear();
  keep_up(s);
  CHECK(s.status.empty());                                // read and cleared: said once
}

// A new patch saved and read back has three seconds and the default score,
// and opening it seeds the Note tab with 60, 0.8 and two seconds.
void run_new_patch_round_trip_tests() {
  std::printf("a new patch: saved and read back with its score; the Note tab seeded from it\n");
  const std::string path = scratch("mforce_new_patch.json");
  Session made;
  new_file(made);
  save_file_as(made, path);
  Session s;
  open_file(s, path);
  CHECK(s.open.path == path && s.open.patch.seconds && *s.open.patch.seconds == 3.0);
  CHECK(s.open.patch.score == default_embedded_score());
  CHECK(s.transport.note == "60" && s.keyboard.velocity == 0.8f && s.keyboard.seconds == 2.0f && s.keyboard.octave == 5);
}

// set_note_tab composes the text and moves the keyboard's octave, within 0
// to 8, the pitch class wrapping; note_tab_number reads the text as Generate
// does.
void run_note_tab_tests() {
  std::printf("the Note tab: pitch and octave compose the text; the number read back\n");
  Session s;
  set_note_tab(s, 1, 4);
  CHECK(s.transport.note == "C#4" && s.keyboard.octave == 4 && note_tab_number(s) == 49);
  set_note_tab(s, 11, 12);
  CHECK(s.transport.note == "B8" && s.keyboard.octave == 8 && note_tab_number(s) == 107);
  set_note_tab(s, -1, -3);
  CHECK(s.transport.note == "B0" && s.keyboard.octave == 0 && note_tab_number(s) == 11);
  s.transport.note = "60";
  CHECK(note_tab_number(s) == 60);
  s.transport.note = "1e40";
  CHECK(note_tab_number(s) == 60);                        // too large to read: the default
}

// The three sounds the output plays, and the keys: Live on and off, the slur
// back and the forgotten key, stop, and the last note.
#include "mforce/testing/check.h"
#include "transport_fixture.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/running_sound.h"
#include "mforce/ui_control/sounds.h"
#include <cstdio>
#include <memory>
#include <vector>

using namespace transport_fixture;

namespace {

// `frames` of a sound, in pieces of `piece`.
std::vector<float> fill_all(Sound& sound, int frames, int piece) {
  std::vector<float> out(size_t(frames) * 2);
  for (int done = 0; done < frames;) {
    const int n = std::min(piece, frames - done);
    sound.fill(out.data() + 2 * done, n);
    done += n;
  }
  return out;
}

int zero_crossings(const std::vector<float>& stereo) {
  int crossings = 0;
  for (size_t i = 2; i < stereo.size(); i += 2)
    if ((stereo[i - 2] < 0.0f) != (stereo[i] < 0.0f)) ++crossings;
  return crossings;
}

}  // namespace

// A take plays its frames once and is over; at another rate it lasts the
// same seconds and a sine in it keeps its frequency. A plain sound plays as
// the engine's running sound.
void run_sounds_tests() {
  std::printf("the sounds: a take once, a take at another rate, a plain sound\n");
  Take take;
  take.sampleRate = 48000;
  take.frames = 1000;
  for (int i = 0; i < take.frames; ++i) take.stereo.insert(take.stereo.end(), {float(i) / 1000.0f, -float(i) / 1000.0f});
  TakeSound once(take, 48000);
  CHECK(!once.over());
  const auto out = fill_all(once, 1600, 300);
  CHECK(std::equal(take.stereo.begin(), take.stereo.end(), out.begin()) &&
        std::all_of(out.begin() + 2000, out.end(), [](float x) { return x == 0.0f; }) && once.over());

  Take sine;
  sine.sampleRate = 22050;
  sine.frames = 22050;
  for (int i = 0; i < sine.frames; ++i) {
    const float v = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * float(i) / 22050.0f);
    sine.stereo.insert(sine.stereo.end(), {v, v});
  }
  TakeSound faster(sine, 48000);
  CHECK(faster.frames() == 48000);
  const int crossings = zero_crossings(fill_all(faster, 48000, 512));
  CHECK(std::abs(crossings - 880) <= 2);

  const Patch plain = load_patch(kPlain);
  PlainSound sound(plain, 48000);
  RunningSound twin(plain, 48000);
  std::vector<float> want(2 * 4096);
  twin.fill(want.data(), 4096);
  const auto got = fill_all(sound, 4096, 700);
  CHECK(got == want && std::any_of(got.begin(), got.end(), [](float x) { return x != 0.0f; }) && !sound.over());
}

// Live on: three keys held on the piano sound together, and each key-up
// takes its own note. Live off: a key-up does nothing, the note plays its
// length.
void run_live_keys_tests() {
  std::printf("the keys: three held on the piano, each released by its own key; Live off\n");
  Session s;
  open_file(s, kPiano);
  REQUIRE(s.live.player() != nullptr && !s.open.patch.instrument->sustaining);
  s.keyboard.seconds = 20.0f;                            // a struck note this long would outlast every wait below
  key_down(s, 1, 60);
  key_down(s, 2, 64);
  key_down(s, 3, 67);
  (void)run(s, 10);
  CHECK((sounding_notes(s) == std::vector<int>{60, 64, 67}));
  key_up(s, 2);
  CHECK(until_sounding(s, 2, 4.0f) && (sounding_notes(s) == std::vector<int>{60, 67}));
  key_up(s, 1);
  CHECK(until_sounding(s, 1, 4.0f) && (sounding_notes(s) == std::vector<int>{67}));
  key_up(s, 3);
  CHECK(until_quiet(s, 4.0f));

  s.transport.live = false;
  s.keyboard.seconds = 0.5f;
  key_down(s, 4, 62);
  key_up(s, 4);
  (void)run(s, 20);                                      // past any release, short of the length
  CHECK((sounding_notes(s) == std::vector<int>{62}));
  CHECK(until_quiet(s));
}

// On the instrument that sustains: a second key carries the line; letting it
// go while the first is down carries the line back, at the first's own
// velocity, with no release; letting go of the key the line has moved on
// from only forgets it.
void run_live_slur_back_tests() {
  std::printf("the keys: the slur back to a key still down, and the forgotten key\n");
  Session s;
  open_file(s, kSustaining);
  REQUIRE(s.live.player() != nullptr && s.open.patch.instrument->sustaining);
  key_down(s, 1, 60);
  key_down(s, 2, 64);
  (void)run(s, 10);
  CHECK((sounding_notes(s) == std::vector<int>{64}) && sounding_voices(s) == 1);
  key_up(s, 2);
  (void)run(s, 200);                                     // two seconds: a release would have ended
  CHECK((sounding_notes(s) == std::vector<int>{60}) && sounding_voices(s) == 1);
  CHECK(last_note(s).rfind("Last note: 64 ", 0) == 0);  // the slur back is not a note played
  key_up(s, 1);
  CHECK(until_quiet(s));

  key_down(s, 1, 60);
  key_down(s, 2, 64);
  key_up(s, 1);                                          // not the sounding key: forgotten
  (void)run(s, 200);
  CHECK((sounding_notes(s) == std::vector<int>{64}));
  key_up(s, 2);
  CHECK(until_quiet(s));
}

// stop: every voice fades and the keys still down sound no more, their
// key-ups starting nothing; the last note, set by a key and not by stop.
void run_live_stop_and_last_note_tests() {
  std::printf("the keys: stop leaves nothing sounding; the last note\n");
  Session s;
  CHECK(last_note(s).empty());
  open_file(s, kSustaining);
  s.keyboard.velocity = 0.8f;
  key_down(s, 1, 60);
  key_down(s, 2, 64);
  (void)run(s, 4);
  CHECK(last_note(s) == "Last note: 64  E5  329.6Hz  vel 0.80");
  stop(s);
  (void)run(s, 2);
  CHECK(sounding_voices(s) == 0 && !playing(s));
  key_up(s, 2);
  key_up(s, 1);
  (void)run(s, 4);
  CHECK(sounding_voices(s) == 0);
  key_down(s, 3, 60, 0.5f);
  CHECK(last_note(s) == "Last note: 60  C5  261.6Hz  vel 0.50");
}

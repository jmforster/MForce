// MIDI in, each message fed by hand to midi_message (no test lists or opens a
// port): a note held and let go with Live off, a note on with velocity 0, on
// every channel; the wheel and the pressure reaching the player, and the
// player built after them; the messages that do nothing; a MIDI note kept
// when the computer's keys all go up (the line carried back to it on an
// instrument that sustains); Stop; and pump_midi with no port.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/midi.h"
#include "control_fixture.h"
#include "transport_fixture.h"
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;
using transport_fixture::kOboe;
using transport_fixture::kSustaining;
using transport_fixture::run;
using transport_fixture::until_quiet;

namespace {

constexpr int kBuffersPerSecond = 48000 / 512;

void send(Session& s, int status, int data1, int data2 = 0) {
  midi_message(s, MidiMessage{(unsigned char)status, (unsigned char)data1, (unsigned char)data2});
}

// The control patch with the sine's amplitude read from the note's `field`
// (wheel or pressure): silent while the field is 0.
Patch field_patch(const std::string& field) {
  Patch p = control_fixture::control_patch();
  (void)connect(p, "osc", "amplitude", face_field(p, "Note1", field), false);
  return p;
}

}  // namespace

void run_midi_note_tests() {
  Session s;
  open_file(s, kOboe);
  REQUIRE(s.live.player() != nullptr);
  s.transport.live = false;                              // a computer key would strike; a MIDI key holds
  s.keyboard.seconds = 0.2f;
  send(s, 0x90, 60, 100);
  CHECK((held_notes(s) == std::vector<int>{60}));
  CHECK(s.lastNote.note == 60 && s.lastNote.velocity == 100.0f / 127.0f);
  (void)run(s, kBuffersPerSecond);
  CHECK(sounding_voices(s) == 1);                        // held past the note's length
  send(s, 0x80, 60, 64);
  CHECK(held_notes(s).empty());
  CHECK(until_quiet(s));

  send(s, 0x90, 62, 90);
  (void)run(s, kBuffersPerSecond / 2);
  send(s, 0x90, 62, 0);                                  // a note on with velocity 0 lets it go
  CHECK(held_notes(s).empty());
  CHECK(until_quiet(s));

  for (int channel = 0; channel < 16; ++channel) {       // every channel
    send(s, 0x90 | channel, 40 + channel, 80);
    CHECK((held_notes(s) == std::vector<int>{40 + channel}));
    send(s, 0x80 | channel, 40 + channel, 0);
    CHECK(held_notes(s).empty());
  }
  CHECK(until_quiet(s));
}

void run_midi_wheel_pressure_tests() {
  for (const std::string field : {"wheel", "pressure"}) {
    Session s;
    control_fixture::start(s, field_patch(field));
    REQUIRE(s.live.player() != nullptr);
    send(s, 0x90, 60, 100);
    CHECK(run(s, 10) == 0.0f);                           // the field is 0: silent
    if (field == "wheel") send(s, 0xB3, 1, 127);         // controller 1, on channel 4
    else send(s, 0xD5, 127);                             // channel pressure, two bytes, on channel 6
    CHECK(run(s, 10) > 0.0f);
    CHECK((field == "wheel" ? s.live.wheel() : s.live.pressure()) == 1.0f);
    send(s, 0x80, 60);
    CHECK(until_quiet(s));

    sound_follows(s);                                    // a new player: it is given the last value
    send(s, 0x90, 64, 100);
    CHECK(run(s, 10) > 0.0f);
    send(s, 0x80, 64);
    CHECK(until_quiet(s));
  }
  Session before;                                        // given before there is any player
  send(before, 0xB0, 1, 127);
  control_fixture::start(before, field_patch("wheel"));
  send(before, 0x90, 60, 100);
  CHECK(run(before, 10) > 0.0f);
}

void run_midi_nothing_tests() {
  Session s;
  open_file(s, kOboe);
  for (const MidiMessage m : {MidiMessage{0xE0, 0, 127}, MidiMessage{0xB0, 64, 127}, MidiMessage{0xA0, 60, 100},
                              MidiMessage{0xB0, 7, 100}, MidiMessage{0xC0, 5, 0}, MidiMessage{0xF8, 0, 0},
                              MidiMessage{0xF0, 1, 2}, MidiMessage{0x80, 61, 0}}) {
    midi_message(s, m);
    CHECK(held_notes(s).empty());
    CHECK(s.live.wheel() == 0.0f && s.live.pressure() == 0.0f);
    CHECK(s.lastNote.note == -1);
  }
  CHECK(run(s, 10) == 0.0f);
  pump_midi(s);                                          // no port open: nothing
  CHECK(held_notes(s).empty() && midi_port(s).empty());
}

void run_midi_kept_tests() {
  Session s;
  open_file(s, kOboe);
  s.transport.live = true;
  key_down(s, int(kMouseKey), 64);                       // the mouse on the piano
  key_down(s, 600, 67);                                  // a computer key
  send(s, 0x90, 60, 100);
  CHECK((held_notes(s) == std::vector<int>{60, 64, 67}));
  release_all_keys(s);                                   // the window lost the keyboard
  CHECK((held_notes(s) == std::vector<int>{60}));
  (void)run(s, 2 * kBuffersPerSecond);
  CHECK(sounding_voices(s) == 1);                        // the MIDI note still sounds
  send(s, 0x80, 60);
  CHECK(until_quiet(s));

  open_file(s, kSustaining);                             // one legato line
  REQUIRE(s.open.patch.instrument && s.open.patch.instrument->sustaining);
  s.transport.live = true;
  send(s, 0x90, 60, 100);
  key_down(s, 600, 64);                                  // the line moves to the computer key
  (void)run(s, 10);
  release_all_keys(s);                                   // it carries back to the MIDI note
  CHECK((held_notes(s) == std::vector<int>{60}));
  (void)run(s, kBuffersPerSecond);
  CHECK(sounding_notes(s) == std::vector<int>{60});
  send(s, 0x80, 60);
  CHECK(until_quiet(s));
}

// Closing the port, or choosing another, lets every MIDI-held note go: their
// note offs will never come.
void run_midi_close_tests() {
  Session s;
  open_file(s, kOboe);
  send(s, 0x90, 60, 100);
  key_down(s, 600, 64);                                  // a computer key is left alone
  (void)run(s, 10);
  close_midi(s);
  CHECK((held_notes(s) == std::vector<int>{64}));
  key_up(s, 600);
  CHECK(until_quiet(s));
  send(s, 0x90, 62, 100);
  open_midi(s, -1);                                      // another port, even one that does not open
  CHECK(held_notes(s).empty() && !s.status.empty());
  CHECK(until_quiet(s));
}

void run_midi_stop_tests() {
  Session s;
  open_file(s, kOboe);
  send(s, 0x90, 60, 100);
  send(s, 0x90, 64, 100);
  (void)run(s, 10);
  CHECK(sounding_voices(s) == 2);
  stop(s);                                               // Stop lets everything go
  CHECK(until_quiet(s));
  send(s, 0x80, 60);
  send(s, 0x80, 64);
  CHECK(held_notes(s).empty());
  send(s, 0x90, 60, 100);                                // and the key plays again
  (void)run(s, 10);
  CHECK(sounding_voices(s) == 1);
}

#include "mforce/ui_control/midi.h"
#include <array>
#include <string>
#include <vector>
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

namespace {

constexpr unsigned char kNoteOff = 0x80, kNoteOn = 0x90, kController = 0xB0, kChannelPressure = 0xD0;
constexpr unsigned char kWheelController = 1;
constexpr float kMaxValue = 127.0f;
constexpr int kPollChunk = 64;                     // messages read at a time

void note_on(Session& s, int note, int velocity) {
  const float v = float(velocity) / kMaxValue;
  if (s.live.key_down(midi_key(note), note, s.keyboard, v, true)) s.lastNote = {note, v};
}

}  // namespace

// The status byte's upper half is the message, its lower half the channel,
// which is not looked at.
void midi_message(Session& s, MidiMessage m) {
  const unsigned char kind = m.status & 0xF0;
  if (kind == kChannelPressure) s.live.set_pressure(float(m.data1) / kMaxValue);
  else if (kind == kController && m.data1 == kWheelController) s.live.set_wheel(float(m.data2) / kMaxValue);
  else if (kind == kNoteOn && m.data2 > 0) note_on(s, m.data1, m.data2);
  else if (kind == kNoteOff || kind == kNoteOn) s.live.key_up(midi_key(m.data1));
}

void pump_midi(Session& s) {
  std::array<MidiMessage, kPollChunk> got{};
  for (;;) {
    const int n = s.midi.poll(got);
    for (int k = 0; k < n; ++k) midi_message(s, got[std::size_t(k)]);
    if (n < kPollChunk) return;
  }
}

std::vector<std::string> midi_ports(Session& s) { return s.midi.ports(); }

std::string midi_port(const Session& s) { return s.midi.open_port(); }

void open_midi(Session& s, int port) {
  const std::string why = s.midi.open(port);
  s.status = why.empty() ? "MIDI in: " + s.midi.open_port() : why;
}

void close_midi(Session& s) { s.midi.close(); }

void open_first_midi(Session& s) {
  if (s.midi.ports().empty()) return;
  if (s.midi.open(0).empty()) s.status = "MIDI in: " + s.midi.open_port();
}

}  // namespace mforce::ui

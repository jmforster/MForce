// MidiIn never asked for a port: it names none, a poll reads nothing, and a
// close does nothing. No test lists or opens a port.
#include "mforce/testing/check.h"
#include "mforce/ui_audio/midi_in.h"
#include <array>

using namespace mforce::ui;

void run_midi_in_unasked_tests() {
  MidiIn midi;
  CHECK(midi.open_port().empty());
  std::array<MidiMessage, 8> into{};
  CHECK(midi.poll(into) == 0);
  midi.close();
  CHECK(midi.open_port().empty());
  CHECK(midi.poll(into) == 0);
}

#pragma once
#include <string>
#include <vector>
#include "mforce/ui_audio/midi_in.h"

namespace mforce::ui {

struct Session;

// MIDI in: what a message does, on every channel. A note on with a velocity
// holds its note until its note off, whether or not Live is checked (a key
// struck on a MIDI keyboard is always meant to play), at the velocity / 127
// and the keyboard's note length, and is the last note; a note off, or a note
// on with velocity 0, lets it go; controller 1 is the wheel and channel
// pressure the pressure, each value / 127, given to the player and to every
// player built after it. Everything else does nothing: pitch bend, the
// sustain pedal, poly aftertouch, the other controllers, system messages.
// A MIDI note is held under its own key number (midi_key): the computer's
// keys never meet it, and the window losing the keyboard does not let it go.
void midi_message(Session& s, MidiMessage m);
// Once a frame: every message the port has queued since the last frame, fed
// to midi_message in the order they came. Nothing when no port is open.
void pump_midi(Session& s);

// The ports there are, asked afresh: a keyboard plugged in later appears.
std::vector<std::string> midi_ports(Session& s);
std::string midi_port(const Session& s);             // the port open; empty for none
// Opens the port (the one open before it closed first): status "MIDI in:
// <name>", or the library's words for why not. Every note a MIDI key holds
// is let go first, as close_midi does.
void open_midi(Session& s, int port);
void close_midi(Session& s);                         // every MIDI-held note let go, then no port; nothing said
// At start: the first port when there is one, as open_midi; nothing said when
// there is none or it does not open.
void open_first_midi(Session& s);

}  // namespace mforce::ui

#pragma once
#include <string>

namespace mforce::ui {

struct Session;

// The keyboard's side: notes from keys as session.keyboard says, its octave
// and a struck note's length, and what the output is doing.
void key_down(Session& s, int key, int noteNumber);  // plays as session.keyboard says
void key_up  (Session& s, int key);
void change_octave(Session& s, int by);              // kept within 0 to 8
void change_note_length(Session& s, float factor);   // kept within 0.05 to 30 seconds
float       output_peak(const Session& s);
std::string sound_trouble(const Session& s);         // the adapter's error, or empty

}  // namespace mforce::ui

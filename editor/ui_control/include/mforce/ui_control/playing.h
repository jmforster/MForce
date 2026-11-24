#pragma once
#include <string>
#include <vector>

namespace mforce::ui {

struct Session;

// After a change the build reads (a structural edit, an open, a new patch,
// an undo or a redo): a new player for the patch, or, when it is a plain
// sound, has nothing wired to its Output, or the engine will not build it,
// silence (the keys let go, the adapter handed nothing) with the reason in
// status. What sounds is the patch on screen, or nothing.
void sound_follows(Session& s);

// The keyboard's side: notes from keys as session.keyboard says, its octave
// and a struck note's length, and what the output is doing.
void key_down(Session& s, int key, int noteNumber);  // a computer key: at session.keyboard's velocity
void key_down(Session& s, int key, int noteNumber, float velocity);   // a click on the piano: where it fell
void key_up  (Session& s, int key);
void release_all_keys(Session& s);                   // every key down goes up: the window lost the keyboard
std::vector<int> held_notes(const Session& s);       // the note numbers of the keys down, lowest first
// Once a frame: a value the player's queue could not take is sent, and a
// player the output has finished with is destroyed.
void keep_up(Session& s);
void toggle_computer_keys(Session& s);               // the PC Keyboard button; a key already down still goes up
int  sounding_voices(Session& s);                    // the voices the player has sounding; 0 with none
void change_octave(Session& s, int by);              // kept within 0 to 8
void change_note_length(Session& s, float factor);   // kept within 0.05 to 30 seconds
float       output_peak(const Session& s);
std::string sound_trouble(const Session& s);         // the adapter's error, or empty

}  // namespace mforce::ui

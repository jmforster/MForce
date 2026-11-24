#pragma once
#include <string>
#include "mforce/ui_control/transport.h"

namespace mforce::ui {

struct Session;

// The transport: what Generate renders, what Play plays, and their files.

// A field of the transport changed. Not an undo step.
void set_transport(Session& s, const Transport& t);
// The Note tab's note as a number, as Generate reads its text; 60 for a
// number too large to read.
int note_tab_number(const Session& s);
// The Note tab's pitch (0 for C to 11 for B) and octave (kept within 0 to
// 8): the note's text composed ("C#4"), and the keyboard's octave moved to
// it, as the old transport's were.
void set_note_tab(Session& s, int pitchClass, int octave);

// Renders the patch in the window, saved or not, at its own rate, as the tab
// says, into session.take, a playing take stopped first:
//   Note: one note of the Note tab's text at the keyboard's velocity and
//     length, every signal node captured by id; the last note is set.
//   Passage: the passage at the keyboard's octave and velocity and the tab's
//     bpm and onsets, every signal node captured.
//   Chords: through the chord performer, normalised, nothing captured.
//   Drums: the map's patches read from disk, one per drum, playing the
//     pattern at the open patch's rate.
// A plain sound renders its own length on any tab. What went wrong (a text
// the parser refuses, a missing drum patch, the engine's refusal) goes to
// status and stays; the take is then empty. What the render's nodes
// reported goes to status after the success line.
void generate(Session& s);

// Generate as the window asks for it: the button reads "Generating..." on
// the frame after the click, and the render runs once that frame has been
// shown. request_generate asks; run_requested, called after each frame is
// presented, counts the frames down and renders on the second.
void request_generate(Session& s);
bool generate_requested(const Session& s);
void run_requested(Session& s);

// Everything that sounds stops, keys too; then the take plays on the
// playback line: a Note take rendered again first when it is stale (the
// note text, the velocity or the length changed, or it is not a Note take);
// a Passage take as it is (rendered when there is none); Chords and Drums
// rendered every time. A plain sound runs until Stop. When the take's rate is
// not the output's, status says so.
void play(Session& s);
// The playback line silenced, and every sounding voice faded out.
void stop(Session& s);
// The playback line has a sound that is not over, or a voice is sounding.
bool playing(Session& s);

// The take as a 16-bit stereo WAV at its own rate; status says what was
// written or why not.
void save_wav(Session& s, const std::string& path);
// The tab's text (the passage on the Passage tab, the chords on the Chords
// tab) to a file as it is, and back: a load drops every '\r' and the
// newlines and spaces at the end.
void save_text(Session& s, const std::string& path);
void load_text(Session& s, const std::string& path);

// "Last note: 60  C5  261.6Hz  vel 0.80": the note's number, its name and
// octave (number / 12), its frequency, its velocity. Empty before any note.
std::string last_note(const Session& s);

// On opening a file whose patch has an embedded score: the Note tab's note
// (the score's first note as "%g" text), the keyboard's velocity, its note
// length (kept within 0.05 to 30 seconds) and its octave (note / 12, kept
// within 0 to 8). A patch with no score leaves them as they are.
void seed_from_score(Session& s);

}  // namespace mforce::ui

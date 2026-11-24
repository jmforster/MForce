#pragma once
#include <string>

namespace mforce::ui {

struct Session;

// Opens the file: the patch, its PerformNodes no face names gathered into
// faces and a position for everything the canvas draws that has none
// (neither is an undo step or an unsaved change), a fresh history, the
// selection, the offered pins, the drag and any waiting question cleared,
// the sound following it (sound_follows), the path first among the recent
// files. A file that cannot be read leaves the session as it was and says
// why in status; one that is not there any more also leaves the recent
// files. status says why there is no sound, or that the file's rate differs
// from the output's.
void open_file   (Session& s, const std::string& path);
// Starts the patch File > New gives (new_patch, with two seeds from
// session.seeds) at once, with no file, as open_file starts one.
void new_file    (Session& s);
// Saves to the patch's own file, which then becomes the newest of the recent
// files; a drag under way ends there. A patch with no file, or a save that
// fails, says why in status and stays unsaved.
void save_file   (Session& s);
void save_file_as(Session& s, const std::string& path);
// "MForce - oboe1.json *": the file's name, "untitled" for a patch never
// saved, a star when unsaved; "MForce" before anything is opened.
std::string title(const Session& s);

}  // namespace mforce::ui

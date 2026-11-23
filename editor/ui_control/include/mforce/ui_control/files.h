#pragma once
#include <string>

namespace mforce::ui {

struct Session;

// Opens the file: the patch, a position for every node that has none (not
// an unsaved change), the selection and any waiting question cleared, a
// player for it, the path first among the recent files. A file that cannot
// be read leaves the session as it was and says why in status. A plain
// sound opens with no player, and so does an instrument the engine will not
// build: either way the player of the patch before it is silenced, so no
// other patch's sound plays under it. status says why there is no sound, or
// that the file's rate differs from the output's.
void open_file   (Session& s, const std::string& path);
// Saves to the patch's own file. A patch with no file, or a save that fails,
// says why in status and stays unsaved.
void save_file   (Session& s);
void save_file_as(Session& s, const std::string& path);
// "MForce - oboe1.json *": the file's name, a star when unsaved; "MForce"
// before anything is opened.
std::string title(const Session& s);

}  // namespace mforce::ui

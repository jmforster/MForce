#pragma once
#include <string>

namespace mforce::ui {

// The kinds of file the editor asks for, each with its own filter, default
// name and remembered folder.
enum class FileKind { Patch, Wav, Passage, Chords };

// What the views need from the platform: a file to open, a file to save to.
// ui_app supplies the platform's own dialogs; a test supplies a stand-in.
// (A class of pure virtual functions is C#'s interface.)
class FileDialogs {
public:
  virtual ~FileDialogs() = default;
  // A path, or empty when the user cancelled.
  virtual std::string pick_open(FileKind kind) = 0;
  // `suggested` is the name the dialog offers; empty: the kind's default.
  virtual std::string pick_save(FileKind kind, const std::string& suggested) = 0;
};

}  // namespace mforce::ui

#pragma once
#include <string>

namespace mforce::ui {

// What the menu needs from the platform: a file to open, a file to save to.
// ui_app supplies the platform's own dialogs; a test supplies a stand-in.
// (A class of pure virtual functions is C#'s interface.)
class FileDialogs {
public:
  virtual ~FileDialogs() = default;
  virtual std::string pick_open() = 0;                             // a path, or empty when the user cancelled
  virtual std::string pick_save(const std::string& suggested) = 0;
};

}  // namespace mforce::ui

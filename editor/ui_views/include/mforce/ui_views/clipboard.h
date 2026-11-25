#pragma once
#include <string>

namespace mforce::ui {

// What the views need of the platform's clipboard: its text, read and set.
// ui_app supplies the system's; a test supplies one that keeps a string, so
// no test ever writes the owner's clipboard. (A class of pure virtual
// functions is C#'s interface.)
class Clipboard {
public:
  virtual ~Clipboard() = default;
  virtual std::string text() = 0;                  // empty when it holds no text
  virtual void set_text(const std::string& text) = 0;
};

}  // namespace mforce::ui

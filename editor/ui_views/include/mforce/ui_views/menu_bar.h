#pragma once
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/file_dialogs.h"

namespace mforce::ui {

// The window's menu bar: File with Open (Ctrl+O), Open Recent, Save (Ctrl+S),
// Save As (Ctrl+Shift+S) and Quit; View with Arrange nodes. The shortcuts
// work wherever the focus is.
class MenuBar {
public:
  explicit MenuBar(FileDialogs& dialogs);
  void draw(Session& s);

private:
  void open(Session& s);
  void save(Session& s);
  void save_as(Session& s);
  FileDialogs& dialogs_;
};

}  // namespace mforce::ui

#pragma once
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/file_dialogs.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui {

// The window's menu bar: File with New (Ctrl+N), Open (Ctrl+O), Open Recent
// (each file by its name, the full path as its tooltip, then Clear), Save
// (Ctrl+S), Save As (Ctrl+Shift+S) and Quit; Edit with Undo (Ctrl+Z) and Redo
// (Ctrl+Y), greyed when there is nothing to undo or redo, and Restart audio
// (Ctrl+Shift+A); Transport with
// Play, or Stop while anything plays (Space); View with Arrange nodes. The
// shortcuts work wherever the focus is, but a text field that has the
// keyboard keeps Ctrl+Z, Ctrl+Y, Ctrl+Shift+A and Space for itself.
class MenuBar {
public:
  explicit MenuBar(FileDialogs& dialogs);
  void draw(Session& s);
  // Where each menu and item was drawn last frame, as "menu:<words>".
  const Places& places() const { return places_; }

private:
  void shortcuts(Session& s);
  void recent(Session& s);
  void open(Session& s);
  void save(Session& s);
  void save_as(Session& s);
  void play_or_stop(Session& s);
  FileDialogs& dialogs_;
  Places places_;
};

}  // namespace mforce::ui

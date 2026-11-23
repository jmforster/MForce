#pragma once
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/file_dialogs.h"

namespace mforce::ui {

// The question asked while a close or an open waits on unsaved changes:
// Save, Discard or Cancel. Save on a patch with no file asks for one first.
class UnsavedPrompt {
public:
  explicit UnsavedPrompt(FileDialogs& dialogs);
  void draw(Session& s);

private:
  void save(Session& s);
  FileDialogs& dialogs_;
};

}  // namespace mforce::ui

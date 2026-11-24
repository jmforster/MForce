#pragma once
#include <string>
#include "mforce/ui_model/folder_memory.h"
#include "mforce/ui_views/file_dialogs.h"

namespace mforce::ui {

// The platform's open and save dialogs: on Windows the Win32 ones (the ANSI
// calls, since the engine takes narrow paths), filtered to *.json, starting
// in the folder a patch was last picked from (the memory it is handed), else
// in `patches` under the working folder when it is there; a file picked is
// remembered. Elsewhere each answers as if cancelled.
class Win32FileDialogs final : public FileDialogs {
public:
  explicit Win32FileDialogs(FolderMemory& folders) : folders_(folders) {}
  std::string pick_open() override;
  std::string pick_save(const std::string& suggested) override;

private:
  FolderMemory& folders_;
};

}  // namespace mforce::ui

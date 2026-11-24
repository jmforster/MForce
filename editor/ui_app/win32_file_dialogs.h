#pragma once
#include <string>
#include "mforce/ui_model/folder_memory.h"
#include "mforce/ui_views/file_dialogs.h"

namespace mforce::ui {

// The platform's open and save dialogs: on Windows the Win32 ones (the ANSI
// calls, since the engine takes narrow paths). Each kind of file has its
// filter, its default name ("output.wav", "passage.psg", "chords.chd") and
// its own remembered folder (the memory it is handed); a first dialog for a
// patch starts in `patches` and one for a WAV in `renders` under the working
// folder when they are there. A file picked is remembered. Elsewhere each
// answers as if cancelled.
class Win32FileDialogs final : public FileDialogs {
public:
  explicit Win32FileDialogs(FolderMemory& folders) : folders_(folders) {}
  std::string pick_open(FileKind kind) override;
  std::string pick_save(FileKind kind, const std::string& suggested) override;

private:
  FolderMemory& folders_;
};

}  // namespace mforce::ui

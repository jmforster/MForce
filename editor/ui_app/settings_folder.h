#pragma once
#include <string>

namespace mforce::ui {

// Where the editor keeps its own files: %APPDATA%\MForce\, made when
// missing. Empty paths: keep nothing (the program was run for a proof, or
// the folder cannot be found or made).
struct SettingsFiles {
  std::string recentFiles;   // the recent files, one path per line
  std::string folders;       // the dialogs' folders, one kind=folder per line
  std::string imguiIni;      // ImGui's window settings
};

SettingsFiles settings_files();

}  // namespace mforce::ui

#include "win32_file_dialogs.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
// Only the parts of the Windows headers this file needs, and no min/max macros.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

namespace mforce::ui {

#ifdef _WIN32

namespace {

// The filter list: pairs of a description and a pattern, each ended by a
// zero, the list by a second zero.
constexpr char kFilter[] = "Patches (*.json)\0*.json\0All files (*.*)\0*.*\0";

std::string default_folder() {
  std::error_code failed;
  const auto folder = std::filesystem::absolute("patches", failed);
  return !failed && std::filesystem::is_directory(folder) ? folder.string() : std::string();
}

// The dialog's common fields: the buffer it writes the chosen path into, the
// filter, the folder it starts in. OFN_NOCHANGEDIR keeps the working folder.
OPENFILENAMEA dialog_fields(std::array<char, MAX_PATH>& path, const std::string& folder) {
  OPENFILENAMEA d{};
  d.lStructSize = sizeof d;
  d.lpstrFilter = kFilter;
  d.lpstrFile = path.data();
  d.nMaxFile = DWORD(path.size());
  d.lpstrInitialDir = folder.empty() ? nullptr : folder.c_str();
  d.lpstrDefExt = "json";
  d.Flags = OFN_NOCHANGEDIR;
  return d;
}

// The folder a patch was last picked from, else the default.
std::string first_folder(const FolderMemory& folders) {
  const std::string remembered = start_folder(folders, kPatchFiles);
  return remembered.empty() ? default_folder() : remembered;
}

}  // namespace

std::string Win32FileDialogs::pick_open() {
  std::array<char, MAX_PATH> path{};
  const std::string folder = first_folder(folders_);
  OPENFILENAMEA d = dialog_fields(path, folder);
  d.Flags |= OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
  if (!GetOpenFileNameA(&d)) return {};
  remember_folder(folders_, kPatchFiles, path.data());
  return path.data();
}

std::string Win32FileDialogs::pick_save(const std::string& suggested) {
  std::array<char, MAX_PATH> path{};
  std::memcpy(path.data(), suggested.c_str(), std::min(suggested.size(), path.size() - 1));
  const std::string folder = first_folder(folders_);
  OPENFILENAMEA d = dialog_fields(path, folder);
  d.Flags |= OFN_OVERWRITEPROMPT;
  if (!GetSaveFileNameA(&d)) return {};
  remember_folder(folders_, kPatchFiles, path.data());
  return path.data();
}

#else

std::string Win32FileDialogs::pick_open() { return {}; }
std::string Win32FileDialogs::pick_save(const std::string&) { return {}; }

#endif

}  // namespace mforce::ui

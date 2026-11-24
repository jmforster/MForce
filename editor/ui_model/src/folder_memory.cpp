#include "mforce/ui_model/folder_memory.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <system_error>

namespace mforce::ui {

namespace {

bool is_folder(const std::string& path) {
  std::error_code failed;
  return !path.empty() && std::filesystem::is_directory(path, failed) && !failed;
}

}  // namespace

void remember_folder(FolderMemory& memory, const std::string& kind, const std::string& picked) {
  const std::string folder = std::filesystem::path(picked).parent_path().string();
  if (is_folder(folder)) memory.folders[kind] = folder;
}

std::string start_folder(const FolderMemory& memory, const std::string& kind) {
  const auto it = memory.folders.find(kind);
  return it != memory.folders.end() && is_folder(it->second) ? it->second : std::string();
}

FolderMemory read_folder_memory(const std::string& file) {
  FolderMemory memory;
  std::ifstream in(file);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();   // a line ended the Windows way
    const auto equals = line.find('=');
    if (equals == std::string::npos || equals == 0) continue;
    memory.folders[line.substr(0, equals)] = line.substr(equals + 1);
  }
  return memory;
}

void write_folder_memory(const FolderMemory& memory, const std::string& file) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  for (const auto& [kind, folder] : memory.folders) out << kind << '=' << folder << '\n';
  out.close();
  if (!out) throw std::runtime_error(file + ": cannot write the dialogs' folders");
}

}  // namespace mforce::ui

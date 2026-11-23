#include "mforce/ui_model/recent_files.h"
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <stdexcept>

namespace mforce::ui {

namespace {

constexpr std::size_t kMostFiles = 10;

}  // namespace

void note_opened(RecentFiles& recent, const std::string& path) {
  auto& paths = recent.paths;
  paths.erase(std::remove(paths.begin(), paths.end(), path), paths.end());
  paths.insert(paths.begin(), path);
  if (paths.size() > kMostFiles) paths.resize(kMostFiles);
}

RecentFiles read_recent_files(const std::string& file) {
  RecentFiles recent;
  std::ifstream in(file);
  std::string line;
  while (recent.paths.size() < kMostFiles && std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();   // a line ended the Windows way
    if (line.empty()) continue;
    if (std::find(recent.paths.begin(), recent.paths.end(), line) == recent.paths.end())
      recent.paths.push_back(line);
  }
  return recent;
}

void write_recent_files(const RecentFiles& recent, const std::string& file) {
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  for (const auto& path : recent.paths) out << path << '\n';
  out.close();
  if (!out) throw std::runtime_error(file + ": cannot write the list of recent files");
}

}  // namespace mforce::ui

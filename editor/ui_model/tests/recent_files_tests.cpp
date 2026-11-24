// RecentFiles: newest first, no repeat, at most twelve, a path forgotten; the plain text file,
// written and read back, and a missing file read as none.
#include "mforce/testing/check.h"
#include "mforce/ui_model/recent_files.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mforce::ui;

void run_recent_files_tests() {
  RecentFiles r;
  note_opened(r, "a.json");
  note_opened(r, "b.json");
  note_opened(r, "a.json");
  CHECK((r.paths == std::vector<std::string>{"a.json", "b.json"}));
  for (int k = 0; k < 14; ++k) note_opened(r, "f" + std::to_string(k) + ".json");
  CHECK(r.paths.size() == 12);
  CHECK(r.paths.front() == "f13.json" && r.paths.back() == "f2.json");
  forget(r, "f5.json");
  CHECK(r.paths.size() == 11 && std::find(r.paths.begin(), r.paths.end(), "f5.json") == r.paths.end());
  forget(r, "never.json");
  CHECK(r.paths.size() == 11);

  const std::filesystem::path file = std::filesystem::temp_directory_path() / "mforce_ui_model_recent_files.txt";
  write_recent_files(r, file.string());
  CHECK(read_recent_files(file.string()).paths == r.paths);
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "x.json\r\n\r\ny.json\nx.json\n";               // Windows line ends, a blank line, a repeat
  }
  CHECK((read_recent_files(file.string()).paths == std::vector<std::string>{"x.json", "y.json"}));
  write_recent_files(RecentFiles{}, file.string());         // an emptied list is written as an empty file
  CHECK(std::filesystem::file_size(file) == 0 && read_recent_files(file.string()).paths.empty());
  std::error_code ignored;
  std::filesystem::remove(file, ignored);
  CHECK(read_recent_files(file.string()).paths.empty());   // missing: none
}

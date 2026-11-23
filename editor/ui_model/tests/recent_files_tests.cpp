// RecentFiles: newest first, no repeat, at most ten; the plain text file,
// written and read back, and a missing file read as none.
#include "mforce/testing/check.h"
#include "mforce/ui_model/recent_files.h"
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
  for (int k = 0; k < 12; ++k) note_opened(r, "f" + std::to_string(k) + ".json");
  CHECK(r.paths.size() == 10);
  CHECK(r.paths.front() == "f11.json" && r.paths.back() == "f2.json");

  const std::filesystem::path file = std::filesystem::temp_directory_path() / "mforce_ui_model_recent_files.txt";
  write_recent_files(r, file.string());
  CHECK(read_recent_files(file.string()).paths == r.paths);
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "x.json\r\n\r\ny.json\nx.json\n";               // Windows line ends, a blank line, a repeat
  }
  CHECK((read_recent_files(file.string()).paths == std::vector<std::string>{"x.json", "y.json"}));
  std::error_code ignored;
  std::filesystem::remove(file, ignored);
  CHECK(read_recent_files(file.string()).paths.empty());   // missing: none
}

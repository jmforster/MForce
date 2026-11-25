#include "build_stamp_reader.h"
#include <chrono>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>
#include "build_sources.h"
#ifdef _WIN32
// Only the parts of the Windows header this file needs, and no min/max macros.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace mforce::ui {

namespace {

namespace fs = std::filesystem;

// The running program's own file; empty when it cannot be found.
fs::path program_file() {
#ifdef _WIN32
  wchar_t path[MAX_PATH];
  const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  if (n == 0 || n == MAX_PATH) return {};          // MAX_PATH: the name was cut short
  return fs::path(path, path + n);
#else
  return {};
#endif
}

// A file's time on the computer's clock, as stamp_time writes it; on the
// world clock (UTC) when the system names no time zone.
std::string local_stamp(fs::file_time_type t) {
  using namespace std::chrono;
  const sys_seconds world = floor<seconds>(clock_cast<system_clock>(t));
  try {
    return stamp_time(current_zone()->to_local(world));
  } catch (const std::exception&) {
    return stamp_time(local_seconds(world.time_since_epoch()));
  }
}

}  // namespace

BuildStamp read_build_stamp() {
  BuildStamp stamp;
  const fs::path program = program_file();
  if (program.empty()) return stamp;
  std::error_code ec;
  const fs::file_time_type built = fs::last_write_time(program, ec);
  if (ec) return stamp;
  stamp.built = local_stamp(built);
  stamp.commit = read_commit(fs::path(build_sources::kGit));
  const std::vector<fs::path> folders(std::begin(build_sources::kFolders), std::end(build_sources::kFolders));
  const NewestFile newest = newest_source(folders);
  if (!newest.path.empty() && newest.time > built)
    stamp.newer = newest.path.filename().string() + " (" + local_stamp(newest.time) + ")";
  return stamp;
}

}  // namespace mforce::ui

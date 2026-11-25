#include "mforce/ui_model/build_stamp.h"
#include <cstdio>
#include <system_error>

namespace mforce::ui {

namespace {

bool counted(const std::filesystem::path& file) {
  const std::string ext = file.extension().string();
  return ext == ".h" || ext == ".hpp" || ext == ".cpp";
}

}  // namespace

bool stale(const BuildStamp& stamp) { return !stamp.newer.empty(); }

std::string title_suffix(const BuildStamp& stamp) {
  if (stamp.built.empty()) return {};
  std::string s = "  [build " + stamp.built;
  if (!stamp.commit.empty()) s += " @" + stamp.commit;
  s += "]";
  if (stale(stamp)) s += "  *** STALE — REBUILD ***";
  return s;
}

std::array<std::string, 2> banner_lines(const BuildStamp& stamp) {
  return {"This mforce_editor was built " + stamp.built + ", but " + stamp.newer + " is newer.",
          "Engine features added since then WILL APPEAR MISSING. Rebuild mforce_editor."};
}

std::filesystem::path build_mark(const std::filesystem::path& program) {
  return program.parent_path() / (program.stem().string() + ".built");
}

std::optional<std::filesystem::file_time_type> built_time(const std::filesystem::path& program) {
  std::error_code ec;
  const auto mark = std::filesystem::last_write_time(build_mark(program), ec);
  if (!ec) return mark;
  ec.clear();
  const auto own = std::filesystem::last_write_time(program, ec);
  if (!ec) return own;
  return std::nullopt;
}

std::string stamp_time(std::chrono::local_seconds t) {
  using namespace std::chrono;
  const local_days day = floor<days>(t);
  const year_month_day date{day};
  const hh_mm_ss<minutes> clock{floor<minutes>(t - day)};
  char text[32];
  std::snprintf(text, sizeof text, "%02u-%02u %02d:%02d", unsigned(date.month()), unsigned(date.day()),
                int(clock.hours().count()), int(clock.minutes().count()));
  return text;
}

// A folder that cannot be read, or a file that goes while it is walked, is
// passed over: the error code is the walk's, never thrown.
NewestFile newest_source(const std::vector<std::filesystem::path>& folders) {
  NewestFile newest;
  for (const std::filesystem::path& folder : folders) {
    std::error_code ec;
    std::filesystem::recursive_directory_iterator walk(folder, ec), end;
    for (; !ec && walk != end; walk.increment(ec)) {
      std::error_code fileError;
      if (!walk->is_regular_file(fileError) || !counted(walk->path())) continue;
      const auto time = walk->last_write_time(fileError);
      if (fileError) continue;
      if (newest.path.empty() || time > newest.time) newest = NewestFile{walk->path(), time};
    }
  }
  return newest;
}

}  // namespace mforce::ui

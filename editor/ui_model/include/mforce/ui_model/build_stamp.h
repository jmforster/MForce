#pragma once
#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mforce::ui {

// When the running program was built, from which commit, and the source file
// that is newer than it, if any: a program older than a source of its own
// shows what the source no longer says. All empty in a session no program
// filled (a test's).
struct BuildStamp {
  std::string built;    // the last build's time (built_time), "11-24 14:41" (stamp_time); empty: not known
  std::string commit;   // the checkout's commit, its first seven characters; empty: none read
  std::string newer;    // the newest source file when it is newer than the last build, "edit.h (11-24 15:02)"; else empty
};

bool stale(const BuildStamp& stamp);                // a source file is newer than the program
// "  [build 11-24 14:41 @b441c05]" (no " @..." when no commit was read), then
// "  *** STALE — REBUILD ***" when stale; empty when the time is not known.
std::string title_suffix(const BuildStamp& stamp);
// The stale banner's two lines: "This mforce_editor was built <built>, but
// <newer> is newer." and "Engine features added since then WILL APPEAR
// MISSING. Rebuild mforce_editor."
std::array<std::string, 2> banner_lines(const BuildStamp& stamp);

// The mark the build leaves beside the program each time it builds it or
// finds it built: the program's name with ".built" ("mforce_editor.built").
std::filesystem::path build_mark(const std::filesystem::path& program);
// The time a stamp compares the sources against and shows: the build mark's
// when there is one, else the program file's own; none when neither is
// there. A source edited that no build needs again (a header nothing
// compiled includes) is then older than the next build, not newer than the
// program for ever.
std::optional<std::filesystem::file_time_type> built_time(const std::filesystem::path& program);

// "MM-DD HH:MM" of a local time, the year left out.
std::string stamp_time(std::chrono::local_seconds t);

// The newest of the .h, .hpp and .cpp files under the folders, each searched
// through every folder below it; a folder that is not there is passed over.
struct NewestFile {
  std::filesystem::path path;                     // empty when no file was found
  std::filesystem::file_time_type time{};
};
NewestFile newest_source(const std::vector<std::filesystem::path>& folders);

// The commit a checkout has out, from its `.git`: a folder, or a file
// "gitdir: <folder>" (a worktree, whose refs live in the folder its
// "commondir" names). HEAD names the commit, or "ref: <ref>", read from the
// ref's own file or else from "packed-refs". Its first seven characters;
// empty when nothing reads as a commit.
std::string read_commit(const std::filesystem::path& git);

}  // namespace mforce::ui

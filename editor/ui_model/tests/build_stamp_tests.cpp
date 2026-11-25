// BuildStamp: the title's suffix and the banner's words, not stale, stale,
// with no commit and with no time; the time's form; the newest source file
// of folders the test makes; and the commit read from a `.git` the test makes
// in each of its forms (a loose ref, packed-refs, a detached HEAD, a
// worktree's "gitdir:"), and from none.
#include "mforce/testing/check.h"
#include "mforce/ui_model/build_stamp.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace mforce::ui;
namespace fs = std::filesystem;

namespace {

const std::string kHash = "b441c05d2f0e9a1b3c4d5e6f708192a3b4c5d6e7";
const std::string kOther = "0123456789abcdef0123456789abcdef01234567";

void write(const fs::path& file, const std::string& text) {
  fs::create_directories(file.parent_path());
  std::ofstream out(file, std::ios::binary | std::ios::trunc);
  out << text;
}

// A fresh, empty folder of the test's own under the system's temporary folder.
fs::path fresh(const std::string& name) {
  const fs::path root = fs::temp_directory_path() / ("mforce_ui_model_stamp_" + name);
  std::error_code ignored;
  fs::remove_all(root, ignored);
  fs::create_directories(root);
  return root;
}

}  // namespace

void run_build_stamp_words_tests() {
  const BuildStamp current{"11-24 14:41", "b441c05", ""};
  CHECK(!stale(current));
  CHECK(title_suffix(current) == "  [build 11-24 14:41 @b441c05]");
  const BuildStamp old{"11-24 14:41", "b441c05", "edit.h (11-24 15:02)"};
  CHECK(stale(old));
  CHECK(title_suffix(old) == "  [build 11-24 14:41 @b441c05]  *** STALE — REBUILD ***");
  const auto lines = banner_lines(old);
  CHECK(lines[0] == "This mforce_editor was built 11-24 14:41, but edit.h (11-24 15:02) is newer.");
  CHECK(lines[1] == "Engine features added since then WILL APPEAR MISSING. Rebuild mforce_editor.");
  CHECK(title_suffix(BuildStamp{"11-24 14:41", "", ""}) == "  [build 11-24 14:41]");   // no commit read
  CHECK(title_suffix(BuildStamp{}).empty());                                         // a test's session
  CHECK(!stale(BuildStamp{}));

  using namespace std::chrono;
  const local_seconds t = local_days{year{2026} / 3 / 7} + hours{9} + minutes{5} + seconds{59};
  CHECK(stamp_time(t) == "03-07 09:05");
  CHECK(stamp_time(local_days{year{2026} / 12 / 31} + hours{23} + minutes{59}) == "12-31 23:59");
}

void run_newest_source_tests() {
  const fs::path root = fresh("newest");
  write(root / "a" / "include" / "x.h", "x");
  write(root / "a" / "src" / "deep" / "y.cpp", "y");
  write(root / "a" / "src" / "z.hpp", "z");
  write(root / "a" / "src" / "notes.txt", "not counted");
  write(root / "b" / "w.cpp", "w");
  const auto base = fs::last_write_time(root / "a" / "include" / "x.h");
  fs::last_write_time(root / "a" / "src" / "deep" / "y.cpp", base + std::chrono::hours(2));
  fs::last_write_time(root / "a" / "src" / "z.hpp", base + std::chrono::hours(1));
  fs::last_write_time(root / "a" / "src" / "notes.txt", base + std::chrono::hours(9));
  fs::last_write_time(root / "b" / "w.cpp", base + std::chrono::hours(3));

  NewestFile n = newest_source({root / "a" / "include", root / "a" / "src", root / "missing"});
  CHECK(n.path.filename() == "y.cpp");                     // through a folder below; the .txt not counted
  CHECK(n.time == base + std::chrono::hours(2));
  n = newest_source({root / "a" / "include", root / "b"});
  CHECK(n.path.filename() == "w.cpp");
  CHECK(newest_source({root / "missing"}).path.empty());
  CHECK(newest_source({}).path.empty());
  std::error_code ignored;
  fs::remove_all(root, ignored);
}

void run_read_commit_tests() {
  const fs::path root = fresh("git");
  // A loose ref.
  write(root / "loose" / ".git" / "HEAD", "ref: refs/heads/main\n");
  write(root / "loose" / ".git" / "refs" / "heads" / "main", kHash + "\n");
  CHECK(read_commit(root / "loose" / ".git") == "b441c05");
  // The ref packed: only "packed-refs" has it, after a comment and a peeled tag line.
  write(root / "packed" / ".git" / "HEAD", "ref: refs/heads/main\r\n");
  write(root / "packed" / ".git" / "packed-refs",
        "# pack-refs with: peeled fully-peeled sorted\n" + kOther + " refs/heads/other\n" + kHash +
            " refs/heads/main\n^" + kOther + "\n");
  CHECK(read_commit(root / "packed" / ".git") == "b441c05");
  // A detached HEAD holds the hash itself.
  write(root / "detached" / ".git" / "HEAD", kOther + "\n");
  CHECK(read_commit(root / "detached" / ".git") == "0123456");
  // A worktree: `.git` is a file naming its folder, whose refs live in the
  // folder "commondir" names.
  write(root / "main" / ".git" / "HEAD", "ref: refs/heads/main\n");
  write(root / "main" / ".git" / "refs" / "heads" / "side", kOther + "\n");
  write(root / "main" / ".git" / "worktrees" / "wt" / "HEAD", "ref: refs/heads/side\n");
  write(root / "main" / ".git" / "worktrees" / "wt" / "commondir", "../..\n");
  write(root / "wt" / ".git", "gitdir: ../main/.git/worktrees/wt\n");
  CHECK(read_commit(root / "wt" / ".git") == "0123456");
  // Nothing that reads as a commit.
  CHECK(read_commit(root / "none" / ".git").empty());              // no .git at all
  write(root / "dangling" / ".git" / "HEAD", "ref: refs/heads/gone\n");
  CHECK(read_commit(root / "dangling" / ".git").empty());          // a ref with no file and no packed line
  write(root / "junk" / ".git" / "HEAD", "not a hash\n");
  CHECK(read_commit(root / "junk" / ".git").empty());
  write(root / "notgit" / ".git", "something else\n");
  CHECK(read_commit(root / "notgit" / ".git").empty());           // a file that is not "gitdir:"
  std::error_code ignored;
  fs::remove_all(root, ignored);
}

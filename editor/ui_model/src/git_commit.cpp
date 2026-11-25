// read_commit: git's own files read as text, with no git program.
#include "mforce/ui_model/build_stamp.h"
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace mforce::ui {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kShortHash = 7;

// The file's first line without its line end or trailing blanks; empty when
// it cannot be read.
std::string first_line(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  std::string line;
  std::getline(in, line);
  while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
  return line;
}

bool is_hash(std::string_view s) {
  if (s.size() < kShortHash) return false;
  for (char c : s)
    if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
  return true;
}

// A path a git file names: as written when absolute, else from `from`.
fs::path named_from(const fs::path& from, const std::string& written) {
  const fs::path p(written);
  return (p.is_absolute() ? p : from / p).lexically_normal();
}

// The folder that holds HEAD: `.git` itself, or the folder its "gitdir:"
// line names; empty when it is neither.
fs::path head_folder(const fs::path& git) {
  std::error_code ec;
  if (fs::is_directory(git, ec)) return git;
  const std::string line = first_line(git);
  constexpr std::string_view kGitdir = "gitdir: ";
  if (line.rfind(kGitdir, 0) != 0) return {};
  return named_from(git.parent_path(), line.substr(kGitdir.size()));
}

// Where the refs shared by every worktree live: the folder "commondir"
// names, else the folder itself.
fs::path common_folder(const fs::path& dir) {
  const std::string line = first_line(dir / "commondir");
  return line.empty() ? dir : named_from(dir, line);
}

// The hash "packed-refs" gives `ref`; empty when it lists none. A line is
// "<hash> <ref>"; "#" starts a comment and "^" a tag's peeled commit.
std::string packed_ref(const fs::path& common, std::string_view ref) {
  std::ifstream in(common / "packed-refs", std::ios::binary);
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
    if (line.empty() || line[0] == '#' || line[0] == '^') continue;
    const std::size_t space = line.find(' ');
    if (space != std::string::npos && std::string_view(line).substr(space + 1) == ref) return line.substr(0, space);
  }
  return {};
}

// The hash `ref` names: its own file in the worktree's folder or the shared
// one, else its line in "packed-refs".
std::string resolve(const fs::path& dir, const fs::path& common, const std::string& ref) {
  if (std::string hash = first_line(dir / ref); !hash.empty()) return hash;
  if (std::string hash = first_line(common / ref); !hash.empty()) return hash;
  return packed_ref(common, ref);
}

}  // namespace

std::string read_commit(const fs::path& git) {
  const fs::path dir = head_folder(git);
  if (dir.empty()) return {};
  std::string head = first_line(dir / "HEAD");
  constexpr std::string_view kRef = "ref: ";
  if (head.rfind(kRef, 0) == 0) head = resolve(dir, common_folder(dir), head.substr(kRef.size()));
  return is_hash(head) ? head.substr(0, kShortHash) : std::string();
}

}  // namespace mforce::ui

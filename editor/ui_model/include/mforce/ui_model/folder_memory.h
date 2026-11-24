#pragma once
#include <map>
#include <string>

namespace mforce::ui {

// The folder each kind of file was last picked from, so a dialog for that
// kind starts there next time, across restarts. One kind today, the
// patches; a later kind (a WAV, a passage) is one more name.
struct FolderMemory {
  std::map<std::string, std::string> folders;   // kind -> folder
};
inline constexpr const char* kPatchFiles = "patch";

// `picked` (a file a dialog gave) was of `kind`: its folder is remembered,
// when that folder exists.
void remember_folder(FolderMemory& memory, const std::string& kind, const std::string& picked);
// Where a dialog for `kind` starts: the folder remembered, when it still
// exists; empty for none.
std::string start_folder(const FolderMemory& memory, const std::string& kind);

// The memory kept in `file`, one "kind=folder" per line. A file that is
// missing or cannot be read reads as none; a line with no "=" is skipped.
FolderMemory read_folder_memory(const std::string& file);
// Writes it, one "kind=folder" per line. Throws std::runtime_error naming
// the file when it cannot be written.
void write_folder_memory(const FolderMemory& memory, const std::string& file);

}  // namespace mforce::ui

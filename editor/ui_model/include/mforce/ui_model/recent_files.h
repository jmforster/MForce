#pragma once
#include <string>
#include <vector>

namespace mforce::ui {

// The files opened most recently: newest first, no path twice, at most ten.
struct RecentFiles { std::vector<std::string> paths; };

// `path` becomes the newest; an older mention of it goes, and the eleventh goes.
void note_opened(RecentFiles& recent, const std::string& path);

// The list kept in `file`, one path per line. A file that is missing or
// cannot be read reads as none; blank lines and repeats are skipped.
RecentFiles read_recent_files(const std::string& file);

// Writes the list to `file`, one path per line, newest first. Throws
// std::runtime_error naming the file when it cannot be written.
void write_recent_files(const RecentFiles& recent, const std::string& file);

}  // namespace mforce::ui

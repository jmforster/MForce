#pragma once
#include <string>
#include "mforce/patch/patch.h"

namespace mforce::ui {

// A patch open in the editor: the patch, the file it was read from or saved
// to, and whether it has changes the file does not.
struct OpenPatch {
  Patch       patch;
  std::string path;          // empty for a patch never saved
  bool        dirty{false};  // changed since it was read or saved
};

// Reads the file with load_patch. Throws its std::runtime_error, whose
// message begins with the path.
OpenPatch open_patch(const std::string& path);

// Writes the patch to its own path with save_patch and clears dirty. Throws
// std::runtime_error, and leaves `open` as it was, when the patch has no path
// yet (save_as gives it one) or the file cannot be written.
void save(OpenPatch& open);

// Writes the patch to `path` with save_patch; `path` becomes its path and
// dirty is cleared. On a throw `open` is left as it was.
void save_as(OpenPatch& open, const std::string& path);

}  // namespace mforce::ui

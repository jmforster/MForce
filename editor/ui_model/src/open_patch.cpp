#include "mforce/ui_model/open_patch.h"
#include <stdexcept>
#include "mforce/patch/patch_json.h"

namespace mforce::ui {

OpenPatch open_patch(const std::string& path) {
  return OpenPatch{load_patch(path), path, false};
}

void save(OpenPatch& open) {
  if (open.path.empty())
    throw std::runtime_error("the patch has never been saved: it needs a file name (save as)");
  save_patch(open.path, open.patch);
  open.dirty = false;
}

void save_as(OpenPatch& open, const std::string& path) {
  save_patch(path, open.patch);
  open.path = path;
  open.dirty = false;
}

}  // namespace mforce::ui

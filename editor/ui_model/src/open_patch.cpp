#include "mforce/ui_model/open_patch.h"
#include <stdexcept>
#include "mforce/patch/patch_json.h"

namespace mforce::ui {

bool unsaved(const OpenPatch& open) { return unsaved(open.history); }

OpenPatch open_patch(const std::string& path) {
  return OpenPatch{load_patch(path), path, History{}};
}

void save(OpenPatch& open) {
  if (open.path.empty())
    throw std::runtime_error("the patch has never been saved: it needs a file name (save as)");
  save_patch(open.path, open.patch);
  mark_saved(open.history);
}

void save_as(OpenPatch& open, const std::string& path) {
  save_patch(path, open.patch);
  open.path = path;
  mark_saved(open.history);
}

}  // namespace mforce::ui

// OpenPatch: open, save, save as, and whether it is unsaved, on a library patch and
// on copies written to a folder under the system's temporary folder.
#include "mforce/testing/check.h"
#include "mforce/patch/patch_json.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_model/open_patch.h"
#include <filesystem>
#include <stdexcept>
#include <string>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kPatch = "patches/library/winds/oboe1.json";

// A fresh folder for one test's files, deleted with everything in it when
// the test ends (a C# `using` block over a temporary directory).
struct TempFolder {
  std::filesystem::path path;
  explicit TempFolder(const std::string& name)
      : path(std::filesystem::temp_directory_path() / ("mforce_ui_model_tests_" + name)) {
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempFolder() {
    std::error_code ignored;
    std::filesystem::remove_all(path, ignored);
  }
  std::string file(const std::string& name) const { return (path / name).string(); }
};

// The message of what `f` throws, or empty when it throws nothing.
template <class F>
std::string thrown_message(F f) {
  try {
    f();
  } catch (const std::exception& e) {
    return e.what();
  }
  return {};
}

}  // namespace

void run_open_patch_tests() {
  register_all_sources();
  const OpenPatch open = open_patch(kPatch);
  CHECK(open.path == kPatch);
  CHECK(!unsaved(open));
  CHECK(open.patch == load_patch(kPatch));
  CHECK(open.patch.check().empty());

  const std::string missing = "patches/library/no_such_patch.json";
  const std::string message = thrown_message([&] { open_patch(missing); });
  CHECK(message.rfind(missing, 0) == 0);   // the message begins with the path
}

void run_save_tests() {
  const TempFolder folder("save");
  OpenPatch open = open_patch(kPatch);
  open.path = folder.file("copy.json");
  remember(open.history, open.patch);
  open.patch.comment = "saved by the ui_model tests";
  REQUIRE(unsaved(open));
  save(open);
  CHECK(!unsaved(open));
  CHECK(open.path == folder.file("copy.json"));
  CHECK(load_patch(open.path) == open.patch);

  OpenPatch unsaved;
  unsaved.patch = open.patch;
  remember(unsaved.history, Patch{});
  CHECK(!thrown_message([&] { save(unsaved); }).empty());
  CHECK(ui::unsaved(unsaved));
  CHECK(unsaved.path.empty());
}

void run_save_as_tests() {
  const TempFolder folder("save_as");
  OpenPatch open = open_patch(kPatch);
  remember(open.history, open.patch);
  save_as(open, folder.file("renamed.json"));
  CHECK(open.path == folder.file("renamed.json"));
  CHECK(!unsaved(open));
  CHECK(open_patch(open.path).patch == open.patch);

  // A folder that does not exist cannot be written to: the patch keeps its path and its flag.
  remember(open.history, open.patch);
  const std::string nowhere = folder.file("no_such_folder/x.json");
  CHECK(!thrown_message([&] { save_as(open, nowhere); }).empty());
  CHECK(open.path == folder.file("renamed.json"));
  CHECK(unsaved(open));
}

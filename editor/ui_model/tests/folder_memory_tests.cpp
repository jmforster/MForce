// FolderMemory: a picked file's folder remembered per kind, a folder that no
// longer exists not offered, the plain text file written and read back.
#include "mforce/testing/check.h"
#include "mforce/ui_model/folder_memory.h"
#include <filesystem>
#include <fstream>
#include <string>

using namespace mforce::ui;

void run_folder_memory_tests() {
  const std::filesystem::path root = std::filesystem::temp_directory_path() / "mforce_ui_model_folders";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root / "a");
  FolderMemory m;
  CHECK(start_folder(m, kPatchFiles).empty());
  remember_folder(m, kPatchFiles, (root / "a" / "x.json").string());
  CHECK(start_folder(m, kPatchFiles) == (root / "a").string());
  remember_folder(m, kPatchFiles, (root / "gone" / "y.json").string());   // no such folder: kept as it was
  CHECK(start_folder(m, kPatchFiles) == (root / "a").string());
  remember_folder(m, "wav", (root / "a" / "z.wav").string());           // a second kind, apart
  CHECK(m.folders.size() == 2);

  const std::string file = (root / "folders.txt").string();
  write_folder_memory(m, file);
  CHECK(read_folder_memory(file).folders == m.folders);
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "patch=" << (root / "a").string() << "\r\nno equals here\n=nameless\n";
  }
  CHECK(read_folder_memory(file).folders.size() == 1);
  std::filesystem::remove_all(root / "a");
  CHECK(start_folder(read_folder_memory(file), kPatchFiles).empty());   // the folder went: none offered
  std::filesystem::remove_all(root);
  CHECK(read_folder_memory(file).folders.empty());                      // missing: none
}

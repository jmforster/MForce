#include "settings_folder.h"
#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace mforce::ui {

namespace {

// The value of the environment variable `name`, or empty.
std::string environment(const char* name) {
#ifdef _MSC_VER
  // MSVC's checked form of getenv: it copies the value into a buffer it
  // allocates, which the caller frees.
  char* value = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&value, &size, name) != 0 || !value) return {};
  const std::string text(value);
  std::free(value);
  return text;
#else
  const char* value = std::getenv(name);
  return value ? value : "";
#endif
}

}  // namespace

SettingsFiles settings_files() {
  const std::string appData = environment("APPDATA");
  if (appData.empty()) return {};
  const std::filesystem::path folder = std::filesystem::path(appData) / "MForce";
  std::error_code failed;
  std::filesystem::create_directories(folder, failed);
  if (failed) return {};
  return {(folder / "recent_files.txt").string(), (folder / "folders.txt").string(), (folder / "imgui.ini").string()};
}

}  // namespace mforce::ui

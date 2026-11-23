#include "error_box.h"
#ifdef _WIN32
// Only the parts of the Windows header this file needs, and no min/max macros.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <cstdio>
#endif

namespace mforce::ui {

void show_error_box(const std::string& title, const std::string& message) {
#ifdef _WIN32
  MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
#else
  std::fprintf(stderr, "%s: %s\n", title.c_str(), message.c_str());
#endif
}

}  // namespace mforce::ui

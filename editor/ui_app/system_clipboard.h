#pragma once
#include <string>
#include "mforce/ui_views/clipboard.h"

namespace mforce::ui {

// The system's clipboard, through what the window's backend gave ImGui
// (GLFW's), so a copy carries between two editor windows.
class SystemClipboard final : public Clipboard {
public:
  std::string text() override;
  void set_text(const std::string& text) override;
};

}  // namespace mforce::ui

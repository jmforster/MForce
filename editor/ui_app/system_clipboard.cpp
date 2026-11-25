#include "system_clipboard.h"
#include <imgui.h>

namespace mforce::ui {

std::string SystemClipboard::text() {
  const char* text = ImGui::GetClipboardText();
  return text ? std::string(text) : std::string();
}

void SystemClipboard::set_text(const std::string& text) { ImGui::SetClipboardText(text.c_str()); }

}  // namespace mforce::ui

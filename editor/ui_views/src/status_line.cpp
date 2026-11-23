#include "mforce/ui_views/status_line.h"
#include <string>
#include <imgui.h>
#include "mforce/ui_control/playing.h"

namespace mforce::ui {

void StatusLine::draw(const Session& s) {
  ImGui::TextUnformatted(s.status.c_str());
  const std::string trouble = sound_trouble(s);
  if (trouble.empty()) return;
  ImGui::SameLine();
  ImGui::TextColored(ImVec4(0.95f, 0.6f, 0.3f, 1.0f), "   %s", trouble.c_str());
}

}  // namespace mforce::ui

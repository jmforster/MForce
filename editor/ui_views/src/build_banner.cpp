#include "mforce/ui_views/build_banner.h"
#include <string>
#include <imgui.h>
#include "mforce/ui_control/banner.h"
#include "mforce/ui_model/build_stamp.h"

namespace mforce::ui {

namespace {

const ImVec4 kBannerColour(0.45f, 0.08f, 0.08f, 0.96f);
constexpr float kBannerTop = 8.0f;

constexpr ImGuiWindowFlags kBannerFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                          ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;

}  // namespace

void BuildBanner::draw(Session& s) {
  places_.clear();
  if (!banner_shown(s)) return;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, kBannerColour);
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, kBannerTop), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
  if (ImGui::Begin("##stalebanner", nullptr, kBannerFlags)) {
    for (const std::string& line : banner_lines(s.stamp)) ImGui::TextUnformatted(line.c_str());
    if (ImGui::Button("Dismiss")) dismiss_banner(s);
    places_.note_item("banner:Dismiss");
  }
  ImGui::End();
  ImGui::PopStyleColor();
}

}  // namespace mforce::ui

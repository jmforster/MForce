#include "mforce/ui_views/breadcrumb.h"
#include <filesystem>
#include <string>
#include <imgui.h>
#include "mforce/ui_control/grouping.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_model/groups.h"

namespace mforce::ui {

namespace {

constexpr ImVec4 kListenWord(0.6f, 0.6f, 0.6f, 1.0f);
constexpr ImVec4 kOn(0.5f, 0.8f, 0.5f, 1.0f);      // the side in force
constexpr ImVec4 kOff(0.5f, 0.5f, 0.5f, 1.0f);
constexpr float kSwitchRoom = 40.0f;                // beside the switch's words, at the bar's right

// A small button in `colour`; true when clicked.
bool coloured_button(const char* words, const ImVec4& colour) {
  ImGui::PushStyleColor(ImGuiCol_Text, colour);
  const bool clicked = ImGui::SmallButton(words);
  ImGui::PopStyleColor();
  return clicked;
}

}  // namespace

bool Breadcrumb::shown(const Session& s) { return !s.open.patch.layout.groups.empty() || !s.groupPath.empty(); }

// "Listen:" and its two sides, at the bar's right, while inside a group that has an output.
void Breadcrumb::listen_switch(Session& s) {
  if (s.groupPath.empty()) return;
  const std::string group = s.groupPath.back();
  const std::string out = output_node(s.open.patch, group);
  if (out.empty()) return;
  const bool listening = s.listening == out;
  const float room = ImGui::CalcTextSize("Listen:  Patch | Group").x + kSwitchRoom;
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - room);
  ImGui::TextColored(kListenWord, "Listen:");
  ImGui::SameLine();
  const bool patch = coloured_button("Patch", listening ? kOff : kOn);
  places_.note_item("listen:Patch");
  ImGui::SameLine();
  ImGui::TextUnformatted("|");
  ImGui::SameLine();
  const bool inGroup = coloured_button("Group", listening ? kOn : kOff);
  places_.note_item("listen:Group");
  if (patch) stop_listening(s);
  else if (inGroup) listen_at_group(s, group);
}

void Breadcrumb::draw(Session& s) {
  places_.clear();
  const std::string stem = s.open.path.empty() ? std::string("Patch") : std::filesystem::path(s.open.path).stem().string();
  std::size_t up = s.groupPath.size() + 1;           // where a button goes; none clicked yet
  if (ImGui::SmallButton((stem + "##crumb").c_str())) up = 0;
  places_.note_item("crumb:0");
  for (std::size_t k = 0; k < s.groupPath.size(); ++k) {
    ImGui::SameLine();
    ImGui::TextUnformatted(">");
    ImGui::SameLine();
    if (ImGui::SmallButton((s.groupPath[k] + "##crumb" + std::to_string(k + 1)).c_str())) up = k + 1;
    places_.note_item("crumb:" + std::to_string(k + 1));
  }
  listen_switch(s);
  ImGui::Separator();
  if (up <= s.groupPath.size()) go_up(s, up);
}

}  // namespace mforce::ui

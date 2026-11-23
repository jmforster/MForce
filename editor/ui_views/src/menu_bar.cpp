#include "mforce/ui_views/menu_bar.h"
#include <imgui.h>
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/unsaved.h"

namespace mforce::ui {

MenuBar::MenuBar(FileDialogs& dialogs) : dialogs_(dialogs) {}

void MenuBar::open(Session& s) {
  const std::string path = dialogs_.pick_open();
  if (!path.empty()) request_open(s, path);
}

void MenuBar::save(Session& s) {
  if (s.open.path.empty()) save_as(s);
  else save_file(s);
}

void MenuBar::save_as(Session& s) {
  const std::string path = dialogs_.pick_save(s.open.path);
  if (!path.empty()) save_file_as(s, path);
}

void MenuBar::draw(Session& s) {
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal)) open(s);
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) save(s);
  if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) save_as(s);
  if (!ImGui::BeginMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("Open...", "Ctrl+O")) open(s);
    if (ImGui::BeginMenu("Open Recent", !s.recent.paths.empty())) {
      const auto paths = s.recent.paths;               // request_open may reorder the list
      for (const auto& path : paths)
        if (ImGui::MenuItem(path.c_str())) request_open(s, path);
      ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Save", "Ctrl+S")) save(s);
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) save_as(s);
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) request_close(s);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    if (ImGui::MenuItem("Arrange nodes", nullptr, false, !s.open.patch.graph.nodes.empty())) arrange(s);
    ImGui::EndMenu();
  }
  ImGui::EndMenuBar();
}

}  // namespace mforce::ui

#include "mforce/ui_views/menu_bar.h"
#include <filesystem>
#include <imgui.h>
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/unsaved.h"

namespace mforce::ui {

namespace {

// A shortcut that works wherever the focus is. ImGuiInputFlags_RouteGlobal is
// ImGui's lowest-priority route, so a text field that has the keyboard keeps
// its own keys first (Ctrl+Z among them).
bool shortcut(ImGuiKeyChord keys) { return ImGui::Shortcut(keys, ImGuiInputFlags_RouteGlobal); }

}  // namespace

MenuBar::MenuBar(FileDialogs& dialogs) : dialogs_(dialogs) {}

void MenuBar::open(Session& s) {
  const std::string path = dialogs_.pick_open(FileKind::Patch);
  if (!path.empty()) request_open(s, path);
}

void MenuBar::save(Session& s) {
  if (s.open.path.empty()) save_as(s);
  else save_file(s);
}

void MenuBar::save_as(Session& s) {
  const std::string path = dialogs_.pick_save(FileKind::Patch, s.open.path);
  if (!path.empty()) save_file_as(s, path);
}

void MenuBar::shortcuts(Session& s) {
  if (shortcut(ImGuiMod_Ctrl | ImGuiKey_N)) request_new(s);
  if (shortcut(ImGuiMod_Ctrl | ImGuiKey_O)) open(s);
  if (shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) save(s);
  if (shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S)) save_as(s);
  if (ImGui::GetIO().WantTextInput) return;
  if (shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) undo(s);
  if (shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)) redo(s);
  if (ImGui::IsKeyPressed(ImGuiKey_Space, false)) play_or_stop(s);
}

void MenuBar::play_or_stop(Session& s) {
  if (playing(s)) stop(s);
  else play(s);
}

// Each file by its name, its full path as the tooltip; then Clear.
void MenuBar::recent(Session& s) {
  if (!ImGui::BeginMenu("Open Recent", !s.recent.paths.empty())) return;
  const auto paths = s.recent.paths;               // request_open may reorder the list
  for (const auto& path : paths) {
    ImGui::PushID(path.c_str());
    if (ImGui::MenuItem(std::filesystem::path(path).filename().string().c_str())) request_open(s, path);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
    ImGui::PopID();
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Clear")) clear_recent(s);
  ImGui::EndMenu();
}

void MenuBar::draw(Session& s) {
  shortcuts(s);
  if (!ImGui::BeginMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem("New", "Ctrl+N")) request_new(s);
    if (ImGui::MenuItem("Open...", "Ctrl+O")) open(s);
    recent(s);
    if (ImGui::MenuItem("Save", "Ctrl+S")) save(s);
    if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S")) save_as(s);
    ImGui::Separator();
    if (ImGui::MenuItem("Quit")) request_close(s);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Edit")) {
    if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !s.open.history.past.empty())) undo(s);
    if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !s.open.history.future.empty())) redo(s);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Transport")) {
    if (playing(s)) {
      if (ImGui::MenuItem("Stop", "Space")) stop(s);
    } else if (ImGui::MenuItem("Play", "Space")) {
      play(s);
    }
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    if (ImGui::MenuItem("Arrange nodes", nullptr, false, !s.open.patch.graph.nodes.empty())) arrange(s);
    ImGui::EndMenu();
  }
  ImGui::EndMenuBar();
}

}  // namespace mforce::ui

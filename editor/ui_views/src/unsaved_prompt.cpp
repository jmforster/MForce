#include "mforce/ui_views/unsaved_prompt.h"
#include <string>
#include <imgui.h>
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/unsaved.h"

namespace mforce::ui {

namespace {

constexpr const char* kTitle = "Unsaved changes";

}  // namespace

UnsavedPrompt::UnsavedPrompt(FileDialogs& dialogs) : dialogs_(dialogs) {}

void UnsavedPrompt::save(Session& s) {
  if (s.open.path.empty()) {
    const std::string path = dialogs_.pick_save({});
    if (path.empty()) return;                       // no file chosen: the question stays
    save_file_as(s, path);
  }
  answer_unsaved(s, Answer::Save);
}

namespace {

// The question, worded for what waits.
const char* question(Unsaved::Waiting waiting) {
  switch (waiting) {
    case Unsaved::Waiting::Close: return "The patch has unsaved changes. Save them before closing?";
    case Unsaved::Waiting::New:   return "The patch has unsaved changes. Save them before starting a new one?";
    case Unsaved::Waiting::Open:
    case Unsaved::Waiting::Nothing: break;
  }
  return "The patch has unsaved changes. Save them before opening another?";
}

}  // namespace

void UnsavedPrompt::draw(Session& s) {
  if (s.unsaved.waiting == Unsaved::Waiting::Nothing) return;
  ImGui::OpenPopup(kTitle);                         // each frame while it waits; once open, it stays open
  if (!ImGui::BeginPopupModal(kTitle, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
  ImGui::TextUnformatted(question(s.unsaved.waiting));
  if (!s.status.empty()) ImGui::TextDisabled("%s", s.status.c_str());
  if (ImGui::Button("Save")) save(s);
  ImGui::SameLine();
  if (ImGui::Button("Discard")) answer_unsaved(s, Answer::Discard);
  ImGui::SameLine();
  if (ImGui::Button("Cancel")) answer_unsaved(s, Answer::Cancel);
  if (s.unsaved.waiting == Unsaved::Waiting::Nothing) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

}  // namespace mforce::ui

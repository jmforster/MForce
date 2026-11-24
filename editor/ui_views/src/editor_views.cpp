#include "mforce/ui_views/editor_views.h"
#include <imgui.h>

namespace mforce::ui {

namespace {

constexpr float kPanelWidth = 360.0f;
constexpr float kKeyboardHeight = 112.0f;
constexpr float kStatusHeight = 26.0f;

constexpr ImGuiWindowFlags kMainFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar |
                                        ImGuiWindowFlags_NoSavedSettings;

}  // namespace

EditorViews::EditorViews(FileDialogs& dialogs) : menu_(dialogs), prompt_(dialogs) {}

EditorViews::~EditorViews() = default;

void EditorViews::draw(Session& session) {
  const ImGuiViewport* view = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(view->WorkPos);
  ImGui::SetNextWindowSize(view->WorkSize);
  ImGui::Begin("MForce", nullptr, kMainFlags);
  menu_.draw(session);
  const ImVec2 room = ImGui::GetContentRegionAvail();
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const float upper = room.y - kKeyboardHeight - kStatusHeight - 2.0f * spacing;
  ImGui::BeginChild("canvas", ImVec2(room.x - kPanelWidth, upper), ImGuiChildFlags_Borders);
  canvas_.draw(session);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("properties", ImVec2(0.0f, upper), ImGuiChildFlags_Borders);
  properties_.draw(session);
  ImGui::EndChild();
  ImGui::BeginChild("keyboard", ImVec2(0.0f, kKeyboardHeight), ImGuiChildFlags_Borders);
  keyboard_.draw(session);
  ImGui::EndChild();
  ImGui::BeginChild("status", ImVec2(0.0f, kStatusHeight));
  status_.draw(session, canvas_.size());
  ImGui::EndChild();
  ImGui::End();
  prompt_.draw(session);
}

}  // namespace mforce::ui

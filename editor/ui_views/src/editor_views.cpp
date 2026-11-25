#include "mforce/ui_views/editor_views.h"
#include <imgui.h>
#include "mforce/ui_control/waveforms.h"

namespace mforce::ui {

namespace {

constexpr float kPanelWidth = 360.0f;
constexpr float kStatusHeight = 26.0f;
constexpr float kTransportHeight = 172.0f;

constexpr ImGuiWindowFlags kMainFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar |
                                        ImGuiWindowFlags_NoSavedSettings;

}  // namespace

EditorViews::EditorViews(FileDialogs& dialogs, Clipboard& clipboard)
    : menu_(dialogs, clipboard), transport_(dialogs), canvas_(clipboard), prompt_(dialogs) {}

EditorViews::~EditorViews() = default;

std::optional<Area> EditorViews::area(const std::string& name) const {
  if (auto c = transport_.centre_of(name)) return Area{*c, *c};
  if (auto a = menu_.places().area(name)) return a;
  if (auto a = canvas_.places().area(name)) return a;
  if (auto a = breadcrumb_.places().area(name)) return a;
  if (auto a = properties_.places().area(name)) return a;
  if (auto a = banner_.places().area(name)) return a;
  return pane_.area(name);
}

// The pane takes its share of the room under the menu bar; the canvas and
// the panel take what the transport, the pane and the status line leave.
void EditorViews::draw(Session& session) {
  const ImGuiViewport* view = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(view->WorkPos);
  ImGui::SetNextWindowSize(view->WorkSize);
  ImGui::Begin("MForce", nullptr, kMainFlags);
  pane_.begin_frame();
  menu_.draw(session);
  ImGui::BeginChild("transport", ImVec2(0.0f, kTransportHeight), ImGuiChildFlags_Borders);
  transport_.draw(session);
  ImGui::EndChild();
  const ImVec2 room = ImGui::GetContentRegionAvail();
  const float spacing = ImGui::GetStyle().ItemSpacing.y;
  const float paneHeight = BottomPane::height(session, view->WorkSize.y - ImGui::GetFrameHeight());
  const float upper = room.y - paneHeight - kStatusHeight - 2.0f * spacing;
  ImGui::BeginChild("canvas", ImVec2(room.x - kPanelWidth, upper), ImGuiChildFlags_Borders);
  if (Breadcrumb::shown(session)) breadcrumb_.draw(session);
  canvas_.draw(session);
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("properties", ImVec2(0.0f, upper), ImGuiChildFlags_Borders);
  properties_.draw(session);
  ImGui::EndChild();
  ImGui::BeginChild("pane", ImVec2(0.0f, paneHeight), ImGuiChildFlags_Borders,
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  pane_.draw(session);
  ImGui::EndChild();
  ImGui::BeginChild("status", ImVec2(0.0f, kStatusHeight));
  status_.draw(session, canvas_.size());
  ImGui::EndChild();
  ImGui::End();
  pane_.draw_pop_outs(session);
  set_over_strips(session, pane_.over_strips());
  banner_.draw(session);
  prompt_.draw(session);
}

}  // namespace mforce::ui

#include "mforce/ui_views/headless.h"
#include <memory>
#include <string>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_views/editor_views.h"

namespace mforce::ui {

namespace {

// Three frames: the first creates every node, the second draws them where
// the first measured them, the third draws them again unchanged.
constexpr int kFrames = 3;

class CancelledDialogs final : public FileDialogs {
public:
  std::string pick_open(FileKind) override { return {}; }
  std::string pick_save(FileKind, const std::string&) override { return {}; }
};

// The two contexts, made by the constructor and destroyed by the destructor
// however the drawing ends.
struct Contexts {
  ImGuiContext* gui;
  Contexts() : gui(ImGui::CreateContext()) {
    ImNodes::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1400.0f, 900.0f);
    io.DeltaTime = 1.0f / 60.0f;
    // Says a renderer takes the font atlas's textures as they change, so
    // ImGui builds the atlas itself; with no renderer they are never drawn.
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
  }
  ~Contexts() {
    ImNodes::DestroyContext();
    ImGui::DestroyContext(gui);
  }
  Contexts(const Contexts&) = delete;
  Contexts& operator=(const Contexts&) = delete;
};

}  // namespace

struct HeadlessEditor::Parts {
  Contexts contexts;
  CancelledDialogs dialogs;
  EditorViews views{dialogs};
};

HeadlessEditor::HeadlessEditor(Session& session) : parts_(std::make_unique<Parts>()), session_(session) {}

// Defined here, where Parts is complete, so unique_ptr can destroy it.
HeadlessEditor::~HeadlessEditor() = default;

// A frame as the program's loop runs it (app_loop.cpp), less the window.
void HeadlessEditor::frames(int n) {
  for (int frame = 0; frame < n; ++frame) {
    keep_up(session_);
    ImGui::NewFrame();
    parts_->views.draw(session_);
    ImGui::Render();
    run_requested(session_);
  }
}

bool HeadlessEditor::click(const std::string& label) {
  const auto at = parts_->views.transport_item(label);
  if (!at) return false;
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(at->x, at->y);
  frames(1);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  frames(1);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  frames(1);
  return true;
}

void HeadlessEditor::click_canvas_corner() {
  const Vec2 at = parts_->views.canvas_origin();
  ImGuiIO& io = ImGui::GetIO();
  io.AddMousePosEvent(at.x + 10.0f, at.y + 10.0f);
  frames(2);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  frames(1);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  frames(2);
}

// A key down for a frame, then up for a frame.
void HeadlessEditor::press(int key) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiKey(key), true);
  frames(1);
  io.AddKeyEvent(ImGuiKey(key), false);
  frames(1);
}

void HeadlessEditor::press_delete() { press(ImGuiKey_Delete); }

bool HeadlessEditor::drawn(const std::string& label) const { return parts_->views.transport_item(label).has_value(); }

void HeadlessEditor::press_f() { press(ImGuiKey_F); }

void HeadlessEditor::press_space() { press(ImGuiKey_Space); }

Vec2 HeadlessEditor::canvas_size() const { return parts_->views.canvas_size(); }

Vec2 HeadlessEditor::canvas_origin() const { return parts_->views.canvas_origin(); }

Vec2 HeadlessEditor::piano_origin() const { return parts_->views.piano_origin(); }

void HeadlessEditor::rest_at(Vec2 at) {
  ImGui::GetIO().AddMousePosEvent(at.x, at.y);
  frames(2);
}

// A button down for a frame, then up, then a frame for what the release began.
void HeadlessEditor::click_button(int button) {
  ImGuiIO& io = ImGui::GetIO();
  io.AddMouseButtonEvent(button, true);
  frames(1);
  io.AddMouseButtonEvent(button, false);
  frames(2);
}

void HeadlessEditor::click_at(Vec2 at) {
  rest_at(at);
  click_button(ImGuiMouseButton_Left);
}

void HeadlessEditor::right_click_at(Vec2 at) {
  rest_at(at);
  click_button(ImGuiMouseButton_Right);
}

void HeadlessEditor::mouse_down_at(Vec2 at) {
  rest_at(at);
  ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  frames(1);
}

void HeadlessEditor::mouse_up() {
  ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  frames(2);
}

void HeadlessEditor::drag(Vec2 from, Vec2 to) {
  constexpr int kSteps = 4;
  mouse_down_at(from);
  for (int k = 1; k <= kSteps; ++k) {
    const float t = float(k) / float(kSteps);
    ImGui::GetIO().AddMousePosEvent(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
    frames(1);
  }
  mouse_up();
}

void HeadlessEditor::wheel_at(Vec2 at, float notches) {
  rest_at(at);
  ImGui::GetIO().AddMouseWheelEvent(0.0f, notches);
  frames(2);
}

bool HeadlessEditor::popup_open() const {
  return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void HeadlessEditor::select_link(int id) { ImNodes::SelectLink(id); }

int HeadlessEditor::selected_links() const { return ImNodes::NumSelectedLinks(); }

void draw_once_headless(Session& session) {
  const Contexts contexts;
  CancelledDialogs dialogs;
  EditorViews views(dialogs);
  for (int frame = 0; frame < kFrames; ++frame) {
    ImGui::NewFrame();
    views.draw(session);
    ImGui::Render();
  }
}

}  // namespace mforce::ui

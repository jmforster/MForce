#include "mforce/ui_views/headless.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/watchdog.h"
#include "mforce/ui_views/editor_views.h"

namespace mforce::ui {

namespace {

// Three frames: the first creates every node, the second draws them where
// the first measured them, the third draws them again unchanged.
constexpr int kFrames = 3;
constexpr double kFrameSeconds = 1.0 / 60.0;

class CancelledDialogs final : public FileDialogs {
public:
  std::string pick_open(FileKind) override { return {}; }
  std::string pick_save(FileKind, const std::string&) override { return {}; }
};

// ImGui's own clipboard, for a text field's Ctrl+C and Ctrl+V: a string the
// contexts keep, never the system's.
void keep_clipboard(std::string& kept) {
  ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
  platform.Platform_ClipboardUserData = &kept;
  // Captureless lambdas: plain function pointers, as C#'s static delegates.
  platform.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) {
    *static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData) = text ? text : "";
  };
  platform.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
    return static_cast<std::string*>(ImGui::GetPlatformIO().Platform_ClipboardUserData)->c_str();
  };
}

// The two contexts, made by the constructor and destroyed by the destructor
// however the drawing ends.
struct Contexts {
  ImGuiContext* gui;
  std::string clipboard;                           // ImGui's own clipboard text (keep_clipboard)
  Contexts() : gui(ImGui::CreateContext()) {
    keep_clipboard(clipboard);
    ImNodes::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = ImVec2(1400.0f, 900.0f);
    io.DeltaTime = float(kFrameSeconds);
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

// The ImGui key a test names.
ImGuiKey key_named(const std::string& key) {
  static constexpr struct { const char* name; ImGuiKey key; } kKeys[] = {
      {"Left", ImGuiKey_LeftArrow}, {"Right", ImGuiKey_RightArrow}, {"Up", ImGuiKey_UpArrow},
      {"Down", ImGuiKey_DownArrow}, {"Home", ImGuiKey_Home}, {"End", ImGuiKey_End}, {"=", ImGuiKey_Equal},
      {"-", ImGuiKey_Minus}, {"0", ImGuiKey_0}, {"Keypad+", ImGuiKey_KeypadAdd}, {"Keypad-", ImGuiKey_KeypadSubtract},
      {"Delete", ImGuiKey_Delete}, {"Space", ImGuiKey_Space}, {"Enter", ImGuiKey_Enter}};
  for (const auto& k : kKeys)
    if (key == k.name) return k.key;
  if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') return static_cast<ImGuiKey>(ImGuiKey_A + (key[0] - 'A'));
  throw std::invalid_argument("press_key: no key named " + key);
}

// The views' clipboard: the string ImGui's own clipboard keeps, so a copy
// on the canvas and a text field's Ctrl+V meet, as on the system's.
class KeptClipboard final : public Clipboard {
public:
  explicit KeptClipboard(std::string& kept) : kept_(kept) {}
  std::string text() override { return kept_; }
  void set_text(const std::string& text) override { kept_ = text; }

private:
  std::string& kept_;
};

}  // namespace

struct HeadlessEditor::Parts {
  Contexts contexts;
  CancelledDialogs dialogs;
  KeptClipboard clipboard{contexts.clipboard};
  EditorViews views{dialogs, clipboard};
};

HeadlessEditor::HeadlessEditor(Session& session) : parts_(std::make_unique<Parts>()), session_(session) {}

// Defined here, where Parts is complete, so unique_ptr can destroy it.
HeadlessEditor::~HeadlessEditor() = default;

// A frame as the program's loop runs it (app_loop.cpp), less the window.
void HeadlessEditor::frames(int n) {
  for (int frame = 0; frame < n; ++frame) {
    keep_up(session_);
    watch_audio(session_, clock_, true);
    clock_ += kFrameSeconds;
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

void HeadlessEditor::hover(Vec2 at) {
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
  hover(at);
  click_button(ImGuiMouseButton_Left);
}

void HeadlessEditor::ctrl_click_at(Vec2 at) {
  hover(at);
  ImGuiIO& io = ImGui::GetIO();
  io.AddKeyEvent(ImGuiMod_Ctrl, true);
  frames(1);
  click_button(ImGuiMouseButton_Left);
  io.AddKeyEvent(ImGuiMod_Ctrl, false);
  frames(1);
}

void HeadlessEditor::double_click_at(Vec2 at) {
  click_at(at);
  click_at(at);
}

// ImGui's double-click time is 0.30 s: twenty frames are a third of a second.
void HeadlessEditor::settle() { frames(20); }

void HeadlessEditor::right_click_at(Vec2 at) {
  hover(at);
  click_button(ImGuiMouseButton_Right);
}

void HeadlessEditor::mouse_down_at(Vec2 at) {
  hover(at);
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

void HeadlessEditor::wheel_at(Vec2 at, float notches, bool ctrl) {
  hover(at);
  ImGuiIO& io = ImGui::GetIO();
  if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddMouseWheelEvent(0.0f, notches);
  frames(1);
  if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
  frames(1);
}

std::optional<Area> HeadlessEditor::area(const std::string& name) const { return parts_->views.area(name); }

bool HeadlessEditor::click_on(const std::string& name) {
  const auto a = area(name);
  if (!a) return false;
  click_at(a->centre());
  return true;
}

bool HeadlessEditor::right_click_on(const std::string& name) {
  const auto a = area(name);
  if (!a) return false;
  right_click_at(a->centre());
  return true;
}

bool HeadlessEditor::ctrl_click_on(const std::string& name) {
  const auto a = area(name);
  if (!a) return false;
  ctrl_click_at(a->centre());
  return true;
}

bool HeadlessEditor::double_click_on(const std::string& name) {
  const auto a = area(name);
  if (!a) return false;
  double_click_at(a->centre());
  return true;
}

Clipboard& HeadlessEditor::clipboard() { return parts_->clipboard; }

bool HeadlessEditor::hover_on(const std::string& name) {
  const auto a = area(name);
  if (!a) return false;
  hover(a->centre());
  return true;
}

void HeadlessEditor::press_key(const std::string& key, bool shift, bool ctrl) {
  ImGuiIO& io = ImGui::GetIO();
  const ImGuiKey k = key_named(key);
  if (shift) io.AddKeyEvent(ImGuiMod_Shift, true);
  if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, true);
  io.AddKeyEvent(k, true);
  frames(1);
  io.AddKeyEvent(k, false);
  if (shift) io.AddKeyEvent(ImGuiMod_Shift, false);
  if (ctrl) io.AddKeyEvent(ImGuiMod_Ctrl, false);
  frames(1);
}

void HeadlessEditor::type_text(const std::string& text) {
  ImGui::GetIO().AddInputCharactersUTF8(text.c_str());
  frames(1);
}

const std::string& HeadlessEditor::spectrum_readout() const { return parts_->views.spectrum_readout(); }

bool HeadlessEditor::popup_open() const {
  return ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

void HeadlessEditor::select_link(int id) { ImNodes::SelectLink(id); }

int HeadlessEditor::selected_links() const { return ImNodes::NumSelectedLinks(); }

void draw_once_headless(Session& session) {
  const Contexts contexts;
  CancelledDialogs dialogs;
  std::string kept;
  KeptClipboard clipboard(kept);
  EditorViews views(dialogs, clipboard);
  for (int frame = 0; frame < kFrames; ++frame) {
    ImGui::NewFrame();
    views.draw(session);
    ImGui::Render();
  }
}

}  // namespace mforce::ui

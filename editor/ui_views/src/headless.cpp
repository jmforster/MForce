#include "mforce/ui_views/headless.h"
#include <string>
#include <imgui.h>
#include <imnodes.h>
#include "mforce/ui_views/editor_views.h"

namespace mforce::ui {

namespace {

// Two frames: the first creates every node, the second draws them where
// the first measured them.
constexpr int kFrames = 2;

class CancelledDialogs final : public FileDialogs {
public:
  std::string pick_open() override { return {}; }
  std::string pick_save(const std::string&) override { return {}; }
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

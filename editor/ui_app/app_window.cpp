#include "app_window.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include "screenshot.h"
#include <GLFW/glfw3.h>   // also brings the system's OpenGL header (glClear, glViewport)
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imnodes.h>

namespace mforce::ui {
namespace {

// `what`, followed by GLFW's description of its last error when it has one.
std::string with_glfw_error(const std::string& what) {
  const char* description = nullptr;
  glfwGetError(&description);
  return description ? what + ": " + description : what;
}

// Each part below is started by its constructor and stopped by its
// destructor (RAII; in C# the nearest is an IDisposable in a `using`, but
// here the compiler calls the destructor itself, in reverse order of start).

struct GlfwLibrary {
  GlfwLibrary() {
    if (!glfwInit()) throw std::runtime_error(with_glfw_error("GLFW did not start"));
  }
  ~GlfwLibrary() { glfwTerminate(); }
  GlfwLibrary(const GlfwLibrary&) = delete;
  GlfwLibrary& operator=(const GlfwLibrary&) = delete;
};

// A window with an OpenGL 3.3 core context, current on this thread.
GLFWwindow* create_window(int width, int height, const std::string& title) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  GLFWwindow* window = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
  if (!window) throw std::runtime_error(with_glfw_error("the window did not open"));
  // On the primary monitor's work area, 100 across and 60 down from its corner,
  // so the window cannot land on a monitor that is not there.
  int left = 0, top = 0, width0 = 0, height0 = 0;
  if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) glfwGetMonitorWorkarea(monitor, &left, &top, &width0, &height0);
  glfwSetWindowPos(window, left + 100, top + 60);
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);   // one frame per screen refresh
  return window;
}

struct GlfwWindow {
  GLFWwindow* handle;
  GlfwWindow(int width, int height, const std::string& title)
      : handle(create_window(width, height, title)) {}
  ~GlfwWindow() { glfwDestroyWindow(handle); }
  GlfwWindow(const GlfwWindow&) = delete;
  GlfwWindow& operator=(const GlfwWindow&) = delete;
};

// The font the program draws with, from the ImGui submodule, when it is
// there; ImGui's own font otherwise.
constexpr const char* kFont = "third_party/imgui/misc/fonts/Roboto-Medium.ttf";
constexpr float kFontSize = 15.0f;

// `ini` is ImGui's settings file, kept alive here as long as the context
// reads it; empty: ImGui writes none.
struct GuiContexts {
  std::string ini;
  explicit GuiContexts(const std::string& iniFile) : ini(iniFile) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImNodes::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = ini.empty() ? nullptr : ini.c_str();
    if (std::filesystem::exists(kFont)) io.Fonts->AddFontFromFileTTF(kFont, kFontSize);
    io.ConfigDragClickToInputText = true;            // one click on a drag field types a number
    ImGui::StyleColorsDark();
    // The canvas pans with Alt and a left drag, as well as with the middle button.
    ImNodes::GetIO().EmulateThreeButtonMouse.Modifier = &io.KeyAlt;
    ImNodesStyle& canvas = ImNodes::GetStyle();      // rounded nodes, a grid, wires 2.5 wide
    canvas.NodeCornerRounding = 4.0f;
    canvas.NodePadding = ImVec2(8.0f, 8.0f);
    canvas.PinCircleRadius = 4.0f;
    canvas.LinkThickness = 2.5f;
    canvas.Flags |= ImNodesStyleFlags_GridLines;
  }
  ~GuiContexts() {
    ImNodes::DestroyContext();
    ImGui::DestroyContext();
  }
  GuiContexts(const GuiContexts&) = delete;
  GuiContexts& operator=(const GuiContexts&) = delete;
};

// ImGui's GLFW backend (input) and OpenGL 3 backend (drawing).
struct GuiBackends {
  explicit GuiBackends(GLFWwindow* window) {
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
      throw std::runtime_error("ImGui's GLFW backend did not start");
    if (!ImGui_ImplOpenGL3_Init("#version 330")) {
      ImGui_ImplGlfw_Shutdown();
      throw std::runtime_error("ImGui's OpenGL 3 backend did not start");
    }
  }
  ~GuiBackends() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
  }
  GuiBackends(const GuiBackends&) = delete;
  GuiBackends& operator=(const GuiBackends&) = delete;
};

}  // namespace

// Members are started in the order they are declared and stopped in reverse;
// when one throws, the ones before it are stopped.
struct AppWindow::Parts {
  GlfwLibrary glfw;
  GlfwWindow  window;
  GuiContexts gui;
  GuiBackends backends;
  Parts(int width, int height, const std::string& title, const std::string& ini)
      : window(width, height, title), gui(ini), backends(window.handle) {}
};

AppWindow::AppWindow(int width, int height, const std::string& title, const std::string& imguiIni)
    : parts_(std::make_unique<Parts>(width, height, title, imguiIni)) {}

// Defined here, where Parts is complete, so unique_ptr can destroy it.
AppWindow::~AppWindow() = default;

void AppWindow::poll_events() { glfwPollEvents(); }

bool AppWindow::close_requested() const { return glfwWindowShouldClose(parts_->window.handle) != 0; }

void AppWindow::keep_open() { glfwSetWindowShouldClose(parts_->window.handle, GLFW_FALSE); }

void AppWindow::set_title(const std::string& title) { glfwSetWindowTitle(parts_->window.handle, title.c_str()); }

double AppWindow::now() const { return glfwGetTime(); }

bool AppWindow::focused() const { return glfwGetWindowAttrib(parts_->window.handle, GLFW_FOCUSED) != 0; }

void AppWindow::begin_frame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void AppWindow::end_frame(const std::string& shot) {
  ImGui::Render();
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(parts_->window.handle, &width, &height);
  glViewport(0, 0, width, height);
  glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  if (!shot.empty()) {
    std::vector<unsigned char> rgb(size_t(width) * size_t(height) * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);            // rows packed with no padding
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
    write_bmp(shot, width, height, rgb);
  }
  glfwSwapBuffers(parts_->window.handle);
}

}  // namespace mforce::ui

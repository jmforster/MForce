#include "app_window.h"
#include <stdexcept>
#include <string>
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

struct GuiContexts {
  GuiContexts() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImNodes::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;   // ImGui writes no imgui.ini
    ImGui::StyleColorsDark();
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
  Parts(int width, int height, const std::string& title)
      : window(width, height, title), backends(window.handle) {}
};

AppWindow::AppWindow(int width, int height, const std::string& title)
    : parts_(std::make_unique<Parts>(width, height, title)) {}

// Defined here, where Parts is complete, so unique_ptr can destroy it.
AppWindow::~AppWindow() {}

bool AppWindow::should_close() const { return glfwWindowShouldClose(parts_->window.handle) != 0; }

void AppWindow::begin_frame() {
  glfwPollEvents();
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void AppWindow::end_frame() {
  ImGui::Render();
  int width = 0;
  int height = 0;
  glfwGetFramebufferSize(parts_->window.handle, &width, &height);
  glViewport(0, 0, width, height);
  glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(parts_->window.handle);
}

}  // namespace mforce::ui

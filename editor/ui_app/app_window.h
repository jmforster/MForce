#pragma once
#include <memory>
#include <string>

namespace mforce::ui {

// The program's one window: a GLFW window with an OpenGL 3 context, and the
// ImGui and imnodes contexts drawn into it. main makes one and passes it
// down. The constructor starts each part in turn and throws
// std::runtime_error naming the part that did not start; the parts started
// before it are stopped again. The destructor stops them all, last first.
class AppWindow {
public:
  // `imguiIni`: the file ImGui keeps its window settings in; empty for none.
  AppWindow(int width, int height, const std::string& title, const std::string& imguiIni);
  ~AppWindow();
  // "= delete" removes the copy: there is one window and one owner of it.
  AppWindow(const AppWindow&) = delete;
  AppWindow& operator=(const AppWindow&) = delete;

  void poll_events();                // reads the input that arrived
  bool close_requested() const;      // the window's close button was pressed
  void keep_open();                  // forgets the request: the program decides when to close
  void set_title(const std::string& title);
  void begin_frame();                // starts an ImGui frame
  // Draws the frame ImGui built and shows it; with a `shot` path, first
  // writes what was drawn there as a 24-bit BMP.
  void end_frame(const std::string& shot = {});

private:
  // Declared here, defined in the .cpp: GLFW, OpenGL and ImGui stay out of
  // this header, so no file that includes it sees a library's type.
  struct Parts;
  std::unique_ptr<Parts> parts_;
};

}  // namespace mforce::ui

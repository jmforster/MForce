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
  AppWindow(int width, int height, const std::string& title);
  ~AppWindow();
  // "= delete" removes the copy: there is one window and one owner of it.
  AppWindow(const AppWindow&) = delete;
  AppWindow& operator=(const AppWindow&) = delete;

  bool should_close() const;   // the window's close button was pressed
  void begin_frame();          // reads the input that arrived, starts an ImGui frame
  void end_frame();            // draws the frame ImGui built and shows it

private:
  // Declared here, defined in the .cpp: GLFW, OpenGL and ImGui stay out of
  // this header, so no file that includes it sees a library's type.
  struct Parts;
  std::unique_ptr<Parts> parts_;
};

}  // namespace mforce::ui

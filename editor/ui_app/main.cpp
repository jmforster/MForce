// mforce_editor, the patch editor.
//
//   mforce_editor              opens the window and runs until it is closed
//   mforce_editor --frames N   draws N frames and exits with 0
//
// A failure to start is shown in a message box and the exit code is 1.
#include <stdexcept>
#include <string>
#include "app_window.h"
#include "error_box.h"

namespace {

constexpr int kUntilClosed = -1;

// The number of frames to draw, from the command line: kUntilClosed with no
// arguments. Throws std::runtime_error on any other command line.
int frame_limit(int argc, char** argv) {
  if (argc == 1) return kUntilClosed;
  const std::string usage = "usage: mforce_editor [--frames N]";
  if (argc != 3 || std::string(argv[1]) != "--frames") throw std::runtime_error(usage);
  try {
    const int frames = std::stoi(argv[2]);
    if (frames >= 0) return frames;
  } catch (const std::exception&) {   // not a number, or out of range
  }
  throw std::runtime_error(usage + ": N is a count of frames, 0 or more");
}

void run(mforce::ui::AppWindow& window, int frames) {
  for (int drawn = 0; frames == kUntilClosed || drawn < frames; ++drawn) {
    if (window.should_close()) return;
    window.begin_frame();
    window.end_frame();
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const int frames = frame_limit(argc, argv);
    mforce::ui::AppWindow window(1400, 900, "MForce");
    run(window, frames);
    return 0;
  } catch (const std::exception& e) {
    mforce::ui::show_error_box("MForce could not start", e.what());
    return 1;
  }
}

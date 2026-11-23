#pragma once
#include <string>

namespace mforce::ui {

// The command line: mforce_editor [patch.json] [--frames N] [--shot FILE]
struct Options {
  std::string patch;         // opened at start; empty for none
  int frames{-1};            // frames to draw before exiting; -1: until the window is closed
  std::string shot;          // where the last frame is written as a BMP; empty for none
};

// Throws std::runtime_error with the usage on a command line it cannot read.
Options read_options(int argc, char** argv);

}  // namespace mforce::ui

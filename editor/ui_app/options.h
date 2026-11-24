#pragma once
#include <string>
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/transport.h"

namespace mforce::ui {

// The command line: mforce_editor [patch.json] [--frames N] [--shot FILE] [--select NAME] [--tab NAME]
//   [--generate] [--pane NAME] [--columns N] [--popout NAME] [--collapse] [--listen NAME]
struct Options {
  std::string patch;         // opened at start; empty for none
  int frames{-1};            // frames to draw before exiting; -1: until the window is closed
  std::string shot;          // where the last frame is written as a BMP; empty for none
  std::string select;        // the node or face selected before the first frame; empty for none
  std::string tab;           // the transport tab shown first: note, passage, chords or drums; empty for note
  // For pictures, each applied before the first frame:
  bool generate{false};      // a Generate on the tab shown
  std::string pane;          // the pane's tab shown: waveforms, spectrum or keyboard; empty for waveforms
  int columns{0};            // the strips' columns; 0 leaves them as they are
  std::string popout;        // a strip popped out, by its node ("Output" for the output)
  bool collapse{false};      // the pane collapsed
  std::string listen;        // the node listened to
};

// Throws std::runtime_error with the usage on a command line it cannot read.
Options read_options(int argc, char** argv);

// `t` showing the tab `--tab` names (note, passage, chords, drums).
Transport with_tab(Transport t, const std::string& tab);

// The picture options applied to the session, each a call of ui_control, in
// the order the program would take them: listen, generate, then the pane.
void apply_picture_options(Session& s, const Options& o);

}  // namespace mforce::ui

#pragma once
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "mforce/ui_audio/audio_adapter.h"
#include "mforce/ui_audio/midi_in.h"
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/live_play.h"
#include "mforce/ui_control/pane.h"
#include "mforce/ui_control/take.h"
#include "mforce/ui_control/transport.h"
#include "mforce/ui_control/unsaved.h"
#include "mforce/ui_control/watchdog.h"
#include "mforce/ui_control/waveforms.h"
#include "mforce/ui_model/build_stamp.h"
#include "mforce/ui_model/open_patch.h"
#include "mforce/ui_model/recent_files.h"

namespace mforce::ui {

// The last note played, for the transport's "Last note".
struct LastNote {
  int   note{-1};                                  // rounded; -1 before any
  float velocity{0.0f};
};

// The editor's whole state. main owns one; the views are handed it and
// change it only through the functions of edits.h, selecting.h, building.h,
// grouping.h, clipboard.h, wiring.h, files.h, unsaved.h, playing.h,
// generating.h, waveforms.h, listening.h, watchdog.h, pane.h, midi.h and
// banner.h. Made with no device and no MIDI port: main calls audio.open()
// and open_first_midi, and fills the build stamp.
struct Session {
  OpenPatch    open;
  AudioAdapter audio;
  LivePlay     live{audio};   // keys to notes; it keeps the last wheel and pressure for each player it builds
  MidiIn       midi;         // the MIDI port; none until one is asked for
  Keyboard     keyboard;
  Transport    transport;
  Take         take;         // the last render; empty before one
  int          takes{0};     // how many times the take has been replaced or cleared (replace_take)
  WaveView     waves;        // how the pane's strips are looked at
  std::vector<PopOut> popOuts;   // the strips popped out, oldest first
  bool         overStrips{false};  // the mouse was over the strips last frame: the strips have their keys
  Pane         pane;         // the bottom pane: the tab shown, collapsed or not
  int          nextPopOut{1};    // the number the next pop-out takes
  std::string  listening;    // the node the keys hear in place of the output; empty for the output
  Watchdog     watchdog;     // the sound card's heartbeat as last seen
  Away         away;         // the window's focus as last seen
  LastNote     lastNote;
  int          generating{0};  // frames still to show before a requested Generate runs; 0 for none
  // The plain sound running on the output's playback line, by address (only
  // compared, never followed); null when none runs.
  const Sound* runningPlain{nullptr};
  RecentFiles  recent;
  BuildStamp   stamp;        // when the program was built and whether a source is newer; empty in a test's session
  bool         bannerDismissed{false};   // the stale banner's Dismiss was clicked: not shown again this session
  std::vector<std::string> selections;  // node ids, face labels, group names, in the order chosen (selecting.h)
  std::vector<std::string> groupPath;   // the groups gone into, outermost first; not saved (grouping.h)
  Vec2         levelPanning{};            // the canvas's panning inside a group; at the top the patch holds it
  std::vector<Vec2> pans;                 // pans[d]: the panning left at depth d on going deeper
  std::string  status;       // the last thing worth telling: a refusal, why there is no sound
  Unsaved      unsaved;      // a close, an open or a new patch waiting for the answer about unsaved changes
  // How many patches this session has opened or started; a view that keeps
  // drawing state for the open patch drops it when this changes.
  int          opened{0};
  std::string  gesture;      // the edit a drag is in the middle of; empty when none
  std::set<std::pair<std::string, std::string>> offered;   // (node, setting): a pin shown on a node with nothing feeding it; not saved
  std::mt19937 seeds;        // where a new node's seed comes from: main seeds it from std::random_device, a test with a constant
};

}  // namespace mforce::ui

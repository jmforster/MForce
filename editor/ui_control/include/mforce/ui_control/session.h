#pragma once
#include <random>
#include <set>
#include <string>
#include <utility>
#include "mforce/ui_audio/audio_adapter.h"
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/live_play.h"
#include "mforce/ui_control/take.h"
#include "mforce/ui_control/transport.h"
#include "mforce/ui_control/unsaved.h"
#include "mforce/ui_model/open_patch.h"
#include "mforce/ui_model/recent_files.h"

namespace mforce::ui {

// The last note played, for the transport's "Last note".
struct LastNote {
  int   note{-1};                                  // rounded; -1 before any
  float velocity{0.0f};
};

// The editor's whole state. main owns one; the views are handed it and
// change it only through the functions of edits.h, building.h, wiring.h,
// files.h, unsaved.h, playing.h and generating.h. Made with no device: main
// calls audio.open().
struct Session {
  OpenPatch    open;
  AudioAdapter audio;
  LivePlay     live{audio};
  Keyboard     keyboard;
  Transport    transport;
  Take         take;         // the last render; empty before one
  LastNote     lastNote;
  int          generating{0};  // frames still to show before a requested Generate runs; 0 for none
  RecentFiles  recent;
  std::string  selected;     // a node's id or a face's label (one namespace), or empty
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

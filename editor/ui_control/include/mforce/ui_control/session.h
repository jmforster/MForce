#pragma once
#include <string>
#include "mforce/ui_audio/audio_adapter.h"
#include "mforce/ui_control/keyboard.h"
#include "mforce/ui_control/live_play.h"
#include "mforce/ui_control/unsaved.h"
#include "mforce/ui_model/open_patch.h"
#include "mforce/ui_model/recent_files.h"

namespace mforce::ui {

// The editor's whole state. main owns one; the views are handed it and
// change it only through the functions of edits.h, files.h, unsaved.h and
// playing.h. Made with no device: main calls audio.open().
struct Session {
  OpenPatch    open;
  AudioAdapter audio;
  LivePlay     live{audio};
  Keyboard     keyboard;
  RecentFiles  recent;
  std::string  selected;     // a node id, or empty
  std::string  status;       // the last thing worth telling: a refusal, why there is no sound
  Unsaved      unsaved;      // a close or an open waiting for the answer about unsaved changes
};

}  // namespace mforce::ui

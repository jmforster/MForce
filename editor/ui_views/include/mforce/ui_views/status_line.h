#pragma once
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The last thing worth telling (session.status), and why there is no sound
// when there is a reason.
class StatusLine {
public:
  void draw(const Session& s);
};

}  // namespace mforce::ui

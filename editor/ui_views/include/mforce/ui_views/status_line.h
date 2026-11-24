#pragma once
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// The last thing worth telling (session.status), and why there is no sound
// when there is a reason; at its right end what the canvas draws, "N nodes,
// M wires", four buttons that pan the canvas 100 points each way, and Fit,
// which centres the patch in a canvas of `canvasSize`.
class StatusLine {
public:
  void draw(Session& s, Vec2 canvasSize);
};

}  // namespace mforce::ui

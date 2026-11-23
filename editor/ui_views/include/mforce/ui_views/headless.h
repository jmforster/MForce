#pragma once
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// Draws every view with no window: makes an ImGui and an imnodes context
// with no backend, draws the frames a first showing needs, destroys both.
// The file dialogs answer as if cancelled.
void draw_once_headless(Session& session);

}  // namespace mforce::ui

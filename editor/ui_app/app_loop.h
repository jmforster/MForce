#pragma once
#include "app_window.h"
#include "options.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/editor_views.h"

namespace mforce::ui {

// The frame loop: input, the close button turned into a question about
// unsaved changes, a frame of every view, a Generate asked for run once its
// frame is shown, the window's title kept to the session's. Ends when the session may close, or after `o.frames` frames,
// writing the last one to `o.shot` when it names a file.
void run(AppWindow& window, Session& session, EditorViews& views, const Options& o);

}  // namespace mforce::ui

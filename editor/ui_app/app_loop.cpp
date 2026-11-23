#include "app_loop.h"
#include <string>
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/unsaved.h"

namespace mforce::ui {

void run(AppWindow& window, Session& session, EditorViews& views, const Options& o) {
  std::string shownTitle;
  for (int frame = 0; o.frames < 0 || frame < o.frames; ++frame) {
    window.poll_events();
    if (window.close_requested()) {
      request_close(session);
      window.keep_open();
    }
    if (session.unsaved.closing) return;
    keep_up(session);
    window.begin_frame();
    views.draw(session);
    window.end_frame(frame + 1 == o.frames ? o.shot : std::string());
    if (const std::string t = title(session); t != shownTitle) {
      window.set_title(t);
      shownTitle = t;
    }
  }
}

}  // namespace mforce::ui

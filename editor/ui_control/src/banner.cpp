#include "mforce/ui_control/banner.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/build_stamp.h"

namespace mforce::ui {

bool banner_shown(const Session& s) { return stale(s.stamp) && !s.bannerDismissed; }

void dismiss_banner(Session& s) { s.bannerDismissed = true; }

}  // namespace mforce::ui

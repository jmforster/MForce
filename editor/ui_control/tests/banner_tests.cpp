// The build stamp in the session: the title's suffix, the banner shown while
// stale until dismissed, and nothing of either with an empty stamp.
#include "mforce/testing/check.h"
#include "mforce/ui_control/banner.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/session.h"

using namespace mforce::ui;

void run_title_stamp_tests() {
  Session s;
  CHECK(title(s) == "MForce");                           // a test's session: no suffix, no banner
  CHECK(!banner_shown(s));
  s.stamp = BuildStamp{"11-24 14:41", "b441c05", ""};
  CHECK(title(s) == "MForce  [build 11-24 14:41 @b441c05]");
  CHECK(!banner_shown(s));
  open_file(s, "patches/library/winds/oboe1.json");
  CHECK(title(s) == "MForce - oboe1.json  [build 11-24 14:41 @b441c05]");
  s.stamp.newer = "edit.h (11-24 15:02)";
  CHECK(title(s) == "MForce - oboe1.json  [build 11-24 14:41 @b441c05]  *** STALE — REBUILD ***");
  CHECK(banner_shown(s));
  dismiss_banner(s);
  CHECK(!banner_shown(s));
  CHECK(title(s).find("STALE") != std::string::npos);    // the title still says so
}

// The transport's MIDI box and the stale banner under the headless editor,
// by the mouse: the box drawn with no port, opened, and "(none)" chosen from
// it; the banner drawn while stale, not when the stamp is fresh or empty, and
// gone for the session once its Dismiss is clicked.
#include "mforce/testing/check.h"
#include "mforce/ui_control/banner.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/midi.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/headless.h"
#include <string>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kOboe = "patches/library/winds/oboe1.json";

}  // namespace

void run_midi_box_gesture_tests() {
  Session s;
  open_file(s, kOboe);
  HeadlessEditor ed(s);
  ed.frames(2);
  CHECK(ed.drawn("MIDI: none"));                         // no port open
  CHECK(!ed.drawn("(none)") && !ed.popup_open());
  const std::string status = s.status;
  REQUIRE(ed.click("MIDI: none"));                       // the box opens: the ports asked for, "(none)" first
  CHECK(ed.popup_open() && ed.drawn("(none)"));
  REQUIRE(ed.click("(none)"));                           // chosen: no port, nothing said
  CHECK(!ed.popup_open() && !ed.drawn("(none)"));
  CHECK(midi_port(s).empty() && s.status == status);
  CHECK(ed.drawn("MIDI: none"));
}

void run_banner_gesture_tests() {
  {
    Session s;                                           // a test's session: an empty stamp
    open_file(s, kOboe);
    HeadlessEditor ed(s);
    ed.frames(2);
    CHECK(!ed.area("banner:Dismiss").has_value());
    CHECK(title(s) == "MForce - oboe1.json");
  }
  {
    Session s;
    s.stamp = BuildStamp{"11-24 14:41", "b441c05", ""};  // not stale
    open_file(s, kOboe);
    HeadlessEditor ed(s);
    ed.frames(2);
    CHECK(!ed.area("banner:Dismiss").has_value());
  }
  Session s;
  s.stamp = BuildStamp{"11-24 14:41", "b441c05", "edit.h (11-24 15:02)"};
  open_file(s, kOboe);
  HeadlessEditor ed(s);
  ed.frames(2);
  CHECK(banner_shown(s));
  REQUIRE(ed.click_on("banner:Dismiss"));
  CHECK(s.bannerDismissed && !banner_shown(s));
  CHECK(!ed.area("banner:Dismiss").has_value());         // not drawn again
  ed.frames(3);
  CHECK(!ed.area("banner:Dismiss").has_value());
  CHECK(title(s).find("STALE") != std::string::npos);
}

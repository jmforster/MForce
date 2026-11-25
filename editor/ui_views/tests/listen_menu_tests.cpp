// Listen from a node's menu under the headless editor's mouse, and the
// restart of the sound card by the Edit menu and by its keys.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_views/headless.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

constexpr Vec2 kNote{40, 40}, kOsc{300, 40}, kLfo{300, 200}, kChannel{40, 220}, kOutput{620, 40};
constexpr Vec2 kGrip{20, 10};

Vec2 operator+(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }

// A note into osc, the output; a sine lfo standing by; a channel, a part.
Patch listen_patch() {
  Patch p;
  (void)set_instrument(p, InstrumentBlock{});
  (void)add_face(p, "Note1", kNote);
  (void)add_node(p, "SineSource", "osc", 2, kOsc);
  (void)add_node(p, "SineSource", "lfo", 3, kLfo);
  (void)add_node(p, "SoundChannel", "ch", 4, kChannel);
  (void)connect(p, "osc", "frequency", face_field(p, "Note1", "frequency"), false);
  (void)set_output(p, "osc");
  p.layout.outputPos = kOutput;
  return p;
}

struct Bench {
  Session s;
  HeadlessEditor e;
  Bench() : e(start(s)) { e.frames(3); }
  static Session& start(Session& s) {
    s.open = OpenPatch{listen_patch(), {}, History{}};
    sound_follows(s);
    return s;
  }
  Vec2 grip(Vec2 node) const { return e.canvas_origin() + canvas_panning(s.open.patch) + node + kGrip; }
};

bool sounds(Session& s) {
  std::vector<float> out(2 * 512);
  bool any = false;
  for (int k = 0; k < 20; ++k) {
    s.audio.fill(out.data(), 512);
    any = any || std::any_of(out.begin(), out.end(), [](float x) { return x != 0.0f; });
  }
  return any;
}

}  // namespace

void run_listen_menu_tests() {
  std::printf("listen: from a node's menu; the keys then sound it; stop; none on a Note; a part refused\n");
  Bench b;
  b.e.right_click_at(b.grip(kLfo));
  REQUIRE(b.e.click_on("menu:Listen here"));
  CHECK(b.s.listening == "lfo" && heard_patch(b.s).graph.output == "lfo");
  CHECK(b.e.area("mark:lfo").has_value() && !b.e.area("mark:osc") && !b.e.area("mark:ch"));   // the mark on it alone
  b.e.press_key("Q");
  CHECK(sounds(b.s));
  b.e.right_click_at(b.grip(kLfo));
  CHECK(!b.e.area("menu:Listen here"));
  REQUIRE(b.e.click_on("menu:Stop listening here"));
  CHECK(b.s.listening.empty() && heard_patch(b.s).graph.output == "osc");
  b.e.frames(1);
  CHECK(!b.e.area("mark:lfo"));
  b.e.right_click_at(b.grip(kNote));
  CHECK(b.e.popup_open() && b.e.area("menu:Delete").has_value());
  CHECK(!b.e.area("menu:Listen here") && !b.e.area("menu:Stop listening here"));
  b.e.click_at(b.e.canvas_origin() + Vec2{600, 300});
  b.e.right_click_at(b.grip(kChannel));
  REQUIRE(b.e.click_on("menu:Listen here"));
  CHECK(b.s.listening.empty() && b.s.status.find("a part, not a signal") != std::string::npos);
}

void run_restart_menu_tests() {
  std::printf("restart: Edit > Restart audio and Ctrl+Shift+A say so; not while a text field has the keys\n");
  Bench b;
  const std::string said = "Audio not restarted (by hand): no sound device was ever opened";
  REQUIRE(b.e.click_on("menu:Edit"));
  REQUIRE(b.e.click_on("menu:Restart audio"));
  CHECK(b.s.status == said);
  b.s.status.clear();
  b.e.press_key("A", true, true);
  CHECK(b.s.status == said);
  REQUIRE(b.e.click("Drums"));
  REQUIRE(b.e.click_on("drum pattern"));
  b.s.status.clear();
  b.e.press_key("A", true, true);
  CHECK(b.s.status != said);
}

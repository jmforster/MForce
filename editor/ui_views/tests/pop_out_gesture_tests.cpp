// A pop-out's own gestures under the mouse and keys: its keys (one job per
// key holds over it), a left drag, Ctrl and the wheel, its zoom-out arrow,
// Fit and scroll slider; a number typed into the pane's Columns field; and
// closing the first of two pop-outs leaves the second's window where it was.
#include "mforce/testing/check.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/headless.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kOboe = "patches/library/winds/oboe1.json";

struct Bench {
  Session s;
  HeadlessEditor e;
  Bench() : e(start(s)) { e.frames(3); }
  static Session& start(Session& s) {
    open_file(s, kOboe);
    generate(s);
    return s;
  }
  int total() const { return int(s.take.mono.size()); }
  int plot() const { return int(e.area("popout 1:strip")->hi.x - e.area("popout 1:strip")->lo.x); }
  Vec2 centre() const { return e.area("popout 1:strip")->centre(); }
  WaveView& view() { return s.popOuts[0].view; }
  // The Output popped out and zoomed in four times, the mouse over it.
  void pop_out() {
    REQUIRE(e.click_on("arrow:Output"));
    e.frames(2);
    for (int k = 0; k < 4; ++k) REQUIRE(e.click_on("popout 1:zoom in"));
    e.hover(centre());
  }
};

bool near(double a, double b, double within) { return std::fabs(a - b) <= within; }

}  // namespace

void run_pop_out_key_tests() {
  std::printf("pop-out: its keys over it, with no note and no change of the note's length\n");
  Bench b;
  b.pop_out();
  const WaveView pane = b.s.waves;
  const int zoom = b.view().zoom, page = b.plot() * zoom;
  const float seconds = b.s.keyboard.seconds;
  b.s.lastNote = LastNote{};
  b.e.press_key("Right");
  CHECK(near(b.view().scroll, page / 10, 2) && b.s.keyboard.seconds == seconds);
  b.e.press_key("Right", true);
  CHECK(near(b.view().scroll, page / 10 + page, 2));
  b.e.press_key("End");
  CHECK(b.view().scroll == max_scroll(b.view(), b.total(), b.plot()));
  b.e.press_key("Home");
  CHECK(b.view().scroll == 0);
  b.e.press_key("=");
  CHECK(b.view().zoom == zoom / 2);
  b.e.press_key("-");
  CHECK(b.view().zoom == (zoom / 2) * 2);
  b.e.press_key("Keypad+");
  CHECK(b.view().zoom == zoom / 2);
  b.e.press_key("Keypad-");
  CHECK(b.view().zoom == (zoom / 2) * 2);
  b.e.press_key("0");
  CHECK(b.view().zoom == b.total() / b.plot() && b.view().scroll == 0);
  CHECK(b.s.lastNote.note == -1 && held_notes(b.s).empty() && b.s.keyboard.seconds == seconds);
  CHECK(b.s.waves.zoom == pane.zoom && b.s.waves.scroll == pane.scroll);
}

void run_pop_out_mouse_tests() {
  std::printf("pop-out: a left drag, Ctrl and the wheel, the zoom-out arrow, Fit, the scroll slider\n");
  Bench b;
  b.pop_out();
  const int zoom = b.view().zoom, before = b.view().scroll;
  b.e.drag(b.centre(), Vec2{b.centre().x - 100, b.centre().y});
  CHECK(b.view().scroll == before + 100 * zoom);
  b.e.wheel_at(b.centre(), 1.0f, true);
  CHECK(b.view().zoom == zoom / 2);
  b.e.wheel_at(b.centre(), -1.0f, true);
  CHECK(b.view().zoom == (zoom / 2) * 2);
  const int now = b.view().zoom;
  REQUIRE(b.e.click_on("popout 1:zoom out"));
  CHECK(b.view().zoom == now * 2);
  REQUIRE(b.e.click_on("popout 1:zoom in"));
  const auto slider = b.e.area("popout 1:scroll");
  REQUIRE(slider.has_value());
  b.e.drag(Vec2{slider->lo.x + 4, slider->centre().y}, Vec2{slider->hi.x + 40, slider->centre().y});
  CHECK(b.view().scroll == max_scroll(b.view(), b.total(), b.plot()));
  REQUIRE(b.e.click_on("popout 1:Fit"));
  CHECK(b.view().zoom == b.total() / b.plot() && b.view().scroll == 0);
}

void run_columns_typed_tests() {
  std::printf("strips: a number typed into the Columns field\n");
  Bench b;
  REQUIRE(b.e.click_on("columns"));
  b.e.press_key("A", false, true);                                          // all of the field's text
  b.e.type_text("3");
  b.e.press_key("Enter");
  CHECK(b.s.waves.columns == 3);
}

void run_pop_out_window_kept_tests() {
  std::printf("pop-out: closing the first of two leaves the second's window where it was\n");
  Bench b;
  REQUIRE(b.e.click_on("arrow:Output"));
  REQUIRE(b.e.click_on("arrow:Drive_env"));
  b.e.frames(2);
  const auto before = b.e.area("popout 2:close");
  REQUIRE(before.has_value() && b.e.click_on("popout 1:close"));
  b.e.frames(2);
  const auto after = b.e.area("popout 2:close");
  REQUIRE(after.has_value());
  CHECK(after->lo.x == before->lo.x && after->lo.y == before->lo.y && !b.e.area("popout 1:strip"));
}

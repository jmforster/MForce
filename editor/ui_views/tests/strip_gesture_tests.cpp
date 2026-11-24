// The strips under the headless editor's mouse and keys: the toolbar, the
// wheel, Ctrl and the wheel, a left drag, the strips' keys (and the same keys
// over the canvas, the keyboard's and the canvas's), a strip's arrow and its
// pop-out, and closing one of two pop-outs.
#include "mforce/testing/check.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_views/headless.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kOboe = "patches/library/winds/oboe1.json";

// oboe1 rendered, drawn until the strips have their first fit.
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
  // One strip's plot width, as drawn: the Output strip's plot area.
  float plot() const { return e.area("strip:Output")->hi.x - e.area("strip:Output")->lo.x; }
  Vec2 strip_centre() const { return e.area("strip:Output")->centre(); }
};

Vec2 operator+(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }

bool near(double a, double b, double within) { return std::fabs(a - b) <= within; }

}  // namespace

void run_strip_toolbar_tests() {
  std::printf("strips: the toolbar's zoom arrows, Fit, Columns, Envelopes and the scroll slider\n");
  Bench b;
  REQUIRE(b.s.waves.fitted);
  const int fitted = b.s.waves.zoom;
  CHECK(b.e.click_on("zoom in") && b.s.waves.zoom == fitted / 2);
  CHECK(b.e.click_on("zoom out") && b.s.waves.zoom == (fitted / 2) * 2);
  CHECK(b.e.click_on("zoom in") && b.e.click_on("zoom in") && b.e.click_on("Fit") && b.s.waves.zoom == fitted);
  CHECK(b.e.click_on("columns +") && b.s.waves.columns == 2);
  CHECK(b.e.click_on("columns -") && b.s.waves.columns == 1);
  CHECK(b.e.click_on("Envelopes") && !b.s.waves.envelopes && !b.e.area("strip:Ampl_env"));
  CHECK(b.e.click_on("Envelopes") && b.s.waves.envelopes && b.e.area("strip:Ampl_env").has_value());
  for (int k = 0; k < 4; ++k) REQUIRE(b.e.click_on("zoom in"));
  const auto slider = b.e.area("scroll");
  REQUIRE(slider.has_value() && b.s.waves.scroll == 0);
  b.e.drag(Vec2{slider->lo.x + 4, slider->centre().y}, Vec2{slider->hi.x + 40, slider->centre().y});
  CHECK(b.s.waves.scroll > 0 && b.s.waves.scroll == max_scroll(b.s.waves, b.total(), int(b.plot())));
}

void run_strip_wheel_and_drag_tests() {
  std::printf("strips: the wheel scrolls, Ctrl and the wheel zoom, a left drag scrolls\n");
  Bench b;
  for (int k = 0; k < 4; ++k) REQUIRE(b.e.click_on("zoom in"));
  const int zoom = b.s.waves.zoom;
  b.e.wheel_at(b.strip_centre(), -1.0f);
  const double tenth = b.plot() * zoom * 0.1;
  CHECK(near(b.s.waves.scroll, tenth, zoom * 1.5));                        // a notch toward the hand: a tenth of the plot on
  b.e.wheel_at(b.strip_centre(), 0.5f);
  CHECK(near(b.s.waves.scroll, tenth * 0.5, zoom * 1.5));                  // half a notch away: half of it back
  b.e.wheel_at(b.strip_centre(), 1.0f, true);
  CHECK(b.s.waves.zoom == zoom / 2);
  b.e.wheel_at(b.strip_centre(), -1.0f, true);
  CHECK(b.s.waves.zoom == (zoom / 2) * 2);
  const int before = b.s.waves.scroll, now = b.s.waves.zoom;
  b.e.drag(b.strip_centre(), b.strip_centre() + Vec2{-100, 0});
  CHECK(b.s.waves.scroll == before + 100 * now);                          // the samples follow the mouse left
  if (b.s.waves.scroll != before + 100 * now) std::printf("  drag: %d -> %d at zoom %d\n", before, b.s.waves.scroll, now);
}

// Over the strips their keys are theirs: no note, no change of the note's
// length, no pan of the canvas. Over the canvas they are the keyboard's.
void run_strip_key_tests() {
  std::printf("strips: their keys over them; the keyboard's and the canvas's elsewhere\n");
  Bench b;
  for (int k = 0; k < 4; ++k) REQUIRE(b.e.click_on("zoom in"));
  const int zoom = b.s.waves.zoom, page = int(b.plot()) * zoom;
  const float seconds = b.s.keyboard.seconds;
  const Vec2 panning = canvas_panning(b.s.open.patch);
  b.e.hover(b.strip_centre());
  b.s.lastNote = LastNote{};
  b.e.press_key("Right");
  CHECK(near(b.s.waves.scroll, page / 10, 2));
  b.e.press_key("Right", true);
  CHECK(near(b.s.waves.scroll, page / 10 + page, 2));
  b.e.press_key("Left", true);
  CHECK(near(b.s.waves.scroll, page / 10, 2) && canvas_panning(b.s.open.patch) == panning);
  b.e.press_key("End");
  CHECK(b.s.waves.scroll == max_scroll(b.s.waves, b.total(), int(b.plot())));
  b.e.press_key("Home");
  CHECK(b.s.waves.scroll == 0);
  b.e.press_key("=");
  CHECK(b.s.waves.zoom == zoom / 2);
  b.e.press_key("-");
  CHECK(b.s.waves.zoom == (zoom / 2) * 2);
  b.e.press_key("Keypad+");
  CHECK(b.s.waves.zoom == zoom / 2);
  b.e.press_key("Keypad-");
  CHECK(b.s.waves.zoom == (zoom / 2) * 2);
  b.e.press_key("0");
  CHECK(b.s.waves.zoom > zoom && b.s.waves.scroll == 0);
  CHECK(b.s.lastNote.note == -1 && held_notes(b.s).empty() && b.s.keyboard.seconds == seconds);
  const int fitted = b.s.waves.zoom;
  b.e.hover(b.e.canvas_origin() + Vec2{200, 150});
  b.e.press_key("=");
  CHECK(b.s.lastNote.note >= 0 && b.s.waves.zoom == fitted);               // a note, not a zoom
  b.e.press_key("Right");
  CHECK(b.s.keyboard.seconds == seconds * 2.0f && b.s.waves.scroll == 0);
  b.e.press_key("Left", true);
  CHECK(!(canvas_panning(b.s.open.patch) == panning));                    // Shift and Left pan the canvas here
}

// A strip's arrow pops it out; the wheel over the pop-out moves its view and
// not the pane's; of two, closing the first leaves the second as it was.
void run_pop_out_tests() {
  std::printf("strips: an arrow pops a strip out; its own view; closing one of two\n");
  Bench b;
  REQUIRE(b.e.click_on("arrow:Drive_env"));
  REQUIRE(b.s.popOuts.size() == 1 && b.s.popOuts[0].node == "Drive_env" && b.s.popOuts[0].number == 1);
  REQUIRE(b.e.area("popout 1:strip").has_value());
  const WaveView pane = b.s.waves;
  b.e.wheel_at(b.e.area("popout 1:strip")->centre(), -1.0f);
  CHECK(b.s.popOuts[0].view.scroll > 0 && b.s.waves.scroll == pane.scroll && b.s.waves.zoom == pane.zoom);
  REQUIRE(b.e.click_on("popout 1:zoom in"));
  CHECK(b.s.popOuts[0].view.zoom == pane.zoom / 2 && b.s.waves.zoom == pane.zoom);
  REQUIRE(b.e.click_on("arrow:Output"));
  REQUIRE(b.s.popOuts.size() == 2 && b.s.popOuts[1].node.empty() && b.s.popOuts[1].number == 2);
  b.e.frames(1);
  const WaveView second = b.s.popOuts[1].view;
  REQUIRE(b.e.click_on("popout 1:close"));
  REQUIRE(b.s.popOuts.size() == 1);
  CHECK(b.s.popOuts[0].number == 2 && b.s.popOuts[0].view.zoom == second.zoom && b.e.area("popout 2:strip").has_value());
  CHECK(!b.e.area("popout 1:strip"));
}

// The strips' scroll stays inside the take, from the mouse and keys: after
// the zoom-out arrow, after fewer columns, after a shorter take, in the pane
// and in a pop-out; and Generate again keeps the zoom and the scroll.
#include "mforce/testing/check.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/headless.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

const std::string kOboe = "patches/library/winds/oboe1.json";

// oboe1 opened, a Generate clicked, drawn until the strips have their first fit.
struct Bench {
  Session s;
  HeadlessEditor e;
  Bench() : e(start(s)) {
    e.frames(3);
    generate_by_hand();
  }
  static Session& start(Session& s) {
    open_file(s, kOboe);
    return s;
  }
  void generate_by_hand() {
    REQUIRE(e.click("Generate"));
    e.frames(3);
  }
  int total() const { return int(s.take.mono.size()); }
  int plot(const std::string& strip) const { return int(e.area(strip)->hi.x - e.area(strip)->lo.x); }
  // The scroll is inside the take and the strip's first column holds samples.
  bool inside(const WaveView& v, const Strip& strip, const std::string& area) const {
    ExtentCache cache;
    std::vector<Extent> into(size_t(plot(area)));
    return v.scroll <= max_scroll(v, total(), plot(area)) && extents(strip, s.takes, v.zoom, v.scroll, into, cache) > 0;
  }
  bool pane_inside() const { return inside(s.waves, strip_of(s, ""), "strip:Output"); }
  void end_of(const std::string& strip) {
    e.hover(e.area(strip)->centre());
    e.press_key("End");
  }
};

}  // namespace

void run_strip_limit_tests() {
  std::printf("strips: the scroll stays inside the take after zoom out, fewer columns, a shorter take\n");
  Bench b;
  for (int k = 0; k < 4; ++k) REQUIRE(b.e.click_on("zoom in"));
  b.end_of("strip:Output");
  REQUIRE(b.s.waves.scroll > 0);
  REQUIRE(b.e.click_on("zoom out"));
  b.e.frames(2);
  CHECK(b.pane_inside());
  for (int k = 0; k < 3; ++k) REQUIRE(b.e.click_on("columns +"));
  b.end_of("strip:Output");
  for (int k = 0; k < 3; ++k) REQUIRE(b.e.click_on("columns -"));
  b.e.frames(2);
  CHECK(b.s.waves.columns == 1 && b.pane_inside());
  b.end_of("strip:Output");
  set_note_length(b.s, 0.1f);
  b.generate_by_hand();
  CHECK(b.total() < 48000 && b.pane_inside());
}

void run_pop_out_limit_tests() {
  std::printf("strips: a pop-out's scroll stays inside a shorter take\n");
  Bench b;
  REQUIRE(b.e.click_on("arrow:Output"));
  b.e.frames(2);
  for (int k = 0; k < 4; ++k) REQUIRE(b.e.click_on("popout 1:zoom in"));
  b.end_of("popout 1:strip");
  REQUIRE(b.s.popOuts[0].view.scroll > 0);
  set_note_length(b.s, 0.1f);
  b.generate_by_hand();
  CHECK(b.inside(b.s.popOuts[0].view, strip_of(b.s, ""), "popout 1:strip"));
}

// Generate again on the same note keeps the zoom and the scroll; a shorter
// note brings the scroll back inside.
void run_generate_again_tests() {
  std::printf("strips: Generate again keeps the zoom and scroll; a shorter take brings the scroll inside\n");
  Bench b;
  for (int k = 0; k < 2; ++k) REQUIRE(b.e.click_on("zoom in"));
  b.e.hover(b.e.area("strip:Output")->centre());
  b.e.press_key("Right");
  const WaveView before = b.s.waves;
  REQUIRE(before.scroll > 0);
  b.generate_by_hand();
  CHECK(b.s.waves.zoom == before.zoom && b.s.waves.scroll == before.scroll);
  b.end_of("strip:Output");
  set_note_length(b.s, 0.1f);
  b.generate_by_hand();
  CHECK(b.s.waves.zoom == before.zoom && b.pane_inside());
}

// Wormholes by the mouse: a right click on a wire routes it through a pair,
// a right click on a wire that runs through one says why not, a half
// selected shows the span's ghost, and a double-click jumps to the twin.
#include "gesture_bench.h"
#include "mforce/ui_control/grouping.h"

using namespace gesture_bench;

namespace {

// A sine into a filter, the filter the output, far enough apart for a pair
// of Wormholes between them.
Patch span_patch() {
  Patch p;
  (void)set_instrument(p, InstrumentBlock{});
  (void)add_node(p, "SineSource", "osc", 2, Vec2{40, 40});
  (void)add_node(p, "SVFSource", "filt", 3, Vec2{700, 40});
  (void)connect(p, "filt", "source", "osc", false);
  (void)set_output(p, "filt");
  p.layout.outputPos = Vec2{900, 40};
  return p;
}

// The middle of the wire between two pins: a link's curve passes through
// the middle of its two ends.
Vec2 wire_between(const Bench& b, const std::string& from, const std::string& to) {
  const auto a = b.editor.area(from), z = b.editor.area(to);
  if (!a || !z) return Vec2{-1, -1};
  return Vec2{(a->centre().x + z->centre().x) * 0.5f, (a->centre().y + z->centre().y) * 0.5f};
}

void run_route_and_ghost() {
  Bench b(span_patch());
  const Patch before = b.s.open.patch;
  b.editor.right_click_at(wire_between(b, "pin:osc.out", "pin:filt.source"));
  CHECK(b.s.status == "wormhole pair linked — span hidden; hover or select either half to see it");
  CHECK(node_of(b.s, "filt")->wires.at("source") == (std::vector<Wire>{Wire{"Wormhole2"}}) && holds(b.s));
  CHECK(b.drawn("node:Wormhole") && b.drawn("node:Wormhole2") && !b.drawn("pin:Wormhole.tap"));
  b.editor.settle();
  b.editor.right_click_at(wire_between(b, "pin:Wormhole2.out", "pin:filt.source"));
  CHECK(b.s.status == "the wire runs through a wormhole already: it is not routed again");
  CHECK(!b.drawn("ghost:Wormhole"));
  b.editor.click_at(b.grip("node:Wormhole"));
  CHECK(b.s.selections == (Names{"Wormhole"}) && b.drawn("ghost:Wormhole"));
  b.editor.settle();
  b.editor.click_at(b.empty());
  CHECK(!b.drawn("ghost:Wormhole"));
  b.editor.press_key("Z", false, true);
  CHECK(b.s.open.patch == before && holds(b.s));
}

void run_jump() {
  Bench b(span_patch());
  b.editor.right_click_at(wire_between(b, "pin:osc.out", "pin:filt.source"));
  REQUIRE(node_of(b.s, "Wormhole2") != nullptr);
  select_nodes(b.s, {"Wormhole2", "filt"});
  group_selection(b.s);                                       // the far half inside a group
  b.editor.settle();
  REQUIRE(b.drawn("node:Wormhole"));
  b.editor.double_click_at(b.grip("node:Wormhole"));
  b.editor.frames(2);
  CHECK(b.s.groupPath == (Names{"Group"}) && b.s.selections == (Names{"Wormhole2"}) && holds(b.s));
  CHECK(b.drawn("node:Wormhole2") && b.drawn("ghost:Wormhole2") == false);   // the near half is not shown here
}

}  // namespace

void run_wormhole_gesture_tests() {
  std::printf("wormholes by the mouse: a wire routed, refused, the ghost, the jump\n");
  run_route_and_ghost();
  run_jump();
}

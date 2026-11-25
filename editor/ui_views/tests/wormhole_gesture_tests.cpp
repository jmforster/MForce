// Wormholes by the mouse: a right click on a wire routes it through a pair,
// a right click on a wire that runs through one says why not, a half
// selected or under the mouse shows the span's ghost and the tooltip names
// the twin, a double-click jumps to the twin and centres it, and a wire made
// by hand between two Wormholes pairs them.
#include "gesture_bench.h"
#include "mforce/ui_control/grouping.h"
#include <cmath>

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
  const auto twin = b.editor.area("node:Wormhole2");       // its place at the canvas's middle
  REQUIRE(twin.has_value());
  const Vec2 middle{b.editor.canvas_origin().x + b.editor.canvas_size().x * 0.5f,
                    b.editor.canvas_origin().y + b.editor.canvas_size().y * 0.5f};
  CHECK(std::fabs(twin->lo.x - middle.x) < 4.0f && std::fabs(twin->lo.y - middle.y) < 4.0f);
}

// The mouse over each half of a pair: the ghost of the span and the tooltip
// naming the twin; over an unpaired Wormhole, the words that say so.
void run_hover() {
  Patch p = span_patch();
  (void)add_node(p, "Wormhole", "Lone", 5, Vec2{40, 330});
  Bench b(p);
  b.editor.right_click_at(wire_between(b, "pin:osc.out", "pin:filt.source"));
  b.editor.settle();
  REQUIRE(b.editor.hover_on("node:Wormhole"));
  CHECK(b.editor.canvas_tooltip() == "twin: Wormhole2 — in main\ndouble-click: jump" && b.drawn("ghost:Wormhole"));
  REQUIRE(b.editor.hover_on("node:Wormhole2"));
  CHECK(b.editor.canvas_tooltip() == "twin: Wormhole — in main\ndouble-click: jump" && b.drawn("ghost:Wormhole2"));
  CHECK(!b.drawn("ghost:Wormhole"));                         // the mouse left it, and it is not selected
  REQUIRE(b.editor.hover_on("node:Lone"));
  CHECK(b.editor.canvas_tooltip() == "unpaired wormhole — wire it to another Wormhole to open the far end");
  CHECK(!b.drawn("ghost:Lone"));
  b.editor.hover(b.empty());
  CHECK(b.editor.canvas_tooltip().empty() && holds(b.s));
}

// Two Wormholes paired by hand: a wire dragged from one's output to the
// other's input; the pairing's words, and the span between them not drawn.
void run_pair_by_hand() {
  Patch p = span_patch();
  (void)add_node(p, "Wormhole", "WA", 5, Vec2{40, 330});
  (void)add_node(p, "Wormhole", "WB", 6, Vec2{500, 330});
  Bench b(p);
  const auto out = b.editor.area("pin:WA.out"), in = b.editor.area("pin:WB.source");
  REQUIRE(out && in);
  b.editor.drag(out->centre(), in->centre());
  CHECK(node_of(b.s, "WB")->wires.at("source") == (std::vector<Wire>{Wire{"WA"}}));
  CHECK(b.s.status == "wormhole pair linked — span hidden; hover or select either half to see it" && holds(b.s));
  CanvasNames names;
  bool spanDrawn = false;
  for (const auto& l : canvas_table(b.s.open.patch, b.s.groupPath, b.s.offered, names).links)
    spanDrawn = spanDrawn || l.reader.node == "WB";
  CHECK(!spanDrawn);
}

}  // namespace

void run_wormhole_gesture_tests() {
  std::printf("wormholes by the mouse: a wire routed, refused, the ghost, the jump\n");
  run_route_and_ghost();
  run_jump();
  run_hover();
  run_pair_by_hand();
}

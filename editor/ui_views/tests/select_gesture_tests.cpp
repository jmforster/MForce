// Selecting on the canvas, by the mouse: a click, Ctrl and a click on a node
// and on a pin, Ctrl and a drag out of a pin (a wire, no toggle), a box, a
// drag of all that is selected, and a right click.
#include "gesture_bench.h"
#include <set>

using namespace gesture_bench;

namespace {

std::set<std::string> as_set(const Names& names) { return std::set<std::string>(names.begin(), names.end()); }

void run_click_and_ctrl() {
  Bench b;
  b.editor.click_at(b.grip("node:osc"));
  CHECK(b.s.selections == (Names{"osc"}));
  b.editor.ctrl_click_at(b.grip("node:lfo"));
  CHECK(as_set(b.s.selections) == (std::set<std::string>{"osc", "lfo"}));
  b.editor.settle();
  b.editor.ctrl_click_at(b.grip("node:osc"));                  // a second Ctrl and a click takes it out
  CHECK(b.s.selections == (Names{"lfo"}) && b.s.status == "deselected");
  CHECK(holds(b.s));
  b.editor.click_at(b.empty());
  CHECK(b.s.selections.empty() && holds(b.s));

  b.editor.settle();
  b.editor.ctrl_click_on("pin:lfo.phase");                     // on a pin: imnodes would start a wire there
  CHECK(b.s.selections == (Names{"lfo"}));
  b.editor.settle();
  b.editor.ctrl_click_on("pin:lfo.phase");
  CHECK(b.s.selections.empty() && b.s.status == "deselected" && holds(b.s));
  CHECK(node_of(b.s, "lfo")->wires.empty());                  // and no wire was made
}

// Ctrl and a drag out of a pin makes a wire and leaves the selection as it was.
void run_ctrl_drag_from_pin() {
  Bench b;
  b.editor.click_at(b.grip("node:osc"));
  b.editor.settle();
  const auto from = b.editor.area("pin:lfo.out"), amp = b.editor.area("pin:osc.amplitude");
  REQUIRE(from && amp);
  b.editor.drag(from->centre(), amp->centre(), true);
  CHECK(node_of(b.s, "osc")->wires.at("amplitude") == (std::vector<Wire>{Wire{"lfo"}}));
  CHECK(b.s.selections == (Names{"osc"}) && b.s.status != "deselected" && holds(b.s));
}

void run_box_and_drag() {
  Bench b;
  const auto osc = b.editor.area("node:osc");
  const auto lfo = b.editor.area("node:lfo");
  REQUIRE(osc && lfo);
  b.editor.drag(Vec2{osc->lo.x - 20.0f, osc->lo.y - 10.0f}, lfo->centre());   // from empty canvas over the two
  CHECK(as_set(b.s.selections) == (std::set<std::string>{"osc", "lfo"}) && holds(b.s));
  const Vec2 oscWas = b.s.open.patch.layout.positions.at("osc"), lfoWas = b.s.open.patch.layout.positions.at("lfo");
  const size_t steps = b.s.open.history.past.size();
  b.editor.settle();
  const Vec2 from = b.grip("node:osc");
  b.editor.drag(from, Vec2{from.x + 40.0f, from.y + 20.0f});
  const Vec2 oscNow = b.s.open.patch.layout.positions.at("osc"), lfoNow = b.s.open.patch.layout.positions.at("lfo");
  CHECK(oscNow.x - oscWas.x > 30.0f && lfoNow.x - lfoWas.x > 30.0f && oscNow.y - oscWas.y > 10.0f);
  CHECK(b.s.open.history.past.size() == steps + 1);          // one step for all of it
  CHECK(as_set(b.s.selections) == (std::set<std::string>{"osc", "lfo"}) && holds(b.s));
}

void run_right_click() {
  Bench b;
  b.editor.click_at(b.grip("node:osc"));
  b.editor.right_click_at(b.grip("node:lfo"));                // outside the selection: it alone
  CHECK(b.s.selections == (Names{"lfo"}) && b.editor.popup_open() && b.drawn("menu:Copy (1)"));
  b.editor.click_at(b.empty());
  b.editor.settle();
  b.editor.click_at(b.grip("node:osc"));
  b.editor.ctrl_click_at(b.grip("node:lfo"));
  b.editor.right_click_at(b.grip("node:lfo"));                // inside it: the selection stays
  CHECK(b.s.selections.size() == 2 && b.drawn("menu:Copy (2)") && b.drawn("menu:Group selection (2)"));
  CHECK(holds(b.s));
}

}  // namespace

void run_select_gesture_tests() {
  std::printf("selecting by the mouse: a click, Ctrl and a click on a node and a pin, a box, a drag, a right click\n");
  run_click_and_ctrl();
  run_ctrl_drag_from_pin();
  run_box_and_drag();
  run_right_click();
}

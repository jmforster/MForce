// Copy, cut and paste by the menus and the keys, with the headless editor's
// clipboard (a string), and Duplicate and Replace with from a node's menu.
#include "gesture_bench.h"
#include "mforce/ui_model/canvas_view.h"

using namespace gesture_bench;

namespace {

bool holds_fragment(Bench& b) { return b.editor.clipboard().text().find("\"mforceClip\":1") != std::string::npos; }

void run_copy_and_paste() {
  Bench b;
  b.editor.right_click_at(b.grip("node:lfo"));
  REQUIRE(b.editor.click_on("menu:Copy (1)"));
  CHECK(holds_fragment(b) && b.s.status == "Copied 1 node");
  b.editor.settle();
  b.editor.press_key("V", false, true);                       // Ctrl+V: wherever the mouse is
  CHECK(node_of(b.s, "lfo2") && b.s.selections == (Names{"lfo2"}) && holds(b.s));
  b.editor.press_key("V", false, true);                       // held keys do not repeat; a second press pastes again
  CHECK(node_of(b.s, "lfo3"));
  REQUIRE(b.editor.click_on("menu:Edit"));
  REQUIRE(b.editor.click_on("menu:Paste"));
  CHECK(node_of(b.s, "lfo4") && holds(b.s));

  b.editor.settle();
  b.editor.click_at(b.grip("node:osc"));
  b.editor.press_key("C", false, true);                       // Ctrl+C
  CHECK(b.editor.clipboard().text().find("\"osc\"") != std::string::npos);
  b.editor.settle();
  const Vec2 at = b.empty();
  b.editor.right_click_at(at);
  REQUIRE(b.editor.click_on("menu:Paste"));                   // from the create menu: where it was opened
  const Vec2 record{at.x - b.editor.canvas_origin().x - canvas_panning(b.s.open.patch).x,
                    at.y - b.editor.canvas_origin().y - canvas_panning(b.s.open.patch).y};
  const Vec2 landed = b.s.open.patch.layout.positions.at("osc2");
  CHECK(landed.x - record.x < 2.0f && record.x - landed.x < 2.0f && landed.y - record.y < 2.0f &&
        record.y - landed.y < 2.0f);
}

void run_cut_and_fields() {
  Bench b;
  const Patch before = b.s.open.patch;
  b.editor.click_at(b.grip("node:lfo"));
  b.editor.press_key("X", false, true);                       // Ctrl+X
  CHECK(!node_of(b.s, "lfo") && holds_fragment(b) && holds(b.s));
  b.editor.press_key("Z", false, true);
  CHECK(b.s.open.patch == before && b.s.selections.empty());   // what went is no longer selected
  b.editor.settle();
  b.editor.click_at(b.grip("node:lfo"));
  REQUIRE(b.editor.click_on("menu:Edit"));
  REQUIRE(b.editor.click_on("menu:Cut"));
  CHECK(!node_of(b.s, "lfo"));
  b.editor.press_key("Z", false, true);

  b.editor.settle();
  b.editor.click_at(b.grip("node:osc"));
  b.editor.clipboard().set_text("wobble");
  REQUIRE(b.editor.click_on("field:name"));                   // the name field has the keyboard
  const size_t nodes = b.s.open.patch.graph.nodes.size();
  b.editor.press_key("V", false, true);
  b.editor.press_key("Enter");
  CHECK(b.s.open.patch.graph.nodes.size() == nodes && node_of(b.s, "oscwobble") && holds(b.s));
}

void run_duplicate_and_replace() {
  Bench b;
  b.editor.right_click_at(b.grip("node:lfo"));
  REQUIRE(b.editor.click_on("menu:Duplicate"));
  const Vec2 was = b.s.open.patch.layout.positions.at("lfo");
  CHECK(node_of(b.s, "lfo2") && b.s.open.patch.layout.positions.at("lfo2") == (Vec2{was.x + 30, was.y + 30}));
  CHECK(b.s.selections == (Names{"lfo2"}) && holds(b.s));
  b.editor.settle();
  b.editor.right_click_at(b.grip("node:lfo"));
  CHECK(!b.drawn("menu:Duplicate") || b.drawn("menu:Replace with"));
  REQUIRE(b.editor.click_on("menu:Replace with"));
  REQUIRE(b.editor.click_on("menu:Var"));
  CHECK(!node_of(b.s, "lfo") && node_of(b.s, "Var") && b.s.selections == (Names{"Var"}) && holds(b.s));
  CHECK(b.s.status.rfind("Replaced lfo with Var", 0) == 0);
  b.editor.settle();
  b.editor.right_click_at(b.grip("node:Note1"));              // a Note: neither
  CHECK(b.editor.popup_open() && !b.drawn("menu:Duplicate") && !b.drawn("menu:Replace with"));
}

}  // namespace

void run_clipboard_gesture_tests() {
  std::printf("the clipboard by the menus and the keys; Duplicate and Replace with\n");
  run_copy_and_paste();
  run_cut_and_fields();
  run_duplicate_and_replace();
}

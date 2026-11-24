// The mouse on the canvas and on the piano, under the headless editor: a
// click selects, a drag moves, a right click opens the menu its place has and
// the menu's item acts, the wheel pans, a press on the piano plays until the
// button goes up.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_views/headless.h"
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

// Where the record places each thing, apart enough that a point on one is on
// no other; kEmpty is on none, and clear of the mini-map.
constexpr Vec2 kNote{40, 40}, kOsc{300, 40}, kLfo{300, 260}, kOutput{680, 40}, kEmpty{620, 330};
constexpr Vec2 kGrip{20, 10};          // a point of a node's title bar, from the node's corner

Vec2 operator+(Vec2 a, Vec2 b) { return Vec2{a.x + b.x, a.y + b.y}; }

// A note's frequency into osc, osc the output; lfo standing by.
Patch mouse_patch() {
  Patch p;
  (void)set_instrument(p, InstrumentBlock{});
  (void)add_face(p, "Note1", kNote);
  (void)add_node(p, "SineSource", "osc", 2, kOsc);
  (void)add_node(p, "SineSource", "lfo", 3, kLfo);
  (void)connect(p, "osc", "frequency", face_field(p, "Note1", "frequency"), false);
  (void)set_output(p, "osc");
  p.layout.outputPos = kOutput;
  return p;
}

// The session on mouse_patch, drawn until it is settled.
struct Bench {
  Session s;
  HeadlessEditor editor;
  Bench() : editor(start(s)) { editor.frames(3); }
  static Session& start(Session& s) {
    s.open = OpenPatch{mouse_patch(), {}, History{}};
    sound_follows(s);
    return s;
  }
  // The point on screen of a point of the record.
  Vec2 screen(Vec2 record) const { return editor.canvas_origin() + canvas_panning(s.open.patch) + record; }
  Vec2 grip(Vec2 node) const { return screen(node + kGrip); }
  bool has_node(const std::string& id) const {
    for (const auto& n : s.open.patch.graph.nodes)
      if (n.id == id) return true;
    return false;
  }
};

// Within a point and a half each way; a place the record does not hold is near nothing.
bool near(std::optional<Vec2> a, Vec2 b) {
  return a && a->x - b.x < 1.5f && b.x - a->x < 1.5f && a->y - b.y < 1.5f && b.y - a->y < 1.5f;
}

}  // namespace

// A click on a node or a face selects it; on the Output node or the empty
// canvas, nothing; no click moves anything.
void run_canvas_click_tests() {
  std::printf("the canvas: a click selects a node or a face, and nothing on the Output node or empty canvas\n");
  Bench b;
  const Patch before = b.s.open.patch;
  b.editor.click_at(b.grip(kOsc));
  CHECK(b.s.selected == "osc");
  b.editor.click_at(b.grip(kNote));
  CHECK(b.s.selected == "Note1");
  b.editor.click_at(b.grip(kOutput));
  CHECK(b.s.selected.empty());
  b.editor.click_at(b.grip(kLfo));
  CHECK(b.s.selected == "lfo");
  b.editor.click_at(b.screen(kEmpty));
  CHECK(b.s.selected.empty());
  CHECK(b.s.open.patch == before && !b.editor.popup_open());
}

// A drag of a node, a face or the Output node moves it by the drag, one step
// each; undo puts it back.
void run_canvas_drag_tests() {
  std::printf("the canvas: a drag moves a node, a face and the Output node, one step each\n");
  Bench b;
  const Vec2 by{120, 90};
  b.editor.drag(b.grip(kLfo), b.grip(kLfo + by));
  CHECK(near(b.s.open.patch.layout.positions.at("lfo"), kLfo + by));
  CHECK(b.s.selected == "lfo");
  b.editor.drag(b.grip(kOutput), b.grip(kOutput + by));
  CHECK(near(b.s.open.patch.layout.outputPos, kOutput + by));
  CHECK(b.s.selected.empty());
  const Vec2 down{0, 200};
  b.editor.drag(b.grip(kNote), b.grip(kNote + down));
  REQUIRE(!b.s.open.patch.layout.faces.empty());
  CHECK(near(b.s.open.patch.layout.faces.front().pos, kNote + down));
  undo(b.s);
  CHECK(near(b.s.open.patch.layout.faces.front().pos, kNote));
  undo(b.s);
  CHECK(near(b.s.open.patch.layout.outputPos, kOutput));
  undo(b.s);
  CHECK(near(b.s.open.patch.layout.positions.at("lfo"), kLfo));
  b.editor.frames(2);
  CHECK(near(b.s.open.patch.layout.positions.at("lfo"), kLfo));   // the canvas shows the record again and carries nothing back
}

// A right click on empty canvas opens the create menu, whose Note makes a
// face where the click was; on a node, its menu, whose Delete removes it; on
// the Output node, no menu. Each item is clicked where it was drawn.
void run_canvas_menu_tests() {
  std::printf("the canvas: a right click's menu on empty canvas, on a node and on the Output node\n");
  Bench b;
  b.editor.right_click_at(b.screen(kEmpty));
  REQUIRE(b.editor.popup_open());
  REQUIRE(b.editor.click_on("menu:Note"));
  CHECK(!b.editor.popup_open());
  REQUIRE(b.s.open.patch.layout.faces.size() == 2);
  CHECK(near(b.s.open.patch.layout.faces.back().pos, kEmpty));
  CHECK(b.s.selected == b.s.open.patch.layout.faces.back().label);

  b.editor.right_click_at(b.grip(kLfo));
  REQUIRE(b.editor.popup_open());
  REQUIRE(b.editor.click_on("menu:Delete"));
  CHECK(!b.editor.popup_open() && !b.has_node("lfo") && b.has_node("osc"));

  b.editor.right_click_at(b.grip(kOutput));
  CHECK(!b.editor.popup_open());
}

// The mini-map is not the canvas: a click on it leaves the selection and
// moves the view; a right click on it opens no menu.
void run_canvas_minimap_tests() {
  std::printf("the canvas: a click on the mini-map moves the view and selects nothing new\n");
  Bench b;
  const Vec2 corner{20, 20};
  const Vec2 size = b.editor.canvas_size();
  const Vec2 map{b.editor.canvas_origin().x + size.x - corner.x, b.editor.canvas_origin().y + size.y - corner.y};
  b.editor.click_at(b.grip(kOsc));
  REQUIRE(b.s.selected == "osc");
  const Vec2 was = canvas_panning(b.s.open.patch);
  b.editor.click_at(map);
  CHECK(b.s.selected == "osc");
  CHECK(!(canvas_panning(b.s.open.patch) == was));
  b.editor.right_click_at(map);
  CHECK(!b.editor.popup_open());
}

// The wheel over the canvas pans it, 40 a notch; over the piano it does not.
void run_canvas_wheel_tests() {
  std::printf("the canvas: the wheel pans it, and only over it\n");
  Bench b;
  const Vec2 was = canvas_panning(b.s.open.patch);
  const Vec2 kNotch{0, 40};
  b.editor.wheel_at(b.screen(kEmpty), 1.0f);
  CHECK(canvas_panning(b.s.open.patch) == was + kNotch);
  REQUIRE(b.editor.click_on("tab:Keyboard"));
  b.editor.wheel_at(b.editor.piano_origin() + Vec2{5, 60}, 1.0f);
  CHECK(canvas_panning(b.s.open.patch) == was + kNotch);
}

// A press on the piano's first key plays the keyboard octave's C, louder
// further down the key, until the button goes up; with Live off the key up
// leaves the struck note to ring.
void run_piano_mouse_tests() {
  std::printf("the piano: a press plays the key under the mouse until the button goes up\n");
  Bench b;
  const int c = b.s.keyboard.octave * 12;
  REQUIRE(b.editor.click_on("tab:Keyboard"));                // the piano is the pane's Keyboard tab
  b.editor.mouse_down_at(b.editor.piano_origin() + Vec2{5, 60});
  CHECK(held_notes(b.s) == std::vector<int>{c});
  CHECK(b.s.lastNote.note == c && b.s.lastNote.velocity > 0.8f);
  b.editor.frames(3);
  CHECK(held_notes(b.s) == std::vector<int>{c});
  b.editor.mouse_up();
  CHECK(held_notes(b.s).empty());
  b.editor.mouse_down_at(b.editor.piano_origin() + Vec2{5, 5});
  CHECK(b.s.lastNote.note == c && b.s.lastNote.velocity < 0.3f);
  b.editor.mouse_up();
  CHECK(held_notes(b.s).empty());
}

// A wire selected on the canvas, the patch then changed under it: the
// selection goes, and the Delete key removes no wire nobody chose (the
// review's sequence).
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/wiring.h"
#include "mforce/ui_views/canvas_table.h"
#include "mforce/ui_model/canvas_view.h"
#include "mforce/ui_views/create_menu.h"
#include "mforce/ui_views/headless.h"
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

// Three sines, two feeding the list pin `source` of a MultiSource l, a third
// standing by; a note's frequency into the output osc.
Patch list_patch() {
  Patch p;
  (void)set_instrument(p, InstrumentBlock{});
  (void)add_face(p, "Note1", Vec2{40, 400});
  const std::string pitch = face_field(p, "Note1", "frequency");
  (void)add_node(p, "SineSource", "osc", 2, Vec2{400, 60});
  (void)add_node(p, "SineSource", "lfo", 3, Vec2{400, 260});
  (void)add_node(p, "SineSource", "lfo2", 5, Vec2{400, 460});
  (void)add_node(p, "SineSource", "lfo3", 6, Vec2{400, 660});
  (void)add_node(p, "MultiSource", "l", 4, Vec2{700, 300});
  (void)connect(p, "osc", "frequency", pitch, false);
  (void)connect(p, "l", "source", "lfo", false);
  (void)connect(p, "l", "source", "lfo2", false);
  (void)set_output(p, "osc");
  return p;
}

std::vector<Wire> feeds_of(const Session& s) {
  for (const auto& n : s.open.patch.graph.nodes)
    if (n.id == "l") return n.wires.at("source");
  return {};
}

}  // namespace

void run_canvas_selection_tests() {
  Session s;
  s.open = OpenPatch{list_patch(), {}, History{}};
  sound_follows(s);
  HeadlessEditor editor(s);
  editor.frames(3);
  editor.click_canvas_corner();                           // the canvas takes the keyboard
  CanvasNames names;
  (void)canvas_table(s.open.patch, s.groupPath, s.offered, names);     // the same names the canvas gave
  int second = -1;
  for (const auto& l : canvas_table(s.open.patch, s.groupPath, s.offered, names).links)
    if (l.reader.node == "l" && l.feed == 1) second = l.id;
  REQUIRE(second > 0);
  editor.select_link(second);
  editor.frames(2);
  CHECK(editor.selected_links() == 1);                    // the link stays selected while nothing changes
  delete_these(s, {}, {"lfo"});                           // the first feed's node: the node menu's Delete
  editor.frames(2);
  CHECK(editor.selected_links() == 0);
  wire(s, PinEnd{PinEnd::Kind::Output, "lfo3", {}}, PinEnd{PinEnd::Kind::Input, "l", "source"});
  editor.frames(2);
  CHECK(editor.selected_links() == 0);
  const std::vector<Wire> before = feeds_of(s);
  REQUIRE(before.size() == 2);
  editor.press_delete();
  CHECK(feeds_of(s) == before);
}

// F centres the graph with the mouse nowhere near the canvas.
void run_fit_key_tests() {
  Session s;
  s.open = OpenPatch{list_patch(), {}, History{}};
  s.open.patch.layout.panning = Vec2{5000, 5000};
  HeadlessEditor editor(s);
  editor.frames(3);
  editor.press_f();
  CHECK(s.open.patch.layout.panning == centred_panning(s.open.patch, editor.canvas_size()));
}

// The Note greyed on a plain sound; any other item, and the Note on an
// instrument, can be chosen.
void run_create_item_tests() {
  const CreateMenu menu = create_menu();
  Patch instrument = list_patch();
  Patch plain;
  CHECK(can_create(menu.favourites[0], instrument) && !can_create(menu.favourites[0], plain));
  CHECK(can_create(menu.favourites[1], plain) && can_create(menu.favourites[1], instrument));
}

// What a setting's pin on the canvas says when hovered.
void run_pin_tooltip_tests() {
  Patch p = list_patch();
  (void)add_node(p, "Envelope", "env", 7, Vec2{100, 100});
  (void)feed_setting(p, "env", "timeScale", "lfo3");
  CanvasNames names;
  std::set<std::pair<std::string, std::string>> offered;
  const CanvasTable t = canvas_table(p, {}, offered, names);
  bool seen = false;
  for (const auto& n : t.nodes)
    for (const auto& pin : n.inputs) {
      if (pin.look != CanvasPin::Look::Setting) {
        CHECK(pin_tooltip(pin).empty());
        continue;
      }
      seen = true;
      CHECK(pin.driver == "lfo3");
      CHECK(pin_tooltip(pin).find("set once at note-on by 'lfo3'") != std::string::npos);
    }
  CHECK(seen);
  CanvasPin unwired{0, PinEnd{PinEnd::Kind::SettingPin, "l", "x"}, CanvasPin::Look::Setting, "x", false, {}};
  CHECK(pin_tooltip(unwired).find("Dynamic pin (unwired)") == 0);
}

// The one way the control changes the patch (a step), the sound following
// the patch, and undo and redo.
#include "mforce/testing/check.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/wiring.h"
#include "control_fixture.h"
#include <filesystem>
#include <string>

using namespace mforce;
using namespace mforce::ui;
using namespace control_fixture;

namespace {

using Kind = PinEnd::Kind;

PinEnd out_of(const char* node) { return PinEnd{Kind::Output, node, {}}; }
PinEnd in_of(const char* node, const char* pin) { return PinEnd{Kind::Input, node, pin}; }

}  // namespace

void run_step_tests() {
  register_all_sources();
  Session s;
  start(s, control_patch());
  REQUIRE(s.audio.player() != nullptr && s.status.empty());
  const Patch opened = s.open.patch;

  edit_value(s, "nope", "x", 1.0f);                       // a refusal: nothing changes, no step
  CHECK(!s.status.empty() && s.open.patch == opened && steps(s) == 0);
  edit_value(s, "lfo", "amplitude", node_of(s, "lfo")->values.at("amplitude"));   // the same value: no step
  CHECK(steps(s) == 0 && !unsaved(s.open));

  for (int k = 0; k < 60; ++k) edit_value(s, "lfo", "amplitude", 0.01f * float(k + 1));
  CHECK(steps(s) == 1 && s.status.empty());               // sixty writes of one slider: one step
  end_gesture(s);
  edit_value(s, "lfo", "amplitude", 0.9f);
  CHECK(steps(s) == 2);

  edit_position(s, "env", Vec2{1, 1});                    // a drag of two nodes and a face: one step
  edit_position(s, "lfo", Vec2{2, 2});
  edit_position(s, "Note1", Vec2{3, 3});
  CHECK(steps(s) == 3 && s.open.patch.layout.faces[0].pos == (Vec2{3, 3}));
  end_gesture(s);

  // Two engine edits in one step, the second refused: the first is undone too.
  const Patch before = s.open.patch;
  wire(s, PinEnd{Kind::NoteField, "Note1", "velocity"}, in_of("osc", "nope"));
  CHECK(s.status.find("'nope' is not a pin of SineSource") != std::string::npos);
  CHECK(s.open.patch == before && steps(s) == 3 && !node_of(s, "__perf_velocity"));
}

void run_sound_follows_tests() {
  Session s;
  start(s, control_patch());
  REQUIRE(s.audio.player() != nullptr);
  unwire(s, PinEnd{Kind::PatchOutput, {}, {}}, 0);        // the patch can no longer be built
  CHECK(s.audio.player() == nullptr);
  CHECK(s.status == "nothing is wired to the Output yet: wire a node's output to it to hear the patch");
  (void)sounds(s, 1);
  CHECK(!sounds(s, 2));
  wire(s, PinEnd{Kind::PatchOutput, {}, {}}, out_of("osc"));
  CHECK(s.audio.player() != nullptr && s.status.empty());

  Patch plain = control_patch();
  plain.instrument.reset();
  plain.layout.faces.clear();
  plain.graph.nodes.erase(plain.graph.nodes.begin());     // the face's PerformNode
  plain.graph.nodes[1].wires.erase("frequency");
  start(s, plain);
  CHECK(s.audio.player() == nullptr && s.status.rfind("a plain sound", 0) == 0);
}

// An undo that changes nothing the build reads keeps the player and the
// notes it sounds; one that does gives another player.
void run_undo_keeps_player_tests() {
  Session s;
  start(s, control_patch());
  edit_position(s, "lfo", Vec2{9, 9});
  end_gesture(s);
  LivePlayer* player = s.audio.player();
  REQUIRE(player != nullptr);
  undo(s);
  CHECK(s.audio.player() == player);
  redo(s);
  CHECK(s.audio.player() == player);
  wire(s, out_of("lfo"), in_of("l", "source"));
  LivePlayer* wired = s.audio.player();
  CHECK(wired != player);
  undo(s);
  CHECK(s.audio.player() != wired && s.audio.player() != nullptr);
}

void run_undo_redo_tests() {
  const std::filesystem::path file = std::filesystem::temp_directory_path() / "mforce_ui_control_undo.json";
  Session s;
  start(s, control_patch());
  save_file_as(s, file.string());
  const Patch saved = s.open.patch;
  CHECK(title(s) == "MForce - mforce_ui_control_undo.json");
  wire(s, out_of("lfo"), in_of("l", "source"));
  const Patch wired = s.open.patch;
  CHECK(title(s) == "MForce - mforce_ui_control_undo.json *");
  pan_canvas(s, Vec2{77, 88});
  undo(s);
  CHECK(title(s) == "MForce - mforce_ui_control_undo.json");   // back at the saved state: no star
  Patch want = saved;
  want.layout.panning = Vec2{77, 88};                    // the panning stays where it is
  CHECK(s.open.patch == want && s.audio.player() != nullptr);
  redo(s);
  CHECK(title(s) == "MForce - mforce_ui_control_undo.json *");
  want = wired;
  want.layout.panning = Vec2{77, 88};
  CHECK(s.open.patch == want);
  redo(s);
  CHECK(s.status == "nothing to redo");

  select_node(s, "lfo");
  create_node(s, "SineSource", Vec2{});                   // selects "Sine"
  REQUIRE(s.selected == "Sine");
  offer_setting_pin(s, "Sine", "noSuchSetting");
  CHECK(s.offered.empty());
  s.offered.insert({"Sine", "phase"});
  undo(s);                                                // the node it named went: so do the selection and the offer
  CHECK(s.selected.empty() && s.offered.empty() && !node_of(s, "Sine"));
  select_node(s, "lfo");
  undo(s);                                                // lfo is still there: the selection stays
  CHECK(s.selected == "lfo");
  while (!s.open.history.past.empty()) undo(s);
  undo(s);
  CHECK(s.status == "nothing to undo");
  std::error_code ignored;
  std::filesystem::remove(file, ignored);
}

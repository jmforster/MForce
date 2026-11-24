// What a view asks for to build a patch: wiring, unwiring, deleting,
// renaming, making nodes and notes, the instrument, the offered setting pins,
// a new patch, fitting the canvas and clearing the recent files.
#include "mforce/testing/check.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/unsaved.h"
#include "mforce/ui_control/wiring.h"
#include "mforce/ui_model/canvas_view.h"
#include "control_fixture.h"
#include <random>
#include <string>

using namespace mforce;
using namespace mforce::ui;
using namespace control_fixture;

namespace {

using Kind = PinEnd::Kind;

PinEnd end(Kind k, const char* node = "", const char* name = "") { return PinEnd{k, node, name}; }

// `act` on a fresh session: accepted as one step, undone to the patch it began with.
template <class Act>
bool one_step_undone(const char* what, Act act) {
  Session s;
  start(s, control_patch());
  const Patch before = s.open.patch;
  act(s);
  const bool stepped = steps(s) == 1 && s.status.empty() && !(s.open.patch == before);
  undo(s);
  const bool back = s.open.patch == before;
  if (!stepped || !back) std::printf("  %s: stepped %d, undone %d, status '%s'\n", what, stepped, back, s.status.c_str());
  return stepped && back;
}

// `act` on a fresh session: refused with `part` in status, and nothing changed.
template <class Act>
bool refused(const char* what, Act act, const std::string& part) {
  Session s;
  start(s, control_patch());
  const Patch before = s.open.patch;
  act(s);
  const bool ok = s.status.find(part) != std::string::npos && s.open.patch == before && steps(s) == 0;
  if (!ok) std::printf("  %s: status '%s'\n", what, s.status.c_str());
  return ok;
}

}  // namespace

void run_wire_tests() {
  CHECK(one_step_undone("an output into an input", [](Session& s) { wire(s, end(Kind::Output, "lfo"), end(Kind::Input, "l", "source")); }));
  CHECK(one_step_undone("either order", [](Session& s) { wire(s, end(Kind::Input, "l", "source"), end(Kind::Output, "lfo")); }));
  CHECK(one_step_undone("a tap into an input", [](Session& s) { wire(s, end(Kind::Tap, "osc"), end(Kind::Input, "env", "maxValue")); }));
  CHECK(one_step_undone("a note field into an input", [](Session& s) {
    wire(s, end(Kind::NoteField, "Note1", "velocity"), end(Kind::Input, "lfo", "amplitude"));
    CHECK(node_of(s, "__perf_velocity") && node_of(s, "lfo")->wires.at("amplitude")[0].source == "__perf_velocity");
  }));
  CHECK(one_step_undone("an output into a setting pin", [](Session& s) {
    s.offered.insert({"env", "timeScale"});
    wire(s, end(Kind::Output, "lfo"), end(Kind::SettingPin, "env", "timeScale"));
    CHECK(node_of(s, "env")->dynamicPins.at("timeScale") == "lfo" && s.offered.empty());
  }));
  CHECK(one_step_undone("a note field into a setting pin", [](Session& s) { wire(s, end(Kind::NoteField, "Note1", "pressure"), end(Kind::SettingPin, "env", "timeScale")); }));
  CHECK(one_step_undone("an output into the patch's output", [](Session& s) { wire(s, end(Kind::Output, "lfo"), end(Kind::PatchOutput)); }));

  const std::string direction = "a wire runs from an output to an input";
  CHECK(refused("two inputs", [](Session& s) { wire(s, end(Kind::Input, "l", "source"), end(Kind::Input, "lfo", "amplitude")); }, direction));
  CHECK(refused("two outputs", [](Session& s) { wire(s, end(Kind::Output, "lfo"), end(Kind::Tap, "osc")); }, direction));
  CHECK(refused("a tap into a setting pin", [](Session& s) { wire(s, end(Kind::Tap, "lfo"), end(Kind::SettingPin, "env", "timeScale")); }, "a tap feeds a pin"));
  CHECK(refused("a tap into the patch's output", [](Session& s) { wire(s, end(Kind::Tap, "lfo"), end(Kind::PatchOutput)); }, "a tap feeds a pin"));
  CHECK(refused("a loop with no tap", [](Session& s) { wire(s, end(Kind::Output, "osc"), end(Kind::Input, "env", "maxValue")); }, "a cycle with no tap in it"));
  CHECK(refused("a field the note does not have", [](Session& s) { wire(s, end(Kind::NoteField, "Note1", "pitch"), end(Kind::Input, "lfo", "amplitude")); }, "not one of its labels"));
}

void run_unwire_tests() {
  CHECK(one_step_undone("an input's feed", [](Session& s) { unwire(s, end(Kind::Input, "osc", "amplitude"), 0); }));
  Session s;
  start(s, control_patch());
  const Patch opened = s.open.patch;
  unwire(s, end(Kind::PatchOutput), 0);                   // one step; the patch then has no sound
  CHECK(s.open.patch.graph.output.empty() && s.audio.player() == nullptr && steps(s) == 1);
  undo(s);
  CHECK(s.open.patch == opened && s.audio.player() != nullptr);
  wire(s, end(Kind::Output, "lfo"), end(Kind::SettingPin, "env", "timeScale"));
  const Patch fed = s.open.patch;
  unwire(s, end(Kind::SettingPin, "env", "timeScale"), 0);
  CHECK(node_of(s, "env")->dynamicPins.empty() && s.offered.count({"env", "timeScale"}));   // the pin stays on offer
  undo(s);
  CHECK(s.open.patch == fed);
  CHECK(refused("an output", [](Session& t) { unwire(t, end(Kind::Output, "lfo"), 0); }, "at the pin it feeds"));
}

void run_delete_tests() {
  Session s;
  start(s, control_patch());
  wire(s, end(Kind::Output, "lfo"), end(Kind::Input, "l", "source"));
  wire(s, end(Kind::Output, "env"), end(Kind::Input, "l", "source"));
  const Patch two = s.open.patch;
  const size_t before = steps(s);
  delete_these(s, {{end(Kind::Input, "l", "source"), 0}, {end(Kind::Input, "l", "source"), 1}}, {});
  CHECK(steps(s) == before + 1 && !node_of(s, "l")->wires.count("source"));
  undo(s);
  CHECK(s.open.patch == two);

  select_node(s, "Note1");
  s.offered.insert({"lfo", "phase"});
  delete_these(s, {}, {"Note1", "lfo", "lfo"});           // a face, a node, and a name already gone
  CHECK(s.open.patch.layout.faces.empty() && !node_of(s, "__perf_frequency") && !node_of(s, "lfo"));
  CHECK(s.selected.empty() && s.offered.empty() && steps(s) == before + 1);
  undo(s);
  CHECK(s.open.patch == two);
}

void run_rename_tests() {
  const auto by = [](const char* from, std::string to) { return [=](Session& s) { rename(s, from, to); }; };
  CHECK(refused("empty", by("lfo", ""), "name is empty"));
  CHECK(refused("a dot", by("lfo", "a.b"), "'.' not allowed (a pin is named node.pin)"));
  CHECK(refused("a leading __", by("lfo", "__x"), "'__' prefix is reserved"));
  CHECK(refused("a node's id", by("lfo", "env"), "name already in use: env"));
  CHECK(refused("a face's label", by("lfo", "Note1"), "name already in use: Note1"));
  Session g;
  Patch grouped = control_patch();
  grouped.layout.groups = {Group{"G", {"env"}, Vec2{}}};
  start(g, grouped);
  rename(g, "lfo", "G");
  CHECK(g.status == "name already in use by a group: G");

  Session s;
  start(s, control_patch());
  select_node(s, "lfo");
  s.offered.insert({"lfo", "phase"});
  rename(s, "lfo", "wobble");
  CHECK(node_of(s, "wobble") && s.selected == "wobble" && s.offered.count({"wobble", "phase"}) && steps(s) == 1);
  rename(s, "wobble", "wobble");                          // its own name: nothing changes
  CHECK(steps(s) == 1);
  select_node(s, "Note1");
  rename(s, "Note1", "Pitch");
  CHECK(s.open.patch.layout.faces[0].label == "Pitch" && s.selected == "Pitch");
}

void run_create_tests() {
  Session s;
  start(s, control_patch());
  s.seeds.seed(42);
  create_node(s, "SineSource", Vec2{5, 6});
  REQUIRE(node_of(s, "Sine") != nullptr);
  CHECK(s.selected == "Sine" && node_of(s, "Sine")->seed == std::mt19937(42)());
  CHECK(s.open.patch.layout.positions.at("Sine") == (Vec2{5, 6}));
  create_note(s, Vec2{7, 8});
  CHECK(s.selected == "Note2" && s.open.patch.layout.faces.back().pos == (Vec2{7, 8}));

  InstrumentBlock block;
  block.polyphony = 6;
  block.sustaining = true;
  edit_instrument(s, block);
  CHECK(*s.open.patch.instrument == block && s.audio.player() != nullptr);

  Session plain;
  Patch p = control_patch();
  p.instrument.reset();
  p.layout.faces.clear();
  p.graph.nodes.erase(p.graph.nodes.begin());
  p.graph.nodes[1].wires.erase("frequency");
  start(plain, p);
  create_note(plain, Vec2{});
  CHECK(plain.status == "a plain sound has no note to read" && plain.open.patch.layout.faces.empty());
  edit_instrument(plain, block);
  CHECK(!plain.open.patch.instrument && plain.status.find("a plain sound") == 0);
  offer_setting_pin(plain, "env", "timeScale");
  CHECK(plain.offered.empty() && plain.status == "node 'env' (Envelope): a plain sound has no note, so nothing drives a dynamic pin");
}

void run_offer_tests() {
  Session s;
  start(s, control_patch());
  offer_setting_pin(s, "env", "timeScale");
  CHECK(s.offered.count({"env", "timeScale"}) && steps(s) == 0);
  withdraw_setting_pin(s, "env", "timeScale");
  CHECK(s.offered.empty() && steps(s) == 0);

  std::string type, setting;
  for (const auto& t : declared_types())
    for (const auto& st : type_declarations(t)->settings)
      if (st.structural && type.empty()) { type = t; setting = st.name; }
  REQUIRE(!type.empty());
  create_node(s, type, Vec2{});
  const std::string id = s.selected;
  offer_setting_pin(s, id, setting);
  CHECK(s.offered.empty() && s.status.find("is a structural setting on " + type) != std::string::npos);

  wire(s, PinEnd{Kind::Output, "lfo", {}}, PinEnd{Kind::SettingPin, "env", "timeScale"});
  const size_t fed = steps(s);
  withdraw_setting_pin(s, "env", "timeScale");            // its feed goes too: a step
  CHECK(node_of(s, "env")->dynamicPins.empty() && steps(s) == fed + 1);
}

void run_new_patch_tests() {
  Session s;
  start(s, control_patch());
  const int opened = s.opened;
  edit_position(s, "lfo", Vec2{9, 9});
  request_new(s);                                         // unsaved: it waits
  CHECK(s.unsaved.waiting == Unsaved::Waiting::New && node_of(s, "lfo"));
  answer_unsaved(s, Answer::Discard);
  CHECK(node_of(s, "Sine") && node_of(s, "Envelope") && s.open.patch.graph.nodes.size() == 2);
  CHECK(s.open.path.empty() && title(s) == "MForce - untitled" && steps(s) == 0 && s.opened == opened + 1);
  CHECK(s.audio.player() == nullptr);
  CHECK(s.status == "nothing is wired to the Output yet: wire a node's output to it to hear the patch");
  request_new(s);                                         // nothing unsaved: at once
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Nothing && s.opened == opened + 2);

  fit_canvas(s, Vec2{800, 600});
  CHECK(s.open.patch.layout.panning == centred_panning(s.open.patch, Vec2{800, 600}));
  CHECK(steps(s) == 0 && !unsaved(s.open));
  s.recent.paths = {"a.json", "b.json"};
  clear_recent(s);
  CHECK(s.recent.paths.empty());
}

// A node driving a setting's pin deleted: the pin stays on offer, unfed.
void run_delete_driver_offer_tests() {
  Session s;
  start(s, control_patch());
  wire(s, PinEnd{Kind::Output, "lfo", {}}, PinEnd{Kind::SettingPin, "env", "timeScale"});
  REQUIRE(node_of(s, "env")->dynamicPins.count("timeScale") && s.offered.empty());
  delete_these(s, {}, {"lfo"});
  CHECK(!node_of(s, "lfo") && node_of(s, "env")->dynamicPins.empty());
  CHECK(s.offered.count({"env", "timeScale"}) == 1);
}

// The onsets typed as the old editor read them: comma-separated, each name's
// spaces at its ends dropped, an empty one dropped.
void run_onsets_text_tests() {
  CHECK((onsets_from_text(" tongue, slur ,,breath\t,  ") == std::vector<std::string>{"tongue", "slur", "breath"}));
  CHECK(onsets_from_text("").empty() && onsets_from_text(" , ").empty());
  CHECK(onsets_text({"tongue", "slur"}) == "tongue, slur");
  CHECK(onsets_from_text(onsets_text({"a b", "c"})) == (std::vector<std::string>{"a b", "c"}));
}

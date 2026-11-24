// The note-face edits of patch/edit.h, the rule that a face's node nothing
// reads goes, and the names fresh_id and fresh_face_label give.
#include "patch/tests/edit_fixture.h"
#include <limits>
#include <string>

using namespace mforce;
using namespace edit_fixture;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();

// The fixture with a face "Note1" naming freq for the frequency field.
Patch with_face() {
  Patch p = instrument();
  p.layout.faces = {NoteFace{"Note1", {{"frequency", "freq"}}, Vec2{-100, 0}}};
  return p;
}

// A PerformNode of field index `field` as face_field makes it.
Node perform_node(const char* id, float field) {
  Node n = node(id, "PerformNode");
  n.settings["field"] = field;
  return n;
}

bool has_node(const Patch& p, const char* id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return true;
  return false;
}

}  // namespace

void run_face_edit_tests() {
  Patch want = instrument();
  want.layout.faces = {NoteFace{"Note1", {}, Vec2{5, 6}}};
  expect_applies("a face", instrument(), [](Patch& p) { return add_face(p, "Note1", Vec2{5, 6}); }, want,
                 Change::Layout);
  want.layout.faces[0].pos = Vec2{7, 8};
  expect_applies("a face moved", want, [](Patch& p) { return move_face(p, "Note1", Vec2{7, 8}); }, want,
                 Change::Layout);
  Patch grouped = with_face();
  grouped.layout.groups = {Group{"G", {"Note1", "env"}, Vec2{}}};
  want = grouped;
  want.layout.faces[0].label = "Pitch";
  want.layout.groups[0].members = {"Pitch", "env"};
  expect_applies("a face renamed, its group following", grouped,
                 [](Patch& p) { return rename_face(p, "Note1", "Pitch"); }, want, Change::Layout);

  expect_refused("a label taken by a node", instrument(), [](Patch& p) { add_face(p, "osc", Vec2{}); },
                 "'osc' names two things: a node, a face");
  expect_refused("a face off the plane", instrument(), [](Patch& p) { add_face(p, "Note1", Vec2{0, kInf}); },
                 "face 'Note1': its position (0, inf) is not two finite numbers");
  expect_refused("no face to move", instrument(), [](Patch& p) { move_face(p, "Note1", Vec2{}); },
                 "'Note1' names no face: there is nothing to move");
  expect_refused("a face renamed to a node's id", with_face(), [](Patch& p) { rename_face(p, "Note1", "env"); },
                 "'env' names two things");

  want = with_face();
  erase_node(want, "freq");
  at(want, "osc").wires.erase("frequency");
  at(want, "osc").values["frequency"] = default_of("SineSource", "frequency");
  at(want, "env").dynamicPins.clear();
  want.layout.positions.erase("freq");
  want.layout.faces.clear();
  expect_applies("a face removed with its node", with_face(), [](Patch& p) { return remove_face(p, "Note1"); }, want,
                 Change::Structure);
}

void run_face_field_tests() {
  Patch p = with_face();
  CHECK(face_field(p, "Note1", "frequency") == "freq");
  CHECK(p == with_face());

  Patch want = with_face();
  want.layout.faces[0].fields["velocity"] = "__perf_velocity";
  want.graph.nodes.push_back(perform_node("__perf_velocity", 1.0f));
  CHECK(face_field(p, "Note1", "velocity") == "__perf_velocity");
  CHECK(p == want);
  CHECK(p.check().empty());

  Patch taken = with_face();
  taken.graph.nodes.push_back(perform_node("__perf_onset", 5.0f));
  CHECK(face_field(taken, "Note1", "onset") == "__perf_onset2");
  CHECK(at(taken, "__perf_onset2").settings.at("field") == 5.0f);

  expect_refused("a field the note does not have", with_face(), [](Patch& q) { face_field(q, "Note1", "pitch"); },
                 "face 'Note1': 'field' holds \"pitch\", which is not one of its labels (frequency, velocity, wheel, "
                 "pressure, duration, onset) or an index into them");
  expect_refused("no face", with_face(), [](Patch& q) { face_field(q, "Note9", "frequency"); },
                 "'Note9' names no face: it has no field 'frequency'");
  Patch plain = instrument();
  erase_node(plain, "freq");
  at(plain, "osc").wires.erase("frequency");
  at(plain, "env").dynamicPins.clear();
  plain.layout.positions.erase("freq");
  plain.instrument.reset();
  plain.layout.faces = {NoteFace{"Note1", {}, Vec2{}}};
  expect_refused("a note field on a plain sound", plain, [](Patch& q) { face_field(q, "Note1", "frequency"); },
                 "a plain sound has no note to read");
}

// A face's node goes when an edit takes away the last thing that read it,
// and only then.
void run_unread_face_node_tests() {
  Patch p = with_face();
  REQUIRE(disconnect(p, "osc", "frequency", 0) == Change::Structure);
  CHECK(has_node(p, "freq"));   // still read by env's dynamic pin
  REQUIRE(unfeed_setting(p, "env", "timeScale") == Change::Structure);
  CHECK(!has_node(p, "freq"));
  CHECK(p.layout.faces.size() == 1 && p.layout.faces[0].fields.empty());
  CHECK(!p.layout.positions.count("freq"));
  CHECK(p.check().empty());

  Patch replaced = with_face();
  REQUIRE(feed_setting(replaced, "env", "timeScale", "lfo") == Change::Structure);
  REQUIRE(connect(replaced, "osc", "frequency", "lfo", false) == Change::Structure);
  CHECK(!has_node(replaced, "freq"));

  Patch reader = with_face();
  REQUIRE(unfeed_setting(reader, "env", "timeScale") == Change::Structure);
  REQUIRE(remove_node(reader, "osc") == Change::Structure);   // the node that read it goes
  CHECK(!has_node(reader, "freq"));

  Patch untouched = with_face();
  REQUIRE(face_field(untouched, "Note1", "velocity") == "__perf_velocity");
  REQUIRE(disconnect(untouched, "osc", "amplitude", 0) == Change::Structure);
  REQUIRE(connect(untouched, "osc", "amplitude", "lfo", false) == Change::Structure);
  REQUIRE(set_output(untouched, "lfo") == Change::Structure);
  CHECK(has_node(untouched, "__perf_velocity"));
  CHECK(untouched.layout.faces[0].fields.at("velocity") == "__perf_velocity");
}

void run_fresh_name_tests() {
  const Patch empty;
  CHECK(fresh_id(empty, "SineSource") == "Sine");
  CHECK(fresh_id(empty, "CurveNode") == "Curve");
  CHECK(fresh_id(empty, "PerformNode") == "Note");
  CHECK(fresh_id(empty, "SoundChannel") == "Channel");
  CHECK(fresh_id(empty, "StereoMixer") == "Mixer");
  CHECK(fresh_id(empty, "Envelope") == "Envelope");
  int types = 0;
  for (const auto& type : declared_types()) {
    ++types;
    const std::string id = fresh_id(empty, type);
    const bool stripped = type.size() > 6 && type.ends_with("Source");
    if (stripped) CHECK(id + "Source" == type);
    CHECK(!id.empty());
    Patch p;
    p.graph.nodes.push_back(node(id.c_str(), type.c_str()));
    CHECK(fresh_id(p, type) == id + "2");
    p.layout.groups.push_back(Group{id + "2", {}, Vec2{}});
    CHECK(fresh_id(p, type) == id + "3");
    p.layout.faces.push_back(NoteFace{id + "3", {}, std::nullopt});
    CHECK(fresh_id(p, type) == id + "4");
  }
  CHECK(types > 0);

  Patch p;
  CHECK(fresh_face_label(p) == "Note1");
  p.layout.faces.push_back(NoteFace{"Note1", {}, std::nullopt});
  CHECK(fresh_face_label(p) == "Note2");
  p.graph.nodes.push_back(node("Note2", "SineSource"));
  CHECK(fresh_face_label(p) == "Note3");
}

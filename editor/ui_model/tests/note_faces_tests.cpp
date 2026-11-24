// gather_note_faces by hand and over every measured patch, and new_patch.
#include "mforce/testing/check.h"
#include "mforce/patch/patch_json.h"
#include "mforce/ui_model/new_patch.h"
#include "mforce/ui_model/note_faces.h"
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <map>
#include <string>

using namespace mforce;
using namespace mforce::ui;

namespace {

Node perform(const char* id, float field) {
  Node n;
  n.id = id;
  n.type = "PerformNode";
  n.settings["field"] = field;
  return n;
}

// How many fields of faces name each id.
std::map<std::string, int> named_count(const Patch& p) {
  std::map<std::string, int> count;
  for (const auto& f : p.layout.faces)
    for (const auto& [field, id] : f.fields) ++count[id];
  return count;
}

}  // namespace

void run_note_faces_tests() {
  Patch p;
  p.instrument = InstrumentBlock{};
  p.graph.nodes = {perform("f1", 0), perform("v1", 1), perform("f2", 0), perform("named", 5)};
  p.layout.faces = {NoteFace{"Pitch", {{"onset", "named"}}, Vec2{1, 1}}};
  p.layout.positions = {{"f1", Vec2{10, 20}}, {"v1", Vec2{30, 40}}, {"f2", Vec2{50, 60}}};
  CHECK(gather_note_faces(p) == 1);
  REQUIRE(p.layout.faces.size() == 2);
  // f1 and v1 join the face that has their slots free; f2's slot there is
  // taken, so it opens a face of its own, at its position.
  CHECK((p.layout.faces[0].fields == std::map<std::string, std::string>{
            {"onset", "named"}, {"frequency", "f1"}, {"velocity", "v1"}}));
  CHECK(p.layout.faces[1].label == "Note1");
  CHECK((p.layout.faces[1].fields == std::map<std::string, std::string>{{"frequency", "f2"}}));
  CHECK(p.layout.faces[1].pos == (Vec2{50, 60}));
  CHECK(p.layout.positions.empty());                   // a face's node is not on the canvas
  CHECK(p.check().empty());
  const Patch once = p;
  CHECK(gather_note_faces(p) == 0 && p == once);

  Patch loose;
  loose.instrument = InstrumentBlock{};
  loose.graph.nodes = {perform("a", 0), perform("b", 0)};
  CHECK(gather_note_faces(loose) == 2);
  CHECK(loose.layout.faces[0].label == "Note1" && loose.layout.faces[1].label == "Note2");
  CHECK(!loose.layout.faces[0].pos.has_value());
}

void run_note_faces_census_tests() {
  int patches = 0, gatheredIn = 0, faces = 0, faults = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      const std::string file = entry.path().generic_string();
      const Patch before = load_patch(file);
      Patch p = before;
      ++patches;
      const int made = gather_note_faces(p);
      if (made > 0) ++gatheredIn;
      faces += made;
      const auto count = named_count(p);
      bool each = true;
      for (const auto& n : p.graph.nodes)
        if (n.type == "PerformNode") each = each && count.count(n.id) && count.at(n.id) == 1;
      Patch rest = p;                                  // nothing else changed
      rest.layout.faces = before.layout.faces;
      rest.layout.positions = before.layout.positions;
      bool movedOnlyFaceNodes = true;
      for (const auto& [id, pos] : before.layout.positions)
        if (!p.layout.positions.count(id)) movedOnlyFaceNodes = movedOnlyFaceNodes && count.count(id);
      const Patch once = p;
      const bool again = gather_note_faces(p) == 0 && p == once;
      if (!(each && rest == before && movedOnlyFaceNodes && again && p.check().empty())) {
        ++faults;
        std::printf("  %s: the gathering broke a rule\n", file.c_str());
      }
    }
  std::printf("  note faces: %d patches, gathered in %d, %d faces made\n", patches, gatheredIn, faces);
  CHECK(patches > 0);
  CHECK(faults == 0);
}

// A face's node nothing reads goes, with its field; a read one stays.
void run_drop_unread_face_node_tests() {
  Patch p;
  p.instrument = InstrumentBlock{};
  Node osc;
  osc.id = "osc";
  osc.type = "SineSource";
  osc.wires["frequency"] = {Wire{"f"}};
  p.graph.nodes = {perform("f", 0), perform("v", 1), osc};
  p.graph.output = "osc";
  p.layout.faces = {NoteFace{"Note1", {{"frequency", "f"}, {"velocity", "v"}}, Vec2{}}};
  CHECK(drop_unread_face_nodes(p) == 1);
  CHECK(p.graph.nodes.size() == 2 && p.graph.nodes[0].id == "f");
  CHECK((p.layout.faces[0].fields == std::map<std::string, std::string>{{"frequency", "f"}}));
  CHECK(p.check().empty() && drop_unread_face_nodes(p) == 0);
}

// Over every measured patch, after gathering as opening does: every node a
// face names is read afterwards, and nothing else changed.
void run_drop_unread_face_node_census_tests() {
  int patches = 0, had = 0, dropped = 0, faults = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      Patch before = load_patch(entry.path().generic_string());
      (void)gather_note_faces(before);
      Patch p = before;
      ++patches;
      const int went = drop_unread_face_nodes(p);
      if (went > 0) ++had;
      dropped += went;
      bool allRead = true;
      for (const auto& f : p.layout.faces)
        for (const auto& [field, id] : f.fields) {
          bool read = p.graph.output == id;
          for (const auto& n : p.graph.nodes) {
            for (const auto& [pin, feeds] : n.wires)
              for (const auto& w : feeds) read = read || w.source == id;
            for (const auto& [s, src] : n.dynamicPins) read = read || src == id;
          }
          allRead = allRead && read;
        }
      Patch rest = before;                              // the same, less the nodes that went and their fields
      std::erase_if(rest.graph.nodes, [&](const Node& n) {
        return std::none_of(p.graph.nodes.begin(), p.graph.nodes.end(), [&](const Node& m) { return m.id == n.id; });
      });
      rest.layout.faces = p.layout.faces;
      if (!allRead || !(rest == p) || !p.check().empty()) {
        ++faults;
        std::printf("  %s: dropping a face's unread node broke a rule\n", entry.path().generic_string().c_str());
      }
    }
  std::printf("  unread face nodes: %d patches, %d had one, %d nodes went\n", patches, had, dropped);
  CHECK(faults == 0);
}

void run_new_patch_tests() {
  const Patch p = new_patch(11, 22);
  CHECK(p.check().empty());
  REQUIRE(p.instrument.has_value());
  CHECK(*p.instrument == InstrumentBlock{});
  REQUIRE(p.graph.nodes.size() == 2);
  CHECK(p.graph.nodes[0].id == "Sine" && p.graph.nodes[0].type == "SineSource" && p.graph.nodes[0].seed == 11u);
  CHECK(p.graph.nodes[1].id == "Envelope" && p.graph.nodes[1].type == "Envelope" && p.graph.nodes[1].seed == 22u);
  CHECK(p.graph.output.empty());
  CHECK(p.graph.nodes[0].wires.empty() && p.graph.nodes[1].wires.empty());
  CHECK(p.layout.positions.at("Sine") == (Vec2{50, 80}) && p.layout.positions.at("Envelope") == (Vec2{270, 80}));
  CHECK(p.layout.outputPos == (Vec2{490, 80}));
}

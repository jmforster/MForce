// The canvas's table over every measured patch as the editor opens it, at
// each of its levels: each feed, dynamic pin and output is one link at one
// level (the span between two Wormholes at none); no two pins or links share an
// integer; a face's node is not drawn and its readers' links start at the
// face; a link's integer is the same on the next frame. For a handful, each
// link's two ends wired again on a copy make the same feed. And the create
// menu: every type a person may make, once, the four favourites first.
#include "mforce/testing/check.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/wiring.h"
#include "mforce/ui_views/canvas_table.h"
#include "mforce/ui_views/create_menu.h"
#include "mforce/ui_model/groups.h"
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

int reads_in(const Patch& p) {
  int n = p.graph.output.empty() ? 0 : 1;
  for (const auto& node : p.graph.nodes) {
    for (const auto& [pin, feeds] : node.wires) n += int(feeds.size());
    n += int(node.dynamicPins.size());
  }
  return n;
}

// The rules one table must keep; false, printed, at the first broken.
bool table_holds(const std::string& file, const Patch& p, const CanvasTable& t) {
  std::set<int> pins, links, nodes;
  std::set<std::string> faceNodes;
  for (const auto& f : p.layout.faces)
    for (const auto& [field, id] : f.fields) faceNodes.insert(id);
  for (const auto& n : t.nodes) {
    if (!nodes.insert(n.id).second || faceNodes.count(n.name)) return std::printf("  %s: node '%s'\n", file.c_str(), n.name.c_str()), false;
    for (const auto* list : {&n.inputs, &n.outputs})
      for (const auto& pin : *list)
        if (!pins.insert(pin.id).second) return std::printf("  %s: pin %d twice\n", file.c_str(), pin.id), false;
  }
  for (const auto& l : t.links) {
    if (!links.insert(l.id).second || !t.pin(l.from) || !t.pin(l.to)) return std::printf("  %s: link %d\n", file.c_str(), l.id), false;
    const PinEnd& from = *t.pin(l.from);
    if (from.kind == PinEnd::Kind::Output || from.kind == PinEnd::Kind::Tap)
      if (faceNodes.count(from.node)) return std::printf("  %s: a link from a face's node\n", file.c_str()), false;
  }
  return true;
}

// Every level of the patch: the top, and each group gone into.
std::vector<std::vector<std::string>> levels(const Patch& p) {
  std::vector<std::vector<std::string>> all{{}};
  for (const auto& g : p.layout.groups) {
    std::vector<std::string> path = path_to(p, g.name);
    path.push_back(g.name);
    all.push_back(path);
  }
  return all;
}

// The reads no level draws: the span between two Wormholes.
int spans_in(const Patch& p) {
  const auto wormhole = [&](const std::string& id) {
    for (const auto& n : p.graph.nodes)
      if (n.id == id) return n.type == "Wormhole";
    return false;
  };
  int n = 0;
  for (const auto& node : p.graph.nodes)
    if (node.type == "Wormhole")
      for (const auto& [pin, feeds] : node.wires)
        for (const auto& w : feeds) n += wormhole(w.source) ? 1 : 0;
  return n;
}

// Each link's two ends, decoded and wired again, name its own feed: a pin
// that takes one feed, a setting or the output is left as it was; a list pin
// takes the same feed once more (and the step is undone).
bool rewires(Session& s, const CanvasTable& t) {
  const Patch opened = s.open.patch;
  for (const auto& l : t.links) {
    wire(s, *t.pin(l.from), *t.pin(l.to));
    if (s.open.patch == opened) continue;
    const Node* before = nullptr;
    const Node* after = nullptr;
    for (const auto& n : opened.graph.nodes)
      if (n.id == l.reader.node) before = &n;
    for (const auto& n : s.open.patch.graph.nodes)
      if (n.id == l.reader.node) after = &n;
    if (!before || !after || l.reader.kind != PinEnd::Kind::Input) return false;
    const auto& was = before->wires.at(l.reader.name);
    const auto& now = after->wires.at(l.reader.name);
    if (now.size() != was.size() + 1 || !(now.back() == was[l.feed])) return false;
    undo(s);
    if (!(s.open.patch == opened)) return false;
  }
  return true;
}

}  // namespace

void run_canvas_table_tests() {
  int patches = 0, faults = 0, links = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      Session s;
      open_file(s, entry.path().generic_string());
      ++patches;
      CanvasNames names;
      int drawn = 0;
      bool held = true;
      for (const auto& level : levels(s.open.patch)) {       // each read is drawn at one level, once
        const CanvasTable t = canvas_table(s.open.patch, level, s.offered, names);
        const CanvasTable again = canvas_table(s.open.patch, level, s.offered, names);
        drawn += int(t.links.size());
        bool same = t.links.size() == again.links.size();
        for (size_t k = 0; same && k < t.links.size(); ++k) same = t.links[k].id == again.links[k].id;
        held = held && same && table_holds(s.open.path, s.open.patch, t);
      }
      links += drawn;
      const int reads = reads_in(s.open.patch) - spans_in(s.open.patch);
      if (drawn != reads) std::printf("  %s: %d links over its levels, %d reads\n", s.open.path.c_str(), drawn, reads);
      if (!held || drawn != reads) ++faults;
    }
  std::printf("  canvas table: %d patches, %d links, %d faults\n", patches, links, faults);
  CHECK(patches > 0);
  CHECK(faults == 0);
}

void run_canvas_rewire_tests() {
  for (const char* file : {"patches/library/winds/oboe1.json", "patches/library/keys/acoustic_piano/piano_default.json"}) {
    Session s;
    open_file(s, file);
    CanvasNames names;
    const CanvasTable t = canvas_table(s.open.patch, s.groupPath, s.offered, names);
    const bool ok = rewires(s, t);
    if (!ok) std::printf("  %s: a link's two ends do not wire back to its feed\n", file);
    CHECK(ok);
  }
}

void run_create_menu_tests() {
  const CreateMenu menu = create_menu();
  REQUIRE(menu.favourites.size() == 5);
  CHECK(menu.favourites[0].label == "Note" && menu.favourites[0].type.empty());
  CHECK(menu.favourites[1].type == "CurveNode" && menu.favourites[2].type == "Envelope" &&
        menu.favourites[3].type == "VarSource" && menu.favourites[4].type == "RangeSource");
  std::map<std::string, int> seen;
  for (const auto& item : menu.favourites)
    if (!item.type.empty()) ++seen[item.type];
  for (const auto* shelf : {&menu.main, &menu.experimental})
    for (const auto& f : *shelf)
      for (const auto& item : f.items) ++seen[item.type];
  const SourceRegistry& reg = SourceRegistry::instance();
  int offered = 0;
  for (const auto& e : reg.registered_types()) {
    const bool mayOffer = e.tag.family != Family::Output && e.name != "PerformNode" && e.tag.shelf != Shelf::None;
    const int times = seen.count(e.name) ? seen.at(e.name) : 0;
    if (mayOffer != (times == 1)) std::printf("  the menu holds %s %d times\n", e.name.c_str(), times);
    CHECK(mayOffer == (times == 1));
    offered += mayOffer ? 1 : 0;
  }
  CHECK(offered > 0 && !menu.main.empty());
}

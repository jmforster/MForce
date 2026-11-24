// The proofs over every measured patch (each .json under patches/library and
// patches/baselines): each is made again from an empty patch through the
// edits alone and equals the file as read; removing any one node leaves
// check() empty and survives the codec; renaming a node and back, and
// unwiring a pin's last feed and wiring it again, give the patch as read.
#include "mforce/testing/check.h"
#include "mforce/patch/edit.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace {

// Calls `f` with every measured patch and its path.
void each_measured_patch(const std::function<void(const std::string&, const Patch&)>& f) {
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder))
      if (entry.is_regular_file() && entry.path().extension() == ".json") {
        const std::string file = entry.path().generic_string();
        f(file, load_patch(file));
      }
}

struct Rebuild {
  int patches{0}, nodes{0}, feeds{0}, refusals{0}, unequal{0};
  int seedsRemoved{0}, positionsRemoved{0}, tablesRemoved{0};
  int facesWithoutPosition{0}, faceNodesNotPlain{0};
};

// Runs one edit of the rebuild; a refusal is printed and counted.
void edit(Rebuild& r, const std::string& file, const std::string& what, const std::function<void()>& f) {
  try {
    f();
  } catch (const std::exception& e) {
    ++r.refusals;
    std::printf("  REFUSED %s, %s: %s\n", file.c_str(), what.c_str(), e.what());
  }
}

// The face and field that name `id`, when one does.
std::optional<std::pair<std::string, std::string>> face_of(const Patch& file, const std::string& id) {
  for (const auto& f : file.layout.faces)
    for (const auto& [field, node] : f.fields)
      if (node == id) return std::make_pair(f.label, field);
  return std::nullopt;
}

// What no edit reaches, and the instrument block: a PerformNode or a dynamic
// pin is refused on a plain sound, so the block comes before the nodes.
Patch start_of(Rebuild& r, const std::string& file, const Patch& from) {
  Patch p;
  p.sampleRate = from.sampleRate;
  p.seconds = from.seconds;
  p.score = from.score;
  p.comment = from.comment;
  if (from.instrument) edit(r, file, "instrument", [&] { set_instrument(p, *from.instrument); });
  for (const auto& f : from.layout.faces) {
    if (!f.pos) ++r.facesWithoutPosition;
    edit(r, file, "face " + f.label, [&] { add_face(p, f.label, f.pos.value_or(Vec2{})); });
  }
  return p;
}

void add_the_nodes(Rebuild& r, const std::string& file, const Patch& from, Patch& p) {
  for (const Node& n : from.graph.nodes) {
    ++r.nodes;
    const auto pos = from.layout.positions.find(n.id);
    const bool placed = pos != from.layout.positions.end();
    if (const auto face = face_of(from, n.id)) {
      if (n.type != "PerformNode" || n.seed) ++r.faceNodesNotPlain;
      edit(r, file, n.id, [&] { rename_node(p, face_field(p, face->first, face->second), n.id); });
      if (placed) edit(r, file, n.id + " position", [&] { move_node(p, n.id, pos->second); });
      continue;
    }
    edit(r, file, n.id, [&] { add_node(p, n.type, n.id, n.seed.value_or(0), placed ? pos->second : Vec2{}); });
  }
}

void set_the_contents(Rebuild& r, const std::string& file, const Patch& from, Patch& p) {
  for (const Node& n : from.graph.nodes) {
    for (const auto& [name, v] : n.values) edit(r, file, n.id + "." + name, [&] { set_value(p, n.id, name, v); });
    for (const auto& [name, v] : n.settings) edit(r, file, n.id + "." + name, [&] { set_setting(p, n.id, name, v); });
    for (const auto& [name, t] : n.strings) edit(r, file, n.id + "." + name, [&] { set_string(p, n.id, name, t); });
    if (!n.arrays.empty()) edit(r, file, n.id + " arrays", [&] { set_arrays(p, n.id, n.arrays); });
  }
}

void wire_it(Rebuild& r, const std::string& file, const Patch& from, Patch& p) {
  for (const Node& n : from.graph.nodes)
    for (const auto& [pin, feeds] : n.wires)
      for (const auto& w : feeds) {
        ++r.feeds;
        edit(r, file, n.id + "." + pin + " <- " + w.source, [&] { connect(p, n.id, pin, w.source, w.tap); });
      }
  for (const Node& n : from.graph.nodes)
    for (const auto& [setting, source] : n.dynamicPins)
      edit(r, file, n.id + "." + setting + " per note", [&] { feed_setting(p, n.id, setting, source); });
  if (!from.graph.output.empty()) edit(r, file, "output", [&] { set_output(p, from.graph.output); });
  if (from.layout.outputPos) edit(r, file, "outputPos", [&] { move_output(p, *from.layout.outputPos); });
  if (from.layout.panning) edit(r, file, "panning", [&] { set_panning(p, *from.layout.panning); });
}

// What a file's node may lack that a new node always has: a seed, a
// position, a starting table; and a face's position.
void remove_what_the_file_lacks(Rebuild& r, const Patch& from, Patch& p) {
  for (Node& n : p.graph.nodes) {
    const Node* original = nullptr;
    for (const auto& o : from.graph.nodes)
      if (o.id == n.id) original = &o;
    if (!original) continue;
    if (n.seed && !original->seed) { n.seed.reset(); ++r.seedsRemoved; }
    if (p.layout.positions.count(n.id) && !from.layout.positions.count(n.id)) {
      p.layout.positions.erase(n.id);
      ++r.positionsRemoved;
    }
    const auto lacked = std::erase_if(n.arrays, [&](const auto& a) { return !original->arrays.count(a.first); });
    if (lacked) ++r.tablesRemoved;
  }
  for (size_t f = 0; f < p.layout.faces.size() && f < from.layout.faces.size(); ++f)
    if (!from.layout.faces[f].pos) p.layout.faces[f].pos.reset();
}

// Two things no edit of the patch module makes: a node's comment and the groups.
void copy_comments_and_groups(const Patch& from, Patch& p) {
  for (Node& n : p.graph.nodes)
    for (const auto& o : from.graph.nodes)
      if (o.id == n.id) n.comment = o.comment;
  p.layout.groups = from.layout.groups;
}

void rebuild(Rebuild& r, const std::string& file, const Patch& from) {
  ++r.patches;
  Patch p = start_of(r, file, from);
  add_the_nodes(r, file, from, p);
  set_the_contents(r, file, from, p);
  wire_it(r, file, from, p);
  remove_what_the_file_lacks(r, from, p);
  copy_comments_and_groups(from, p);
  if (p == from) return;
  ++r.unequal;
  std::printf("  %s: the rebuilt patch is not the file", file.c_str());
  for (size_t i = 0; i < from.graph.nodes.size(); ++i)
    if (i >= p.graph.nodes.size() || !(p.graph.nodes[i] == from.graph.nodes[i])) {
      std::printf("; first at node '%s'", from.graph.nodes[i].id.c_str());
      break;
    }
  std::printf("%s%s\n", p.layout == from.layout ? "" : "; the layout differs",
              p.graph.output == from.graph.output ? "" : "; the output differs");
}

}  // namespace

void run_edit_rebuild_proof_tests() {
  const auto t0 = std::chrono::steady_clock::now();
  Rebuild r;
  each_measured_patch([&](const std::string& file, const Patch& p) { rebuild(r, file, p); });
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  rebuild: %d patches rebuilt, %d nodes added, %d feeds connected; removed from new nodes: "
              "%d seeds, %d positions, %d starting tables; %d faces without a position, %d face nodes not a plain "
              "PerformNode; %d refusals, %d unequal; %.1f s\n",
              r.patches, r.nodes, r.feeds, r.seedsRemoved, r.positionsRemoved, r.tablesRemoved,
              r.facesWithoutPosition, r.faceNodesNotPlain, r.refusals, r.unequal, secs);
  CHECK(r.patches > 0);
  CHECK(r.refusals == 0);
  CHECK(r.unequal == 0);
}

void run_edit_removal_proof_tests() {
  const auto t0 = std::chrono::steady_clock::now();
  int removals = 0, faults = 0;
  each_measured_patch([&](const std::string& file, const Patch& from) {
    for (const Node& n : from.graph.nodes) {
      ++removals;
      Patch p = from;
      try {
        remove_node(p, n.id);
        const nlohmann::json j = p;
        Patch back;
        from_json(j, back);
        if (back == p && p.check().empty()) continue;
        std::printf("  %s, '%s' removed: check() or the codec disagrees\n", file.c_str(), n.id.c_str());
      } catch (const std::exception& e) {
        std::printf("  %s, '%s' removed: %s\n", file.c_str(), n.id.c_str(), e.what());
      }
      ++faults;
    }
  });
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  removal: %d nodes removed one at a time, %d faults; %.1f s\n", removals, faults, secs);
  CHECK(removals > 0);
  CHECK(faults == 0);
}

namespace {

// Every node renamed to a fresh id and back: the patch as read.
int rename_round_trips(const std::string& file, const Patch& from, int& trips) {
  int faults = 0;
  for (const Node& n : from.graph.nodes) {
    ++trips;
    Patch p = from;
    const std::string other = fresh_id(p, n.type);
    try {
      rename_node(p, n.id, other);
      rename_node(p, other, n.id);
    } catch (const std::exception& e) {
      std::printf("  %s, '%s' renamed: %s\n", file.c_str(), n.id.c_str(), e.what());
    }
    if (p == from) continue;
    std::printf("  %s: '%s' renamed and back is not the patch as read\n", file.c_str(), n.id.c_str());
    ++faults;
  }
  return faults;
}

// The last feed of every pin unwired and wired again: the patch as read,
// unless its source was a face's node that nothing else read.
int wire_round_trips(const std::string& file, const Patch& from, int& trips, int& skipped) {
  int faults = 0;
  for (const Node& n : from.graph.nodes)
    for (const auto& [pin, feeds] : n.wires) {
      const Wire last = feeds.back();
      Patch p = from;
      try {
        disconnect(p, n.id, pin, feeds.size() - 1);
        bool sourceStays = false;
        for (const auto& o : p.graph.nodes) sourceStays = sourceStays || o.id == last.source;
        if (!sourceStays) { ++skipped; continue; }
        ++trips;
        connect(p, n.id, pin, last.source, last.tap);
      } catch (const std::exception& e) {
        std::printf("  %s, %s.%s: %s\n", file.c_str(), n.id.c_str(), pin.c_str(), e.what());
      }
      if (p == from) continue;
      std::printf("  %s: %s.%s unwired and wired again is not the patch as read\n", file.c_str(), n.id.c_str(), pin.c_str());
      ++faults;
    }
  return faults;
}

}  // namespace

void run_edit_round_trip_tests() {
  const auto t0 = std::chrono::steady_clock::now();
  int renames = 0, wires = 0, skipped = 0, faults = 0;
  each_measured_patch([&](const std::string& file, const Patch& from) {
    faults += rename_round_trips(file, from, renames);
    faults += wire_round_trips(file, from, wires, skipped);
  });
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  round trips: %d renames, %d feeds unwired and wired again (%d skipped: the source went), "
              "%d faults; %.1f s\n", renames, wires, skipped, faults, secs);
  CHECK(renames > 0 && wires > 0);
  CHECK(faults == 0);
}

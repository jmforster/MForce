// Every view drawn without a window: on every measured patch and a new one,
// with nothing selected, with each thing the canvas draws selected (faces
// included) and with a setting's pin on offer, a view that only draws changes
// nothing; the unsaved prompt, an empty session and a node of an
// unregistered type drawn.
#include "mforce/testing/check.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/session.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_views/canvas_table.h"
#include "mforce/ui_views/headless.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

// Draws `s` and checks the patch, its unsaved flag and the status are as they were.
bool draws_unchanged(Session& s) {
  const Patch before = s.open.patch;
  const bool dirty = unsaved(s.open);
  const std::string status = s.status;
  draw_once_headless(s);
  return s.open.patch == before && unsaved(s.open) == dirty && s.status == status;
}

}  // namespace

namespace {

// What the canvas draws as nodes, by name: the nodes no face names, the faces.
std::vector<std::string> drawn_names(const Session& s) {
  CanvasNames names;
  std::vector<std::string> out;
  for (const auto& n : canvas_table(s.open.patch, s.groupPath, s.offered, names).nodes)
    if (n.kind != CanvasNode::Kind::Output) out.push_back(n.name);
  return out;
}

// Drawn with nothing selected, with each drawn thing selected in turn, and
// with a setting's pin on offer: the number of draws that changed the session.
int changed_draws(Session& s, int& draws) {
  int changed = 0;
  ++draws;
  if (!draws_unchanged(s)) ++changed;
  for (const auto& name : drawn_names(s)) {
    select_node(s, name);
    ++draws;
    if (!draws_unchanged(s)) {
      ++changed;
      std::printf("  %s, '%s' selected: the draw changed the session\n", s.open.path.c_str(), name.c_str());
    }
  }
  for (const auto& n : s.open.patch.graph.nodes) {
    const TypeDeclarations* d = type_declarations(n.type);
    if (!d || d->settings.empty() || !s.open.patch.instrument) continue;
    s.offered.insert({n.id, d->settings.front().name});
    select_node(s, n.id);
    ++draws;
    if (!draws_unchanged(s)) ++changed;
    s.offered.clear();
    break;
  }
  return changed;
}

}  // namespace

void run_headless_census_tests() {
  register_all_sources();
  const auto t0 = std::chrono::steady_clock::now();
  int patches = 0, draws = 0, changed = 0;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      Session s;
      open_file(s, entry.path().generic_string());
      ++patches;
      changed += changed_draws(s, draws);
      CHECK(!unsaved(s.open));
    }
  Session fresh;
  new_file(fresh);
  changed += changed_draws(fresh, draws);
  CHECK(!unsaved(fresh.open));
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  headless: %d patches and a new one, %d draws, %d changed something; %.1f s\n", patches, draws,
              changed, secs);
  CHECK(patches > 0);
  CHECK(changed == 0);
}

void run_headless_prompt_and_empty_tests() {
  Session empty;
  CHECK(draws_unchanged(empty));

  Session s;
  open_file(s, "patches/library/winds/oboe1.json");
  edit_position(s, "Formant1", Vec2{1, 1});
  s.unsaved.waiting = Unsaved::Waiting::Close;
  CHECK(draws_unchanged(s));
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Close);
  s.unsaved = Unsaved{Unsaved::Waiting::Open, "patches/library/keys/acoustic_piano/piano_default.json", false};
  CHECK(draws_unchanged(s));
  CHECK(s.unsaved.waiting == Unsaved::Waiting::Open);

  Session unknown;
  Node n;
  n.id = "x";
  n.type = "NoSuchSource";
  unknown.open.patch.graph.nodes = {n};
  unknown.open.patch.graph.output = "x";
  unknown.open.patch.layout.positions["x"] = Vec2{0, 0};
  CHECK(draws_unchanged(unknown));
  select_node(unknown, "x");
  CHECK(draws_unchanged(unknown));
}

// Every view drawn without a window: on every measured patch, with nothing
// selected and with each node selected, a view that only draws changes
// nothing; the unsaved prompt, an empty session and a node of an
// unregistered type drawn.
#include "mforce/testing/check.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/ui_control/edits.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/headless.h"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

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
      ++draws;
      if (!draws_unchanged(s)) ++changed;
      for (const auto& node : s.open.patch.graph.nodes) {
        select_node(s, node.id);
        ++draws;
        if (!draws_unchanged(s)) {
          ++changed;
          std::printf("  %s, '%s' selected: the draw changed the session\n", s.open.path.c_str(), node.id.c_str());
        }
      }
    }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  headless: %d patches, %d draws, %d changed something; %.1f s\n", patches, draws, changed, secs);
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

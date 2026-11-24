// A handful of measured patches opened as the editor opens them: the first
// few wires of each taken away and the step undone gives the opened patch,
// with no star in the title. Each step rebuilds the player, so the handful
// is kept small.
#include "mforce/testing/check.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/files.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/wiring.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;
using namespace mforce::ui;

namespace {

constexpr int kWiresEach = 4;

const char* const kHandful[] = {
  "patches/library/winds/oboe1.json",
  "patches/library/keys/acoustic_piano/piano_default.json",
  "patches/library/bells/bell_strike.json",
  "patches/library/strings/viola_default.json",
};

// The first wires of the patch as the canvas names their reading ends.
std::vector<std::pair<PinEnd, std::size_t>> first_wires(const Patch& p) {
  std::vector<std::pair<PinEnd, std::size_t>> wires;
  for (const auto& n : p.graph.nodes)
    for (const auto& [pin, feeds] : n.wires)
      for (std::size_t f = 0; f < feeds.size() && wires.size() < kWiresEach; ++f)
        wires.push_back({PinEnd{PinEnd::Kind::Input, n.id, pin}, f});
  if (!p.graph.output.empty()) wires.push_back({PinEnd{PinEnd::Kind::PatchOutput, {}, {}}, 0});
  return wires;
}

}  // namespace

void run_history_census_tests() {
  const auto t0 = std::chrono::steady_clock::now();
  int patches = 0, unwired = 0, faults = 0;
  for (const char* file : kHandful) {
    Session s;
    open_file(s, file);
    REQUIRE(s.open.path == file);
    ++patches;
    const Patch opened = s.open.patch;
    for (const auto& [reader, feed] : first_wires(opened)) {
      unwire(s, reader, feed);
      ++unwired;
      const bool stepped = s.open.history.past.size() == 1;
      undo(s);
      if (stepped && s.open.patch == opened && title(s).back() != '*') continue;
      ++faults;
      std::printf("  %s, %s.%s feed %zu: unwired and undone is not the opened patch (%s)\n", file, reader.node.c_str(),
                  reader.name.c_str(), feed, s.status.c_str());
    }
  }
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  history census: %d patches, %d wires taken away and undone, %d faults; %.1f s\n", patches, unwired,
              faults, secs);
  CHECK(faults == 0);
}

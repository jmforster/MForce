// The census of LivePlayer::set_value: one case per (node type, pin) that
// holds a constant anywhere in the measured patches (patches/library and
// patches/baselines). Each case takes the first instrument that has the
// pair, changes that constant, and plays one short note on a player built
// from the edited patch and on one built from the patch as it was and told
// with set_value before the note. The two must be equal sample for sample:
// a difference names a node that reads the constant where a live write does
// not reach it.
#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/patch/edit.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/render/live_player.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace {

constexpr int kBuffer = 256;
constexpr int kMostBuffers = 94;   // half a second at 48 kHz: the note and the start of its release

// The first place a (type, pin) pair holds a constant.
struct Case {
  std::string file, node;
  float value{0.0f};
};
using Pair = std::pair<std::string, std::string>;

// Every (type, pin) of the measured patches, at its first instrument; and
// the pairs only plain sounds hold.
struct Pairs {
  std::map<Pair, Case> instruments;
  std::set<Pair> plainOnly;
};

Pairs gather() {
  Pairs out;
  std::set<Pair> plain;
  for (const char* folder : {"patches/library", "patches/baselines"})
    for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
      if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
      const std::string file = entry.path().generic_string();
      const Patch p = load_patch(file);
      for (const Node& n : p.graph.nodes)
        for (const auto& [pin, v] : n.values) {
          if (p.instrument) out.instruments.try_emplace({n.type, pin}, Case{file, n.id, v});
          else plain.insert({n.type, pin});
        }
    }
  for (const auto& pair : plain)
    if (!out.instruments.count(pair)) out.plainOnly.insert(pair);
  return out;
}

// Another value for the constant: a tenth of the Param's declared range away
// (down when up would leave it), or a tenth of its magnitude where it
// declares no range.
float other_value(const std::string& type, const std::string& pin, float v) {
  const TypeDeclarations* d = type_declarations(type);
  const TypeDeclarations::Param* p = d ? d->param(pin) : nullptr;
  if (p && p->max > p->min) {
    const float step = 0.1f * (p->max - p->min);
    return v + step <= p->max ? v + step : v - step;
  }
  return v == 0.0f ? 0.1f : v * 1.1f;
}

std::vector<float> one_note(LivePlayer& player) {
  std::vector<float> out, buf(kBuffer);
  (void)player.note_on(note_at(0.0f, 0.1f, 60.0f, 0.8f));
  for (int k = 0; k < kMostBuffers && (k == 0 || player.sounding() > 0); ++k) {
    player.fill(buf.data(), kBuffer);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

// What one case found.
enum class Outcome { Equal, Differs, NotFollowed };

Outcome run_case(const Pair& pair, const Case& c, bool& heard) {
  Patch base = load_patch(c.file);
  base.instrument->polyphony = 1;                        // one voice is enough
  const float v = other_value(pair.first, pair.second, c.value);
  Patch changed = base;
  (void)set_value(changed, c.node, pair.second, v);
  LivePlayer a(changed, 1), b(base, 1), untold(base, 1);
  if (!b.set_value(c.node, pair.second, v)) return Outcome::NotFollowed;
  const auto fromA = one_note(a), fromB = one_note(b);
  heard = !patch_door::same_samples(fromB, one_note(untold));
  if (patch_door::same_samples(fromA, fromB)) return Outcome::Equal;
  std::printf("  DIFFERS %s.%s (%s, node '%s', %g to %g): %s\n", pair.first.c_str(), pair.second.c_str(),
              c.file.c_str(), c.node.c_str(), double(c.value), double(v), patch_door::difference(fromA, fromB).c_str());
  return Outcome::Differs;
}

}  // namespace

void run_live_set_value_census_tests() {
  std::printf("the live player: set_value over every (type, pin) holding a constant\n");
  const auto t0 = std::chrono::steady_clock::now();
  const Pairs pairs = gather();
  int equal = 0, differs = 0, notFollowed = 0, inert = 0;
  for (const auto& [pair, c] : pairs.instruments) {
    bool heard = false;
    switch (run_case(pair, c, heard)) {
      case Outcome::Equal: ++equal; break;
      case Outcome::Differs: ++differs; break;
      case Outcome::NotFollowed:
        ++notFollowed;
        std::printf("  NOT FOLLOWED %s.%s (%s, node '%s')\n", pair.first.c_str(), pair.second.c_str(),
                    c.file.c_str(), c.node.c_str());
        break;
    }
    if (!heard) ++inert;
  }
  for (const auto& [type, pin] : pairs.plainOnly) std::printf("  only plain sounds hold %s.%s\n", type.c_str(), pin.c_str());
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  std::printf("  census: %zu (type, pin) pairs in instruments, %d equal, %d differ, %d not followed; "
              "%d not heard in the note either way; %zu pairs only plain sounds hold; %.1f s\n",
              pairs.instruments.size(), equal, differs, notFollowed, inert, pairs.plainOnly.size(), secs);
  CHECK(!pairs.instruments.empty());
  CHECK(differs == 0);   // a pair not followed was checked only for its false
}

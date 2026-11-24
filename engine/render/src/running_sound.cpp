#include "mforce/render/running_sound.h"
#include "mforce/render/build_sound.h"
#include <algorithm>
#include <array>
#include <utility>

namespace mforce {

namespace {

// The Envelope's stage table, one column per field, in its declared order.
constexpr std::array<const char*, 8> kStageColumns = {"startVal", "endVal", "power",  "type",
                                                      "percent",  "minSec", "maxSec", "nominal"};

// Time in seconds, and a stage of 0 percent (the one an envelope holds at)
// appended at its last level when the table has none. The table's rows are
// as many as its shortest column has cells; cells past them are dropped
// first, as the Envelope drops them.
void hold(Node& env) {
  env.settings["timeMode"] = 1.0f;
  size_t rows = env.arrays[kStageColumns[0]].size();
  for (const char* c : kStageColumns) rows = std::min(rows, env.arrays[c].size());
  if (rows == 0) return;
  const auto& percent = env.arrays["percent"];
  if (std::any_of(percent.begin(), percent.begin() + std::ptrdiff_t(rows), [](float p) { return p == 0.0f; })) return;
  const float last = env.arrays["endVal"][rows - 1];
  for (const char* c : kStageColumns) env.arrays[c].resize(rows);
  env.arrays["startVal"].push_back(last);
  env.arrays["endVal"].push_back(last);
  for (const char* c : {"power", "type", "percent", "minSec", "maxSec", "nominal"}) env.arrays[c].push_back(0.0f);
}

}  // namespace

Patch held_patch(const Patch& patch) {
  Patch held = patch;
  for (Node& n : held.graph.nodes)
    if (n.type == "Envelope") hold(n);
  return held;
}

RunningSound::RunningSound(const Patch& patch, int sampleRate) : sampleRate_(sampleRate) {
  Patch held = held_patch(patch);
  held.sampleRate = sampleRate;
  BuiltSound built = build_sound(held);
  warnings_ = std::move(built.warnings);
  mixer_ = std::move(built.mixer);
  mixer_->start_running(kHorizonSeconds * sampleRate, kBlock);
}

void RunningSound::fill(float* stereo, int frames) noexcept { mixer_->render_running(stereo, frames); }

}  // namespace mforce

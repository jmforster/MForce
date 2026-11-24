#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/render/fault_report.h"
#include <algorithm>
#include <vector>

namespace mforce {

// A mono producer the mixer owns. prepare(ctx) once, from the host that
// built it; render() as often as the host wants frames.
struct MonoSource {
  virtual ~MonoSource() = default;
  virtual void prepare(const RenderContext& ctx) = 0;
  virtual void render(float* out, int frames) = 0;
  // Running: started once over `frames`, then render_running gives the next
  // `frames` samples, block after block, with no start and no allocation. A
  // producer with no running form writes silence (an instrument: nothing is
  // played into it while it runs).
  virtual void start_running(int /*frames*/) {}
  virtual void render_running(float* out, int frames) noexcept { std::fill(out, out + frames, 0.0f); }
  // The faults every node of this producer reported since the last call,
  // one entry per node id, cleared as they are read. Control side only,
  // after a render. A producer with no ids of its own (a plain sound's graph)
  // reports nothing; the mixer lists such a graph's nodes itself (signals).
  virtual void collect_faults(std::vector<FaultReport>&) {}
};

} // namespace mforce

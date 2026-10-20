#pragma once
#include <string>
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>

namespace mforce {

// One node's faults, as a host reports them after a render.
struct FaultReport {
  std::string nodeId;
  const char* type;
  unsigned char faults;
};

// "osc1 (SineSource): frequency clamped, table length clamped" — the one
// wording every host prints.
inline std::string fault_report_text(const FaultReport& r) {
  std::string s = r.nodeId + " (" + r.type + "):";
  bool first = true;
  for (unsigned char bit = 1; bit; bit = static_cast<unsigned char>(bit << 1)) {
    if (!(r.faults & bit)) continue;
    s += first ? " " : ", ";
    s += fault_name(static_cast<Fault>(bit));
    first = false;
  }
  return s;
}

// A mono producer the mixer owns. prepare(ctx) once, from the host that
// built it; render() as often as the host wants frames.
struct MonoSource {
  virtual ~MonoSource() = default;
  virtual void prepare(const RenderContext& ctx) = 0;
  virtual void render(float* out, int frames) = 0;
  // The faults every node of this producer reported since the last call,
  // one entry per node id, cleared as they are read. Control side only,
  // after a render. A producer with no file ids (a mixer-mode graph) reports
  // nothing until Part 4 gives it ids.
  virtual void collect_faults(std::vector<FaultReport>&) {}
};

struct Channel {
  std::unique_ptr<MonoSource> source;
  std::shared_ptr<ValueSource> volume;
  std::shared_ptr<ValueSource> pan; // [-1,1]
};

struct StereoMixer {
  std::vector<Channel> channels;
  std::shared_ptr<ValueSource> gainL;
  std::shared_ptr<ValueSource> gainR;

  StereoMixer();
  // Once, after the channels are assembled: the gains, volumes, pans and
  // sources learn the rate; the gains, volumes and pans also learn this
  // mixer's clock (a channel's source is its own pull loop and completes
  // the host's facts with a clock of its own).
  void prepare(const RenderContext& ctx);
  // One span of `frames`: the gains, volumes and pans start over it, each
  // channel renders into it, and the mix loop pulls the gains, volumes and
  // pans once per sample per channel on this mixer's clock.
  void render(float* outLR, int frames);
  // Every channel's faults, in channel order.
  void collect_faults(std::vector<FaultReport>& out) {
    for (auto& ch : channels)
      if (ch.source) ch.source->collect_faults(out);
  }

private:
  SampleClock clock_;
  RenderContext ctx_;
};

} // namespace mforce

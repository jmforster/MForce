#pragma once
#include <string>
#include "mforce/core/dsp_value_source.h"
#include "mforce/render/channel.h"
#include "mforce/render/fault_report.h"
#include "mforce/render/mono_source.h"
#include <memory>
#include <vector>

namespace mforce {

struct Mixer {
  std::vector<Channel> channels;
  std::shared_ptr<ValueSource> gainL;
  std::shared_ptr<ValueSource> gainR;
  // The nodes of a plain sound's graph by id, in id order; empty when the
  // channels' sources report for themselves.
  struct Signal { std::string id; std::shared_ptr<ValueSource> node; };
  std::vector<Signal> signals;

  Mixer();
  // Once, after the channels are assembled: the gains, volumes, pans and
  // sources learn the rate; the gains, volumes and pans also learn this
  // mixer's clock (a channel's source is its own pull loop and completes
  // the host's facts with a clock of its own).
  void prepare(const RenderContext& ctx);
  // One span of `frames`: the gains, volumes and pans start over it, each
  // channel renders into it, and the mix loop pulls the gains, volumes and
  // pans once per sample per channel on this mixer's clock.
  void render(float* outLR, int frames);
  // Every channel's faults, in channel order, then each listed signal's under
  // its id, cleared as they are read.
  void collect_faults(std::vector<FaultReport>& out) {
    for (auto& ch : channels)
      if (ch.source) ch.source->collect_faults(out);
    for (auto& s : signals)
      if (const unsigned char f = s.node->take_faults())
        out.push_back({s.id, s.node->type_name(), f});
  }

private:
  SampleClock clock_;
  RenderContext ctx_;
};

} // namespace mforce

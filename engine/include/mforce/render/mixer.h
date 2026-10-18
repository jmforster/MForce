#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>

namespace mforce {

// A mono producer the mixer owns. prepare(ctx) once, from the host that
// built it; render() as often as the host wants frames.
struct MonoSource {
  virtual ~MonoSource() = default;
  virtual void prepare(const RenderContext& ctx) = 0;
  virtual void render(float* out, int frames) = 0;
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
  // sources learn the rate.
  void prepare(const RenderContext& ctx);
  // One span of `frames`: the gains, volumes and pans start over it, each
  // channel renders into it.
  void render(float* outLR, int frames);
};

} // namespace mforce

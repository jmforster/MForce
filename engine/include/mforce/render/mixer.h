#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>
#include <vector>

namespace mforce {

struct MonoSource {
  virtual ~MonoSource() = default;
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
  void render(float* outLR, int frames);
};

} // namespace mforce

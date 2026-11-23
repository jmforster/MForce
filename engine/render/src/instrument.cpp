#include "mforce/render/instrument.h"
#include "mforce/render/limiter.h"
#include <algorithm>

namespace mforce {

void Instrument::render(float* out, int frames) {
  std::fill(out, out + frames, 0.0f);

  for (auto& rn : rendered_) {
    int start = rn.startSample;
    int len = int(rn.samples.size());
    for (int i = 0; i < len; ++i) {
      int outIdx = start + i;
      if (outIdx >= 0 && outIdx < frames)
        out[outIdx] += rn.samples[i];
    }
  }

  apply_output_stage(out, frames, volume, peakGuard);
}

void apply_output_stage(float* out, int frames, float volume, bool peakGuard) noexcept {
  if (volume != 1.0f) {
    for (int i = 0; i < frames; ++i)
      out[i] *= volume;
  }
  if (peakGuard) {
    // Peak guard: bound output to ±0.999 with a smooth knee above 0.95.
    for (int i = 0; i < frames; ++i)
      out[i] = soft_clip(out[i]);
  }
}

void Instrument::add_rendered(float startTime, const float* data, int count) {
  RenderedNote rn;
  rn.startSample = int(startTime * float(ctx_.sampleRate));
  rn.samples.assign(data, data + count);
  rendered_.push_back(std::move(rn));
}

} // namespace mforce

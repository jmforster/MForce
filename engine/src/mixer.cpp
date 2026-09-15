#include "mforce/render/mixer.h"
#include "mforce/render/limiter.h"
#include <cmath>
#include <algorithm>

namespace mforce {

StereoMixer::StereoMixer()
: gainL(std::make_shared<ConstantSource>(1.0f))
, gainR(std::make_shared<ConstantSource>(1.0f)) {}

void StereoMixer::render(const RenderContext& ctx, float* outLR, int frames) {
  std::fill(outLR, outLR + frames*2, 0.0f);

  gainL->prepare(ctx, frames);
  gainR->prepare(ctx, frames);

  std::vector<float> mono(frames);

  for (auto& ch : channels) {
    ch.volume->prepare(ctx, frames);
    ch.pan->prepare(ctx, frames);

    std::fill(mono.begin(), mono.end(), 0.0f);
    ch.source->render(ctx, mono.data(), frames);

    for (int i = 0; i < frames; ++i) {
      float gl = gainL->next();
      float gr = gainR->next();
      float v = ch.volume->next();
      float p = ch.pan->next();               // [-1,1]
      p = std::clamp(p, -1.0f, 1.0f);

      // Equal-power panning: map [-1,1] -> [0,1], normalized to UNITY at
      // center (Matt 2026-09-14: mono/center patches write x1.0 to both
      // channels so WAV loudness == UI's unity-mono monitoring). Edges
      // reach +3 dB relative to the old -3 dB-center law; soft_clip below
      // still bounds the mix.
      float t = (p + 1.0f) * 0.5f;
      constexpr float kRoot2 = 1.41421356237309504880f;
      float aL = kRoot2 * std::cos(t * 0.5f * 3.14159265358979323846f);
      float aR = kRoot2 * std::sin(t * 0.5f * 3.14159265358979323846f);

      float s = mono[i] * v;
      outLR[i*2 + 0] += s * aL * gl;
      outLR[i*2 + 1] += s * aR * gr;
    }
  }
  // Peak guard: bound mix output to ±0.999.
  for (int i = 0; i < frames * 2; ++i)
    outLR[i] = soft_clip(outLR[i]);
}

} // namespace mforce

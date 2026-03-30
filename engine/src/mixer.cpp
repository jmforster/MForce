#include "mforce/render/mixer.h"
#include <cmath>
#include <algorithm>

namespace mforce {

StereoMixer::StereoMixer()
: gainL(std::make_shared<ConstantSource>(1.0f))
, gainR(std::make_shared<ConstantSource>(1.0f)) {}

void StereoMixer::render(float* outLR, int frames) {
  std::fill(outLR, outLR + frames*2, 0.0f);

  gainL->prepare(frames);
  gainR->prepare(frames);

  std::vector<float> mono(frames);

  for (auto& ch : channels) {
    ch.volume->prepare(frames);
    ch.pan->prepare(frames);

    std::fill(mono.begin(), mono.end(), 0.0f);
    ch.source->render(mono.data(), frames);

    for (int i = 0; i < frames; ++i) {
      float gl = gainL->next();
      float gr = gainR->next();
      float v = ch.volume->next();
      float p = ch.pan->next();               // [-1,1]
      p = std::clamp(p, -1.0f, 1.0f);

      // Equal-power panning: map [-1,1] -> [0,1]
      float t = (p + 1.0f) * 0.5f;
      float aL = std::cos(t * 0.5f * 3.14159265358979323846f);
      float aR = std::sin(t * 0.5f * 3.14159265358979323846f);

      float s = mono[i] * v;
      outLR[i*2 + 0] += s * aL * gl;
      outLR[i*2 + 1] += s * aR * gr;
    }
  }
}

} // namespace mforce

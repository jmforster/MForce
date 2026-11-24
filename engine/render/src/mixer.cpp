#include "mforce/render/mixer.h"
#include "mforce/render/limiter.h"
#include <cmath>
#include <algorithm>

namespace mforce {

Mixer::Mixer()
: gainL(std::make_shared<ConstantSource>(1.0f))
, gainR(std::make_shared<ConstantSource>(1.0f)) {}

void Mixer::prepare(const RenderContext& ctx) {
  ctx_ = ctx.with_clock(&clock_);
  gainL->prepare(ctx_);
  gainR->prepare(ctx_);
  for (auto& ch : channels) {
    ch.volume->prepare(ctx_);
    ch.pan->prepare(ctx_);
    ch.source->prepare(ctx);
  }
}

void Mixer::render(float* outLR, int frames) {
  std::fill(outLR, outLR + frames*2, 0.0f);

  gainL->start(frames);
  gainR->start(frames);

  std::vector<float> mono(frames);

  for (auto& ch : channels) {
    ch.volume->start(frames);
    ch.pan->start(frames);

    std::fill(mono.begin(), mono.end(), 0.0f);
    ch.source->render(mono.data(), frames);
    mix(ch, mono.data(), outLR, frames);
  }
  // Peak guard: bound mix output to ±0.999.
  for (int i = 0; i < frames * 2; ++i)
    outLR[i] = soft_clip(outLR[i]);
}

void Mixer::start_running(int frames, int largestBlock) {
  gainL->start(frames);
  gainR->start(frames);
  for (auto& ch : channels) {
    ch.volume->start(frames);
    ch.pan->start(frames);
    ch.source->start_running(frames);
  }
  scratch_.assign(size_t(std::max(1, largestBlock)), 0.0f);
}

void Mixer::render_running(float* outLR, int frames) noexcept {
  const int block = int(scratch_.size());
  for (int done = 0; done < frames;) {
    const int n = std::min(block, frames - done);
    float* out = outLR + 2 * done;
    std::fill(out, out + 2 * n, 0.0f);
    for (auto& ch : channels) {
      ch.source->render_running(scratch_.data(), n);
      mix(ch, scratch_.data(), out, n);
    }
    for (int i = 0; i < n * 2; ++i)
      out[i] = soft_clip(out[i]);
    done += n;
  }
}

// One channel's mono frames into the stereo mix: per sample, a tick of this
// mixer's clock, the gains, the channel's volume and pan pulled, then added.
void Mixer::mix(Channel& ch, const float* mono, float* outLR, int frames) {
  for (int i = 0; i < frames; ++i) {
    clock_.tick();
    float gl = gainL->next();
    float gr = gainR->next();
    float v = ch.volume->next();
    float p = ch.pan->next();                  // [-1,1]
    p = std::clamp(p, -1.0f, 1.0f);

    // Equal-power panning: map [-1,1] -> [0,1], normalized to unity at
    // center, so a mono or centred patch writes x1.0 to both channels;
    // the edges reach +3 dB over the centre. soft_clip below still bounds
    // the mix.
    float t = (p + 1.0f) * 0.5f;
    constexpr float kRoot2 = 1.41421356237309504880f;
    float aL = kRoot2 * std::cos(t * 0.5f * 3.14159265358979323846f);
    float aR = kRoot2 * std::sin(t * 0.5f * 3.14159265358979323846f);

    float s = mono[i] * v;
    outLR[i*2 + 0] += s * aL * gl;
    outLR[i*2 + 1] += s * aR * gr;
  }
}

} // namespace mforce

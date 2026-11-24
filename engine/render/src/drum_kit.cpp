#include "mforce/render/drum_kit.h"
#include <string>

namespace mforce {

void DrumKit::add(std::shared_ptr<ValueSource> source) {
  Drum d;
  d.source = std::move(source);
  drums_.push_back(std::move(d));
}

void DrumKit::add_gap() { drums_.push_back(Drum{}); }

void DrumKit::prepare(const RenderContext& c) {
  Instrument::prepare(c);
  for (auto& d : drums_) {
    d.ctx = c.with_clock(d.clock.get());
    if (d.source) d.source->prepare(d.ctx);
  }
}

void DrumKit::play(const Performance& part) {
  for (const auto& h : part.hits)
    play_hit(h.drumNumber, h.velocity, h.duration, h.start);
}

void DrumKit::collect_faults(std::vector<FaultReport>& out) {
  for (size_t i = 0; i < drums_.size(); ++i) {
    auto& src = drums_[i].source;
    if (!src) continue;
    const unsigned char f = src->take_faults();
    if (f) out.push_back({"drum " + std::to_string(i), src->type_name(), f});
  }
}

void DrumKit::play_hit(int drumNumber, float velocity, float duration, float startTime) {
  if (drumNumber < 0 || drumNumber >= int(drums_.size())) return;

  auto& d = drums_[size_t(drumNumber)];
  if (!d.source) return;
  int durSamples = int(duration * float(render_context().sampleRate));

  d.source->start(durSamples);

  std::vector<float> buf(durSamples);
  for (int i = 0; i < durSamples; ++i) {
    d.clock->tick();
    buf[i] = d.source->next() * velocity;
  }

  add_rendered(startTime, buf.data(), durSamples);
}

} // namespace mforce

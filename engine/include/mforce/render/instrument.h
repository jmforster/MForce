#pragma once
#include "mforce/contract/instrument_class.h"
#include "mforce/contract/performance.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/render/mono_source.h"
#include <vector>

namespace mforce {

// What a part's performance becomes: each note or hit pre-rendered into a
// buffer at its start, and render() the mix of everything played.
class Instrument : public MonoSource {
public:
  // One played note or hit: its samples and the frame they start at.
  struct RenderedNote {
    std::vector<float> samples;
    int startSample{0};
  };

  float volume{1.0f};
  // When true, render() bounds each sample with soft_clip as a peak guard.
  // False when the caller peak-normalizes the raw sum itself.
  bool peakGuard{true};

  // What a performer may know about this instrument.
  virtual const InstrumentClass& instrument_class() const = 0;

  // Pre-renders one part's performance. Called once per part; several parts
  // may share an instrument. render() mixes everything played.
  virtual void play(const Performance& part) = 0;

  // Forgets what was played.
  void clear() { rendered_.clear(); }

  // The host's facts arrive here, once; every per-note length is computed
  // from them and every per-note envelope is prepared with them.
  void prepare(const RenderContext& c) override { ctx_ = c; }
  const RenderContext& render_context() const { return ctx_; }

  // The played notes summed at their starts, scaled by `volume`, then the
  // peak guard.
  void render(float* out, int frames) override;

  // What was played, in the order it was played.
  const std::vector<RenderedNote>& rendered() const { return rendered_; }

protected:
  // `count` samples from `data` played at `startTime` seconds.
  void add_rendered(float startTime, const float* data, int count);

private:
  RenderContext ctx_{};
  std::vector<RenderedNote> rendered_;
};

} // namespace mforce

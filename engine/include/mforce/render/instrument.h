#pragma once
#include "mforce/render/mixer.h"
#include "mforce/render/limiter.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/music/pitch_bend.h"
#include "mforce/music/pitch_curve.h"
#include "mforce/source/multiplex_source.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Instrument — shared render infrastructure for pitched and percussion.
// Pre-renders notes/hits into buffers, mixes them in render().
// ---------------------------------------------------------------------------
struct Instrument : MonoSource {

  struct RenderedNote {
    std::vector<float> samples;
    int startSample{0};
  };

  int sampleRate{48000};
  float volume{1.0f};
  std::vector<RenderedNote> renderedNotes;

  void render(const RenderContext& /*ctx*/, float* out, int frames) override {
    std::fill(out, out + frames, 0.0f);

    for (auto& rn : renderedNotes) {
      int start = rn.startSample;
      int len = int(rn.samples.size());
      for (int i = 0; i < len; ++i) {
        int outIdx = start + i;
        if (outIdx >= 0 && outIdx < frames)
          out[outIdx] += rn.samples[i];
      }
    }

    if (volume != 1.0f) {
      for (int i = 0; i < frames; ++i)
        out[i] *= volume;
    }
    // Peak guard: bound output to ±0.999 with a smooth knee above 0.95.
    for (int i = 0; i < frames; ++i)
      out[i] = soft_clip(out[i]);
  }

protected:
  void add_rendered(float startTime, float* data, int count) {
    RenderedNote rn;
    rn.startSample = int(startTime * float(sampleRate));
    rn.samples.assign(data, data + count);
    renderedNotes.push_back(std::move(rn));
  }
};

// ---------------------------------------------------------------------------
// PitchedInstrument — polyphonic voice pool for pitched sounds.
// Each voice is a copy of the same source graph with parameterized frequency.
// ---------------------------------------------------------------------------
struct PitchedInstrument final : Instrument {

  // A paramMap slot resolved to the graph node that consumes the value
  // (consumer + paramName) plus the ConstantSource that normally supplies
  // the nominal value. play_note can swap the consumer's param edge between
  // originalCS (for constant pitch) and a time-varying source (for bends),
  // per the "parameters are pluggable ValueSource edges" architecture.
  struct ParamSlot {
    std::shared_ptr<ValueSource>    consumer;
    std::string                      paramName;
    std::shared_ptr<ConstantSource>  originalCS;
    // Node id the paramMap targets (e.g. "Var1" for "Var1.val"). Used by
    // play_note to fan values into Multiplex clones' matching nodes when
    // the voice's output is a MultiplexSource.
    std::string                      targetNodeId;
  };

  struct VoiceGraph {
    std::shared_ptr<ValueSource> source;
    std::unordered_map<std::string, ParamSlot> params;
    // If this voice's output IS a MultiplexSource, fan paramMap changes
    // into its clones. Captured at voice-pool build time by casting
    // `source`. Null for non-Multiplex voices — fan-out is a no-op.
    std::shared_ptr<MultiplexSource> topMultiplex;
  };

  float hiBoost{0.0f};
  std::vector<VoiceGraph> voicePool;
  int nextVoice{0};

  void play_note(float noteNumber, float velocity, float duration, float startTime,
                 const PitchCurve* curve = nullptr) {
    auto& vg = voicePool[nextVoice % voicePool.size()];
    nextVoice++;

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    auto it = vg.params.find("frequency");
    if (it != vg.params.end()) {
      auto& slot = it->second;
      if (curve) {
        // Build an Envelope from the curve, wrap in a PitchBendSource that
        // emits baseHz * 2^(semi/12), and plug it into the consumer's param
        // edge — replacing the nominal ConstantSource for this note.
        auto env = compile_pitch_curve(*curve, sampleRate);
        auto pbs = std::make_shared<PitchBendSource>(freq, std::move(env));
        slot.consumer->set_param(slot.paramName, pbs);
      } else {
        // Plain note: set the nominal value and restore the edge to the
        // original ConstantSource (idempotent if already restored).
        slot.originalCS->set(freq);
        slot.consumer->set_param(slot.paramName, slot.originalCS);
        // Fan out to Multiplex clones so each internal copy retunes too.
        // No-op when the voice's output isn't a Multiplex.
        if (vg.topMultiplex && !slot.targetNodeId.empty()) {
          vg.topMultiplex->set_clone_param(slot.targetNodeId, slot.paramName, freq);
        }
      }
    }

    // Frequency-dependent brightness compensation
    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = velocity * (1.0f + boost);

    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);

    std::vector<float> buf(durSamples);
    for (int i = 0; i < durSamples; ++i)
      buf[i] = vg.source->next() * gain;

    add_rendered(startTime, buf.data(), durSamples);
  }
};

// ---------------------------------------------------------------------------
// DrumKit — percussion instrument with indexed source graphs.
// Each drum number maps to a distinct source. No per-sound polyphony needed.
// ---------------------------------------------------------------------------
struct DrumKit final : Instrument {

  struct DrumSource {
    std::shared_ptr<ValueSource> source;
  };

  std::vector<DrumSource> sources;  // indexed by drum number

  void play_hit(int drumNumber, float velocity, float duration, float startTime) {
    if (drumNumber < 0 || drumNumber >= int(sources.size()))
      return;  // silently ignore out-of-range

    auto& ds = sources[drumNumber];
    int durSamples = int(duration * float(sampleRate));

    RenderContext ctx{ sampleRate };
    ds.source->prepare(ctx, durSamples);

    std::vector<float> buf(durSamples);
    for (int i = 0; i < durSamples; ++i)
      buf[i] = ds.source->next() * velocity;

    add_rendered(startTime, buf.data(), durSamples);
  }
};

} // namespace mforce

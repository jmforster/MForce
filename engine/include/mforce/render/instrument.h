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
  // When true (default), render() applies soft_clip per sample as a peak
  // guard. Set false when the caller will peak-normalize the raw sum itself
  // (e.g. the UI's chord render, which needs to see the un-clipped peak so
  // its scale-to-0.95 normalization can do real work).
  bool peakGuard{true};
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
    if (peakGuard) {
      // Peak guard: bound output to ±0.999 with a smooth knee above 0.95.
      for (int i = 0; i < frames; ++i)
        out[i] = soft_clip(out[i]);
    }
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
    std::shared_ptr<ConstantSource>  originalCS;   // null for config slots
    // Node id the paramMap targets (e.g. "Var1" for "Var1.val"). Used by
    // play_note to fan values into Multiplex clones' matching nodes when
    // the voice's output is a MultiplexSource.
    std::string                      targetNodeId;
    // Config-target slot: the mapped value is delivered via
    // consumer->set_config(paramName, v) instead of a ConstantSource edge.
    bool                             isConfig{false};
    // Optional frequency→value transfer curve — the C++ port of legacy
    // ParameterMapping's Function. Breakpoints (hz, value); evaluated with
    // linear interpolation in log-frequency, clamped at the end values.
    // Empty curve = identity (slot receives the frequency itself).
    std::vector<std::pair<float, float>> curve;
    // Optional velocity→MULTIPLIER curve, composed on top of the frequency
    // mapping: value = map(freq) * vmap(velocity). Breakpoints (velocity
    // 0..1, multiplier); linear interpolation, clamped at the ends. Empty =
    // 1.0. This is the AF-style "Veloc" mod input (velocity->brightness).
    std::vector<std::pair<float, float>> vcurve;

    float vmap(float vel) const {
      if (vcurve.empty()) return 1.0f;
      if (vel <= vcurve.front().first) return vcurve.front().second;
      if (vel >= vcurve.back().first)  return vcurve.back().second;
      for (size_t i = 1; i < vcurve.size(); ++i) {
        if (vel <= vcurve[i].first) {
          float t = (vel - vcurve[i - 1].first) /
                    (vcurve[i].first - vcurve[i - 1].first);
          return vcurve[i - 1].second +
                 (vcurve[i].second - vcurve[i - 1].second) * t;
        }
      }
      return vcurve.back().second;
    }

    float map(float freq) const {
      if (curve.empty()) return freq;
      if (freq <= curve.front().first) return curve.front().second;
      if (freq >= curve.back().first)  return curve.back().second;
      for (size_t i = 1; i < curve.size(); ++i) {
        if (freq <= curve[i].first) {
          float lf = std::log(freq / curve[i - 1].first) /
                     std::log(curve[i].first / curve[i - 1].first);
          return curve[i - 1].second +
                 (curve[i].second - curve[i - 1].second) * lf;
        }
      }
      return curve.back().second;
    }
  };

  struct VoiceGraph {
    std::shared_ptr<ValueSource> source;
    // One logical paramMap name (e.g. "frequency") can target multiple graph
    // edges (e.g. dual-stack instruments where both fmBody.frequency AND
    // fmTine.frequency must be retuned per note). Vector size 1 is the
    // common case; vector empty means no mapping for that name.
    std::unordered_map<std::string, std::vector<ParamSlot>> params;
    // If this voice's output IS a MultiplexSource, fan paramMap changes
    // into its clones. Captured at voice-pool build time by casting
    // `source`. Null for non-Multiplex voices — fan-out is a no-op.
    std::shared_ptr<MultiplexSource> topMultiplex;
  };

  float hiBoost{0.0f};
  // Damper stage: after the note's scored duration, the voice keeps rendering
  // for releaseSeconds with an exponential fade reaching -60 dB at the end,
  // instead of truncating the buffer at note-off. 0 = legacy hard cut.
  float releaseSeconds{0.0f};
  std::vector<VoiceGraph> voicePool;
  int nextVoice{0};

  // Streaming-mode handoff: same set-frequency + prepare logic as play_note,
  // but returns the prepared voice source instead of rendering immediately.
  // Caller is responsible for calling next() durSamples times (e.g. from the
  // audio thread). Used by mforce_ui's live keyboard path.
  struct StreamingVoice {
    std::shared_ptr<ValueSource> source;
    int   durSamples{0};
    // gain includes the instrument's pre-clip volume: streaming callers pull
    // samples directly and never pass through Instrument::render, so the
    // master gain must ride along here or hot chains hit the caller's
    // soft_clip raw (UI keyboard distortion, 2026-08-09).
    float gain{1.0f};
    // Damper window (releaseSeconds in samples). The caller owns the fade:
    // keep pulling this many samples past durSamples with an exponential
    // decay to -60 dB, matching play_note's offline damper.
    int   releaseSamples{0};
  };

  StreamingVoice prepare_voice(float noteNumber, float velocity, float duration,
                               const PitchCurve* curve = nullptr) {
    auto& vg = voicePool[nextVoice % voicePool.size()];
    nextVoice++;

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    auto it = vg.params.find("frequency");
    if (it != vg.params.end()) {
      for (auto& slot : it->second) {
        if (slot.isConfig) {
          // Frequency-driven config (e.g. residue curves): mapped scalar via
          // set_config, applied before prepare so per-note state rebuilds.
          slot.consumer->set_config(slot.paramName, slot.map(freq) * slot.vmap(velocity));
        } else if (curve && slot.curve.empty()) {
          auto env = compile_pitch_curve(*curve, sampleRate);
          auto pbs = std::make_shared<PitchBendSource>(freq, std::move(env));
          slot.consumer->set_param(slot.paramName, pbs);
        } else {
          float v = slot.map(freq) * slot.vmap(velocity);
          slot.originalCS->set(v);
          slot.consumer->set_param(slot.paramName, slot.originalCS);
          if (vg.topMultiplex && !slot.targetNodeId.empty()) {
            vg.topMultiplex->set_clone_param(slot.targetNodeId, slot.paramName, v);
          }
        }
      }
    }

    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = velocity * (1.0f + boost) * volume;

    int relSamples = int(releaseSeconds * float(sampleRate));
    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples + relSamples);

    return { vg.source, durSamples, gain, relSamples };
  }

  void play_note(float noteNumber, float velocity, float duration, float startTime,
                 const PitchCurve* curve = nullptr) {
    auto& vg = voicePool[nextVoice % voicePool.size()];
    nextVoice++;

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    auto it = vg.params.find("frequency");
    if (it != vg.params.end()) {
      for (auto& slot : it->second) {
        if (slot.isConfig) {
          // Frequency-driven config (e.g. residue curves): mapped scalar via
          // set_config, applied before prepare so per-note state rebuilds.
          slot.consumer->set_config(slot.paramName, slot.map(freq) * slot.vmap(velocity));
        } else if (curve && slot.curve.empty()) {
          // Build an Envelope from the curve, wrap in a PitchBendSource that
          // emits baseHz * 2^(semi/12), and plug it into the consumer's param
          // edge — replacing the nominal ConstantSource for this note.
          // Curved slots are excluded: they carry a mapped scalar, not the
          // frequency itself, so pitch bend doesn't apply.
          auto env = compile_pitch_curve(*curve, sampleRate);
          auto pbs = std::make_shared<PitchBendSource>(freq, std::move(env));
          slot.consumer->set_param(slot.paramName, pbs);
        } else {
          // Plain note: set the nominal (or curve-mapped) value and restore
          // the edge to the original ConstantSource (idempotent if already
          // restored).
          float v = slot.map(freq) * slot.vmap(velocity);
          slot.originalCS->set(v);
          slot.consumer->set_param(slot.paramName, slot.originalCS);
          // Fan out to Multiplex clones so each internal copy retunes too.
          // No-op when the voice's output isn't a Multiplex.
          if (vg.topMultiplex && !slot.targetNodeId.empty()) {
            vg.topMultiplex->set_clone_param(slot.targetNodeId, slot.paramName, v);
          }
        }
      }
    }

    // Frequency-dependent brightness compensation
    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = velocity * (1.0f + boost);

    int relSamples = int(releaseSeconds * float(sampleRate));
    int totalSamples = durSamples + relSamples;

    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, totalSamples);

    std::vector<float> buf(totalSamples);
    for (int i = 0; i < durSamples; ++i)
      buf[i] = vg.source->next() * gain;
    if (relSamples > 0) {
      // exp decay hitting -60 dB (1e-3) at the end of the release window
      float k = std::log(1e-3f) / float(relSamples);
      for (int j = 0; j < relSamples; ++j)
        buf[durSamples + j] = vg.source->next() * gain * std::exp(k * float(j));
    }

    add_rendered(startTime, buf.data(), totalSamples);
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

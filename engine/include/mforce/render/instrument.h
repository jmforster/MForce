#pragma once
#include "mforce/render/mixer.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/equal_temperament.h"
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

namespace mforce {

// An Instrument turns abstract note events into audio.
//
// Built from a patch graph blueprint. Holds N voice instances (polyphony),
// each a complete copy of the source graph with its own param targets.
// play_note() claims a free voice, sets frequency/velocity, prepares it.
// render() mixes all active voices.
//
// Implements MonoSource so it plugs directly into a Channel → StereoMixer.
struct Instrument final : MonoSource {

  // A voice graph = source + param targets (reusable for multiple notes)
  struct VoiceGraph {
    std::shared_ptr<ValueSource> source;
    std::unordered_map<std::string, std::shared_ptr<ConstantSource>> params;
  };

  // A rendered note = pre-rendered samples + timing
  struct RenderedNote {
    std::vector<float> samples;
    int startSample{0};
  };

  int sampleRate{48000};
  std::vector<VoiceGraph> voicePool;
  std::vector<RenderedNote> renderedNotes;
  int nextVoice{0};

  // Schedule a note. Picks a voice graph, configures it, renders immediately.
  void play_note(float noteNumber, float velocity, float duration, float startTime) {
    // Round-robin voice selection — each voice is an independent graph copy
    auto& vg = voicePool[nextVoice % voicePool.size()];
    nextVoice++;

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    if (vg.params.count("frequency"))
      vg.params["frequency"]->set(freq);

    // Prepare and render into buffer
    vg.source->prepare(durSamples);

    RenderedNote rn;
    rn.startSample = int(startTime * float(sampleRate));
    rn.samples.resize(durSamples);
    for (int i = 0; i < durSamples; ++i)
      rn.samples[i] = vg.source->next() * velocity;

    renderedNotes.push_back(std::move(rn));
  }

  // MonoSource interface: mix all rendered notes into output buffer
  void render(float* out, int frames) override {
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
  }
};

} // namespace mforce

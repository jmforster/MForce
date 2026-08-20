#pragma once
#include "mforce/render/mixer.h"
#include "mforce/render/limiter.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/music/pitch_bend.h"
#include "mforce/music/pitch_curve.h"
#include "mforce/render/perform_source.h"
#include "mforce/source/multiplex_source.h"
#include <cstdio>
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

  // (ParamSlot retired 2026-08-18 — plan_perform_source_p1.md T7. Its map/
  // vmap formulas live on verbatim as CurveNode's LogX/LogLog and Linear
  // interp modes; its delivery loop became apply_note_bindings.)

  // A converted paramMap entry that must be PUSH-delivered at note time
  // (perform_source_design.md §5): setting targets (set_setting rebuilds
  // state at prepare — no pointer to pull) and every entry on a Multiplex
  // voice (clone fan-out keeps push semantics wholesale, bit-safe).
  // `chain` is the transfer chain rooted at the voice's PerformOut
  // adapters; evaluated once per note at Realization/Setup.
  struct PushBinding {
    std::shared_ptr<ValueSource>     consumer;
    std::string                      paramName;
    std::string                      targetNodeId;  // Multiplex clone fan
    std::shared_ptr<ValueSource>     chain;
    std::shared_ptr<ConstantSource>  cs;            // non-setting delivery
    bool                             isConfig{false};
  };

  struct VoiceGraph {
    std::shared_ptr<ValueSource> source;
    // If this voice's output IS a MultiplexSource, fan paramMap changes
    // into its clones. Captured at voice-pool build time by casting
    // `source`. Null for non-Multiplex voices — fan-out is a no-op.
    std::shared_ptr<MultiplexSource> topMultiplex;

    // --- PerformSource bindings (P1 conversion; ParamSlot path retires
    // once these are the only model — plan_perform_source_p1.md T7) ---
    std::shared_ptr<PerformSource> performSource;
    std::shared_ptr<ValueSource>   freqOut, velOut;   // shared leaf adapters
    std::vector<PushBinding>       pushBindings;
    // Bend-graft swap targets (P1 keeps the legacy PitchBendSource graft;
    // P3 retires it): exactly the entries the old path grafted — those with
    // an empty freq-curve, INCLUDING vcurve-only entries (whose velocity
    // multiplier the old graft dropped for the bent note; preserved
    // verbatim). `restore` is what the pin holds on non-bent notes: the
    // bare freqOut, or the vcurve chain.
    struct BendSwap {
      std::shared_ptr<ValueSource> consumer;
      std::string                  paramName;
      std::shared_ptr<ValueSource> restore;
    };
    std::vector<BendSwap> bendSwaps;
  };

  float hiBoost{0.0f};
  std::vector<VoiceGraph> voicePool;
  int nextVoice{0};

  // Live-voice slot accounting (legacy SourcePool semantics restored
  // 2026-08-18): acquire hands out an idle slot rotating from nextVoice,
  // release returns it to the pool when the voice stops sounding. Live
  // callers (mforce_ui) own the sounding/steal policy and serialize these
  // under their audio mutex — not internally synchronized. The scheduled
  // path (play_note / prepare_voice) doesn't participate: overlap there is
  // fixed by score timing, blind round-robin suffices.
  std::vector<uint8_t> slotInUse;
  int acquire_voice() {
    int n = int(voicePool.size());
    if (n == 0) return -1;
    if (int(slotInUse.size()) != n) slotInUse.assign(size_t(n), 0);
    for (int k = 0; k < n; ++k) {
      int s = (nextVoice + k) % n;
      if (!slotInUse[s]) {
        slotInUse[s] = 1;
        nextVoice = s + 1;   // rotation freshness for the next acquire
        return s;
      }
    }
    return -1;   // every slot sounding — caller picks a steal victim
  }
  void release_voice(int slot) {
    if (slot >= 0 && slot < int(slotInUse.size())) slotInUse[slot] = 0;
  }
  void release_all_voices() { slotInUse.assign(voicePool.size(), 0); }

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
  };

  StreamingVoice prepare_voice(float noteNumber, float velocity, float duration,
                               const PitchCurve* curve = nullptr) {
    int slot = int(nextVoice % int(voicePool.size()));
    nextVoice++;
    return prepare_voice_at(slot, noteNumber, velocity, duration, curve);
  }

  // Slot-explicit variant: callers that know which pool slots are still
  // sounding (mforce_ui's live keyboard) pick a genuinely free slot instead
  // of trusting the blind round-robin — which happily wraps onto a held
  // note's slot while idle slots sit in between (2026-08-18: a held note +
  // poolSize staccato notes cut the held note). Does not advance nextVoice;
  // slot-aware callers manage their own rotation.
  // Realization/Setup delivery (perform_source_design.md §5): one NoteState
  // write, push-binding evaluation (settings + Multiplex fans), and the P1
  // bend graft on the swap targets. Push deliveries happen BEFORE
  // vg.source->prepare — settings rebuild per-note state there.
  void apply_note_bindings(VoiceGraph& vg, float freq, float velocity,
                           int durSamples, const PitchCurve* curve) {
    if (vg.performSource)
      vg.performSource->set_note(freq, velocity, durSamples);

    for (auto& b : vg.pushBindings) {
      b.chain->next();
      float v = b.chain->current();
      if (b.isConfig) {
        b.consumer->set_setting(b.paramName, v);
      } else {
        b.cs->set(v);
        b.consumer->set_param(b.paramName, b.cs);
        if (vg.topMultiplex && !b.targetNodeId.empty())
          vg.topMultiplex->set_clone_param(b.targetNodeId, b.paramName, v);
      }
    }

    if (curve) {
      auto env = compile_pitch_curve(*curve, sampleRate);
      auto pbs = std::make_shared<PitchBendSource>(freq, std::move(env));
      for (auto& bs : vg.bendSwaps)
        bs.consumer->set_param(bs.paramName, pbs);
    } else {
      for (auto& bs : vg.bendSwaps)
        bs.consumer->set_param(bs.paramName, bs.restore);
    }
  }

  StreamingVoice prepare_voice_at(int slot, float noteNumber, float velocity,
                                  float duration,
                                  const PitchCurve* curve = nullptr) {
    auto& vg = voicePool[slot];

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    apply_note_bindings(vg, freq, velocity, durSamples, curve);

    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = velocity * (1.0f + boost) * volume;

    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);

    return { vg.source, durSamples, gain };
  }

  void play_note(float noteNumber, float velocity, float duration, float startTime,
                 const PitchCurve* curve = nullptr) {
    auto& vg = voicePool[nextVoice % voicePool.size()];
    nextVoice++;

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    apply_note_bindings(vg, freq, velocity, durSamples, curve);

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

    // Note-contained-sound check (2026-08-13 spec): output must be at the
    // audibility floor by duration end. WARN, never fail — a miss is a
    // patch-design finding, and optimizer runs must keep scoring.
    int checkStart = std::max(0, durSamples - sampleRate / 1000);
    float tailPeak = 0.0f;
    for (int i = checkStart; i < durSamples; ++i)
      tailPeak = std::max(tailPeak, std::fabs(buf[i]));
    if (tailPeak > 1e-4f)
      std::fprintf(stderr,
          "[containment] note %.1f (%.1f Hz) at t=%.2fs: %.1f dBFS in final 1 ms\n",
          noteNumber, freq, startTime, 20.0f * std::log10(tailPeak));

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

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

  // Voice tail allowance (backlog 63, 2026-09-15): a voice used to live
  // exactly duration samples, so Reverb/ringing-filter state INSIDE the
  // voice was cut mid-sample at envelope end — the faint click at note
  // end no release length could fix (patch-side workaround was a trailing
  // hold-at-zero stage). Every voice now renders/lives this much longer:
  // envelopes are programmatically 0 past their last stage (envelope.h),
  // so the window is pure ring-out. Musical semantics (envelope stage
  // layout, perform fields, bend curves) still use the un-extended
  // duration — prepare() and set_note() see durSamples, only the render/
  // life length grows.
  static constexpr float kVoiceTailSec = 0.4f;
  // Adaptive ring-out bounds (backlog 63b): a voice still above kRingFloor
  // (~-60 dBFS) at the fixed tail's end keeps rendering/living until it
  // decays below the floor or reaches kMaxRingSec past its duration.
  // Percussion-with-physics (BandedWG bars/bowls) rings for seconds; the
  // fixed tail alone cut it mid-ring with a click (Matt, REVIEW 59).
  static constexpr float kRingFloor  = 0.001f;
  static constexpr float kMaxRingSec = 8.0f;

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
    bool                             isSetting{false};
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
    // Shared leaf adapters (frequency articulates the bend as of P3 —
    // the PitchBendSource graft and its BendSwap machinery are retired;
    // plan_perform_source_p3.md T1).
    std::shared_ptr<ValueSource>   freqOut, velOut, wheelOut, pressOut,
                                   durOut, transOut;
    std::vector<PushBinding>       pushBindings;
    // Loop tails consumed only by tap edges (feedback_loop_design.md §3.3):
    // never reached by the pull, ticked once per sample AFTER the root pull
    // (their tap consumers must read the previous value first; their own
    // forward inputs are reached through the normal multi-consumer
    // RefSource wrap, so nothing double-advances).
    std::vector<std::shared_ptr<ValueSource>> advanceList;
    // JSON node id -> this voice's clone (retained from build_graph so
    // offline capture can resolve display ids; ~node-count shared_ptrs,
    // the graph outlives them anyway). Empty for mixer-path instruments.
    std::unordered_map<std::string, std::shared_ptr<ValueSource>> nodesById;
  };

  float hiBoost{0.0f};
  // The instrument as played — mod wheel, channel pressure — outliving
  // every note (perform_source_design.md §2.1). MIDI/UI threads store;
  // each voice's PerformSource smooths and reports it.
  std::shared_ptr<InstrumentState> instrumentState =
      std::make_shared<InstrumentState>();
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

  // --- Offline per-node capture (render-capture unification spec
  // 2026-09-13). Registered by the UI's Generate; play_note SUMS each
  // captured clone's current() into a timeline-length buffer at the
  // note's start offset — strips accumulate across voices exactly like
  // add_rendered accumulates audio. OFFLINE path only: the streaming/
  // live path never touches this, and with no registration the render
  // loop pays one branch. Buffers are pre-allocated here — nothing
  // allocates inside the per-sample loop.
  struct CaptureEntry { ValueSource* node; int bufIdx; };
  std::vector<std::vector<CaptureEntry>> capturePerVoice; // parallel to voicePool
  std::vector<std::vector<float>> captureBuffers;         // per id, timeline frames

  void capture_begin(const std::vector<std::string>& ids, int timelineFrames) {
    captureBuffers.assign(ids.size(), {});
    for (auto& b : captureBuffers) b.assign(size_t(timelineFrames), 0.0f);
    capturePerVoice.assign(voicePool.size(), {});
    for (size_t v = 0; v < voicePool.size(); ++v)
      for (size_t k = 0; k < ids.size(); ++k) {
        auto it = voicePool[v].nodesById.find(ids[k]);
        if (it != voicePool[v].nodesById.end())
          capturePerVoice[v].push_back({it->second.get(), int(k)});
      }
  }
  void capture_end() { capturePerVoice.clear(); }  // buffers stay for the caller

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
    // P3: the voice's sample clock. Streaming callers MUST call
    // performSource->tick() once per sample before source->next(), or bend
    // and wheel/pressure freeze at their note-on values. Null for graphs
    // with no perform context.
    std::shared_ptr<PerformSource> performSource;
    // Loop tails consumed only by tap edges. Streaming callers MUST tick
    // each entry once per sample AFTER source->next(), or tap-closed
    // feedback loops fall silent (same contract class as performSource).
    // Empty for graphs with no tap-only tails.
    std::vector<std::shared_ptr<ValueSource>> advanceList;
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
                           int durSamples, float durSeconds,
                           const PitchCurve* curve) {
    if (vg.performSource) {
      // P3: the bend rides the PerformSource itself — .frequency
      // articulates base * 2^(bend(t)/12), advanced by tick() from the
      // render driver. The PitchBendSource graft (and its swap targets)
      // is retired; every consumer of the frequency chain sees the bend,
      // including curve-fed pins the graft deliberately skipped
      // (re-listen item, plan_perform_source_p3.md T1).
      std::shared_ptr<Envelope> bend;
      if (curve) {
        bend = compile_pitch_curve(*curve, sampleRate);
        bend->prepare(RenderContext{sampleRate}, durSamples);
      }
      vg.performSource->set_note(freq, velocity, durSamples, durSeconds,
                                 std::move(bend));
    }

    // Push deliveries evaluate the chain ONCE at note-on (Setup), so a bend
    // never reaches a setting — dynamic pins stay frozen for the note by
    // definition (pin_model_design.md §5).
    for (auto& b : vg.pushBindings) {
      b.chain->next();
      float v = b.chain->current();
      if (b.isSetting) {
        b.consumer->set_setting(b.paramName, v);
      } else {
        b.cs->set(v);
        b.consumer->set_param(b.paramName, b.cs);
        if (vg.topMultiplex && !b.targetNodeId.empty())
          vg.topMultiplex->set_clone_param(b.targetNodeId, b.paramName, v);
      }
    }
  }

  StreamingVoice prepare_voice_at(int slot, float noteNumber, float velocity,
                                  float duration,
                                  const PitchCurve* curve = nullptr) {
    auto& vg = voicePool[slot];

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    apply_note_bindings(vg, freq, velocity, durSamples, duration, curve);

    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = velocity * (1.0f + boost) * volume;

    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);
    for (auto& a : vg.advanceList) a->prepare(ctx, durSamples);

    // Tail allowance: the returned life length includes the ring-out
    // window; musical prep above used the un-extended duration.
    const int tailSamples = int(kVoiceTailSec * float(sampleRate));
    return { vg.source, durSamples + tailSamples, gain, vg.performSource,
             vg.advanceList };
  }

  void play_note(float noteNumber, float velocity, float duration, float startTime,
                 const PitchCurve* curve = nullptr) {
    int vIdx = int(nextVoice % int(voicePool.size()));
    auto& vg = voicePool[size_t(vIdx)];
    nextVoice++;

    float freq = note_to_freq(noteNumber);
    int durSamples = int(duration * float(sampleRate));

    apply_note_bindings(vg, freq, velocity, durSamples, duration, curve);

    // Frequency-dependent brightness compensation
    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = velocity * (1.0f + boost);

    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);
    for (auto& a : vg.advanceList) a->prepare(ctx, durSamples);

    int startFrame = int(startTime * float(sampleRate));
    const bool capturing = !capturePerVoice.empty();
    // Tail allowance (kVoiceTailSec): render past duration so in-voice
    // reverb/filter state rings out instead of being cut mid-sample.
    // Adaptive ring-out (backlog 63b, 2026-09-16): a voice still audible
    // at the end of the fixed tail keeps rendering until it decays below
    // kRingFloor or hits kMaxRingSec — struck bars/bowls (BandedWG) ring
    // for seconds and the fixed 0.4 s window cut them mid-ring with a
    // click. Voices already quiet at the tail end stop exactly where
    // they always did, so their renders stay byte-identical.
    const int tailSamples = int(kVoiceTailSec * float(sampleRate));
    const int renderSamples = durSamples + tailSamples;
    const int maxSamples = durSamples + int(kMaxRingSec * float(sampleRate));
    std::vector<float> buf(size_t(std::max(renderSamples, maxSamples)));
    float ringEnv = 0.0f;
    // ~50 ms decay follower: per-sample multiplier for the running peak.
    const float ringDecay = std::exp(-1.0f / (0.05f * float(sampleRate)));
    int rendered = 0;
    for (int i = 0; i < maxSamples; ++i) {
      if (i >= renderSamples && ringEnv < kRingFloor) break;
      if (vg.performSource) vg.performSource->tick();   // P3 sample clock
      buf[i] = vg.source->next() * gain;
      for (auto& a : vg.advanceList) a->next();         // tap-only loop tails
      ringEnv = std::max(std::fabs(buf[i]), ringEnv * ringDecay);
      rendered = i + 1;
      // Cap fade: a voice that will still be audible at kMaxRingSec (a
      // near-lossless resonator — struck bowl class) gets an 80 ms ramp
      // to zero instead of a hard cut (Matt, REVIEW 09-16).
      const int fadeN = int(0.08f * float(sampleRate));
      if (i >= maxSamples - fadeN && ringEnv >= kRingFloor)
        buf[i] *= float(maxSamples - i) / float(fadeN);
      if (capturing) {
        // Strips record raw current() — no velocity/volume gain — matching
        // what the per-node display always showed.
        int f = startFrame + i;
        for (auto& ce : capturePerVoice[size_t(vIdx)]) {
          auto& dst = captureBuffers[size_t(ce.bufIdx)];
          if (f >= 0 && f < int(dst.size()))
            dst[size_t(f)] += ce.node->current();
        }
      }
    }

    // Note-contained-sound check (2026-08-13 spec): output must be at the
    // audibility floor by the end of the voice — now measured at the end
    // of the (possibly ring-extended) window. A voice that hits the
    // kMaxRingSec cap still audible is exactly what this warns about.
    int checkStart = std::max(0, rendered - sampleRate / 1000);
    float tailPeak = 0.0f;
    for (int i = checkStart; i < rendered; ++i)
      tailPeak = std::max(tailPeak, std::fabs(buf[i]));
    if (tailPeak > 1e-4f)
      std::fprintf(stderr,
          "[containment] note %.1f (%.1f Hz) at t=%.2fs: %.1f dBFS in final 1 ms\n",
          noteNumber, freq, startTime, 20.0f * std::log10(tailPeak));

    add_rendered(startTime, buf.data(), rendered);
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

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
#include <set>
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// PerformedNote — the Performance→Realization boundary object (spec
// 2026-09-20-note-onsets-v2 §1). Every per-note decision the Performer
// makes crosses here in ONE struct: pitch, velocity, length, how the note
// begins (`onsetId`, already interned by the caller via onset_id()) and
// whether the excitation continues past its end (`hold`). The loose
// (noteNumber, velocity, duration, curve) argument list it replaces let
// the PitchCurve dangle at the end and had nowhere to grow.
//
// Tier discipline: music::Note (Compose) gains NOTHING — onset and hold
// are Interpretation products, the same category as PitchCurve, and live
// only here. NoteState stays the render-side mirror the graph reads.
// ---------------------------------------------------------------------------
struct PerformedNote {
  float noteNumber{60.0f};
  float velocity{0.8f};
  float duration{1.0f};          // seconds
  float onsetId{0.0f};           // interned; 0 = none
  bool  hold{false};
  const PitchCurve* curve{nullptr};
};

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
                                   durOut, onsetOut;
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
    // Envelopes in this voice with a wired `trigger` input (spec
    // 2026-09-20-note-onsets-v2 §6), collected at load. Raw pointers:
    // the voice's graph owns them (same lifetime rationale as
    // CaptureEntry). fire_triggers reads each trigger via current() at
    // note Setup — never in the sample loop.
    std::vector<Envelope*> triggerBindings;
    // EVERY envelope in this voice (spec 2026-09-20-note-onsets-v2 §4/§5),
    // collected at load from the same node table. A held line flips them
    // all to gated before prepare so they hold at sustain across the note
    // boundary, and re-references their release to the releasing note's
    // length at line end. Raw pointers, same lifetime rationale as
    // triggerBindings. Empty (a Multiplex-output voice, whose envelopes
    // live inside clones) = hold is not supported on that voice.
    std::vector<Envelope*> allEnvelopes;
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
  // Onset vocabulary (spec 2026-09-20-note-onsets-v2 §6/§8): the
  // instrument block's "onsets" array, in declaration order. Interned
  // ids are 1-based; 0 = none/unknown. Stable, so ordinal use in a Curve
  // is dependable.
  std::vector<std::string> onsetNames;
  // Does this instrument sustain? (spec §2, instrument block
  // "sustaining"). DECLARED, never inferred — only the patch's author
  // knows, and no envelope-shape test can tell a piano's long decay from
  // a wind's hold. Phrasing (hold, onset gestures, glide) applies only
  // when true, which is what makes the piano invariant hold by
  // construction: a non-sustaining instrument never receives hold.
  bool sustaining{false};
  std::set<std::string> warnedOnsets_;   // once-per-name unknown warn
  float onset_id(const std::string& name) {
    if (name.empty()) return 0.0f;
    // Untaught instrument (no vocabulary): every name maps to 0,
    // SILENTLY — lines still play, gestures just don't exist here
    // (the degradation contract, spec §6). The warn is for a taught
    // instrument receiving a name outside its vocabulary.
    if (onsetNames.empty()) return 0.0f;
    for (size_t k = 0; k < onsetNames.size(); ++k)
      if (onsetNames[k] == name) return float(k + 1);
    if (warnedOnsets_.insert(name).second)
      std::fprintf(stderr, "[onset] name '%s' is not in this "
                   "instrument's onsets[] vocabulary — it will never "
                   "fire\n", name.c_str());
    return 0.0f;
  }

  void apply_note_bindings(VoiceGraph& vg, float freq, float velocity,
                           int durSamples, float durSeconds,
                           const PitchCurve* curve,
                           float onsetId = 0.0f) {
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
                                 std::move(bend), onsetId);
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

  // A held line (spec 2026-09-20-note-onsets-v2 §4/§7). One monophonic
  // line: the voice renders exactly each note's samples and then SUSPENDS
  // WARM — graph state, envelope positions, capture offsets intact —
  // instead of running release + tail. The next note on the line resumes
  // the same voice at the boundary sample; release + tail + adaptive ring
  // run only when a hold:false note ends it. Polyphonic phrasing is out
  // of scope for v2, so one line per instrument is the whole model.
  struct HeldLine {
    bool  open{false};
    int   vIdx{-1};
    float startTime{0.0f};      // timeline seconds of line start
    std::vector<float> buf;     // samples rendered since line start
    float gain{1.0f};
    float lastFreq{0.0f};
    float lastNote{0.0f};       // containment report id
    int   lastDurSamples{0};    // the RELEASING note's length (§5)
  };
  HeldLine line_;

  // Legato retune time, instrument block "glideMs" (spec §4.1). An
  // instantaneous delay retarget puts a kink in a waveguide's stored wave
  // — the measured retune tick; the glide is the waveguide-native fix and
  // doubles as portamento later. 0 = retarget instantly.
  float glideSec{0.015f};
  bool  warnedNoEnvelopes_{false};

  // Trigger firing at Setup (spec §6): envelopes whose trigger input is
  // nonzero at this note's Setup restart from their current value. The
  // bindings are collected at load; empty = no-op (feature at rest).
  // Ordering contract: set_note FIRST (the new onsetId must be
  // visible), push bindings second, fire_triggers LAST.
  void fire_triggers(VoiceGraph& vg) {
    for (auto* env : vg.triggerBindings)
      if (env->trigger_ && env->trigger_->current() != 0.0f)
        env->retrigger();
  }

  // The legato glide as an ordinary bend (spec §4.1): semitones from the
  // OLD pitch down to 0 over glideSec, then hold at 0. PerformSource's
  // frequency() = base * 2^(bend/12) with base = the NEW pitch, so the
  // ramp starts exactly on the old pitch and lands exactly on the new one
  // — no PitchCurve compile needed, and every consumer of the frequency
  // chain sees it. (A legacy paramMap patch delivers frequency by a push
  // evaluated once at Setup, so its retune stays instantaneous; only
  // pulled frequency chains glide.)
  std::shared_ptr<Envelope> make_glide(float fromFreq, float toFreq,
                                       int durSamples) {
    auto g = std::make_shared<Envelope>(sampleRate);
    g->absolute_time = true;
    const float semis = 12.0f * std::log2(fromFreq / toFreq);
    g->add_stage({{semis, 0.0f, RampType::Linear, 0.0f}, glideSec, 0.0f, 0.0f});
    g->add_stage({{0.0f, 0.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
    g->prepare(RenderContext{sampleRate}, durSamples);
    return g;
  }

  // The one sample loop (spec 2026-09-20-note-onsets-v2 §7). Appends n
  // samples of vg to buf — perform tick, root pull, tap-only advance,
  // per-node capture at the absolute timeline frame
  // (startFrame + buf.size()) — and nothing else. Ring-out, tail, cap
  // fade and containment belong to the CALLERS, which is what lets one
  // voice be rendered in pieces across a held line. buf must be reserved
  // by the caller: the resize below then allocates nothing.
  void render_chunk(VoiceGraph& vg, std::vector<float>& buf, int vIdx,
                    int startFrame, int n, float gain) {
    const bool capturing = !capturePerVoice.empty();
    const size_t base = buf.size();
    buf.resize(base + size_t(n));
    for (int k = 0; k < n; ++k) {
      if (vg.performSource) vg.performSource->tick();   // P3 sample clock
      buf[base + size_t(k)] = vg.source->next() * gain;
      for (auto& a : vg.advanceList) a->next();         // tap-only loop tails
      if (capturing) {
        // Strips record raw current() — no velocity/volume gain — matching
        // what the per-node display always showed.
        int f = startFrame + int(base) + k;
        for (auto& ce : capturePerVoice[size_t(vIdx)]) {
          auto& dst = captureBuffers[size_t(ce.bufIdx)];
          if (f >= 0 && f < int(dst.size()))
            dst[size_t(f)] += ce.node->current();
        }
      }
    }
  }

  // Per-note delivery against a possibly-living line voice (spec §4).
  // Three cases, in order:
  //   1. the line is open  -> CONTINUE it (no acquisition, no prepare);
  //   2. fresh + hold:true -> open a line (envelopes gated before prepare);
  //   3. fresh + hold:false-> the classic path, byte for byte.
  // Whether a note continues is derived from engine state, never declared:
  // the same test the live keyboard's key-overlap makes, against the same
  // state, so offline and live are one mechanism and nothing anywhere
  // looks ahead.
  void play_note(const PerformedNote& pn, float startTime) {
    if (voicePool.empty()) return;
    const float freq = note_to_freq(pn.noteNumber);
    const int durSamples = int(pn.duration * float(sampleRate));

    // --- 1. Continuation -------------------------------------------------
    if (line_.open) {
      auto& vg = voicePool[size_t(line_.vIdx)];
      // Per-note determinism (onsets-v2 addendum, Matt 2026-09-20):
      // re-anchor every node's stochastic draw streams at this note's
      // Setup, so an in-line note's realization is pinned exactly like a
      // fresh note's always was — without this, the noise free-runs
      // through the line and each phrase acquires its own holistic
      // character ("a different oboeist per phrase"). Physical state
      // (bore, filters, envelope positions) carries; only DRAWS anchor.
      // Never called on the fresh path: prepare() already seeds AND
      // consumes layout draws, so a post-prepare reseed would not be
      // byte-neutral.
      for (auto& [nid, src] : vg.nodesById) src->reseed();
      std::shared_ptr<Envelope> glide;
      if (glideSec > 0.0f && line_.lastFreq > 0.0f && freq != line_.lastFreq)
        glide = make_glide(line_.lastFreq, freq, durSamples);
      if (vg.performSource)
        vg.performSource->set_note(freq, pn.velocity, durSamples, pn.duration,
                                   std::move(glide), pn.onsetId);
      // Non-setting push bindings re-push; isSetting bindings HOLD their
      // line-start value — set_setting rebuilds per-note state at prepare
      // and must not run mid-render (v1 decision, unchanged). Pull chains
      // (noteFaces curves) need no delivery: they read the new NoteState.
      for (auto& b : vg.pushBindings) {
        if (b.isSetting) continue;
        b.chain->next();
        float v = b.chain->current();
        b.cs->set(v);
        b.consumer->set_param(b.paramName, b.cs);
        if (vg.topMultiplex && !b.targetNodeId.empty())
          vg.topMultiplex->set_clone_param(b.targetNodeId, b.paramName, v);
      }
      fire_triggers(vg);   // the onset gesture restarts from current value
      line_.lastFreq = freq;
      line_.lastNote = pn.noteNumber;
      line_.lastDurSamples = durSamples;
      render_chunk(vg, line_.buf, line_.vIdx,
                   int(line_.startTime * float(sampleRate)), durSamples,
                   line_.gain);
      if (!pn.hold) finish_line();
      return;
    }

    int vIdx = int(nextVoice % int(voicePool.size()));
    auto& vg = voicePool[size_t(vIdx)];
    nextVoice++;

    // Hold needs reachable envelopes to gate. A voice whose output is a
    // MultiplexSource keeps its envelopes inside the clones, so
    // allEnvelopes is empty — untested territory (spec/plan): say so once
    // and deliver ordinary notes instead of guessing.
    bool hold = pn.hold;
    if (hold && vg.allEnvelopes.empty()) {
      if (!warnedNoEnvelopes_) {
        warnedNoEnvelopes_ = true;
        std::fprintf(stderr, "[onset] hold requested but this voice has no "
                     "reachable envelopes (Multiplex output?) — the line is "
                     "delivered as ordinary notes\n");
      }
      hold = false;
    }

    // Frequency-dependent brightness compensation. The voice-mix gain is
    // per-voice and fixed for the line (its first note's velocity): it
    // cannot change mid-buffer without a zipper. Later notes' velocity
    // still reaches the graph through the velocity face for patches that
    // wire it (the winds do — velocity IS the breath there).
    float boost = hiBoost > 0.0f
        ? (std::log10(std::max(freq, 100.0f)) - 2.0f) * hiBoost
        : 0.0f;
    float gain = pn.velocity * (1.0f + boost);

    // --- 2. Fresh held voice ---------------------------------------------
    if (hold) {
      apply_note_bindings(vg, freq, pn.velocity, durSamples, pn.duration,
                          pn.curve, pn.onsetId);
      // Gate BEFORE prepare: the envelopes must lay out a holdable expand
      // stage and sit on it when this note's end arrives.
      for (auto* e : vg.allEnvelopes) e->set_gated(true);
      RenderContext ctx{ sampleRate };
      vg.source->prepare(ctx, durSamples);
      for (auto& a : vg.advanceList) a->prepare(ctx, durSamples);
      fire_triggers(vg);

      line_.open = true;
      line_.vIdx = vIdx;
      line_.startTime = startTime;
      line_.gain = gain;
      line_.lastFreq = freq;
      line_.lastNote = pn.noteNumber;
      line_.lastDurSamples = durSamples;
      line_.buf.clear();
      line_.buf.reserve(size_t(durSamples +
                               int(kMaxRingSec * float(sampleRate))));
      render_chunk(vg, line_.buf, vIdx, int(startTime * float(sampleRate)),
                   durSamples, gain);
      return;
    }

    // --- 3. Classic ------------------------------------------------------
    // Envelopes ungated, release laid out INSIDE the note, one loop with
    // tail + adaptive ring + cap fade + containment: the pre-v2 note path,
    // which is what keeps every unphrased render byte-identical.
    const float noteNumber = pn.noteNumber;      // containment report id

    apply_note_bindings(vg, freq, pn.velocity, durSamples, pn.duration,
                        pn.curve, pn.onsetId);

    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);
    for (auto& a : vg.advanceList) a->prepare(ctx, durSamples);
    fire_triggers(vg);

    int startFrame = int(startTime * float(sampleRate));
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
    std::vector<float> buf;
    buf.reserve(size_t(std::max(renderSamples, maxSamples)));
    float ringEnv = 0.0f;
    // ~50 ms decay follower: per-sample multiplier for the running peak.
    const float ringDecay = std::exp(-1.0f / (0.05f * float(sampleRate)));
    int rendered = 0;
    for (int i = 0; i < maxSamples; ++i) {
      if (i >= renderSamples && ringEnv < kRingFloor) break;
      render_chunk(vg, buf, vIdx, startFrame, 1, gain);
      ringEnv = std::max(std::fabs(buf[size_t(i)]), ringEnv * ringDecay);
      rendered = i + 1;
      // Cap fade: a voice that will still be audible at kMaxRingSec (a
      // near-lossless resonator — struck bowl class) gets an 80 ms ramp
      // to zero instead of a hard cut (Matt, REVIEW 09-16).
      const int fadeN = int(0.08f * float(sampleRate));
      if (i >= maxSamples - fadeN && ringEnv >= kRingFloor)
        buf[size_t(i)] *= float(maxSamples - i) / float(fadeN);
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

  // End of a held line (spec §4.2): the gate closes, the release runs —
  // re-referenced to the RELEASING note's length, not the one that
  // prepared the voice (§5) — and the voice rings out through the normal
  // tail before its samples reach the timeline as one contiguous block.
  void finish_line() {
    if (!line_.open) return;
    auto& vg = voicePool[size_t(line_.vIdx)];

    int rem = 0;
    for (auto* e : vg.allEnvelopes)
      rem = std::max(rem, e->gate_release(line_.lastDurSamples));

    auto& buf = line_.buf;
    const int base = int(buf.size());
    const int startFrame = int(line_.startTime * float(sampleRate));
    const int tailSamples = int(kVoiceTailSec * float(sampleRate));
    const int renderSamples = base + rem + tailSamples;
    const int maxSamples = base + rem + int(kMaxRingSec * float(sampleRate));
    buf.reserve(size_t(maxSamples));
    const float ringDecay = std::exp(-1.0f / (0.05f * float(sampleRate)));
    // Prime the ring follower over what the line already rendered, so the
    // ring-out decision sees the same history a single-note render would.
    float ringEnv = 0.0f;
    for (int i = 0; i < base; ++i)
      ringEnv = std::max(std::fabs(buf[size_t(i)]), ringEnv * ringDecay);
    int rendered = base;
    for (int i = base; i < maxSamples; ++i) {
      if (i >= renderSamples && ringEnv < kRingFloor) break;
      render_chunk(vg, buf, line_.vIdx, startFrame, 1, line_.gain);
      ringEnv = std::max(std::fabs(buf[size_t(i)]), ringEnv * ringDecay);
      rendered = i + 1;
      const int fadeN = int(0.08f * float(sampleRate));
      if (i >= maxSamples - fadeN && ringEnv >= kRingFloor)
        buf[size_t(i)] *= float(maxSamples - i) / float(fadeN);
    }

    // Containment is checked once per LINE, at its end (spec §7).
    int checkStart = std::max(0, rendered - sampleRate / 1000);
    float tailPeak = 0.0f;
    for (int i = checkStart; i < rendered; ++i)
      tailPeak = std::max(tailPeak, std::fabs(buf[size_t(i)]));
    if (tailPeak > 1e-4f)
      std::fprintf(stderr,
          "[containment] line ending on note %.1f (%.1f Hz) at t=%.2fs: "
          "%.1f dBFS in final 1 ms\n",
          line_.lastNote, line_.lastFreq, line_.startTime,
          20.0f * std::log10(tailPeak));

    add_rendered(line_.startTime, buf.data(), rendered);
    // Hand the slot back the way the classic path leaves it — ungated —
    // so the next note that lands on it lays out exactly as it always did.
    for (auto* e : vg.allEnvelopes) e->set_gated(false);
    line_ = HeldLine{};
  }

  // End-of-score safety (spec §4.3): a hold:true final note cannot leak an
  // immortal voice. Score loader and UI generate both call this after
  // their note loop.
  void finish_open_lines() { if (line_.open) finish_line(); }
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

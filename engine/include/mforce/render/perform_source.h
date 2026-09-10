#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/envelope.h"
#include <atomic>
#include <cmath>
#include <memory>

namespace mforce {

// Per-instrument performance state (docs/perform_source_design.md §2.1):
// the instrument as played — one physical wheel, one channel-pressure
// stream — outliving every note. UI/MIDI thread stores, audio thread reads
// (in PerformSource::tick), hence atomics.
struct InstrumentState {
  std::atomic<float> wheel{0.0f};      // CC1, 0..1
  std::atomic<float> pressure{0.0f};   // channel pressure, 0..1
};

// Per-voice performance backing store (docs/perform_source_design.md §2.1).
// NOT a ValueSource — its outputs (PerformOut adapters) are. One write per
// note at Realization/Setup; everything downstream pulls. Heap-owned via
// shared_ptr so adapters stay valid when voicePool vectors reallocate.
struct NoteState {
  float frequency{440.0f};   // Hz, base (bend articulates on top — P3)
  float velocity{0.8f};      // 0..1
  int   durSamples{0};       // actual (Piece) or nominal (Live)
  // Seconds view of durSamples, set at note-on (backlog 64 Tier 1:
  // duration-aware articulation — curves/dynamicPins key on this like
  // frequency). Score playback = the note's actual length; live play =
  // the caller's nominal (the UI's fixed-duration header setting).
  float durSeconds{0.0f};
};

// P3 liveness: the voice owns an explicit per-sample clock. tick() advances
// the bend envelope and the wheel/pressure smoothers EXACTLY once per
// sample, called by the render drivers (the loops that already call
// source->next() once per voice per sample). The graft this replaces
// (PitchBendSource) advanced its envelope inside consumer pulls, which
// under sharing stepped it once PER CONSUMER — the P2a stateless-adapter
// lesson, applied to time. PerformOut stays an idempotent view over the
// post-tick values.
class PerformSource {
public:
  void set_note(float freqHz, float velocity, int durSamples,
                float durSeconds = 0.0f,
                std::shared_ptr<Envelope> bend = nullptr) {
    note_.frequency  = freqHz;
    note_.velocity   = velocity;
    note_.durSamples = durSamples;
    note_.durSeconds = durSeconds;
    bend_            = std::move(bend);
    bendSemis_       = 0.0f;
  }

  void set_instrument_state(std::shared_ptr<InstrumentState> is,
                            int sampleRate) {
    inst_ = std::move(is);
    // One-pole smoother, ~10 ms tau: y += a * (target - y).
    smoothA_ = 1.0f - std::exp(-1.0f / (0.010f * float(sampleRate)));
  }

  // Once per sample, from the render driver — never from a consumer pull.
  void tick() {
    if (bend_) bendSemis_ = bend_->next();
    if (inst_) {
      wheelSm_ += smoothA_ * (inst_->wheel.load(std::memory_order_relaxed)
                              - wheelSm_);
      pressSm_ += smoothA_ * (inst_->pressure.load(std::memory_order_relaxed)
                              - pressSm_);
    }
  }

  const NoteState& note() const { return note_; }
  // Articulated: base * 2^(bend(t)/12) under a bend; the UNTOUCHED base
  // when there is none — no multiply, so unbent renders stay bit-identical
  // to the P1 constant (the null-gate contract).
  float frequency() const {
    return bend_ ? note_.frequency * std::exp2(bendSemis_ / 12.0f)
                 : note_.frequency;
  }
  float wheel()    const { return wheelSm_; }
  float pressure() const { return pressSm_; }
  bool  has_bend() const { return bend_ != nullptr; }

private:
  NoteState note_;
  std::shared_ptr<Envelope> bend_;
  float bendSemis_{0.0f};
  std::shared_ptr<InstrumentState> inst_;
  float smoothA_{1.0f};      // no set_instrument_state = no smoothing state
  float wheelSm_{0.0f};
  float pressSm_{0.0f};
};

// Leaf adapter: pulls nothing, reports one PerformSource output. Constant
// between tick() calls. Idempotent next() — safe for many consumers
// without RefSource wrapping.
struct PerformOut final : ValueSource {
  enum class Field { Frequency, Velocity, Wheel, Pressure, Duration };
  PerformOut(std::shared_ptr<PerformSource> ps, Field f)
      : ps_(std::move(ps)), field_(f) {}

  void prepare(const RenderContext&, int) override {}
  // Stateless view — there is nothing to cache, because the value only
  // moves on tick(). current() must read live, NOT a value stashed by
  // next(): RefSource::next() returns source->current() WITHOUT pulling
  // the source (it assumes the primary consumer already did), so a cached
  // cur_ made every RefSource-wrapped copy report 0 until the primary
  // happened to pull. That surfaced as "WaveSource: non-positive
  // frequency" the first time one PerformNode fed two consumers — which is
  // the normal case for a converted patch, where one node feeds many pins.
  float next() override { return read_(); }
  float current() const override { return read_(); }
  const char* type_name() const override { return "PerformOut"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

private:
  float read_() const {
    switch (field_) {
      case Field::Frequency: return ps_->frequency();
      case Field::Velocity:  return ps_->note().velocity;
      case Field::Wheel:     return ps_->wheel();
      case Field::Pressure:  return ps_->pressure();
      case Field::Duration:  return ps_->note().durSeconds;
    }
    return 0.0f;
  }
  std::shared_ptr<PerformSource> ps_;
  Field field_;
};

} // namespace mforce

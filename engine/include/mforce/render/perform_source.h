#pragma once
#include "mforce/core/dsp_value_source.h"
#include <memory>

namespace mforce {

// Per-voice performance backing store (docs/perform_source_design.md §2.1).
// NOT a ValueSource — its outputs (PerformOut adapters) are. One write per
// note at Realization/Setup; everything downstream pulls. Heap-owned via
// shared_ptr so adapters stay valid when voicePool vectors reallocate.
struct NoteState {
  float frequency{440.0f};   // Hz, base (bend rides the legacy graft in P1)
  float velocity{0.8f};      // 0..1
  int   durSamples{0};       // actual (Piece) or nominal (Live)
};

class PerformSource {
public:
  void set_note(float freqHz, float velocity, int durSamples) {
    note_.frequency  = freqHz;
    note_.velocity   = velocity;
    note_.durSamples = durSamples;
  }
  const NoteState& note() const { return note_; }
private:
  NoteState note_;
};

// Leaf adapter: pulls nothing, reports one NoteState field. Constant
// between set_note calls (baked semantics, P1); P3 liveness changes what
// these return, not who calls them. Idempotent next() — safe for many
// consumers without RefSource wrapping.
struct PerformOut final : ValueSource {
  enum class Field { Frequency, Velocity };
  PerformOut(std::shared_ptr<PerformSource> ps, Field f)
      : ps_(std::move(ps)), field_(f) {}

  void prepare(const RenderContext&, int) override {}
  // Stateless view over NoteState — there is nothing to cache, because the
  // value is constant between set_note calls. current() must read live, NOT
  // a value stashed by next(): RefSource::next() returns source->current()
  // WITHOUT pulling the source (it assumes the primary consumer already did),
  // so a cached cur_ made every RefSource-wrapped copy report 0 until the
  // primary happened to pull. That surfaced as "WaveSource: non-positive
  // frequency" the first time one PerformNode fed two consumers — which is
  // the normal case for a converted patch, where one node feeds many pins.
  float next() override { return read_(); }
  float current() const override { return read_(); }
  const char* type_name() const override { return "PerformOut"; }
  SourceCategory category() const override { return SourceCategory::Modulator; }

private:
  float read_() const {
    return field_ == Field::Frequency ? ps_->note().frequency
                                      : ps_->note().velocity;
  }
  std::shared_ptr<PerformSource> ps_;
  Field field_;
};

} // namespace mforce

#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/render/instrument.h"
#include <memory>
#include <vector>

namespace mforce {

// A percussion instrument: one source graph per drum number, each its own
// pull loop. A hit plays its drum for the hit's length.
class DrumKit final : public Instrument {
public:
  // A drum whose number is the count of drums added before it.
  void add(std::shared_ptr<ValueSource> source);

  // A kit has no onsets and does not sustain.
  const InstrumentClass& instrument_class() const override { return class_; }
  void prepare(const RenderContext& c) override;
  // Each hit of the part in the order given; a hit on a drum the kit lacks
  // is ignored. A kit plays no notes.
  void play(const Performance& part) override;
  // Drum graphs have no file ids: a drum reports as "drum <number>".
  void collect_faults(std::vector<FaultReport>& out) override;

  // The clock drum `drumNumber` computes on, for a host that pulls it.
  SampleClock& clock_of(int drumNumber) { return *drums_[size_t(drumNumber)].clock; }

private:
  struct Drum {
    std::shared_ptr<ValueSource> source;
    // Heap-owned, so the context's pointer survives the vector growing.
    std::unique_ptr<SampleClock> clock{std::make_unique<SampleClock>()};
    RenderContext ctx;
  };
  void play_hit(int drumNumber, float velocity, float duration, float startTime);

  InstrumentClass class_;
  std::vector<Drum> drums_;
};

} // namespace mforce

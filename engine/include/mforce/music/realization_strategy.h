#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// RhythmPattern — durations per bar (negative = rest), with per-bar overrides.
// Lifted from the rhythm-pattern half of ChordAccompanimentConfig. Consumed by
// RhythmPatternRealizationStrategy.
// ---------------------------------------------------------------------------
struct RhythmPattern {
  std::vector<float> defaultPattern{2.0f, 2.0f};
  struct BarOverride {
    std::vector<int> bars;
    std::vector<float> pattern;
  };
  std::vector<BarOverride> overrides;

  const std::vector<float>& pattern_for_bar(int bar1) const {
    for (const auto& ov : overrides) {
      for (int b : ov.bars) if (b == bar1) return ov.pattern;
    }
    return defaultPattern;
  }
};

// ---------------------------------------------------------------------------
// RealizationRequest — input to a RealizationStrategy.
// chord is already voiced (by sc->resolve(...) or, in a future world, a
// VoicingSelector).
// ---------------------------------------------------------------------------
struct RealizationRequest {
  Chord chord;
  float startBeat;
  float durationBeats;
  int   barIndex;                            // 1-based bar within the section
  const RhythmPattern* rhythmPattern;        // optional; nullptr if N/A
};

// ---------------------------------------------------------------------------
// RealizationStrategy — emits zero or more Elements (typically Notes or
// Chord-events) starting at req.startBeat into the supplied output sequence.
// ---------------------------------------------------------------------------
struct RealizationStrategy {
  virtual ~RealizationStrategy() = default;
  virtual std::string name() const = 0;
  virtual void realize(const RealizationRequest& req,
                       ElementSequence& out) = 0;
};

// ---------------------------------------------------------------------------
// Registry — singleton, name-keyed.
// ---------------------------------------------------------------------------
class RealizationStrategyRegistry {
public:
  static RealizationStrategyRegistry& instance() {
    static RealizationStrategyRegistry reg;
    return reg;
  }
  void register_strategy(std::unique_ptr<RealizationStrategy> s) {
    strategies_[s->name()] = std::move(s);
  }
  RealizationStrategy* resolve(const std::string& name) const {
    auto it = strategies_.find(name);
    return it == strategies_.end() ? nullptr : it->second.get();
  }
private:
  std::unordered_map<std::string, std::unique_ptr<RealizationStrategy>> strategies_;
};

// ---------------------------------------------------------------------------
// BlockRealizationStrategy — emits the chord as a single Chord-event at
// startBeat for durationBeats. Mirrors the simplest current behavior (one
// Chord-event per chord). At Stage 9 this rewrites to emit per-tone Notes
// once Chord is dropped from Element variant.
// ---------------------------------------------------------------------------
class BlockRealizationStrategy : public RealizationStrategy {
public:
  std::string name() const override { return "block"; }
  void realize(const RealizationRequest& req, ElementSequence& out) override {
    Chord c = req.chord;
    c.dur = req.durationBeats;
    out.add({req.startBeat, c});
  }
};

// ---------------------------------------------------------------------------
// RhythmPatternRealizationStrategy — for each entry in the rhythm pattern,
// emits the chord at the corresponding beat with the entry's duration
// (negative entries become Rests). Migration target for K467 walker's
// chordConfig.
// ---------------------------------------------------------------------------
class RhythmPatternRealizationStrategy : public RealizationStrategy {
public:
  std::string name() const override { return "rhythm_pattern"; }
  void realize(const RealizationRequest& req, ElementSequence& out) override {
    if (!req.rhythmPattern) {
      // Fallback: behave like Block.
      Chord c = req.chord;
      c.dur = req.durationBeats;
      out.add({req.startBeat, c});
      return;
    }
    const auto& pattern = req.rhythmPattern->pattern_for_bar(req.barIndex);
    float beat = req.startBeat;
    for (float dur : pattern) {
      if (dur < 0.0f) {
        out.add({beat, Rest{-dur}});
        beat += -dur;
      } else {
        Chord c = req.chord;
        c.dur = dur;
        out.add({beat, c});
        beat += dur;
      }
    }
  }
};

} // namespace mforce

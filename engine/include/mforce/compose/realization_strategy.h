#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/compose/strategy_choice.h"
#include "mforce/music/structure.h"
#include <functional>
#include <string>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// RhythmPattern — durations per bar (negative = rest), with per-bar
// overrides. The settings of the rhythm_pattern realization strategy.
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
// RealizationContext — what a realization strategy reads to turn one
// section's harmony into a harmony part's events.
// ---------------------------------------------------------------------------
struct RealizationContext {
  const HarmonyTimeline& harmony;     // the chords to realize, section-relative
  const Section& section;
  float sectionStart;                 // in piece beats
  const StrategyChoice& choice;       // the strategy's name and settings
  // One chord symbol at a piece beat, voiced, for `beats` beats. The
  // composer supplies it; each call is one chord the part plays.
  std::function<Chord(const ScaleChord&, float beat, float beats)> voice;
};

// ---------------------------------------------------------------------------
// RealizationStrategy — how a harmony part turns a section's chords into
// events. The strategy owns the loop over the section; voicing is the
// context's.
// ---------------------------------------------------------------------------
class RealizationStrategy {
public:
  virtual ~RealizationStrategy() = default;
  virtual std::string name() const = 0;
  virtual void realize(const RealizationContext& ctx, ElementSequence& out) = 0;
};

// ---------------------------------------------------------------------------
// BlockRealizationStrategy — one Chord-event per authored chord, at the
// length the progression gives it. Chords are emitted as authored,
// including any that overrun the section: clipping an authored cadence
// chord would be a silent edit.
// ---------------------------------------------------------------------------
class BlockRealizationStrategy : public RealizationStrategy {
public:
  static constexpr const char* kName = "block";
  using Settings = NoSettings;

  std::string name() const override { return kName; }
  void realize(const RealizationContext& ctx, ElementSequence& out) override {
    for (const auto& seg : ctx.harmony.segments) {
      float b = seg.startBeat;
      for (int ci = 0; ci < seg.progression.count(); ++ci) {
        float dur = seg.progression.pulses.get(ci);
        if (dur > 0.0f) {
          const float pos = ctx.sectionStart + b;
          Chord c = ctx.voice(seg.progression.chords.get(ci), pos, dur);
          c.dur = dur;
          out.add({pos, c});
        }
        b += dur;
      }
    }
  }
};

// ---------------------------------------------------------------------------
// RhythmPatternRealizationStrategy — strikes by the bar: each bar plays its
// pattern (a negative entry is a rest of that length), and each strike
// voices the chord the harmony has at that beat. A strike where no chord
// sounds emits nothing.
// ---------------------------------------------------------------------------
class RhythmPatternRealizationStrategy : public RealizationStrategy {
public:
  static constexpr const char* kName = "rhythm_pattern";
  using Settings = RhythmPattern;

  std::string name() const override { return kName; }
  void realize(const RealizationContext& ctx, ElementSequence& out) override {
    const RhythmPattern rp = settings_of<Settings>(ctx.choice);
    const float beatsPerBar = float(ctx.section.meter.beats_per_bar());
    const int totalBars = int(ctx.section.beats / beatsPerBar);
    for (int bar = 0; bar < totalBars; ++bar) {
      float barStart = ctx.sectionStart + bar * beatsPerBar;
      const auto& pattern = rp.pattern_for_bar(bar + 1);

      float pos = barStart;
      for (float dur : pattern) {
        if (dur < 0) {
          pos += (-dur);
          continue;
        }
        const ScaleChord* sc = ctx.harmony.chord_at(pos - ctx.sectionStart);
        if (sc) {
          Chord c = ctx.voice(*sc, pos, dur);
          c.dur = dur;
          out.add({pos, c});
        }
        pos += dur;
      }
    }
  }
};

} // namespace mforce

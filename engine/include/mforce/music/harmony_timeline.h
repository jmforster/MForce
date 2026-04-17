#pragma once
#include "mforce/music/figures.h"
#include <vector>
#include <string>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// HarmonySegment — a passage-owned slice of the harmonic timeline.
// ---------------------------------------------------------------------------
struct HarmonySegment {
  float startBeat{0.0f};
  float endBeat{0.0f};
  ChordProgression progression;
  std::string ownerStrategy;
};

// ---------------------------------------------------------------------------
// HarmonyTimeline — ordered, non-overlapping segments covering a Section.
// ---------------------------------------------------------------------------
struct HarmonyTimeline {
  std::vector<HarmonySegment> segments;

  const ScaleChord* chord_at(float beat) const {
    for (const auto& seg : segments) {
      if (beat < seg.startBeat || beat >= seg.endBeat) continue;
      float b = seg.startBeat;
      for (int i = 0; i < seg.progression.count(); ++i) {
        float dur = seg.progression.pulses.get(i);
        if (beat >= b && beat < b + dur) {
          return &seg.progression.chords.get(i);
        }
        b += dur;
      }
    }
    return nullptr;
  }

  ChordProgression slice(float startBeat, float endBeat) const {
    ChordProgression result;
    for (const auto& seg : segments) {
      if (seg.endBeat <= startBeat || seg.startBeat >= endBeat) continue;
      float b = seg.startBeat;
      for (int i = 0; i < seg.progression.count(); ++i) {
        float dur = seg.progression.pulses.get(i);
        float chordStart = b;
        float chordEnd = b + dur;
        if (chordEnd > startBeat && chordStart < endBeat) {
          float clippedStart = std::max(chordStart, startBeat);
          float clippedEnd = std::min(chordEnd, endBeat);
          result.add(seg.progression.chords.get(i), clippedEnd - clippedStart);
        }
        b += dur;
      }
    }
    return result;
  }

  void set_segment(float startBeat, float endBeat,
                   ChordProgression prog, const std::string& owner) {
    segments.erase(
      std::remove_if(segments.begin(), segments.end(),
        [&](const HarmonySegment& s) {
          return s.startBeat < endBeat && s.endBeat > startBeat;
        }),
      segments.end());

    segments.push_back({startBeat, endBeat, std::move(prog), owner});

    std::sort(segments.begin(), segments.end(),
      [](const HarmonySegment& a, const HarmonySegment& b) {
        return a.startBeat < b.startBeat;
      });
  }

  bool empty() const { return segments.empty(); }
};

} // namespace mforce

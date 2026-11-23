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

  // The chord sounding at a beat, by the realize step's rule: walking the
  // chords in order from each segment's start, the first whose end lies
  // past the beat. A beat at or past the last chord's end gets the last
  // chord. Null only when the timeline is empty.
  const ScaleChord* chord_at_or_last(float beat) const {
    const ScaleChord* last = nullptr;
    for (const auto& seg : segments) {
      float b = seg.startBeat;
      for (int i = 0; i < seg.progression.count(); ++i) {
        last = &seg.progression.chords.get(i);
        if (b + seg.progression.pulses.get(i) > beat) return last;
        b += seg.progression.pulses.get(i);
      }
    }
    return last;
  }

  // Every segment's chords and pulses, in order and unclipped. One segment
  // gives its progression exactly as stored.
  ChordProgression progression() const {
    if (segments.size() == 1) return segments.front().progression;
    ChordProgression result;
    for (const auto& seg : segments) {
      for (int i = 0; i < seg.progression.count(); ++i) {
        result.add(seg.progression.chords.get(i), seg.progression.pulses.get(i));
      }
    }
    return result;
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

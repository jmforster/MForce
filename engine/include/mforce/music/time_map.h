#pragma once
#include "mforce/music/structure.h"
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace mforce {

// Beats to seconds across a piece's sections. Section k starts at the beat
// section_start_beat gives it and at the second the earlier sections' played
// lengths (played_beats * 60 / tempo) sum to, in order. A beat belongs to
// the last section that starts at or before it; a beat at or past the
// piece's end belongs to the last section, and a beat before the first to
// the first.
struct TimeMap {
  explicit TimeMap(const std::vector<Section>& sections) {
    if (sections.empty()) throw std::invalid_argument("TimeMap: a piece needs at least one section");
    float seconds = 0.0f;
    for (std::size_t k = 0; k < sections.size(); ++k) {
      spans_.push_back({section_start_beat(sections, k), seconds, sections[k].tempo});
      seconds += played_beats(sections[k]) * 60.0f / sections[k].tempo;
    }
  }

  // One tempo, without end.
  explicit TimeMap(float bpm) { spans_.push_back({0.0f, 0.0f, bpm}); }

  float seconds_at(float beat) const {
    const Span& s = span_at(beat);
    return s.startSeconds + (beat - s.startBeat) * 60.0f / s.tempo;
  }

  float bpm_at(float beat) const { return span_at(beat).tempo; }

private:
  struct Span { float startBeat, startSeconds, tempo; };
  std::vector<Span> spans_;

  const Span& span_at(float beat) const {
    std::size_t k = 0;
    while (k + 1 < spans_.size() && spans_[k + 1].startBeat <= beat) ++k;
    return spans_[k];
  }
};

} // namespace mforce

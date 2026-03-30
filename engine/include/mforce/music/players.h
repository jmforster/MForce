#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/core/equal_temperament.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// ChordPlayer — converts a Chord into an EventSequence.
// ---------------------------------------------------------------------------
struct ChordPlayer {
  Randomizer rng;

  explicit ChordPlayer(uint32_t seed = 0xCDAE'0000u) : rng(seed) {}

  // Simple: all notes at once, full chord duration
  EventSequence play(const Chord& chord, float velocity = 1.0f) {
    return play(chord, velocity, 0.0f);
  }

  // With delay between notes (broken chord)
  EventSequence play(const Chord& chord, float velocity, float delay) {
    EventSequence seq;
    for (int i = 0; i < chord.pitch_count(); ++i) {
      float dur = chord.duration() - delay * float(i);
      if (dur < 0.01f) dur = 0.01f;
      seq.add_note(chord.pitches[i].note_number(), velocity, dur, delay * float(i));
    }
    return seq;
  }

  // With ChordFigure: detailed control over which notes, when, direction
  EventSequence play(const Chord& chord, const ChordFigure& figure, float bpm) {
    EventSequence seq;
    float elapsed = 0.0f;

    for (int i = 0; i < figure.count(); ++i) {
      const auto& elem = figure.elements[i % figure.count()];
      if (elapsed >= chord.duration()) break;

      // Get pitch indices for this element
      auto indices = get_pitch_indices(elem.selection.type, chord.pitch_count());
      if (!elem.selection.indices.empty())
        indices = elem.selection.indices;

      // Direction
      if (elem.direction == ChordFigure::DIR_DESCENDING)
        std::reverse(indices.begin(), indices.end());

      float noteDur = elem.duration.value * 60.0f / bpm;

      for (int j = 0; j < int(indices.size()); ++j) {
        int idx = indices[j] % chord.pitch_count();
        if (idx < 0) idx += chord.pitch_count();
        seq.add_note(chord.pitches[idx].note_number(), 1.0f,
                     noteDur - elem.delay * float(j),
                     double(elapsed + elem.delay * float(j)));
      }

      elapsed += noteDur;
    }

    return seq;
  }

private:
  std::vector<int> get_pitch_indices(PitchSelectionType type, int count) {
    std::vector<int> result;
    switch (type) {
      case PitchSelectionType::All:
        for (int i = 0; i < count; ++i) result.push_back(i);
        break;
      case PitchSelectionType::Low:
        result.push_back(0);
        break;
      case PitchSelectionType::High:
        result.push_back(count - 1);
        break;
      case PitchSelectionType::LowHigh:
        result.push_back(0);
        result.push_back(count - 1);
        break;
      case PitchSelectionType::Even:
        for (int i = 0; i < count; i += 2) result.push_back(i);
        break;
      case PitchSelectionType::Odd:
        for (int i = 1; i < count; i += 2) result.push_back(i);
        break;
      default:
        for (int i = 0; i < count; ++i) result.push_back(i);
        break;
    }
    return result;
  }
};

// ---------------------------------------------------------------------------
// DrumPlayer — converts a DrumFigure into an EventSequence.
// ---------------------------------------------------------------------------
struct DrumPlayer {
  EventSequence play(const DrumFigure& figure, int repeats, float bpm) {
    EventSequence seq;
    float beatDur = 60.0f / bpm;

    for (int r = 0; r < repeats; ++r) {
      float offset = float(r) * figure.totalTime * beatDur;
      for (auto& hit : figure.hits) {
        seq.add_drum(hit.drumNumber, hit.velocity,
                     hit.duration * beatDur,
                     double(offset + hit.time * beatDur));
      }
    }
    return seq;
  }
};

} // namespace mforce

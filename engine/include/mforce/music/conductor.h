#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/render/instrument.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// ChordPerformer — knows how to play chords on an instrument.
// Handles pitch selection, strum patterns (ChordFigure), timing, sloppiness.
// Figure selection strategy: picks figure based on chord duration.
// ---------------------------------------------------------------------------
struct ChordPerformer {
  float defaultSpreadMs{10.0f};
  float sloppiness{0.0f};
  Randomizer rng{0xCDAE'0000u};

  // Registered figures by duration key (e.g., "4" → Josie4-style figure)
  std::unordered_map<std::string, ChordFigure> figures;
  ChordFigure* defaultFigure{nullptr};

  void perform_chord(const Chord& chord, float startSeconds, float bpm,
                     PitchedInstrument& instrument) {
    float chordDurSeconds = chord.dur * 60.0f / bpm;

    // Select figure
    ChordFigure* fig = select_figure(chord.dur);

    if (fig) {
      perform_with_figure(chord, *fig, startSeconds, chordDurSeconds, bpm, instrument);
    } else {
      perform_block(chord, startSeconds, chordDurSeconds, instrument);
    }
  }

private:
  ChordFigure* select_figure(float durationBeats) {
    // Try exact match first
    std::string key = std::to_string(int(durationBeats));
    auto it = figures.find(key);
    if (it != figures.end()) return &it->second;

    // Try with decimal
    // Format: remove trailing zeros for clean lookup
    char buf[16];
    snprintf(buf, sizeof(buf), "%g", durationBeats);
    it = figures.find(buf);
    if (it != figures.end()) return &it->second;

    return defaultFigure;
  }

  void perform_block(const Chord& chord, float startSeconds, float durSeconds,
                     PitchedInstrument& instrument) {
    float spreadSeconds = defaultSpreadMs * 0.001f;
    for (int i = 0; i < chord.pitch_count(); ++i) {
      float noteStart = startSeconds + float(i) * spreadSeconds;
      float noteDur = durSeconds - float(i) * spreadSeconds;
      if (noteDur < 0.01f) noteDur = 0.01f;
      noteStart += sloppiness * rng.valuePN() * 0.005f;
      instrument.play_note(chord.pitches[i].note_number(), 1.0f, noteDur, noteStart);
    }
  }

  void perform_with_figure(const Chord& chord, const ChordFigure& fig,
                           float startSeconds, float chordDurSeconds, float bpm,
                           PitchedInstrument& instrument) {
    int pitchCount = chord.pitch_count();
    float elapsed = 0.0f;
    int elemIdx = 0;
    std::vector<int> playedList;

    while (elapsed < chordDurSeconds) {
      const auto& elem = fig.elements[elemIdx];
      float elemDurSeconds = elem.duration * 60.0f / bpm;

      // Get pitch indices
      auto indices = get_pitch_indices(elem.selection.type, pitchCount, playedList);
      if (!elem.selection.indices.empty())
        indices = elem.selection.indices;

      // Handle negative indices (count from top)
      for (auto& idx : indices) {
        if (idx < 0) idx = pitchCount - 1 + idx;
        idx = std::clamp(idx, 0, pitchCount - 1);
      }

      // Direction
      if (elem.direction == ChordFigure::DIR_DESCENDING)
        std::reverse(indices.begin(), indices.end());
      else if (elem.direction == ChordFigure::DIR_RANDOM) {
        for (int i = int(indices.size()) - 1; i > 0; --i) {
          int j = int(rng.value() * float(i + 1));
          std::swap(indices[i], indices[j]);
        }
      }

      // Play each note in this element
      for (int j = 0; j < int(indices.size()); ++j) {
        int idx = indices[j];
        float noteStart = startSeconds + elapsed + elem.delay * float(j);
        float noteDur = elemDurSeconds - elem.delay * float(j);
        if (noteDur < 0.01f) noteDur = 0.01f;

        noteStart += sloppiness * rng.valuePN() * 0.003f;

        instrument.play_note(chord.pitches[idx].note_number(), 1.0f, noteDur, noteStart);

        // Track played indices
        if (std::find(playedList.begin(), playedList.end(), idx) == playedList.end())
          playedList.push_back(idx);
      }

      elapsed += elemDurSeconds;
      elemIdx++;

      // Wrap figure (loop)
      if (elemIdx >= fig.count()) {
        elemIdx = 0;
        playedList.clear();  // Reset played tracking on loop
      }
    }
  }

  std::vector<int> get_pitch_indices(PitchSelectionType type, int count,
                                     const std::vector<int>& playedList) {
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
        if (count > 1) result.push_back(count - 1);
        break;
      case PitchSelectionType::AllExLow:
        for (int i = 1; i < count; ++i) result.push_back(i);
        break;
      case PitchSelectionType::AllExHigh:
        for (int i = 0; i < count - 1; ++i) result.push_back(i);
        break;
      case PitchSelectionType::AllExLowHigh:
        for (int i = 1; i < count - 1; ++i) result.push_back(i);
        break;
      case PitchSelectionType::Low2:
        for (int i = 0; i < std::min(2, count); ++i) result.push_back(i);
        break;
      case PitchSelectionType::High2:
        for (int i = std::max(0, count - 2); i < count; ++i) result.push_back(i);
        break;
      case PitchSelectionType::Even:
        for (int i = 0; i < count; i += 2) result.push_back(i);
        break;
      case PitchSelectionType::Odd:
        for (int i = 1; i < count; i += 2) result.push_back(i);
        break;
      case PitchSelectionType::LowHalf:
        for (int i = 0; i < count / 2; ++i) result.push_back(i);
        break;
      case PitchSelectionType::HighHalf:
        for (int i = count / 2; i < count; ++i) result.push_back(i);
        break;
      case PitchSelectionType::AllUnplayed:
        for (int i = 0; i < count; ++i) {
          if (std::find(playedList.begin(), playedList.end(), i) == playedList.end())
            result.push_back(i);
        }
        break;
      default:
        for (int i = 0; i < count; ++i) result.push_back(i);
        break;
    }
    return result;
  }
};

// ---------------------------------------------------------------------------
// Conductor — reads the score and dispatches to sub-performers.
// Pitched parts (notes + chords) go to a PitchedInstrument.
// Drum parts go to a DrumKit.
// ---------------------------------------------------------------------------
struct Conductor {
  ChordPerformer chordPerformer;
  // NotePerformer notePerformer;  // future: ornament realization
  // DrumPerformer drumPerformer;  // future: humanization settings

  // Perform pitched events (notes + chords) through an Instrument
  void perform(const Part& part, float bpm, PitchedInstrument& instrument) {
    for (const auto& event : part.events) {
      float startSeconds = event.startBeats * 60.0f / bpm;

      if (event.is_chord()) {
        chordPerformer.perform_chord(event.chord(), startSeconds, bpm, instrument);
      }
      else if (event.is_note()) {
        const auto& n = event.note();
        float durSeconds = n.durationBeats * 60.0f / bpm;
        instrument.play_note(n.noteNumber, n.velocity, durSeconds, startSeconds);
      }
    }
  }

  // Perform drum events through a DrumKit
  void perform(const Part& part, float bpm, DrumKit& kit) {
    for (const auto& event : part.events) {
      if (!event.is_hit()) continue;

      float startSeconds = event.startBeats * 60.0f / bpm;
      const auto& h = event.hit();
      float durSeconds = h.durationBeats * 60.0f / bpm;
      kit.play_hit(h.drumNumber, h.velocity, durSeconds, startSeconds);
    }
  }
};

} // namespace mforce

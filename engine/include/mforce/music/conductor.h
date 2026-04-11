#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/render/instrument.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <stdexcept>

namespace mforce {

// ---------------------------------------------------------------------------
// Scale-degree stepping: move a note number up/down by scale degrees
// ---------------------------------------------------------------------------
// Snap a note number to the nearest pitch in the scale
inline float snap_to_scale(float noteNumber, const Scale& scale) {
    float rel = noteNumber - float(scale.offset());
    float octaves = std::floor(rel / 12.0f);
    float pos = rel - octaves * 12.0f;
    if (pos < 0) { pos += 12.0f; octaves -= 1.0f; }

    float bestPitch = 0;
    float bestDist = 999.0f;
    float accum = 0;
    for (int d = 0; d < scale.length(); ++d) {
        float dist = std::abs(accum - pos);
        if (dist < bestDist) {
            bestDist = dist;
            bestPitch = float(scale.offset()) + octaves * 12.0f + accum;
        }
        accum += scale.ascending_step(d);
    }
    // Also check the octave above (degree 0 of next octave)
    float dist = std::abs(accum - pos);
    if (dist < bestDist) {
        bestPitch = float(scale.offset()) + octaves * 12.0f + accum;
    }
    return bestPitch;
}

inline float step_note(float noteNumber, int steps, const Scale& scale) {
    if (steps == 0) return noteNumber;

    float nn = noteNumber;

    if (steps > 0) {
        for (int i = 0; i < steps; ++i) {
            float rel = nn - float(scale.offset());
            while (rel < 0) rel += 12.0f;
            float pos = std::fmod(rel, 12.0f);
            float accum = 0;
            int deg = 0;
            for (int d = 0; d < scale.length(); ++d) {
                if (std::abs(accum - pos) < 0.5f) { deg = d; break; }
                accum += scale.ascending_step(d);
            }
            nn += scale.ascending_step(deg % scale.length());
        }
    } else {
        for (int i = 0; i < -steps; ++i) {
            float rel = nn - float(scale.offset());
            while (rel < 0) rel += 12.0f;
            float pos = std::fmod(rel, 12.0f);
            float accum = 0;
            int deg = 0;
            for (int d = 0; d < scale.length(); ++d) {
                if (std::abs(accum - pos) < 0.5f) { deg = d; break; }
                accum += scale.ascending_step(d);
            }
            int prevDeg = (deg - 1 + scale.length()) % scale.length();
            nn -= scale.ascending_step(prevDeg);
        }
    }
    return nn;
}

// ---------------------------------------------------------------------------
// DynamicState — tracks current velocity from dynamic markings
// ---------------------------------------------------------------------------
struct DynamicState {
  float currentVelocity;
  float targetVelocity;
  float rampStartBeat;
  float rampEndBeat;

  explicit DynamicState(Dynamic d = Dynamic::mf)
    : currentVelocity(dynamic_to_velocity(d))
    , targetVelocity(currentVelocity)
    , rampStartBeat(0), rampEndBeat(0) {}

  void set_marking(const DynamicMarking& m, float passageBeatOffset) {
    float markBeat = passageBeatOffset + m.beat;
    if (m.rampBeats <= 0) {
      currentVelocity = dynamic_to_velocity(m.level);
      targetVelocity = currentVelocity;
    } else {
      rampStartBeat = markBeat;
      rampEndBeat = markBeat + m.rampBeats;
      targetVelocity = dynamic_to_velocity(m.level);
    }
  }

  float velocity_at(float beat) {
    if (beat >= rampEndBeat || rampEndBeat <= rampStartBeat) {
      currentVelocity = targetVelocity;
      return currentVelocity;
    }
    if (beat <= rampStartBeat) return currentVelocity;
    float t = (beat - rampStartBeat) / (rampEndBeat - rampStartBeat);
    return currentVelocity + t * (targetVelocity - currentVelocity);
  }
};

// ---------------------------------------------------------------------------
// NotePerformer — plays individual notes. Beat→seconds + humanization.
// ---------------------------------------------------------------------------
struct NotePerformer {
  float sloppiness{0.0f};
  Randomizer rng{0xA07E'0000u};

  void perform_note(float noteNumber, float velocity, float durationBeats,
                    float startBeats, float bpm, PitchedInstrument& instrument) {
    float startSeconds = startBeats * 60.0f / bpm;
    float durSeconds = durationBeats * 60.0f / bpm;
    startSeconds += sloppiness * rng.valuePN() * 0.003f;
    instrument.play_note(noteNumber, velocity, durSeconds, startSeconds);
  }
};

// ---------------------------------------------------------------------------
// DrumPerformer — plays drum hits. Beat→seconds + humanization.
// ---------------------------------------------------------------------------
struct DrumPerformer {
  float sloppiness{0.0f};
  Randomizer rng{0xD12A'0000u};

  void perform_hit(int drumNumber, float velocity, float durationBeats,
                   float startBeats, float bpm, DrumKit& kit) {
    float startSeconds = startBeats * 60.0f / bpm;
    float durSeconds = durationBeats * 60.0f / bpm;
    startSeconds += sloppiness * rng.valuePN() * 0.003f;
    kit.play_hit(drumNumber, velocity, durSeconds, startSeconds);
  }
};

// ---------------------------------------------------------------------------
// ChordPerformer — knows how to play chords on an instrument.
// ---------------------------------------------------------------------------
struct ChordPerformer {
  float defaultSpreadMs{10.0f};
  float sloppiness{0.0f};
  Randomizer rng{0xCDAE'0000u};

  // Named figures (looked up by chord.figureName)
  std::unordered_map<std::string, ChordArticulation> namedFigures;
  // Duration-keyed figures (fallback when no figureName)
  std::unordered_map<std::string, ChordArticulation> figures;
  ChordArticulation* defaultFigure{nullptr};

  void perform_chord(const Chord& chord, float velocity, float startBeats, float bpm,
                     PitchedInstrument& instrument) {
    float startSeconds = startBeats * 60.0f / bpm;
    float chordDurSeconds = chord.dur * 60.0f / bpm;

    ChordArticulation* fig = select_figure(chord);

    if (fig) {
      perform_with_figure(chord, velocity, *fig, startSeconds, chordDurSeconds, bpm, instrument);
    } else {
      perform_block(chord, velocity, startSeconds, chordDurSeconds, instrument);
    }
  }

  // Register the built-in Josie chord figures
  void register_josie_figures() {
    using PS = PitchSelectionType;

    // Josie12, Josie8, Josie4 — identical pattern: bass, strum up, high pick, strum, descend
    ChordArticulation josie8;
    josie8.add(PS::Low, 1.00f);
    josie8.add(PS::AllExLow, 0.77f, ChordArticulation::DIR_ASCENDING, 0.01f);
    josie8.add(PS::High2, 0.24f, ChordArticulation::DIR_DESCENDING, 0.01f);
    josie8.add(PS::AllExLow, 0.48f, ChordArticulation::DIR_ASCENDING, 0.01f);
    josie8.add(PS::HighHalf, 1.02f, ChordArticulation::DIR_DESCENDING, 0.01f);
    josie8.add(PS::HighHalf, 0.49f, ChordArticulation::DIR_DESCENDING, 0.0f);
    namedFigures["Josie12"] = josie8;
    namedFigures["Josie8"]  = josie8;
    namedFigures["Josie4"]  = josie8;

    // Josie3 — strum all, then pick individual strings
    ChordArticulation josie3;
    josie3.add(PS::All, 1.50f, ChordArticulation::DIR_ASCENDING, 0.03f);
    josie3.add(PS::All, std::vector<int>{3}, 0.50f);  // single pitch index 3
    josie3.add(PS::All, std::vector<int>{4}, 0.50f);  // single pitch index 4
    josie3.add(PS::High, 0.50f);
    namedFigures["Josie3"] = josie3;

    // Josie2.5 — strum, then pick
    ChordArticulation josie25;
    josie25.add(PS::All, 1.50f, ChordArticulation::DIR_ASCENDING, 0.03f);
    josie25.add(PS::All, std::vector<int>{1}, 0.50f);
    josie25.add(PS::All, std::vector<int>{2}, 0.50f);
    namedFigures["Josie2.5"] = josie25;

    // Josie2 — single strum
    ChordArticulation josie2;
    josie2.add(PS::All, 2.00f, ChordArticulation::DIR_ASCENDING, 0.03f);
    namedFigures["Josie2"] = josie2;

    // Josie1.5 — strum + bass
    ChordArticulation josie15;
    josie15.add(PS::All, 1.00f, ChordArticulation::DIR_ASCENDING, 0.01f);
    josie15.add(PS::Low, 0.50f);
    namedFigures["Josie1.5"] = josie15;

    // Josie1 — single strum (longer)
    ChordArticulation josie1;
    josie1.add(PS::All, 2.00f, ChordArticulation::DIR_ASCENDING, 0.01f);
    namedFigures["Josie1"] = josie1;

    // Josie0.5 — quick low+high
    ChordArticulation josie05;
    josie05.add(PS::LowHigh, 0.50f, ChordArticulation::DIR_DESCENDING, 0.01f);
    namedFigures["Josie0.5"] = josie05;
  }

private:
  ChordArticulation* select_figure(const Chord& chord) {
    // 1. Check chord's explicit figure name
    if (chord.figureName) {
      auto it = namedFigures.find(*chord.figureName);
      if (it != namedFigures.end()) return &it->second;
    }

    // 2. Fall back to duration-keyed lookup
    char buf[16];
    snprintf(buf, sizeof(buf), "%g", chord.dur);
    auto it = figures.find(buf);
    if (it != figures.end()) return &it->second;

    // Also try integer key
    std::string intKey = std::to_string(int(chord.dur));
    it = figures.find(intKey);
    if (it != figures.end()) return &it->second;

    return defaultFigure;
  }

  void perform_block(const Chord& chord, float velocity, float startSeconds,
                     float durSeconds, PitchedInstrument& instrument) {
    float spreadSeconds = defaultSpreadMs * 0.001f;
    for (int i = 0; i < chord.pitch_count(); ++i) {
      float noteStart = startSeconds + float(i) * spreadSeconds;
      float noteDur = durSeconds - float(i) * spreadSeconds;
      if (noteDur < 0.01f) noteDur = 0.01f;
      noteStart += sloppiness * rng.valuePN() * 0.005f;
      instrument.play_note(chord.pitches[i].note_number(), velocity, noteDur, noteStart);
    }
  }

  void perform_with_figure(const Chord& chord, float velocity, const ChordArticulation& fig,
                           float startSeconds, float chordDurSeconds, float bpm,
                           PitchedInstrument& instrument) {
    int pitchCount = chord.pitch_count();
    float elapsed = 0.0f;
    int elemIdx = 0;
    std::vector<int> playedList;

    while (elapsed < chordDurSeconds) {
      const auto& elem = fig.elements[elemIdx];
      float elemDurSeconds = elem.duration * 60.0f / bpm;

      auto indices = get_pitch_indices(elem.selection.type, pitchCount, playedList);
      if (!elem.selection.indices.empty())
        indices = elem.selection.indices;

      for (auto& idx : indices) {
        if (idx < 0) idx = pitchCount - 1 + idx;
        idx = std::clamp(idx, 0, pitchCount - 1);
      }

      if (elem.direction == ChordArticulation::DIR_DESCENDING)
        std::reverse(indices.begin(), indices.end());
      else if (elem.direction == ChordArticulation::DIR_RANDOM) {
        for (int i = int(indices.size()) - 1; i > 0; --i) {
          int j = int(rng.value() * float(i + 1));
          std::swap(indices[i], indices[j]);
        }
      }

      for (int j = 0; j < int(indices.size()); ++j) {
        int idx = indices[j];
        float noteStart = startSeconds + elapsed + elem.delay * float(j);
        float noteDur = elemDurSeconds - elem.delay * float(j);
        if (noteDur < 0.01f) noteDur = 0.01f;

        noteStart += sloppiness * rng.valuePN() * 0.003f;

        instrument.play_note(chord.pitches[idx].note_number(), velocity, noteDur, noteStart);

        if (std::find(playedList.begin(), playedList.end(), idx) == playedList.end())
          playedList.push_back(idx);
      }

      elapsed += elemDurSeconds;
      elemIdx++;

      if (elemIdx >= fig.count()) {
        elemIdx = 0;
        playedList.clear();
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
// Holds instrument registry. perform(Piece) is the main entry point.
// ---------------------------------------------------------------------------
struct Conductor {
  ChordPerformer chordPerformer;
  NotePerformer notePerformer;
  DrumPerformer drumPerformer;

  // Instrument registry: instrumentType string → Instrument*
  std::unordered_map<std::string, Instrument*> instruments;

  // --- Main entry point: perform a whole Piece ---

  void perform(const Piece& piece) {
    float beatOffset = 0.0f;

    for (const auto& section : piece.sections) {
      float bpm = section.tempo;
      const Scale& scale = section.scale;

      for (const auto& part : piece.parts) {
        Instrument* inst = lookup_instrument(part.instrumentType);
        if (!inst) continue;

        // Event-list path: if the Part has direct events, play them
        if (!part.events.empty()) {
          perform_events(part, bpm, beatOffset, inst);
        }

        // Compositional path: if the Part has a Passage for this Section
        auto it = part.passages.find(section.name);
        if (it != part.passages.end()) {
          perform_passage(it->second, scale, beatOffset, bpm, inst);
        }
      }

      beatOffset += section.beats;
    }
  }

  // --- Convenience: perform a single Part with explicit instrument + bpm ---

  void perform(const Part& part, float bpm, PitchedInstrument& instrument) {
    perform_events(part, bpm, 0.0f, &instrument);
  }

  void perform(const Part& part, float bpm, DrumKit& kit) {
    perform_events(part, bpm, 0.0f, &kit);
  }

private:
  Instrument* lookup_instrument(const std::string& type) {
    auto it = instruments.find(type);
    return (it != instruments.end()) ? it->second : nullptr;
  }

  // --- Event-list performance ---

  void perform_events(const Part& part, float bpm, float beatOffset, Instrument* inst) {
    auto* pitched = dynamic_cast<PitchedInstrument*>(inst);
    auto* drums = dynamic_cast<DrumKit*>(inst);

    for (const auto& event : part.events) {
      float absBeats = beatOffset + event.startBeats;

      if (event.is_note() && pitched) {
        const auto& n = event.note();
        notePerformer.perform_note(n.noteNumber, n.velocity, n.durationBeats,
                                   absBeats, bpm, *pitched);
      }
      else if (event.is_chord() && pitched) {
        chordPerformer.perform_chord(event.chord(), 1.0f, absBeats, bpm, *pitched);
      }
      else if (event.is_hit() && drums) {
        const auto& h = event.hit();
        drumPerformer.perform_hit(h.drumNumber, h.velocity, h.durationBeats,
                                  absBeats, bpm, *drums);
      }
    }
  }

  // --- Compositional performance ---

  void perform_passage(const Passage& passage, const Scale& sectionScale,
                       float beatOffset, float bpm, Instrument* inst) {
    auto* pitched = dynamic_cast<PitchedInstrument*>(inst);
    if (!pitched) return;  // melodic passages only for pitched instruments

    const Scale& scale = passage.scaleOverride.value_or(sectionScale);

    // Set up dynamic tracking — first marking at beat 0 sets initial level, else mf
    DynamicState dynamics;
    int nextMarking = 0;
    if (!passage.dynamicMarkings.empty() && passage.dynamicMarkings[0].beat <= 0.0f) {
      dynamics = DynamicState(passage.dynamicMarkings[0].level);
      nextMarking = 1;
    }

    float currentBeat = beatOffset;
    for (const auto& phrase : passage.phrases) {
      currentBeat = perform_phrase(phrase, scale, currentBeat, bpm,
                                   dynamics, passage.dynamicMarkings, nextMarking,
                                   beatOffset, *pitched);
    }
  }

  float perform_phrase(const Phrase& phrase, const Scale& scale,
                       float startBeat, float bpm,
                       DynamicState& dynamics,
                       const std::vector<DynamicMarking>& markings, int& nextMarking,
                       float passageBeatOffset,
                       PitchedInstrument& instrument) {
    float currentBeat = startBeat;
    float currentNN = phrase.startingPitch.note_number();

    for (int f = 0; f < phrase.figure_count(); ++f) {
      const auto& fig = phrase.figures[f];

      // Walk FigureUnits. Every unit's step is applied, including the first
      // (which bridges from the previous figure's ending pitch to this
      // figure's starting pitch under the cursor model).
      for (int i = 0; i < fig.note_count(); ++i) {
        const auto& u = fig.units[i];
        currentNN = step_note(currentNN, u.step, scale);

        // Apply transient accidental for sounding pitch only
        float soundNN = currentNN + float(u.accidental);

        // Advance dynamic markings
        float passageBeat = currentBeat - passageBeatOffset;
        while (nextMarking < int(markings.size()) &&
               markings[nextMarking].beat <= passageBeat) {
          dynamics.set_marking(markings[nextMarking], passageBeatOffset);
          nextMarking++;
        }

        if (!u.rest) {
          float vel = dynamics.velocity_at(currentBeat);
          notePerformer.perform_note(soundNN, vel, u.duration,
                                     currentBeat, bpm, instrument);
        }
        currentBeat += u.duration;
      }
    }

    return currentBeat;
  }
};

} // namespace mforce

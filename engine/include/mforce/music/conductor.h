#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/render/instrument.h"
#include "mforce/music/pitch_bend.h"
#include "mforce/music/pitch_curve.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <stdexcept>
#include <optional>

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
// Chord-tone stepping: move a note number through chord tones
// ---------------------------------------------------------------------------
inline float step_chord_tone(float noteNumber, int steps, const Chord& chord) {
    if (steps == 0 || chord.pitches.empty()) return noteNumber;

    // Build sorted list of chord tones across ±2 octaves
    std::vector<float> tones;
    for (int octShift = -2; octShift <= 2; ++octShift) {
        for (const auto& p : chord.pitches) {
            tones.push_back(p.note_number() + 12.0f * octShift);
        }
    }
    std::sort(tones.begin(), tones.end());

    // Find closest chord tone to current position
    int closest = 0;
    float minDist = 999.0f;
    for (int i = 0; i < int(tones.size()); ++i) {
        float d = std::abs(tones[i] - noteNumber);
        if (d < minDist) { minDist = d; closest = i; }
    }

    // If first step wants to go up but we snapped below, adjust
    if (steps > 0 && tones[closest] < noteNumber - 0.1f) {
        for (int i = closest + 1; i < int(tones.size()); ++i) {
            if (tones[i] >= noteNumber - 0.1f) { closest = i; break; }
        }
    } else if (steps < 0 && tones[closest] > noteNumber + 0.1f) {
        for (int i = closest - 1; i >= 0; --i) {
            if (tones[i] <= noteNumber + 0.1f) { closest = i; break; }
        }
    }

    int target = closest + steps;
    target = std::max(0, std::min(target, int(tones.size()) - 1));
    return tones[target];
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
  float humanize{0.0f};
  Randomizer rng{0xA07E'0000u};

  // Random timing offset (seconds). humanize is in milliseconds — max |offset|.
  float jitter() { return humanize * 0.001f * rng.valuePN(); }

  // Articulation parameter transforms — returns {adjustedDuration, adjustedVelocity}
  static std::pair<float, float> apply_articulation(const Articulation& art,
                                                     float durSeconds, float velocity) {
    return std::visit([&](const auto& v) -> std::pair<float, float> {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, articulations::Staccato>)
        return {durSeconds * 0.5f, velocity};
      else if constexpr (std::is_same_v<T, articulations::Marcato>)
        return {durSeconds, std::min(velocity * 1.3f, 1.0f)};
      else if constexpr (std::is_same_v<T, articulations::Sforzando>)
        return {durSeconds, std::min(velocity * 1.5f, 1.0f)};
      else if constexpr (std::is_same_v<T, articulations::Mute>)
        return {durSeconds * 0.7f, velocity * 0.6f};
      else
        return {durSeconds, velocity};
    }, art);
  }

  // How long (in seconds) should each ornament sub-note be?
  // Returns 0 if the note is too short to ornament.
  static float ornament_subnote_duration(float parentDurBeats, float bpm) {
    float beatSeconds = 60.0f / bpm;
    if (parentDurBeats >= 1.0f)
      return 0.25f * beatSeconds;        // quarter of a beat
    else if (parentDurBeats >= 0.5f)
      return 0.125f * beatSeconds;       // eighth of a beat
    else
      return 0.0f;                       // too short, skip ornament
  }

  // Trill sub-notes are half the duration of mordent/turn sub-notes, and
  // can go one tier shorter (64th-note trills on a 16th-note parent).
  static float trill_subnote_duration(float parentDurBeats, float bpm) {
    float beatSeconds = 60.0f / bpm;
    if (parentDurBeats >= 1.0f)
      return 0.125f * beatSeconds;       // 32nd note (1/8 beat)
    else if (parentDurBeats >= 0.5f)
      return 0.0625f * beatSeconds;      // 64th note (1/16 beat)
    else if (parentDurBeats >= 0.25f)
      return 0.0625f * beatSeconds;      // 64th note on 16th-note parent
    else
      return 0.0f;                       // too short, skip trill
  }

  void perform_mordent(const Note& note, const Mordent& m,
                       float startSeconds, float durSeconds, float subDur,
                       PitchedInstrument& instrument) {
    float neighborNN = note.noteNumber + float(m.direction * m.semitones);

    Articulation neighborArt = m.articulations.size() > 0 ? m.articulations[0] : Articulation{articulations::Default{}};
    Articulation returnArt   = m.articulations.size() > 1 ? m.articulations[1] : Articulation{articulations::Default{}};

    // 1. Main note (short) — uses the Note's own articulation
    auto [dur1, vel1] = apply_articulation(note.articulation, subDur, note.velocity);
    instrument.play_note(note.noteNumber, vel1, dur1, startSeconds + jitter());

    // 2. Neighbor note
    auto [dur2, vel2] = apply_articulation(neighborArt, subDur, note.velocity);
    instrument.play_note(neighborNN, vel2, dur2, startSeconds + subDur + jitter());

    // 3. Main note (remainder)
    float remainDur = durSeconds - 2.0f * subDur;
    auto [dur3, vel3] = apply_articulation(returnArt, remainDur, note.velocity);
    instrument.play_note(note.noteNumber, vel3, dur3, startSeconds + 2.0f * subDur + jitter());
  }

  void perform_turn(const Note& note, const Turn& t,
                    float startSeconds, float durSeconds, float subDur,
                    PitchedInstrument& instrument) {
    float remainDur = durSeconds - 3.0f * subDur;

    float aboveNN = note.noteNumber + float(t.semitonesAbove);
    float belowNN = note.noteNumber - float(t.semitonesBelow);

    struct SubNote { float nn; float dur; };
    SubNote notes[4];
    if (t.direction >= 0) {
      notes[0] = {aboveNN, subDur};
      notes[1] = {note.noteNumber, subDur};
      notes[2] = {belowNN, subDur};
      notes[3] = {note.noteNumber, remainDur};
    } else {
      notes[0] = {belowNN, subDur};
      notes[1] = {note.noteNumber, subDur};
      notes[2] = {aboveNN, subDur};
      notes[3] = {note.noteNumber, remainDur};
    }

    float cursor = startSeconds;
    for (int i = 0; i < 4; ++i) {
      Articulation art;
      if (i == 0) {
        art = note.articulation;
      } else if (i - 1 < int(t.articulations.size())) {
        art = t.articulations[i - 1];
      } else {
        art = articulations::Default{};
      }

      auto [dur, vel] = apply_articulation(art, notes[i].dur, note.velocity);
      instrument.play_note(notes[i].nn, vel, dur, cursor + jitter());
      cursor += notes[i].dur;
    }
  }

  void perform_trill(const Note& note, const Trill& t,
                     float startSeconds, float durSeconds, float subDur,
                     PitchedInstrument& instrument) {
    float neighborNN = note.noteNumber + float(t.direction * t.semitones);
    int count = std::max(2, int(durSeconds / subDur));
    float actualSubDur = durSeconds / float(count);

    for (int i = 0; i < count; ++i) {
      bool isNeighbor = (i % 2 == 1);
      float nn = isNeighbor ? neighborNN : note.noteNumber;

      Articulation art;
      if (isNeighbor && !t.articulations.empty()) {
        int artIdx = (i / 2) % int(t.articulations.size());
        art = t.articulations[artIdx];
      } else if (!isNeighbor) {
        art = note.articulation;
      } else {
        art = articulations::Default{};
      }

      auto [dur, vel] = apply_articulation(art, actualSubDur, note.velocity);
      instrument.play_note(nn, vel, dur, startSeconds + float(i) * actualSubDur + jitter());
    }
  }

  // Slide-run accumulator. Typically holds 0 or 1 note; grows only while a
  // run of Slide-articulated notes is being bundled.
  struct QueuedNote { Note note; float startBeats; };
  std::vector<QueuedNote> noteBuf;

  void perform_note(const Note& note, float startBeats, float bpm,
                    PitchedInstrument& instrument) {
    const bool isSlide = std::holds_alternative<articulations::Slide>(note.articulation);

    if (isSlide) {
      if (noteBuf.empty())
        throw std::runtime_error("Slide on first note — nothing to slide from");
      noteBuf.push_back({note, startBeats});
      return;
    }

    // Non-slide arriving: prior buffer (if any) is a complete unit.
    if (!noteBuf.empty()) play_buffered(bpm, instrument);
    noteBuf.push_back({note, startBeats});  // new anchor of a possible future run
  }

  // Conductor calls this after iterating a part's notes to drain the buffer.
  void conclude(float bpm, PitchedInstrument& instrument) {
    if (!noteBuf.empty()) play_buffered(bpm, instrument);
  }

private:
  // Emit whatever is currently in noteBuf, then clear.
  void play_buffered(float bpm, PitchedInstrument& instrument) {
    if (noteBuf.size() == 1)
      play_single(noteBuf.front().note, noteBuf.front().startBeats, bpm, instrument);
    else
      play_run(noteBuf, bpm, instrument);
    noteBuf.clear();
  }

  // One note, no slide bundling. Existing articulation/ornament/bend realization.
  void play_single(const Note& note, float startBeats, float bpm,
                   PitchedInstrument& instrument) {
    float startSeconds = startBeats * 60.0f / bpm;
    float durSeconds = note.durationBeats * 60.0f / bpm;

    auto play_plain = [&]() {
      auto [dur, vel] = apply_articulation(note.articulation, durSeconds, note.velocity);
      // Stateful articulations (Bend) compile to a PitchCurve here.
      if (auto bend = std::get_if<articulations::Bend>(&note.articulation)) {
        PitchCurve curve = compile_bend(*bend);
        instrument.play_note(note.noteNumber, vel, dur, startSeconds + jitter(), &curve);
      } else {
        instrument.play_note(note.noteNumber, vel, dur, startSeconds + jitter());
      }
    };

    if (has_ornament(note.ornament)) {
      std::visit([&](auto&& orn) {
        using T = std::decay_t<decltype(orn)>;
        if constexpr (std::is_same_v<T, Mordent>) {
          float subDur = ornament_subnote_duration(note.durationBeats, bpm);
          if (subDur <= 0.0f) { play_plain(); return; }
          perform_mordent(note, orn, startSeconds, durSeconds, subDur, instrument);
        } else if constexpr (std::is_same_v<T, Trill>) {
          float subDur = trill_subnote_duration(note.durationBeats, bpm);
          if (subDur <= 0.0f) { play_plain(); return; }
          perform_trill(note, orn, startSeconds, durSeconds, subDur, instrument);
        } else if constexpr (std::is_same_v<T, Turn>) {
          float subDur = ornament_subnote_duration(note.durationBeats, bpm);
          if (subDur <= 0.0f) { play_plain(); return; }
          perform_turn(note, orn, startSeconds, durSeconds, subDur, instrument);
        } else if constexpr (std::is_same_v<T, BendMordent>) {
          // Continuous-pitch realization — one note, PitchCurve handles the excursion.
          auto [dur, vel] = apply_articulation(note.articulation, durSeconds, note.velocity);
          PitchCurve curve = compile_bend_mordent(orn);
          instrument.play_note(note.noteNumber, vel, dur, startSeconds + jitter(), &curve);
        } else if constexpr (std::is_same_v<T, std::monostate>) {
          // no ornament — unreachable due to has_ornament guard
        }
      }, note.ornament);
    } else {
      play_plain();
    }
  }

  // Bundle: anchor + 1+ slide notes realized as a single legato play_note
  // with a multi-segment PitchCurve spanning the whole run.
  void play_run(const std::vector<QueuedNote>& buf, float bpm,
                PitchedInstrument& instrument) {
    // Build SlideRunNote inputs; first is the anchor (no slide).
    std::vector<SlideRunNote> run;
    run.reserve(buf.size());
    for (size_t i = 0; i < buf.size(); ++i) {
      const auto& qn = buf[i];
      SlideRunNote sn;
      sn.noteNumber    = qn.note.noteNumber;
      sn.durationBeats = qn.note.durationBeats;
      if (i == 0) {
        sn.slideSpeed = 0.0f;
      } else {
        auto* sl = std::get_if<articulations::Slide>(&qn.note.articulation);
        sn.slideSpeed = sl ? sl->speed : 0.1f;  // fallback, shouldn't happen
      }
      run.push_back(sn);
    }

    float totalBeats = 0.0f;
    for (const auto& n : run) totalBeats += n.durationBeats;

    const auto& anchor = buf.front();
    float startSeconds = anchor.startBeats * 60.0f / bpm;
    float totalSeconds = totalBeats * 60.0f / bpm;
    float velocity = anchor.note.velocity;  // run uses anchor's velocity

    PitchCurve curve = combine(run);
    instrument.play_note(anchor.note.noteNumber, velocity, totalSeconds,
                          startSeconds + jitter(), &curve);
  }
};

// ---------------------------------------------------------------------------
// DrumPerformer — plays drum hits. Beat→seconds + humanization.
// ---------------------------------------------------------------------------
struct DrumPerformer {
  float humanize{0.0f};
  Randomizer rng{0xD12A'0000u};

  void perform_hit(int drumNumber, float velocity, float durationBeats,
                   float startBeats, float bpm, DrumKit& kit) {
    float startSeconds = startBeats * 60.0f / bpm;
    float durSeconds = durationBeats * 60.0f / bpm;
    startSeconds += humanize * 0.001f * rng.valuePN();
    kit.play_hit(drumNumber, velocity, durSeconds, startSeconds);
  }
};

// ---------------------------------------------------------------------------
// ChordPerformer — knows how to play chords on an instrument.
// ---------------------------------------------------------------------------
struct ChordPerformer {
  float defaultSpreadMs{10.0f};
  float humanize{0.0f};
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
      noteStart += humanize * 0.001f * rng.valuePN();
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

        noteStart += humanize * 0.001f * rng.valuePN();

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

      // Effective playback length after truncation.
      float effectiveBeats = section.beats - section.truncateTailBeats;
      if (effectiveBeats < 0.0f) effectiveBeats = 0.0f;

      for (const auto& part : piece.parts) {
        Instrument* inst = lookup_instrument(part.instrumentType);
        if (!inst) continue;

        // Authoritative path: Composer-populated ElementSequence wins.
        if (!part.elementSequence.empty()) {
          perform_events(part, bpm, beatOffset, inst);
          continue;
        }

        // Migration scaffolding ONLY: walk the passage tree for Parts whose
        // realize step hasn't been written yet. Removed at Stage 8.
        auto it = part.passages.find(section.name);
        if (it != part.passages.end()) {
          perform_passage(it->second, scale, beatOffset, bpm, inst,
                          section.chordProgression, section.scale,
                          4, effectiveBeats);
        }
      }

      beatOffset += effectiveBeats;
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

    for (const auto& event : part.elementSequence) {
      float absBeats = beatOffset + event.startBeats;

      if (event.is_note() && pitched) {
        notePerformer.perform_note(event.note(), absBeats, bpm, *pitched);
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

    // Drain any pending slide-run buffer.
    if (pitched) notePerformer.conclude(bpm, *pitched);
  }

  // --- Compositional performance ---

  void perform_passage(const Passage& passage, const Scale& sectionScale,
                       float beatOffset, float bpm, Instrument* inst,
                       const std::optional<ChordProgression>& chordProg = std::nullopt,
                       const Scale& secScale = Scale::get("C", "Major"),
                       int baseOctave = 4,
                       float maxSectionBeats = -1.0f) {
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
      // Stop if we've already reached the truncation point
      if (maxSectionBeats >= 0.0f && currentBeat - beatOffset >= maxSectionBeats) break;
      currentBeat = perform_phrase(phrase, scale, currentBeat, bpm,
                                   dynamics, passage.dynamicMarkings, nextMarking,
                                   beatOffset, *pitched,
                                   chordProg, secScale, baseOctave,
                                   maxSectionBeats);
    }

    // Drain any pending slide-run buffer at passage end.
    notePerformer.conclude(bpm, *pitched);
  }

  float perform_phrase(const Phrase& phrase, const Scale& scale,
                       float startBeat, float bpm,
                       DynamicState& dynamics,
                       const std::vector<DynamicMarking>& markings, int& nextMarking,
                       float passageBeatOffset,
                       PitchedInstrument& instrument,
                       const std::optional<ChordProgression>& chordProg = std::nullopt,
                       const Scale& sectionScale = Scale::get("C", "Major"),
                       int baseOctave = 4,
                       float maxSectionBeats = -1.0f) {
    float currentBeat = startBeat;
    float currentNN = phrase.startingPitch.note_number();

    for (int f = 0; f < phrase.figure_count(); ++f) {
      const auto& fig = *phrase.figures[f];
      bool isChordFig = (dynamic_cast<const ChordFigure*>(phrase.figures[f].get()) != nullptr);

      // Walk FigureUnits. Every unit's step is applied, including the first
      // (which bridges from the previous figure's ending pitch to this
      // figure's starting pitch under the cursor model).
      for (int i = 0; i < fig.note_count(); ++i) {
        const auto& u = fig.units[i];

        // Stop if this note starts past the section's truncation boundary
        if (maxSectionBeats >= 0.0f &&
            (currentBeat - passageBeatOffset) >= maxSectionBeats) {
          return currentBeat;
        }

        // Step through chord tones (ChordFigure) or scale degrees (MelodicFigure)
        if (isChordFig && chordProg) {
          // Find active chord at this beat (section-relative)
          float sectionBeat = currentBeat - passageBeatOffset;
          float chordBeat = 0.0f;
          int chordIdx = 0;
          for (int ci = 0; ci < chordProg->count(); ++ci) {
            if (chordBeat + chordProg->pulses.get(ci) > sectionBeat) {
              chordIdx = ci;
              break;
            }
            chordBeat += chordProg->pulses.get(ci);
            chordIdx = ci;
          }
          auto resolved = chordProg->chords.get(chordIdx).resolve(sectionScale, baseOctave);
          currentNN = step_chord_tone(currentNN, u.step, resolved);
        } else {
          currentNN = step_note(currentNN, u.step, scale);
        }

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
          Note n{soundNN, vel, u.duration, u.articulation, u.ornament};
          notePerformer.perform_note(n, currentBeat, bpm, instrument);
        }
        currentBeat += u.duration;
      }
    }

    return currentBeat;
  }
};

} // namespace mforce

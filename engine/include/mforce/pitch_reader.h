#pragma once
#include "music.h"

namespace mforce {

// ---------------------------------------------------------------------------
// PitchReader — navigates pitches within a scale.
// Tracks current octave, scale degree, and note number.
// step() moves up/down by scale degrees, wrapping octaves.
// ---------------------------------------------------------------------------
struct PitchReader {
  Scale scale;
  int octave{4};
  int degree{0};     // 0-based index into scale steps
  float noteNumber{60.0f};

  PitchReader() : scale(Scale::get("C", "Major")) {
    update_note_number();
  }

  explicit PitchReader(const Scale& s, int oct = 4, int deg = 0)
  : scale(s), octave(oct), degree(deg) {
    update_note_number();
  }

  void set_scale(const Scale& s) { scale = s; update_note_number(); }
  void set_octave(int o) { octave = o; update_note_number(); }
  void set_degree(int d) { degree = d; update_note_number(); }

  void set_pitch(const Pitch& p) {
    noteNumber = p.note_number();
    octave = p.octave;
    // Find closest degree in current scale
    int offset = p.pitchDef->offset - scale.offset();
    if (offset < 0) offset += 12;
    float cumulative = 0;
    degree = 0;
    for (int i = 0; i < scale.length(); ++i) {
      if (cumulative >= float(offset)) { degree = i; break; }
      cumulative += scale.ascending_step(i);
    }
  }

  // Step up (positive) or down (negative) by scale degrees
  void step(int steps) {
    if (steps > 0) {
      for (int i = 0; i < steps; ++i) step_up();
    } else {
      for (int i = 0; i < -steps; ++i) step_down();
    }
  }

  float get_note_number() const { return noteNumber; }

  Pitch get_pitch() const { return Pitch::from_note_number(noteNumber); }

private:
  void step_up() {
    noteNumber += scale.ascending_step(degree);
    degree++;
    if (degree >= scale.length()) {
      degree = 0;
      octave++;
    }
  }

  void step_down() {
    degree--;
    if (degree < 0) {
      degree = scale.length() - 1;
      octave--;
    }
    noteNumber -= scale.descending_step(degree);
  }

  void update_note_number() {
    float nn = float(octave * 12 + scale.offset());
    for (int i = 0; i < degree; ++i)
      nn += scale.ascending_step(i);
    noteNumber = nn;
  }
};

} // namespace mforce

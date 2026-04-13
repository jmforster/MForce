#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include <vector>
#include <variant>
#include <string>
#include <optional>
#include <memory>
#include <unordered_map>

namespace mforce {

// ===========================================================================
// Dynamic — standard musical dynamic levels
// ===========================================================================
enum class Dynamic { ppp, pp, p, mp, mf, f, ff, fff };

inline float dynamic_to_velocity(Dynamic d) {
  constexpr float step = 0.125f;
  return step * (float(int(d)) + 1.0f);
}

// ===========================================================================
// DynamicMarking — a dynamic change at a beat offset within a Passage
// ===========================================================================
struct DynamicMarking {
  float beat;        // offset within passage
  Dynamic level;
  float rampBeats;   // 0 = instant change, >0 = crescendo/decrescendo

  DynamicMarking(float b, Dynamic d, float ramp = 0.0f)
    : beat(b), level(d), rampBeats(ramp) {}
};

// ===========================================================================
// Elements — the "ink on paper" of a score. A Part is a sequence of these.
// Using std::variant: closed set, compiler-enforced exhaustive handling.
// ===========================================================================

struct Note {
  float noteNumber;
  float velocity{1.0f};
  float durationBeats;
  Articulation articulation{Articulation::Default};
  Ornament ornament;
};

struct Hit {
  int drumNumber;
  float velocity{1.0f};
  float durationBeats{0.1f};
};

struct Rest {
  float durationBeats;
};

// Element — a timed event in a Part
struct Element {
  float startBeats{0.0f};
  std::variant<Note, Chord, Hit, Rest> content;

  bool is_note()  const { return std::holds_alternative<Note>(content); }
  bool is_chord() const { return std::holds_alternative<Chord>(content); }
  bool is_hit()   const { return std::holds_alternative<Hit>(content); }
  bool is_rest()  const { return std::holds_alternative<Rest>(content); }

  const Note&  note()  const { return std::get<Note>(content); }
  const Chord& chord() const { return std::get<Chord>(content); }
  const Hit&   hit()   const { return std::get<Hit>(content); }
  const Rest&  rest()  const { return std::get<Rest>(content); }
};

// ===========================================================================
// Phrase — a musical sentence. Collection of Figures.
// ===========================================================================
struct Phrase {
  Pitch startingPitch;
  std::vector<std::unique_ptr<Figure>> figures;

  // Deep copy constructor (unique_ptr makes Phrase move-only otherwise)
  Phrase() = default;
  Phrase(Phrase&&) = default;
  Phrase& operator=(Phrase&&) = default;
  Phrase(const Phrase& other) : startingPitch(other.startingPitch) {
    for (const auto& f : other.figures) figures.push_back(f->clone());
  }
  Phrase& operator=(const Phrase& other) {
    if (this != &other) {
      startingPitch = other.startingPitch;
      figures.clear();
      for (const auto& f : other.figures) figures.push_back(f->clone());
    }
    return *this;
  }

  void add_figure(std::unique_ptr<Figure> fig) { figures.push_back(std::move(fig)); }
  void add_melodic_figure(MelodicFigure fig) { figures.push_back(std::make_unique<MelodicFigure>(std::move(fig))); }
  int figure_count() const { return int(figures.size()); }
};

// ===========================================================================
// Passage — what a Part plays during a Section. Collection of Phrases.
// Carries dynamics (velocity timeline) for this Part in this Section.
// ===========================================================================
struct Passage {
  std::vector<Phrase> phrases;
  std::optional<Scale> scaleOverride;  // if different from Section's scale

  // Dynamics: timeline of changes. If empty, defaults to mf.
  std::vector<DynamicMarking> dynamicMarkings;

  void add_phrase(Phrase p) { phrases.push_back(std::move(p)); }
  int phrase_count() const { return int(phrases.size()); }
};

// ===========================================================================
// KeyContext — a key/scale change at a beat offset within a Section
// ===========================================================================
struct KeyContext {
  float beat{0.0f};
  Key key;
  std::optional<Scale> scaleOverride;

  Scale effective_scale() const {
    return scaleOverride.value_or(key.scale);
  }
};

// ===========================================================================
// Section — a time span with shared musical context.
// Owns tempo, meter, scale. The "control track" of the DAW.
// ===========================================================================
struct Section {
  std::string name;
  float beats;          // length in beats
  Meter meter{Meter::M_4_4};
  float tempo{120.0f};  // bpm
  Scale scale;          // defaults from Piece key, overridden for modulations

  Section() : scale(Scale::get("C", "Major")) {}
  Section(const std::string& n, float b, float bpm, Meter m, Scale s)
    : name(n), beats(b), meter(m), tempo(bpm), scale(s) {}

  // Harmony context
  std::vector<KeyContext> keyContexts;
  std::optional<ChordProgression> chordProgression;

  Scale active_scale_at(float beat) const {
    Scale result = scale;
    for (const auto& kc : keyContexts) {
      if (kc.beat <= beat) result = kc.effective_scale();
    }
    return result;
  }
};

// ===========================================================================
// Part — a single voice/instrument line across the whole Piece.
// Contains a Passage per Section (what this part plays in each section),
// and a realized event list (produced by the Conductor, or built directly).
// ===========================================================================
struct Part {
  std::string name;
  std::string instrumentType;  // key into Conductor's instrument registry

  // Compositional content: keyed by section name
  std::unordered_map<std::string, Passage> passages;

  // Realized events (built directly for simple cases)
  std::vector<Element> events;
  float totalBeats{0.0f};

  // --- Direct event building (for testing, simple progressions) ---

  void add_chord(float startBeat, const Chord& chord) {
    events.push_back({startBeat, chord});
    totalBeats = std::max(totalBeats, startBeat + chord.dur);
  }

  void add_chord(const Chord& chord) {
    add_chord(totalBeats, chord);
  }

  void add_note(float startBeat, float noteNumber, float velocity, float durationBeats,
                Articulation art = Articulation::Default, Ornament orn = Ornament{}) {
    events.push_back({startBeat, Note{noteNumber, velocity, durationBeats, art, orn}});
    totalBeats = std::max(totalBeats, startBeat + durationBeats);
  }

  void add_note(float noteNumber, float velocity, float durationBeats,
                Articulation art = Articulation::Default) {
    add_note(totalBeats, noteNumber, velocity, durationBeats, art);
  }

  void add_hit(float startBeat, int drumNumber, float velocity, float durationBeats = 0.1f) {
    events.push_back({startBeat, Hit{drumNumber, velocity, durationBeats}});
    totalBeats = std::max(totalBeats, startBeat + durationBeats);
  }

  void add_rest(float durationBeats) {
    events.push_back({totalBeats, Rest{durationBeats}});
    totalBeats += durationBeats;
  }
};

// ===========================================================================
// Piece — top-level composition.
// Sections (horizontal): tempo, meter, scale per time span.
// Parts (vertical): voices/instruments, each with Passages per Section.
// ===========================================================================
struct Piece {
  Key key;
  std::vector<Section> sections;
  std::vector<Part> parts;

  Piece() : key(Key::get("C Major")) {}

  void add_section(Section s) { sections.push_back(std::move(s)); }
  void add_part(Part p) { parts.push_back(std::move(p)); }
};

} // namespace mforce

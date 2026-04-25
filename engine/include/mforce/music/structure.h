#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include <vector>
#include <variant>
#include <string>
#include <optional>
#include <memory>
#include <unordered_map>
#include <algorithm>

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
  Articulation articulation{articulations::Default{}};
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
// ElementSequence — the realized events for a Part. Composer's authoritative
// output for that Part; Conductor consumes from here.
// ===========================================================================
struct ElementSequence {
  std::vector<Element> elements;
  float totalBeats{0.0f};

  void add(const Element& e) {
    elements.push_back(e);
    float endBeat = e.startBeats;
    if      (e.is_note())  endBeat += e.note().durationBeats;
    else if (e.is_hit())   endBeat += e.hit().durationBeats;
    else if (e.is_rest())  endBeat += e.rest().durationBeats;
    else if (e.is_chord()) endBeat += e.chord().dur;  // remove with stage 9
    if (endBeat > totalBeats) totalBeats = endBeat;
  }

  void sort_by_beat() {
    std::stable_sort(elements.begin(), elements.end(),
      [](const Element& a, const Element& b) {
        return a.startBeats < b.startBeats;
      });
  }

  int  size()  const { return int(elements.size()); }
  bool empty() const { return elements.empty(); }

  auto begin()       { return elements.begin(); }
  auto end()         { return elements.end();   }
  auto begin() const { return elements.begin(); }
  auto end()   const { return elements.end();   }
};

// ===========================================================================
// Phrase / Passage — Composer-internal scratch space.
//
// Built by strategies during compose(); walked by realize_event_sequences_
// to populate Part.elementSequence. NOT consumed by Conductor or any
// downstream code — the authoritative score is Part.elementSequence.
//
// These types are kept in structure.h for now (rather than moved to a
// composer_internal.h) because strategy headers and the DUN parser still
// reference them by inclusion. If the dependency graph ever cleans up,
// they should move.
// ===========================================================================

// Phrase — a musical sentence. Collection of Figures.
struct Phrase {
  Pitch startingPitch;
  std::vector<std::unique_ptr<Figure>> figures;

  // Dense parallel vector — connectors.size() == figures.size() after compose.
  // connectors[0] is a dummy with leadStep=0 by convention; figure 0 is
  // positioned by phrase.startingPitch directly. connectors[i>0].leadStep is
  // the inter-figure cursor advance (applied at realize time, not by
  // mutating the figure).
  std::vector<FigureConnector> connectors;

  // Deep copy constructor (unique_ptr makes Phrase move-only otherwise)
  Phrase() = default;
  Phrase(Phrase&&) = default;
  Phrase& operator=(Phrase&&) = default;
  Phrase(const Phrase& other)
      : startingPitch(other.startingPitch), connectors(other.connectors) {
    for (const auto& f : other.figures) figures.push_back(f->clone());
  }
  Phrase& operator=(const Phrase& other) {
    if (this != &other) {
      startingPitch = other.startingPitch;
      connectors = other.connectors;
      figures.clear();
      for (const auto& f : other.figures) figures.push_back(f->clone());
    }
    return *this;
  }

  void add_figure(std::unique_ptr<Figure> fig) { figures.push_back(std::move(fig)); }
  void add_melodic_figure(MelodicFigure fig) { figures.push_back(std::make_unique<MelodicFigure>(std::move(fig))); }
  // Convenience: append a (figure, connector) pair atomically. Use this in
  // strategies that produce both, to keep figures.size() == connectors.size().
  void add_melodic_figure_with_connector(MelodicFigure fig, FigureConnector fc) {
    figures.push_back(std::make_unique<MelodicFigure>(std::move(fig)));
    connectors.push_back(fc);
  }
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
  float beats;          // length in beats (composed length)
  Meter meter{Meter::M_4_4};
  float tempo{120.0f};  // bpm
  Scale scale;          // defaults from Piece key, overridden for modulations

  // When > 0, omit this many beats from the END when performing.
  // The composition is still the full `beats` long; only playback is
  // truncated. Use this when a following section interrupts mid-bar
  // (e.g. orchestral tutti crashing in on a held cadential note).
  float truncateTailBeats{0.0f};

  Section() : scale(Scale::get("C", "Major")) {}
  Section(const std::string& n, float b, float bpm, Meter m, Scale s)
    : name(n), beats(b), meter(m), tempo(bpm), scale(s) {}

  // Harmony context
  std::vector<KeyContext> keyContexts;
  std::optional<ChordProgression> chordProgression;
  HarmonyTimeline harmonyTimeline;

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

  // Compositional content (Composer-internal scratch): keyed by section name
  std::unordered_map<std::string, Passage> passages;

  // Authoritative realized events. Populated by Composer (via realize step) or
  // by direct-build helpers below.
  ElementSequence elementSequence;

  // Backwards-compat read accessor (was a field; now read from elementSequence)
  float totalBeats() const { return elementSequence.totalBeats; }

  // --- Direct event building (for testing, simple progressions) ---

  void add_chord(float startBeat, const Chord& chord) {
    elementSequence.add({startBeat, chord});
  }

  void add_chord(const Chord& chord) {
    add_chord(elementSequence.totalBeats, chord);
  }

  void add_note(float startBeat, float noteNumber, float velocity, float durationBeats,
                Articulation art = articulations::Default{}, Ornament orn = Ornament{}) {
    elementSequence.add({startBeat, Note{noteNumber, velocity, durationBeats, art, orn}});
  }

  void add_note(float noteNumber, float velocity, float durationBeats,
                Articulation art = articulations::Default{}) {
    add_note(elementSequence.totalBeats, noteNumber, velocity, durationBeats, art);
  }

  void add_hit(float startBeat, int drumNumber, float velocity, float durationBeats = 0.1f) {
    elementSequence.add({startBeat, Hit{drumNumber, velocity, durationBeats}});
  }

  void add_rest(float durationBeats) {
    elementSequence.add({elementSequence.totalBeats, Rest{durationBeats}});
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

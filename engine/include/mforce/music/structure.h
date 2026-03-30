#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include <vector>
#include <memory>
#include <string>

namespace mforce {

// ---------------------------------------------------------------------------
// Element — a musical event at a specific time.
// ---------------------------------------------------------------------------
struct Element {
  TimeValue startTime;
  float noteNumber;
  float duration;  // in same units as startTime
  Articulation articulation{Articulation::Default};
  Ornament ornament{Ornament::None};
};

// ---------------------------------------------------------------------------
// ElementCollection — ordered collection of Elements.
// ---------------------------------------------------------------------------
struct ElementCollection {
  std::vector<Element> elements;
  TimeUnit timeUnit{TimeUnit::Beats};
  float totalTime{0.0f};

  void add(float noteNumber, float duration, Articulation art = Articulation::Default,
           Ornament orn = Ornament::None) {
    elements.push_back({{totalTime, timeUnit}, noteNumber, duration, art, orn});
    totalTime += duration;
  }

  void add_at(float startTime, float noteNumber, float duration,
              Articulation art = Articulation::Default) {
    elements.push_back({{startTime, timeUnit}, noteNumber, duration, art});
    totalTime = std::max(totalTime, startTime + duration);
  }

  int count() const { return int(elements.size()); }
};

// ---------------------------------------------------------------------------
// Passage — melodic figures joined by connectors → ElementCollection.
// The core composition algorithm: walks the scale using PitchReader,
// applying figure steps to generate a sequence of pitched notes.
// ---------------------------------------------------------------------------
struct Passage {
  TimeUnit timeUnit{TimeUnit::Beats};
  Meter meter{Meter::M_4_4};
  Scale scale;
  Pitch startPitch;

  struct FigureEntry {
    MelodicFigure figure;
    FigureConnector connector; // how to connect TO this figure
  };

  std::vector<FigureEntry> entries;

  void add_figure(MelodicFigure fig, FigureConnector conn = FigureConnector::step(0)) {
    entries.push_back({std::move(fig), conn});
  }

  // Compose: convert figures into a stream of pitched, timed elements.
  ElementCollection compose() const {
    ElementCollection coll;
    coll.timeUnit = timeUnit;

    PitchReader reader(scale, startPitch.octave);
    reader.set_pitch(startPitch);

    for (int f = 0; f < int(entries.size()); ++f) {
      const auto& entry = entries[f];
      const auto& fig = entry.figure;
      const auto& conn = entry.connector;

      // Apply connector (how we arrive at this figure's first note)
      if (f > 0) {
        switch (conn.type) {
          case ConnectorType::Step:
            reader.step(conn.stepValue);
            break;
          case ConnectorType::Pitch:
            reader.set_pitch(conn.pitch);
            break;
          case ConnectorType::EndPitch:
            reader.set_pitch(conn.pitch);
            break;
          case ConnectorType::Elide:
            // No movement — first note of next figure = last note of previous
            break;
        }
      }

      // Generate notes from figure
      for (int n = 0; n < fig.note_count(); ++n) {
        float dur = fig.pulses.get(n).value;
        Articulation art = (n < int(fig.articulations.size()))
            ? fig.articulations[n] : Articulation::Default;
        Ornament orn = (n < int(fig.ornaments.size()))
            ? fig.ornaments[n] : Ornament::None;

        coll.add(reader.get_note_number(), dur, art, orn);

        // Step to next note (if not the last note in figure)
        if (n < fig.steps.count()) {
          reader.step(fig.steps.get(n));
        }
      }
    }

    return coll;
  }

  Passage() : scale(Scale::get("C", "Major")), startPitch(Pitch::from_name("C", 4)) {}
};

// ---------------------------------------------------------------------------
// Section — a time-bounded portion of a Piece.
// ---------------------------------------------------------------------------
struct Section {
  std::string name;
  TimeValue startTime{0.0f, TimeUnit::Beats};
  TimeValue endTime{0.0f, TimeUnit::Beats};
};

// ---------------------------------------------------------------------------
// Part — a single voice/instrument line. Contains Passages.
// ---------------------------------------------------------------------------
struct Part {
  std::string name;
  TimeUnit timeUnit{TimeUnit::Beats};
  std::vector<Passage> passages;

  void add_passage(Passage p) { passages.push_back(std::move(p)); }

  // Compose all passages into a single ElementCollection
  ElementCollection compose() const {
    ElementCollection coll;
    coll.timeUnit = timeUnit;
    for (auto& p : passages) {
      auto pc = p.compose();
      for (auto& e : pc.elements) {
        coll.add_at(coll.totalTime + e.startTime.value, e.noteNumber, e.duration,
                    e.articulation);
      }
    }
    return coll;
  }
};

// ---------------------------------------------------------------------------
// Piece — top-level composition. Has a Key, Sections, and Parts.
// ---------------------------------------------------------------------------
struct Piece {
  Key key;
  std::vector<Section> sections;
  std::vector<Part> parts;

  Piece() : key(Key::get("C Major")) {}

  void add_section(Section s) { sections.push_back(std::move(s)); }
  void add_part(Part p) { parts.push_back(std::move(p)); }
};

} // namespace mforce

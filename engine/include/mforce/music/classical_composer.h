#pragma once
#include "mforce/music/structure.h"
#include "mforce/music/figures.h"
#include "mforce/music/pitch_reader.h"
#include "mforce/core/randomizer.h"

namespace mforce {

// ---------------------------------------------------------------------------
// ClassicalComposer — generates a melodic passage in classical binary form.
// Antecedent (f1-f2-f3-cadence) + Consequent (f1-f2-f4-cadence).
// Ported from legacy C# ClassicalComposer.
// ---------------------------------------------------------------------------
struct ClassicalComposer {
  StepGenerator stepGen;
  FigureBuilder builder;
  Randomizer rng;

  explicit ClassicalComposer(uint32_t seed = 0xC1A5'0001u)
    : stepGen(seed), builder(seed + 100), rng(seed + 200) {}

  void compose(Piece& piece) {
    int periodLength = 32;
    bool compFigure = true;
    int repeats = 2;

    Part& melody = piece.parts[0];
    Scale scale = piece.key.scale;
    PitchReader reader(scale);

    int startLevel = 0;

    // Anchor pitches: start, mid (degree 2), end (degree 1)
    reader.set_pitch(5, 2);
    Pitch midPitch = reader.get_pitch();

    reader.set_pitch(5, 1);
    Pitch endPitch = reader.get_pitch();

    reader.set_pitch(5, startLevel);
    Pitch startPitch = reader.get_pitch();

    int currLevel = startLevel;
    float figureLength = float(periodLength) / 8.0f;

    // f1: skip-based opening gesture
    builder.defaultPulse = 1.0f;
    StepSequence ss = stepGen.skip_sequence(3, startLevel);
    MelodicFigure f1 = builder.build(ss, 1.0f);

    if (rng.decide(0.5f)) {
      f1 = builder.vary_rhythm(f1);
    }

    currLevel += f1.net_step();

    // f2: either replicated sub-figure or single generated figure
    MelodicFigure f2;
    if (compFigure) {
      builder.defaultPulse = 0.5f;
      MelodicFigure repFig = builder.build_from_length(figureLength / float(repeats), true);

      int step = rng.select_int({-3, -2, -1, 1, 2, 3});
      f2 = builder.replicate(repFig, repeats, step);
    } else {
      f2 = builder.build_from_length(figureLength, true);
    }

    currLevel += f2.net_step();

    // f3: transition to half-cadence target (degree 1)
    int target = 1;
    int net = target - currLevel;
    net += (net < 0) ? -1 : 1; // overshoot by 1 for the cadential step

    builder.defaultPulse = 1.0f;
    // Use targeted sequence to reach the desired net
    StepSequence targetSteps = stepGen.targeted_sequence(
        int(figureLength * 1.5f / builder.defaultPulse) - 1, net);
    MelodicFigure f3 = builder.build(targetSteps, builder.defaultPulse);

    currLevel += f3.net_step();

    // c1: half-cadence note (2 beats)
    MelodicFigure c1 = builder.single_note(2.0f);

    // Account for f1+f2 being repeated in the consequent
    currLevel += f1.net_step() + f2.net_step();

    // f4: transition to full cadence on tonic (degree 0)
    target = 0;
    net = target - currLevel;
    net += (net < 0) ? -1 : 1;

    StepSequence target2Steps = stepGen.targeted_sequence(3, net);
    MelodicFigure f4 = builder.build(target2Steps, builder.defaultPulse);

    currLevel += f4.net_step();

    // c2: full cadence note (4 beats)
    MelodicFigure c2 = builder.single_note(4.0f);

    // Build the Passage as two Phrases (antecedent + consequent)
    // Antecedent: f1 → f2 → f3 → c1
    Phrase antecedent;
    antecedent.startingPitch = startPitch;
    antecedent.add_figure(f1);
    antecedent.add_figure(f2, FigureConnector::step(-1));
    antecedent.add_figure(f3, FigureConnector{ConnectorType::EndPitch, 0, midPitch});
    antecedent.add_figure(c1, FigureConnector::step(-1));

    // Consequent: f1 → f2 → f4 → c2
    Phrase consequent;
    consequent.startingPitch = startPitch;
    consequent.add_figure(f1);
    consequent.add_figure(f2, FigureConnector::step(-1));
    consequent.add_figure(f4, FigureConnector{ConnectorType::EndPitch, 0, endPitch});
    consequent.add_figure(c2, FigureConnector::step(-1));

    // Compute total beats
    float totalBeats = 0;
    for (auto& ph : {antecedent, consequent})
      for (auto& fig : ph.figures)
        totalBeats += fig.total_duration();

    // Create section and passage
    Passage passage;
    passage.add_phrase(antecedent);
    passage.add_phrase(consequent);

    // Create section if piece has none
    if (piece.sections.empty()) {
      piece.add_section(Section("Main", totalBeats, 100.0f, Meter::M_4_4, scale));
    }

    melody.passages[piece.sections[0].name] = std::move(passage);
  }
};

} // namespace mforce

#pragma once
// Helpers the music tests share with the test programs of the modules above it.
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include <string>
#include <utility>

using namespace mforce;


// A one-section piece (C major, 8 beats at 120) whose part `name` holds one
// passage of one phrase from C4: figure 1 steps 0, +1 (one beat each);
// figure 2 enters by a connector of +2 and steps 0, -1 (two beats, one).
inline Piece realize_probe_piece(const std::string& name) {
    Piece piece;
    piece.add_section(Section("Main", 8.0f, 120.0f, Meter::M_4_4, Scale::get("C", "Major")));
    MelodicFigure a;
    a.units.push_back({1.0f, 0});
    a.units.push_back({1.0f, 1});
    MelodicFigure b;
    b.units.push_back({2.0f, 0});
    b.units.push_back({1.0f, -1});
    FigureConnector lead{};
    lead.leadStep = 2;
    Phrase phrase;
    phrase.startingPitch = Pitch::from_name("C", 4);
    phrase.add_melodic_figure_with_connector(std::move(a), FigureConnector{});
    phrase.add_melodic_figure_with_connector(std::move(b), lead);
    Passage passage;
    passage.add_phrase(std::move(phrase));
    Part part;
    part.name = name;
    part.instrumentType = name;
    part.passages["Main"] = std::move(passage);
    piece.add_part(std::move(part));
    return piece;
}

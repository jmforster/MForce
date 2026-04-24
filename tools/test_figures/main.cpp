#include "mforce/music/basics.h"
#include "mforce/music/classical_composer.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace mforce;

namespace {

// Build a deterministic base figure: steps [0, +1, -1, 0], uniform 1-beat pulse.
// Net step = 0, so the figure is contour-neutral — ideal for a round-trip test.
MelodicFigure make_smoke_figure() {
    MelodicFigure fig;
    fig.units.push_back({1.0f,  0});
    fig.units.push_back({1.0f, +1});
    fig.units.push_back({1.0f, -1});
    fig.units.push_back({1.0f,  0});
    return fig;
}

// Assemble a minimal PieceTemplate: one section, one part, one passage,
// one phrase driven by WrapperPhraseStrategy with a single locked figure.
PieceTemplate make_smoke_template(const MelodicFigure& fig) {
    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = 0xC0FFEEu;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = 8.0f;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;
    part.instrumentPatch = "";

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = fig;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    return tmpl;
}

int fail(const char* msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

} // namespace

int main(int, char**) {
    const MelodicFigure expected = make_smoke_figure();
    const PieceTemplate tmpl = make_smoke_template(expected);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    if (piece.parts.size() != 1) return fail("piece.parts.size() != 1");
    const auto& part = piece.parts[0];

    auto passageIt = part.passages.find("Main");
    if (passageIt == part.passages.end()) return fail("passages[\"Main\"] missing");
    const Passage& passage = passageIt->second;

    if (passage.phrases.size() != 1) return fail("passage.phrases.size() != 1");
    const Phrase& phrase = passage.phrases[0];

    if (phrase.figures.size() != 1) return fail("phrase.figures.size() != 1");
    const Figure* figBase = phrase.figures[0].get();
    const MelodicFigure* fig = dynamic_cast<const MelodicFigure*>(figBase);
    if (!fig) return fail("phrase.figures[0] is not a MelodicFigure");

    if (fig->units.size() != expected.units.size())
        return fail("unit count mismatch");

    for (size_t i = 0; i < expected.units.size(); ++i) {
        if (fig->units[i].step != expected.units[i].step) {
            std::cerr << "  step mismatch at i=" << i
                      << " expected=" << expected.units[i].step
                      << " got=" << fig->units[i].step << "\n";
            return fail("step mismatch");
        }
        if (std::fabs(fig->units[i].duration - expected.units[i].duration) > 1e-5f) {
            std::cerr << "  duration mismatch at i=" << i
                      << " expected=" << expected.units[i].duration
                      << " got=" << fig->units[i].duration << "\n";
            return fail("duration mismatch");
        }
    }

    std::cout << "OK: WrapperPhraseStrategy round-trip (" << fig->units.size()
              << " units)\n";
    return 0;
}

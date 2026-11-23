#include "mforce/testing/check.h"
#include "test_support.h"
#include "figure_helpers.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/templates.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include <cmath>
#include <iostream>
#include <tuple>
#include <variant>
#include <vector>

using namespace mforce;

namespace {

// ----------------------------------------------------------------------------
// Integration helper: run `figure` through the Composer via
// WrapperPhraseStrategy and return the realized MelodicFigure. On any
// structural mismatch returns {false, {}} so the caller can emit its own
// diagnostic.
// ----------------------------------------------------------------------------
struct ComposeResult {
    bool ok;
    MelodicFigure fig;
};

ComposeResult compose_locked(const MelodicFigure& figure) {
    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = 0xABCDu;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = 32.0f;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy.name = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = figure;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece = compose_builtin(tmpl).piece;

    if (piece.parts.size() != 1) return {false, {}};
    auto it = piece.parts[0].passages.find("Main");
    if (it == piece.parts[0].passages.end()) return {false, {}};
    if (it->second.phrases.size() != 1) return {false, {}};
    const Phrase& ph = it->second.phrases[0];
    if (ph.figures.size() != 1) return {false, {}};
    const MelodicFigure* fig = dynamic_cast<const MelodicFigure*>(ph.figures[0].get());
    if (!fig) return {false, {}};
    return {true, *fig};
}
}  // namespace

// ----------------------------------------------------------------------------
// Smoke test — round-trips a locked figure through
// the WrapperPhraseStrategy-driven Composer pipeline and verifies the realized
// figure matches the input unit-for-unit.
// ----------------------------------------------------------------------------
void test_smoke_round_trip() {
    MelodicFigure expected;
    expected.units.push_back({1.0f,  0});
    expected.units.push_back({1.0f, +1});
    expected.units.push_back({1.0f, -1});
    expected.units.push_back({1.0f,  0});

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

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy.name = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = expected;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece = compose_builtin(tmpl).piece;

    REQUIRE(piece.parts.size() == 1u);
    auto passageIt = piece.parts[0].passages.find("Main");
    REQUIRE(passageIt != piece.parts[0].passages.end());
    const Passage& p = passageIt->second;
    REQUIRE(p.phrases.size() == 1u);
    const Phrase& ph = p.phrases[0];
    REQUIRE(ph.figures.size() == 1u);
    const MelodicFigure* fig = dynamic_cast<const MelodicFigure*>(ph.figures[0].get());
    REQUIRE(fig);
    REQUIRE(fig->units.size() == expected.units.size());
    for (size_t i = 0; i < expected.units.size(); ++i) {
        REQUIRE(fig->units[i].step == expected.units[i].step);
        REQUIRE(std::fabs(double(fig->units[i].duration) - double(expected.units[i].duration)) <= double(1e-5f));
    }
}
namespace {

// ----------------------------------------------------------------------------
// figure_transforms::* — elementary transform unit tests.
// ----------------------------------------------------------------------------

MelodicFigure fig4() {
    MelodicFigure f;
    f.units.push_back({1.0f,  0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -2});
    return f;
}
}  // namespace

// ----------------------------------------------------------------------------
// Integration tests — each deterministic transform round-trips through the
// Composer via WrapperPhraseStrategy and must match the transform's direct
// output unit-for-unit.
// ----------------------------------------------------------------------------

void integ_invert() {
    MelodicFigure expected = figure_transforms::invert(fig4());
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "invert") == 0);
}

void integ_retrograde() {
    MelodicFigure expected = figure_transforms::retrograde_steps(fig4());
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "retrograde") == 0);
}

void integ_combine() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    FigureConnector fc; fc.leadStep = +2;
    MelodicFigure expected = figure_transforms::combine(a, b, fc);
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "combine") == 0);
}

void integ_replicate() {
    MelodicFigure expected = figure_transforms::replicate(
        fig4(), std::vector<int>{+2, -2});
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "replicate") == 0);
}

void integ_split() {
    MelodicFigure expected = figure_transforms::split(fig4(), /*splitAt=*/1, /*repeats=*/2);
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "split") == 0);
}

void integ_add_neighbor() {
    MelodicFigure expected = figure_transforms::add_neighbor(fig4(), /*addAt=*/1);
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "add_neighbor") == 0);
}

void integ_add_turn() {
    MelodicFigure expected = figure_transforms::add_turn(fig4(), /*addAt=*/1);
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "add_turn") == 0);
}

void integ_stretch() {
    MelodicFigure expected = figure_transforms::stretch(fig4(), 2.0f);
    auto r = compose_locked(expected);
    REQUIRE(r.ok);
    REQUIRE(expect_figures_equal(r.fig, expected, "stretch") == 0);
}

// ----------------------------------------------------------------------------
// Pitch-realization test — asserts that after compose(), Part::elementSequence
// contains Notes with the expected noteNumbers for base figure [0, +1, -1, 0]
// starting at C4 in C major.
//
// Codebase convention: Pitch::note_number() = octave * 12 + semitone_offset,
// so C4 = 48, D4 = 50, C4 = 48, C4 = 48 (one octave lower than MIDI scientific
// pitch notation).
// ----------------------------------------------------------------------------
void integ_pitch_realization() {
    MelodicFigure base;
    base.units.push_back({1.0f,  0});
    base.units.push_back({1.0f, +1});
    base.units.push_back({1.0f, -1});
    base.units.push_back({1.0f,  0});

    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = 0xABCDu;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = 8.0f;
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);

    PhraseTemplate phrase;
    phrase.name = "wrap";
    phrase.strategy.name = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = base;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece = compose_builtin(tmpl).piece;

    const auto& es = piece.parts[0].elementSequence;
    const int expected[] = {48, 50, 48, 48};
    int noteCount = 0;
    for (size_t i = 0; i < es.elements.size(); ++i) {
        const Element& e = es.elements[i];
        if (std::holds_alternative<Note>(e.content)) {
            const Note& n = std::get<Note>(e.content);
            REQUIRE(noteCount < 4);
            int midi = int(std::round(n.noteNumber));
            REQUIRE(midi == expected[noteCount]);
            ++noteCount;
        }
    }
    REQUIRE(noteCount == 4);
}

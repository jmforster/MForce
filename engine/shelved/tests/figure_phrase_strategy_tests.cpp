#include "mforce/testing/check.h"
#include "test_support.h"
#include "figure_helpers.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/music_json.h"
#include "mforce/music/structure.h"
#include "mforce/shelved/elaborated_phrase_strategy.h"
#include "mforce/shelved/elaborated_phrase_strategy_json.h"
#include "mforce/shelved/two_figure_phrase_strategy.h"
#include "mforce/shelved/two_figure_phrase_strategy_json.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <utility>
#include <vector>
#include <cmath>

using namespace mforce;

namespace {

// ----------------------------------------------------------------------------
// TwoFigurePhraseStrategy integration tests
// ----------------------------------------------------------------------------

struct TwoFigureResult {
    bool ok;
    MelodicFigure fig1;
    MelodicFigure fig2;
};

TwoFigureResult compose_two_figure(const TwoFigurePhraseSettings& cfg) {
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
    phrase.name = "tf";
    phrase.strategy.name = "two_figure_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);
    phrase.strategy.settings = cfg;

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece = compose_builtin(tmpl).piece;

    if (piece.parts.size() != 1) return {false, {}, {}};
    auto it = piece.parts[0].passages.find("Main");
    if (it == piece.parts[0].passages.end()) return {false, {}, {}};
    if (it->second.phrases.size() != 1) return {false, {}, {}};
    const Phrase& ph = it->second.phrases[0];
    if (ph.figures.size() != 2) return {false, {}, {}};
    const MelodicFigure* f1 = dynamic_cast<const MelodicFigure*>(ph.figures[0].get());
    const MelodicFigure* f2 = dynamic_cast<const MelodicFigure*>(ph.figures[1].get());
    if (!f1 || !f2) return {false, {}, {}};
    return {true, *f1, *f2};
}
}  // namespace

void integ_two_figure_count_invert() {
    TwoFigurePhraseSettings cfg;
    cfg.method = TwoFigurePhraseSettings::Method::ByCount;
    cfg.count = 4;
    cfg.seed = 0x2F1Cu;
    cfg.transform = TransformOp::Invert;
    auto r = compose_two_figure(cfg);
    REQUIRE(r.ok);
    // RFB's shape selection (arc/run/zigzag/...) may produce slightly fewer
    // units than `count` depending on which shape is chosen; only assert
    // a non-empty figure and that invert preserves the unit count.
    REQUIRE(!r.fig1.units.empty());
    REQUIRE(r.fig2.units.size() == r.fig1.units.size());
    for (size_t i = 0; i < r.fig1.units.size(); ++i) {
        REQUIRE(r.fig2.units[i].step == -r.fig1.units[i].step);
    }
}

void integ_two_figure_length_retrograde() {
    TwoFigurePhraseSettings cfg;
    cfg.method = TwoFigurePhraseSettings::Method::ByLength;
    cfg.length = 4.0f;
    cfg.seed = 0x2F1Du;
    cfg.transform = TransformOp::Reverse;
    auto r = compose_two_figure(cfg);
    REQUIRE(r.ok);
    MelodicFigure expected = figure_transforms::apply(
        r.fig1, TransformOp::Reverse, 0, cfg.seed + 1);
    REQUIRE(expect_figures_equal(r.fig2, expected, "two_figure length+retrograde") == 0);
}

void integ_two_figure_singleton_stretch() {
    TwoFigurePhraseSettings cfg;
    cfg.method = TwoFigurePhraseSettings::Method::Singleton;
    cfg.seed = 0x2F1Eu;
    cfg.transform = TransformOp::Stretch;
    cfg.transformParam = 2;
    auto r = compose_two_figure(cfg);
    REQUIRE(r.ok);
    REQUIRE(r.fig1.units.size() == 1u);
    REQUIRE(r.fig2.units.size() == 1u);
    REQUIRE(std::fabs(double(r.fig2.units[0].duration) - double(r.fig1.units[0].duration * 2.0f)) <= double(1e-5f));
}

void integ_two_figure_json_round_trip() {
    TwoFigurePhraseSettings cfg;
    cfg.method = TwoFigurePhraseSettings::Method::ByCount;
    cfg.count = 3;
    cfg.seed = 0x2F1Fu;
    cfg.transform = TransformOp::Invert;
    cfg.constraints.net = 0;

    auto r1 = compose_two_figure(cfg);
    REQUIRE(r1.ok);

    nlohmann::json j = cfg;
    TwoFigurePhraseSettings cfg2;
    from_json(j, cfg2);

    auto r2 = compose_two_figure(cfg2);
    REQUIRE(r2.ok);

    int rc = expect_figures_equal(r1.fig1, r2.fig1, "fig1 after round-trip");
    REQUIRE(rc == 0);
    REQUIRE(expect_figures_equal(r1.fig2, r2.fig2, "fig2 after round-trip") == 0);
}
namespace {

// ----------------------------------------------------------------------------
// ElaboratedPhraseStrategy integration tests
// ----------------------------------------------------------------------------

struct ElaboratedResult {
    bool ok;
    std::vector<MelodicFigure> figures;
    std::vector<FigureConnector> connectors;
};

ElaboratedResult compose_elaborated(const ElaboratedPhraseSettings& cfg) {
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
    phrase.name = "ep";
    phrase.strategy.name = "elaborated_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);
    phrase.strategy.settings = cfg;

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece = compose_builtin(tmpl).piece;

    if (piece.parts.size() != 1) return {false, {}, {}};
    auto it = piece.parts[0].passages.find("Main");
    if (it == piece.parts[0].passages.end()) return {false, {}, {}};
    if (it->second.phrases.size() != 1) return {false, {}, {}};
    const Phrase& ph = it->second.phrases[0];

    std::vector<MelodicFigure> figs;
    for (const auto& fp : ph.figures) {
        const MelodicFigure* mf = dynamic_cast<const MelodicFigure*>(fp.get());
        if (!mf) return {false, {}, {}};
        figs.push_back(*mf);
    }
    return {true, std::move(figs), ph.connectors};
}
}  // namespace

void integ_elab_all_leave() {
    ElaboratedPhraseSettings cfg;
    // Provide a literal skeleton so behavior is fully deterministic.
    cfg.skeleton = MelodicFigure{};
    cfg.skeleton->units = {
        {2.0f, 0}, {2.0f, +1}, {2.0f, +1}, {2.0f, -1}
    };
    cfg.choiceMode = ElaboratedPhraseSettings::ChoiceMode::AllLeave;

    auto r = compose_elaborated(cfg);
    REQUIRE(r.ok);
    REQUIRE(r.figures.size() == 4u);
    REQUIRE(r.connectors.size() == 4u);

    // All Leave -> all single-unit figures with step=0.
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(r.figures[i].units.size() == 1u);
        REQUIRE(r.figures[i].units[0].step == 0);
        REQUIRE(std::fabs(double(r.figures[i].units[0].duration) - double(2.0f)) <= double(1e-5f));
    }

    // FC[0] dummy = 0; FC[i>0].leadStep = skel.step[i] - prev_net_step.
    // Leave figures have net_step = 0, so FC[i>0].leadStep = skel.step[i].
    REQUIRE(r.connectors[0].leadStep == 0);
    REQUIRE(r.connectors[1].leadStep == +1);
    REQUIRE(r.connectors[2].leadStep == +1);
    REQUIRE(r.connectors[3].leadStep == -1);
}

void integ_elab_all_generate() {
    ElaboratedPhraseSettings cfg;
    cfg.skeleton = MelodicFigure{};
    cfg.skeleton->units = {
        {2.0f, 0}, {2.0f, +1}, {2.0f, -1}
    };
    cfg.choiceMode = ElaboratedPhraseSettings::ChoiceMode::AllGenerate;
    cfg.seed = 0xE1AB0001u;

    auto r = compose_elaborated(cfg);
    REQUIRE(r.ok);
    REQUIRE(r.figures.size() == 3u);
    REQUIRE(r.connectors.size() == 3u);

    // Each generated figure should be non-empty with units[0].step = 0 (RFB
    // convention). The strategy does not constrain RFB-generated figures to exact
    // anchor duration — the leadStep bridging math accounts for any drift.
    for (size_t i = 0; i < 3; ++i) {
        const auto& fig = r.figures[i];
        REQUIRE(!fig.units.empty());
        REQUIRE(fig.units[0].step == 0);
    }

    // FC[0] dummy.
    REQUIRE(r.connectors[0].leadStep == 0);

    // FC[i>0].leadStep = skel.step[i] - prev_net_step. Verify the math holds
    // by recomputing from observed figure net_steps.
    int prev_net = 0;
    for (const auto& u : r.figures[0].units) prev_net += u.step;
    REQUIRE(r.connectors[1].leadStep == 1 - prev_net);

    int prev_net_2 = 0;
    for (const auto& u : r.figures[1].units) prev_net_2 += u.step;
    REQUIRE(r.connectors[2].leadStep == -1 - prev_net_2);
}

void integ_elab_json_round_trip() {
    ElaboratedPhraseSettings cfg;
    cfg.skeleton = MelodicFigure{};
    cfg.skeleton->units = {
        {2.0f, 0}, {2.0f, +1}, {2.0f, -1}
    };
    cfg.choiceMode = ElaboratedPhraseSettings::ChoiceMode::AllLeave;
    cfg.seed = 0xE1AB1234u;

    auto r1 = compose_elaborated(cfg);
    REQUIRE(r1.ok);

    nlohmann::json j = cfg;
    ElaboratedPhraseSettings cfg2;
    from_json(j, cfg2);

    auto r2 = compose_elaborated(cfg2);
    REQUIRE(r2.ok);

    REQUIRE(r1.figures.size() == r2.figures.size());
    REQUIRE(r1.connectors.size() == r2.connectors.size());
    for (size_t i = 0; i < r1.figures.size(); ++i) {
        REQUIRE(r1.connectors[i].leadStep == r2.connectors[i].leadStep);
        REQUIRE(expect_figures_equal(r1.figures[i], r2.figures[i], "json roundtrip figure") == 0);
    }
}

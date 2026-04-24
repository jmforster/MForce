#include "mforce/music/basics.h"
#include "mforce/music/classical_composer.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include "mforce/music/templates_json.h"
#include "mforce/music/conductor.h"
#include "mforce/music/music_json.h"
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/two_figure_phrase_strategy.h"
#include "mforce/core/randomizer.h"
#include "mforce/render/patch_loader.h"
#include "mforce/render/wav_writer.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

using namespace mforce;

// ============================================================================
// Minimal test harness.
// ============================================================================
namespace {

int g_passed = 0;
int g_failed = 0;

#define EXPECT_EQ(actual, expected, msg) do {                                \
    if (!((actual) == (expected))) {                                          \
        std::cerr << "  FAIL(" << __LINE__ << "): " << (msg)                  \
                  << " — expected " << (expected)                             \
                  << " got " << (actual) << "\n";                             \
        return 1;                                                             \
    }                                                                         \
} while (0)

#define EXPECT_NEAR(actual, expected, tol, msg) do {                         \
    if (std::fabs(double(actual) - double(expected)) > double(tol)) {         \
        std::cerr << "  FAIL(" << __LINE__ << "): " << (msg)                  \
                  << " — expected " << (expected)                             \
                  << " got " << (actual) << "\n";                             \
        return 1;                                                             \
    }                                                                         \
} while (0)

#define RUN_TEST(fn) do {                                                    \
    std::cerr << "[TEST] " #fn " ...";                                        \
    int rc = fn();                                                            \
    if (rc == 0) { std::cerr << " PASS\n"; ++g_passed; }                      \
    else         { std::cerr << " FAIL\n"; ++g_failed; }                      \
} while (0)

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
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = figure;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

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

int expect_figures_equal(const MelodicFigure& a, const MelodicFigure& b, const char* tag) {
    if (a.units.size() != b.units.size()) {
        std::cerr << "  FAIL: " << tag << " unit count " << a.units.size()
                  << " vs " << b.units.size() << "\n";
        return 1;
    }
    for (size_t i = 0; i < a.units.size(); ++i) {
        if (a.units[i].step != b.units[i].step) {
            std::cerr << "  FAIL: " << tag << " step[" << i << "] " << a.units[i].step
                      << " vs " << b.units[i].step << "\n";
            return 1;
        }
        if (std::fabs(a.units[i].duration - b.units[i].duration) > 1e-5f) {
            std::cerr << "  FAIL: " << tag << " dur[" << i << "] " << a.units[i].duration
                      << " vs " << b.units[i].duration << "\n";
            return 1;
        }
    }
    return 0;
}

// ----------------------------------------------------------------------------
// Smoke test (preserved from step 1) — round-trips a locked figure through
// the WrapperPhraseStrategy-driven Composer pipeline and verifies the realized
// figure matches the input unit-for-unit.
// ----------------------------------------------------------------------------
int test_smoke_round_trip() {
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
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = expected;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    EXPECT_EQ(piece.parts.size(), 1u, "piece.parts.size");
    auto passageIt = piece.parts[0].passages.find("Main");
    if (passageIt == piece.parts[0].passages.end()) {
        std::cerr << "  FAIL: passages[Main] missing\n"; return 1;
    }
    const Passage& p = passageIt->second;
    EXPECT_EQ(p.phrases.size(), 1u, "phrases.size");
    const Phrase& ph = p.phrases[0];
    EXPECT_EQ(ph.figures.size(), 1u, "figures.size");
    const MelodicFigure* fig = dynamic_cast<const MelodicFigure*>(ph.figures[0].get());
    if (!fig) { std::cerr << "  FAIL: not MelodicFigure\n"; return 1; }
    EXPECT_EQ(fig->units.size(), expected.units.size(), "units.size");
    for (size_t i = 0; i < expected.units.size(); ++i) {
        EXPECT_EQ(fig->units[i].step, expected.units[i].step, "step");
        EXPECT_NEAR(fig->units[i].duration, expected.units[i].duration, 1e-5f, "dur");
    }
    return 0;
}

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

int test_invert() {
    auto out = figure_transforms::invert(fig4());
    EXPECT_EQ(out.units.size(), 4u, "size");
    EXPECT_EQ(out.units[0].step,  0, "step 0");
    EXPECT_EQ(out.units[1].step, -1, "step 1");
    EXPECT_EQ(out.units[2].step, -1, "step 2");
    EXPECT_EQ(out.units[3].step, +2, "step 3");
    return 0;
}

int test_retrograde_steps() {
    // Doc example: [0, +1, +1, -2] -> [0, +2, -1, -1]
    auto out = figure_transforms::retrograde_steps(fig4());
    EXPECT_EQ(out.units.size(), 4u, "size");
    EXPECT_EQ(out.units[0].step,  0, "step 0");
    EXPECT_EQ(out.units[1].step, +2, "step 1");
    EXPECT_EQ(out.units[2].step, -1, "step 2");
    EXPECT_EQ(out.units[3].step, -1, "step 3");
    return 0;
}

int test_prune_end() {
    auto out = figure_transforms::prune(fig4(), 2);
    EXPECT_EQ(out.units.size(), 2u, "size");
    EXPECT_EQ(out.units[0].step, 0,  "step 0");
    EXPECT_EQ(out.units[1].step, +1, "step 1");
    return 0;
}

int test_prune_start() {
    auto out = figure_transforms::prune(fig4(), 2, /*from_start=*/true);
    EXPECT_EQ(out.units.size(), 2u, "size");
    EXPECT_EQ(out.units[0].step, 0,  "step 0 forced to 0");
    EXPECT_EQ(out.units[1].step, -2, "step 1 preserved");
    return 0;
}

int test_set_last_pulse() {
    MelodicFigure f; f.units.push_back({1.0f, 0}); f.units.push_back({2.0f, +1});
    auto out = figure_transforms::set_last_pulse(f, 0.5f);
    EXPECT_EQ(out.units.size(), 2u, "size");
    EXPECT_NEAR(out.units[0].duration, 1.0f, 1e-5f, "dur 0");
    EXPECT_NEAR(out.units[1].duration, 0.5f, 1e-5f, "dur 1");
    return 0;
}

int test_adjust_last_pulse() {
    MelodicFigure f; f.units.push_back({1.0f, 0}); f.units.push_back({2.0f, +1});
    auto out = figure_transforms::adjust_last_pulse(f, -0.5f);
    EXPECT_NEAR(out.units[1].duration, 1.5f, 1e-5f, "dur 1 adjusted");
    auto clamped = figure_transforms::adjust_last_pulse(f, -10.0f);
    EXPECT_NEAR(clamped.units[1].duration, 0.0f, 1e-5f, "clamped at 0");
    return 0;
}

int test_stretch() {
    auto out = figure_transforms::stretch(fig4(), 2.0f);
    EXPECT_EQ(out.units.size(), 4u, "size");
    for (size_t i = 0; i < out.units.size(); ++i)
        EXPECT_NEAR(out.units[i].duration, 2.0f, 1e-5f, "stretched dur");
    return 0;
}

int test_compress() {
    auto out = figure_transforms::compress(fig4(), 4.0f);
    for (size_t i = 0; i < out.units.size(); ++i)
        EXPECT_NEAR(out.units[i].duration, 0.25f, 1e-5f, "compressed dur");
    return 0;
}

// --- combine / replicate family ---

int test_combine_basic() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    FigureConnector fc; fc.leadStep = +2;
    auto out = figure_transforms::combine(a, b, fc);
    EXPECT_EQ(out.units.size(), 4u, "size");
    EXPECT_EQ(out.units[0].step,  0, "a[0]");
    EXPECT_EQ(out.units[1].step, +1, "a[1]");
    EXPECT_EQ(out.units[2].step, +2, "b[0] leadStep applied");
    EXPECT_EQ(out.units[3].step, -1, "b[1] preserved");
    return 0;
}

int test_combine_with_elide_and_adjust() {
    MelodicFigure a;
    a.units.push_back({1.0f, 0});
    a.units.push_back({1.0f, +1});
    a.units.push_back({1.0f, +1});  // will be elided
    MelodicFigure b;
    b.units.push_back({1.0f, 0});
    b.units.push_back({1.0f, -1});
    FigureConnector fc;
    fc.elideCount = 1;
    fc.adjustCount = 0.5f;
    fc.leadStep = +2;
    auto out = figure_transforms::combine(a, b, fc);
    EXPECT_EQ(out.units.size(), 4u, "size after elide");
    EXPECT_NEAR(out.units[1].duration, 1.5f, 1e-5f, "adjusted last of a");
    EXPECT_EQ(out.units[2].step, +2, "leadStep");
    return 0;
}

int test_combine_sugar() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    auto out = figure_transforms::combine(a, b, /*leadStep=*/-3, /*elide=*/true);
    EXPECT_EQ(out.units.size(), 3u, "1 (a-1) + 2 (b)");
    EXPECT_EQ(out.units[1].step, -3, "b[0] leadStep = -3");
    EXPECT_EQ(out.units[2].step, -1, "b[1] preserved");
    return 0;
}

int test_replicate_repeats() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate(f, /*repeats=*/3, /*leadStep=*/+2, /*elide=*/false);
    EXPECT_EQ(out.units.size(), 9u, "3 x 3 units");
    EXPECT_EQ(out.units[3].step, +2, "copy 2 starts at leadStep");
    EXPECT_EQ(out.units[6].step, +2, "copy 3 starts at leadStep");
    return 0;
}

int test_replicate_connectors() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate(f, std::vector<int>{+2, -2});
    EXPECT_EQ(out.units.size(), 9u, "1 + 2 connector copies");
    EXPECT_EQ(out.units[3].step, +2, "first connector");
    EXPECT_EQ(out.units[6].step, -2, "second connector");
    return 0;
}

int test_replicate_and_prune() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate_and_prune(
        f, std::vector<int>{+2, -2}, /*pruneAt1=*/2);
    EXPECT_EQ(out.units.size(), 8u, "9 minus 1 pruned");
    return 0;
}

// --- decorators: split, add_neighbor, add_turn ---

int test_split() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({2.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::split(f, /*splitAt=*/1, /*repeats=*/2);
    EXPECT_EQ(out.units.size(), 4u, "one extra unit");
    EXPECT_NEAR(out.units[1].duration, 1.0f, 1e-5f, "half dur 1a");
    EXPECT_EQ(out.units[1].step, +1, "first sub inherits step");
    EXPECT_NEAR(out.units[2].duration, 1.0f, 1e-5f, "half dur 1b");
    EXPECT_EQ(out.units[2].step, 0, "subsequent sub has step 0");
    return 0;
}

int test_add_neighbor_up() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    auto out = figure_transforms::add_neighbor(f, /*addAt=*/1, /*down=*/false);
    EXPECT_EQ(out.units.size(), 4u, "+2 units at addAt");
    EXPECT_NEAR(out.units[1].duration, 0.5f,  1e-5f, "half dur");
    EXPECT_EQ(out.units[1].step, +1, "main");
    EXPECT_NEAR(out.units[2].duration, 0.25f, 1e-5f, "quarter dur");
    EXPECT_EQ(out.units[2].step, +1, "upper neighbor");
    EXPECT_EQ(out.units[3].step, -1, "return");
    return 0;
}

int test_add_turn_up() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    auto out = figure_transforms::add_turn(f, /*addAt=*/1, /*down=*/false);
    EXPECT_EQ(out.units.size(), 5u, "+3 units at addAt");
    EXPECT_EQ(out.units[1].step, +1, "main inherits step");
    EXPECT_EQ(out.units[2].step, +1, "upper neighbor");
    EXPECT_EQ(out.units[3].step, -2, "cross to lower");
    EXPECT_EQ(out.units[4].step, +1, "return to main");
    for (int i = 1; i <= 4; ++i)
        EXPECT_NEAR(out.units[i].duration, 0.25f, 1e-5f, "quarter dur");
    return 0;
}

// --- randomized transforms (seeded) ---

float total_duration(const MelodicFigure& f) {
    float t = 0; for (auto& u : f.units) t += u.duration; return t;
}

int test_vary_rhythm_preserves_length() {
    MelodicFigure f;
    f.units.push_back({2.0f, 0});
    f.units.push_back({2.0f, +1});
    f.units.push_back({2.0f, -1});
    Randomizer rng(0x1234u);
    auto out = figure_transforms::vary_rhythm(f, rng);
    EXPECT_NEAR(total_duration(out), total_duration(f), 1e-4f, "total duration preserved");
    return 0;
}

int test_vary_steps_changes_interior() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0x5678u);
    auto out = figure_transforms::vary_steps(f, rng, /*variations=*/1);
    EXPECT_EQ(out.units.size(), f.units.size(), "size preserved");
    EXPECT_EQ(out.units.front().step, f.units.front().step, "first step untouched");
    EXPECT_EQ(out.units.back().step,  f.units.back().step,  "last step untouched");
    bool anyDiff = false;
    for (int i = 1; i + 1 < (int)f.units.size(); ++i)
        if (out.units[i].step != f.units[i].step) { anyDiff = true; break; }
    if (!anyDiff) { std::cerr << "  FAIL: no interior step changed\n"; return 1; }
    return 0;
}

int test_vary_composite() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xABCDu);
    auto out = figure_transforms::vary(f, rng, /*amount=*/1.0f);
    EXPECT_NEAR(total_duration(out), total_duration(f), 1e-4f, "total duration preserved");
    return 0;
}

int test_complexify_grows_unit_count() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xDEADu);
    auto out = figure_transforms::complexify(f, rng, /*amount=*/1.0f);
    if ((int)out.units.size() < (int)f.units.size() + 1) {
        std::cerr << "  FAIL: complexify did not grow units (size=" << out.units.size() << ")\n";
        return 1;
    }
    EXPECT_NEAR(total_duration(out), total_duration(f), 1e-3f, "total duration preserved");
    return 0;
}

int test_embellish_marks_articulation() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xBEEFu);
    auto out = figure_transforms::embellish(f, rng, /*count=*/2);
    EXPECT_EQ(out.units.size(), f.units.size(), "size preserved");
    int marcatoCount = 0;
    for (auto& u : out.units) {
        if (std::holds_alternative<articulations::Marcato>(u.articulation)) ++marcatoCount;
    }
    EXPECT_EQ(marcatoCount, 2, "expected 2 marcato marks");
    return 0;
}

int run_unit_tests() {
    RUN_TEST(test_invert);
    RUN_TEST(test_retrograde_steps);
    RUN_TEST(test_prune_end);
    RUN_TEST(test_prune_start);
    RUN_TEST(test_set_last_pulse);
    RUN_TEST(test_adjust_last_pulse);
    RUN_TEST(test_stretch);
    RUN_TEST(test_compress);
    RUN_TEST(test_combine_basic);
    RUN_TEST(test_combine_with_elide_and_adjust);
    RUN_TEST(test_combine_sugar);
    RUN_TEST(test_replicate_repeats);
    RUN_TEST(test_replicate_connectors);
    RUN_TEST(test_replicate_and_prune);
    RUN_TEST(test_split);
    RUN_TEST(test_add_neighbor_up);
    RUN_TEST(test_add_turn_up);
    RUN_TEST(test_vary_rhythm_preserves_length);
    RUN_TEST(test_vary_steps_changes_interior);
    RUN_TEST(test_vary_composite);
    RUN_TEST(test_complexify_grows_unit_count);
    RUN_TEST(test_embellish_marks_articulation);
    return 0;
}

// ----------------------------------------------------------------------------
// Integration tests — each deterministic transform round-trips through the
// Composer via WrapperPhraseStrategy and must match the transform's direct
// output unit-for-unit.
// ----------------------------------------------------------------------------

int integ_invert() {
    MelodicFigure expected = figure_transforms::invert(fig4());
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "invert");
}

int integ_retrograde() {
    MelodicFigure expected = figure_transforms::retrograde_steps(fig4());
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "retrograde");
}

int integ_combine() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    FigureConnector fc; fc.leadStep = +2;
    MelodicFigure expected = figure_transforms::combine(a, b, fc);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "combine");
}

int integ_replicate() {
    MelodicFigure expected = figure_transforms::replicate(
        fig4(), std::vector<int>{+2, -2});
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "replicate");
}

int integ_split() {
    MelodicFigure expected = figure_transforms::split(fig4(), /*splitAt=*/1, /*repeats=*/2);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "split");
}

int integ_add_neighbor() {
    MelodicFigure expected = figure_transforms::add_neighbor(fig4(), /*addAt=*/1);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "add_neighbor");
}

int integ_add_turn() {
    MelodicFigure expected = figure_transforms::add_turn(fig4(), /*addAt=*/1);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "add_turn");
}

int integ_stretch() {
    MelodicFigure expected = figure_transforms::stretch(fig4(), 2.0f);
    auto r = compose_locked(expected);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    return expect_figures_equal(r.fig, expected, "stretch");
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
int integ_pitch_realization() {
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
    phrase.strategy = "wrapper_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);

    FigureTemplate ft;
    ft.source = FigureSource::Locked;
    ft.lockedFigure = base;
    phrase.figures.push_back(ft);

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    const auto& es = piece.parts[0].elementSequence;
    const int expected[] = {48, 50, 48, 48};
    int noteCount = 0;
    for (size_t i = 0; i < es.elements.size(); ++i) {
        const Element& e = es.elements[i];
        if (std::holds_alternative<Note>(e.content)) {
            const Note& n = std::get<Note>(e.content);
            if (noteCount >= 4) {
                std::cerr << "  FAIL: more than 4 notes\n"; return 1;
            }
            int midi = int(std::round(n.noteNumber));
            if (midi != expected[noteCount]) {
                std::cerr << "  FAIL: note " << noteCount << " MIDI " << midi
                          << " expected " << expected[noteCount] << "\n";
                return 1;
            }
            ++noteCount;
        }
    }
    EXPECT_EQ(noteCount, 4, "4 notes realized");
    return 0;
}

// ----------------------------------------------------------------------------
// TwoFigurePhraseStrategy integration tests
// ----------------------------------------------------------------------------

struct TwoFigureResult {
    bool ok;
    MelodicFigure fig1;
    MelodicFigure fig2;
};

TwoFigureResult compose_two_figure(const TwoFigurePhraseConfig& cfg) {
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
    phrase.strategy = "two_figure_phrase";
    phrase.startingPitch = Pitch::from_name("C", 4);
    phrase.twoFigureConfig = cfg;

    passage.phrases.push_back(phrase);
    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

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

int integ_two_figure_count_invert() {
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::ByCount;
    cfg.count = 4;
    cfg.seed = 0x2F1Cu;
    cfg.transform = TransformOp::Invert;
    auto r = compose_two_figure(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    // RFB's shape selection (arc/run/zigzag/...) may produce slightly fewer
    // units than `count` depending on which shape is chosen; only assert
    // a non-empty figure and that invert preserves the unit count.
    if (r.fig1.units.empty()) { std::cerr << "  FAIL: fig1 empty\n"; return 1; }
    EXPECT_EQ(r.fig2.units.size(), r.fig1.units.size(), "fig2 unit count");
    for (size_t i = 0; i < r.fig1.units.size(); ++i) {
        EXPECT_EQ(r.fig2.units[i].step, -r.fig1.units[i].step,
                  "fig2 step = -fig1 step");
    }
    return 0;
}

int integ_two_figure_length_retrograde() {
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::ByLength;
    cfg.length = 4.0f;
    cfg.seed = 0x2F1Du;
    cfg.transform = TransformOp::Reverse;
    auto r = compose_two_figure(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    MelodicFigure expected = figure_transforms::apply(
        r.fig1, TransformOp::Reverse, 0, cfg.seed + 1);
    return expect_figures_equal(r.fig2, expected, "two_figure length+retrograde");
}

int integ_two_figure_singleton_stretch() {
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::Singleton;
    cfg.seed = 0x2F1Eu;
    cfg.transform = TransformOp::Stretch;
    cfg.transformParam = 2;
    auto r = compose_two_figure(cfg);
    if (!r.ok) { std::cerr << "  FAIL: compose failed\n"; return 1; }
    EXPECT_EQ(r.fig1.units.size(), 1u, "singleton has 1 unit");
    EXPECT_EQ(r.fig2.units.size(), 1u, "stretched singleton has 1 unit");
    EXPECT_NEAR(r.fig2.units[0].duration, r.fig1.units[0].duration * 2.0f,
                1e-5f, "stretch factor 2");
    return 0;
}

int integ_two_figure_json_round_trip() {
    TwoFigurePhraseConfig cfg;
    cfg.method = TwoFigurePhraseConfig::Method::ByCount;
    cfg.count = 3;
    cfg.seed = 0x2F1Fu;
    cfg.transform = TransformOp::Invert;
    cfg.constraints.net = 0;

    auto r1 = compose_two_figure(cfg);
    if (!r1.ok) { std::cerr << "  FAIL: in-code compose\n"; return 1; }

    nlohmann::json j = cfg;
    TwoFigurePhraseConfig cfg2;
    from_json(j, cfg2);

    auto r2 = compose_two_figure(cfg2);
    if (!r2.ok) { std::cerr << "  FAIL: json-round-trip compose\n"; return 1; }

    int rc = expect_figures_equal(r1.fig1, r2.fig1, "fig1 after round-trip");
    if (rc) return rc;
    return expect_figures_equal(r1.fig2, r2.fig2, "fig2 after round-trip");
}

int run_integration_tests() {
    RUN_TEST(test_smoke_round_trip);
    RUN_TEST(integ_invert);
    RUN_TEST(integ_retrograde);
    RUN_TEST(integ_combine);
    RUN_TEST(integ_replicate);
    RUN_TEST(integ_split);
    RUN_TEST(integ_add_neighbor);
    RUN_TEST(integ_add_turn);
    RUN_TEST(integ_stretch);
    RUN_TEST(integ_pitch_realization);
    RUN_TEST(integ_two_figure_count_invert);
    RUN_TEST(integ_two_figure_length_retrograde);
    RUN_TEST(integ_two_figure_singleton_stretch);
    RUN_TEST(integ_two_figure_json_round_trip);
    return 0;
}

int run_render(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: test_figures --render <patch.json> <out_dir> [seed]\n"
                  << "  Produces fig.wav + fig.json demonstrating invert /\n"
                  << "  retrograde / retrograde-invert of a hand-authored base.\n";
        return 1;
    }
    std::string patchPath = argv[2];
    std::string outDir    = argv[3];
    uint32_t    seed      = (argc > 4) ? uint32_t(std::stoul(argv[4])) : 0xF16EF16Eu;

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch not found: " << patchPath << "\n"; return 1;
    }
    std::filesystem::create_directories(outDir);

    // Base figure: steps [0, +2, -1, +1], 1-beat pulses. Hand-authored so the
    // listening comparison isn't influenced by RFB choices.
    MelodicFigure base;
    base.units.push_back({1.0f,  0});
    base.units.push_back({1.0f, +2});
    base.units.push_back({1.0f, -1});
    base.units.push_back({1.0f, +1});

    MelodicFigure inv = figure_transforms::invert(base);
    MelodicFigure ret = figure_transforms::retrograde_steps(base);
    MelodicFigure ri  = figure_transforms::retrograde_steps(inv);

    auto make_phrase = [](const char* name, const MelodicFigure& fig) {
        PhraseTemplate ph;
        ph.name = name;
        ph.strategy = "wrapper_phrase";
        ph.startingPitch = Pitch::from_name("C", 4);
        FigureTemplate ft;
        ft.source = FigureSource::Locked;
        ft.lockedFigure = fig;
        ph.figures.push_back(ft);
        return ph;
    };

    auto make_rest_phrase = [](float beats, int idx) {
        PhraseTemplate ph;
        ph.name = "rest_" + std::to_string(idx);
        FigureTemplate ft;
        ft.source = FigureSource::Literal;
        FigureTemplate::LiteralNote ln; ln.rest = true; ln.duration = beats;
        ft.literalNotes.push_back(ln);
        ph.figures.push_back(ft);
        return ph;
    };

    PieceTemplate tmpl;
    tmpl.keyName = "C";
    tmpl.scaleName = "Major";
    tmpl.bpm = 100.0f;
    tmpl.masterSeed = seed;

    PieceTemplate::SectionTemplate sec;
    sec.name = "Main";
    sec.beats = 32.0f;  // generous: 4 phrases x 4 beats + rests + tail
    tmpl.sections.push_back(sec);

    PartTemplate part;
    part.name = "melody";
    part.role = PartRole::Melody;
    part.instrumentPatch = patchPath;

    PassageTemplate passage;
    passage.name = "Main";
    passage.startingPitch = Pitch::from_name("C", 4);
    passage.phrases.push_back(make_phrase("base",   base));
    passage.phrases.push_back(make_rest_phrase(1.0f, 0));
    passage.phrases.push_back(make_phrase("invert", inv));
    passage.phrases.push_back(make_rest_phrase(1.0f, 1));
    passage.phrases.push_back(make_phrase("retrograde", ret));
    passage.phrases.push_back(make_rest_phrase(1.0f, 2));
    passage.phrases.push_back(make_phrase("retro_invert", ri));

    part.passages["Main"] = passage;
    tmpl.parts.push_back(part);

    Piece piece;
    ClassicalComposer composer(tmpl.masterSeed);
    composer.compose(piece, tmpl);

    auto ip = load_instrument_patch(patchPath);
    ip.instrument->volume = 0.5f;
    ip.instrument->hiBoost = 0.3f;

    Conductor conductor;
    for (const auto& p : piece.parts) {
        conductor.instruments[p.instrumentType] = ip.instrument.get();
    }
    conductor.perform(piece);

    float totalBeats = 0.0f;
    for (auto& s : piece.sections) totalBeats += s.beats;
    float bpm = piece.sections[0].tempo;
    float totalSeconds = totalBeats * 60.0f / bpm + 2.0f;
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    { RenderContext ctx{ip.sampleRate}; ip.instrument->render(ctx, mono.data(), frames); }
    std::vector<float> stereo(frames * 2);
    for (int j = 0; j < frames; ++j) { stereo[j*2] = mono[j]; stereo[j*2+1] = mono[j]; }

    std::string wavPath  = outDir + "/fig.wav";
    std::string jsonPath = outDir + "/fig.json";
    if (!write_wav_16le_stereo(wavPath, ip.sampleRate, stereo)) {
        std::cerr << "Failed to write " << wavPath << "\n"; return 1;
    }
    {
        nlohmann::json pj = piece;
        std::ofstream jf(jsonPath);
        jf << pj.dump(2);
    }

    float peak = 0.0f; double rms = 0.0;
    for (auto s : stereo) { float a = std::fabs(s); if (a > peak) peak = a; rms += double(s)*s; }
    rms = std::sqrt(rms / stereo.size());
    std::cout << "render: base=" << base.units.size() << "u  inv=" << inv.units.size()
              << "u  retro=" << ret.units.size() << "u  ri=" << ri.units.size()
              << "u  peak=" << peak << "  rms=" << rms << "\n"
              << "  " << wavPath << "\n  " << jsonPath << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--render") {
        return run_render(argc, argv);
    }
    run_unit_tests();
    run_integration_tests();
    std::cerr << "\n" << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed > 0 ? 1 : 0;
}

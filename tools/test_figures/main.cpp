#include "mforce/music/basics.h"
#include "mforce/music/classical_composer.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

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

int run_unit_tests() {
    // Populated by subsequent tasks.
    return 0;
}

int run_integration_tests() {
    RUN_TEST(test_smoke_round_trip);
    return 0;
}

int run_render(int argc, char** argv) {
    // Populated by Task 8.
    (void)argc; (void)argv;
    std::cerr << "test_figures --render: not yet implemented\n";
    return 1;
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

#include "mforce/testing/check.h"
#include "mforce/compose/default_phrase_strategy.h"
#include "mforce/compose/figure_constraints.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/pool_figure_builder.h"
#include "mforce/compose/pool_figure_builder_json.h"
#include "mforce/compose/templates_json.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/music_json.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>
#include <cmath>

using namespace mforce;

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

void test_invert() {
    auto out = figure_transforms::invert(fig4());
    REQUIRE(out.units.size() == 4u);
    REQUIRE(out.units[0].step == 0);
    REQUIRE(out.units[1].step == -1);
    REQUIRE(out.units[2].step == -1);
    REQUIRE(out.units[3].step == +2);
}

void test_retrograde_steps() {
    // Doc example: [0, +1, +1, -2] -> [0, +2, -1, -1]
    auto out = figure_transforms::retrograde_steps(fig4());
    REQUIRE(out.units.size() == 4u);
    REQUIRE(out.units[0].step == 0);
    REQUIRE(out.units[1].step == +2);
    REQUIRE(out.units[2].step == -1);
    REQUIRE(out.units[3].step == -1);
}

void test_prune_end() {
    auto out = figure_transforms::prune(fig4(), 2);
    REQUIRE(out.units.size() == 2u);
    REQUIRE(out.units[0].step == 0);
    REQUIRE(out.units[1].step == +1);
}

void test_prune_start() {
    auto out = figure_transforms::prune(fig4(), 2, /*from_start=*/true);
    REQUIRE(out.units.size() == 2u);
    REQUIRE(out.units[0].step == 0);
    REQUIRE(out.units[1].step == -2);
}

void test_set_last_pulse() {
    MelodicFigure f; f.units.push_back({1.0f, 0}); f.units.push_back({2.0f, +1});
    auto out = figure_transforms::set_last_pulse(f, 0.5f);
    REQUIRE(out.units.size() == 2u);
    REQUIRE(std::fabs(double(out.units[0].duration) - double(1.0f)) <= double(1e-5f));
    REQUIRE(std::fabs(double(out.units[1].duration) - double(0.5f)) <= double(1e-5f));
}

void test_adjust_last_pulse() {
    MelodicFigure f; f.units.push_back({1.0f, 0}); f.units.push_back({2.0f, +1});
    auto out = figure_transforms::adjust_last_pulse(f, -0.5f);
    REQUIRE(std::fabs(double(out.units[1].duration) - double(1.5f)) <= double(1e-5f));
    auto clamped = figure_transforms::adjust_last_pulse(f, -10.0f);
    REQUIRE(std::fabs(double(clamped.units[1].duration) - double(0.0f)) <= double(1e-5f));
}

void test_stretch() {
    auto out = figure_transforms::stretch(fig4(), 2.0f);
    REQUIRE(out.units.size() == 4u);
    for (size_t i = 0; i < out.units.size(); ++i)
        REQUIRE(std::fabs(double(out.units[i].duration) - double(2.0f)) <= double(1e-5f));
}

void test_compress() {
    auto out = figure_transforms::compress(fig4(), 4.0f);
    for (size_t i = 0; i < out.units.size(); ++i)
        REQUIRE(std::fabs(double(out.units[i].duration) - double(0.25f)) <= double(1e-5f));
}

// --- combine / replicate family ---

void test_combine_basic() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    FigureConnector fc; fc.leadStep = +2;
    auto out = figure_transforms::combine(a, b, fc);
    REQUIRE(out.units.size() == 4u);
    REQUIRE(out.units[0].step == 0);
    REQUIRE(out.units[1].step == +1);
    REQUIRE(out.units[2].step == +2);
    REQUIRE(out.units[3].step == -1);
}

void test_combine_with_elide_and_adjust() {
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
    REQUIRE(out.units.size() == 4u);
    REQUIRE(std::fabs(double(out.units[1].duration) - double(1.5f)) <= double(1e-5f));
    REQUIRE(out.units[2].step == +2);
}

void test_combine_sugar() {
    MelodicFigure a; a.units.push_back({1.0f, 0}); a.units.push_back({1.0f, +1});
    MelodicFigure b; b.units.push_back({1.0f, 0}); b.units.push_back({1.0f, -1});
    auto out = figure_transforms::combine(a, b, /*leadStep=*/-3, /*elide=*/true);
    REQUIRE(out.units.size() == 3u);
    REQUIRE(out.units[1].step == -3);
    REQUIRE(out.units[2].step == -1);
}

void test_replicate_repeats() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate(f, /*repeats=*/3, /*leadStep=*/+2, /*elide=*/false);
    REQUIRE(out.units.size() == 9u);
    REQUIRE(out.units[3].step == +2);
    REQUIRE(out.units[6].step == +2);
}

void test_replicate_connectors() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate(f, std::vector<int>{+2, -2});
    REQUIRE(out.units.size() == 9u);
    REQUIRE(out.units[3].step == +2);
    REQUIRE(out.units[6].step == -2);
}

void test_replicate_and_prune() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::replicate_and_prune(
        f, std::vector<int>{+2, -2}, /*pruneAt1=*/2);
    REQUIRE(out.units.size() == 8u);
}

// --- decorators: split, add_neighbor, add_turn ---

void test_split() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({2.0f, +1});
    f.units.push_back({1.0f, -1});
    auto out = figure_transforms::split(f, /*splitAt=*/1, /*repeats=*/2);
    REQUIRE(out.units.size() == 4u);
    REQUIRE(std::fabs(double(out.units[1].duration) - double(1.0f)) <= double(1e-5f));
    REQUIRE(out.units[1].step == +1);
    REQUIRE(std::fabs(double(out.units[2].duration) - double(1.0f)) <= double(1e-5f));
    REQUIRE(out.units[2].step == 0);
}

void test_add_neighbor_up() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    auto out = figure_transforms::add_neighbor(f, /*addAt=*/1, /*down=*/false);
    REQUIRE(out.units.size() == 4u);
    REQUIRE(std::fabs(double(out.units[1].duration) - double(0.5f)) <= double(1e-5f));
    REQUIRE(out.units[1].step == +1);
    REQUIRE(std::fabs(double(out.units[2].duration) - double(0.25f)) <= double(1e-5f));
    REQUIRE(out.units[2].step == +1);
    REQUIRE(out.units[3].step == -1);
}

void test_add_turn_up() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    auto out = figure_transforms::add_turn(f, /*addAt=*/1, /*down=*/false);
    REQUIRE(out.units.size() == 5u);
    REQUIRE(out.units[1].step == +1);
    REQUIRE(out.units[2].step == +1);
    REQUIRE(out.units[3].step == -2);
    REQUIRE(out.units[4].step == +1);
    for (int i = 1; i <= 4; ++i)
        REQUIRE(std::fabs(double(out.units[i].duration) - double(0.25f)) <= double(1e-5f));
}
namespace {

// --- randomized transforms (seeded) ---

float total_duration(const MelodicFigure& f) {
    float t = 0; for (auto& u : f.units) t += u.duration; return t;
}
}  // namespace

void test_vary_rhythm_preserves_length() {
    MelodicFigure f;
    f.units.push_back({2.0f, 0});
    f.units.push_back({2.0f, +1});
    f.units.push_back({2.0f, -1});
    Randomizer rng(0x1234u);
    auto out = figure_transforms::vary_rhythm(f, rng);
    REQUIRE(std::fabs(double(total_duration(out)) - double(total_duration(f))) <= double(1e-4f));
}

void test_vary_steps_changes_interior() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0x5678u);
    auto out = figure_transforms::vary_steps(f, rng, /*variations=*/1);
    REQUIRE(out.units.size() == f.units.size());
    REQUIRE(out.units.front().step == f.units.front().step);
    REQUIRE(out.units.back().step == f.units.back().step);
    bool anyDiff = false;
    for (int i = 1; i + 1 < (int)f.units.size(); ++i)
        if (out.units[i].step != f.units[i].step) { anyDiff = true; break; }
    REQUIRE(anyDiff);
}

void test_vary_composite() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xABCDu);
    auto out = figure_transforms::vary(f, rng, /*amount=*/1.0f);
    REQUIRE(std::fabs(double(total_duration(out)) - double(total_duration(f))) <= double(1e-4f));
}

void test_complexify_grows_unit_count() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xDEADu);
    auto out = figure_transforms::complexify(f, rng, /*amount=*/1.0f);
    REQUIRE((int)out.units.size() >= (int)f.units.size() + 1);
    REQUIRE(std::fabs(double(total_duration(out)) - double(total_duration(f))) <= double(1e-3f));
}

void test_embellish_marks_articulation() {
    MelodicFigure f;
    f.units.push_back({1.0f, 0});
    f.units.push_back({1.0f, +1});
    f.units.push_back({1.0f, -1});
    f.units.push_back({1.0f, 0});
    Randomizer rng(0xBEEFu);
    auto out = figure_transforms::embellish(f, rng, /*count=*/2);
    REQUIRE(out.units.size() == f.units.size());
    int marcatoCount = 0;
    for (auto& u : out.units) {
        if (std::holds_alternative<articulations::Marcato>(u.articulation)) ++marcatoCount;
    }
    REQUIRE(marcatoCount == 2);
}

void test_contour_roundtrip() {
    using mforce::Contour;
    REQUIRE(std::string(mforce::to_string(Contour::Arch)) == std::string("Arch"));
    REQUIRE(int(mforce::contour_from_string("Valley")) == int(Contour::Valley));
    REQUIRE(int(mforce::contour_from_string("Up")) == int(Contour::Up));
    mforce::Constraints c;
    c.contour = Contour::Level;
    REQUIRE(bool(c.contour) == true);
}

void test_pool_select() {
    using namespace mforce;
    nlohmann::json pool = nlohmann::json::parse(R"json(
    {
      "version":1,"source":"fixture","leapCapDegrees":7,
      "atoms":[
        {"units":[{"duration":0.5,"step":0},{"duration":0.5,"step":1},{"duration":0.5,"step":1}],
         "noteCount":3,"totalBeats":1.5,"contour":"Up","count":100},
        {"units":[{"duration":0.5,"step":0},{"duration":0.5,"step":1},{"duration":0.5,"step":4}],
         "noteCount":3,"totalBeats":1.5,"contour":"Up","count":1},
        {"units":[{"duration":0.5,"step":0},{"duration":0.5,"step":-1},{"duration":0.5,"step":-1}],
         "noteCount":3,"totalBeats":1.5,"contour":"Down","count":50}
      ]
    })json");
    PoolFigureBuilder b = pool_from_json(pool, 12345u);

    // (a) contour filter: Down never returns an ascending atom.
    for (int i = 0; i < 50; ++i) {
        Constraints c; c.contour = Contour::Down;
        MelodicFigure f = b.build(c);
        REQUIRE(f.net_step() < 0 == true);
    }
    // (b) weighting: among Up, count=100 (net +2) dominates count=1 (net +5).
    int heavy = 0, light = 0;
    for (int i = 0; i < 2000; ++i) {
        Constraints c; c.contour = Contour::Up;
        MelodicFigure f = b.build(c);
        if (f.net_step() == 2) ++heavy; else if (f.net_step() == 5) ++light;
    }
    REQUIRE(heavy > light * 5 == true);
    // (c) over-constrained request throws.
    bool threw = false;
    try { Constraints c; c.count = 2; c.contour = Contour::Arch; b.build(c); }
    catch (const std::runtime_error&) { threw = true; }
    REQUIRE(threw == true);
}

void test_build_approach_steps() {
    using D = mforce::DefaultPhraseStrategy;
    auto land = [](int start, const std::vector<int>& s){ int p=start; for(int x:s)p+=x; return p; };

    REQUIRE(D::nearest_degree_index(10, 0, 7) == 7);
    REQUIRE(D::nearest_degree_index(10, 4, 7) == 11);

    auto a = D::build_approach_steps(2, 0, 3, 7);   // close descent mi->do
    REQUIRE(int(a.size()) == 3);
    REQUIRE(land(2, a) == 0);
    REQUIRE(2 + a[0] + a[1] == 1);

    auto b = D::build_approach_steps(-2, 0, 3, 7);  // ascend from below
    REQUIRE(land(-2, b) == 0);
    REQUIRE(-2 + b[0] + b[1] == -1);

    auto c1 = D::build_approach_steps(5, 0, 3, 7);  // nearest tonic is 7 (la-ti-do ascent)
    REQUIRE(land(5, c1) == 7);
    auto c = D::build_approach_steps(3, 0, 2, 7);   // 2-note tail, dist 3 > tail -> leap then step
    REQUIRE(land(3, c) == 0);
    REQUIRE(c[0] == -2);
    REQUIRE(c[1] == -1);

    auto e = D::build_approach_steps(0, 0, 3, 7);   // on target: neighbor escape
    REQUIRE(land(0, e) == 0);
    REQUIRE(e[1] == -1);
    REQUIRE(e[2] == 1);
}

void test_settle_tail() {
    using D = mforce::DefaultPhraseStrategy;
    auto sum = [](const std::vector<float>& v){ float t=0; for(float x:v)t+=x; return t; };

    auto a = D::settle_tail({0.5f, 0.5f, 1.0f}, 1.0f);
    REQUIRE(int(a.size()) == 3);
    REQUIRE(std::fabs(double(a.back()) - double(1.0f)) <= double(1e-4));

    auto b = D::settle_tail({0.25f,0.25f,0.25f,0.25f}, 1.0f);
    REQUIRE(int(b.size()) == 1);
    REQUIRE(std::fabs(double(b.back()) - double(1.0f)) <= double(1e-4));

    auto c = D::settle_tail({0.5f,0.25f,0.25f}, 0.5f);
    REQUIRE(int(c.size()) == 2);
    REQUIRE(std::fabs(double(c[0]) - double(0.5f)) <= double(1e-4));
    REQUIRE(std::fabs(double(c[1]) - double(0.5f)) <= double(1e-4));

    REQUIRE(std::fabs(double(sum(a)) - double(2.0f)) <= double(1e-4));
    REQUIRE(std::fabs(double(sum(b)) - double(1.0f)) <= double(1e-4));
    REQUIRE(std::fabs(double(sum(c)) - double(1.0f)) <= double(1e-4));
}

#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/default_figure_strategy.h"
#include "mforce/compose/default_passage_strategy.h"
#include "mforce/compose/default_phrase_strategy.h"
#include "mforce/compose/register_builtin.h"
#include "mforce/compose/shape_strategies.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/music/music_json.h"
#include "mforce/shelved/register_shelved.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <memory>
#include <string>

using namespace mforce;


// What compose must leave alone in a request: its JSON, and the sizes of
// its motif list and its three realized maps (which the JSON omits).
struct TemplateFootprint {
    std::string text;
    size_t motifs, realizedMotifs, realizedRhythms, realizedContours;
    bool operator==(const TemplateFootprint&) const = default;
};

static TemplateFootprint footprint_of(const PieceTemplate& t) {
    const json j = json_of(t);
    return {j.dump(), t.motifs.size(), t.realizedMotifs.size(),
            t.realizedRhythms.size(), t.realizedContours.size()};
}

void run_compose_request_tests() {
    // A strategy that adds motifs: period_passage turns each generated
    // antecedent figure into a motif and flattens its periods into phrases.
    {
        std::ifstream f("scores/baselines/period_markov_test.json");
        const PieceTemplate request = template_from(json::parse(f));
        const TemplateFootprint before = footprint_of(request);
        const Composition c = compose_builtin(request);
        CHECK(footprint_of(request) == before);
        CHECK(request.motifs.empty());
        CHECK(c.plan.motifs.size() == 2);
        CHECK(request.parts[0].passages.at("Main").phrases.empty());
        CHECK(c.plan.parts[0].passages.at("Main").phrases.size() == 2);

        // Composing one request twice with one kit gives the same piece.
        ComposeKit kit;
        SettingsCodecs codecs;
        register_builtin(kit, codecs);
        register_shelved(kit, codecs);
        const json a = compose(request, kit).piece;
        const json b = compose(request, kit).piece;
        CHECK(a.dump() == b.dump());
    }

    // A generated motif: realize_motifs writes its notes into the plan.
    {
        json j = compose_probe_json();
        j["motifs"] = json::parse(R"([{"name": "g", "type": "figure"}])");
        j["parts"][0]["passages"]["Main"]["phrases"][0]["figures"] =
            json::parse(R"([{"source": "reference", "motifName": "g"}])");
        const PieceTemplate request = template_from(j);
        const TemplateFootprint before = footprint_of(request);
        const Composition c = compose_builtin(request);
        CHECK(footprint_of(request) == before);
        CHECK(request.realizedMotifs.empty());
        const auto it = c.plan.realizedMotifs.find("g");
        CHECK(it != c.plan.realizedMotifs.end() && !it->second.units.empty());
    }
}

void run_compose_kit_tests() {
    ComposeKit builtin;
    SettingsCodecs codecs;
    register_builtin(builtin, codecs);
    register_shelved(builtin, codecs);
    const PieceTemplate probe = template_from(compose_probe_json());
    CHECK(!compose_builtin(probe).piece.parts.empty());

    // An unregistered name, set in code, is an error of compose that names it.
    PieceTemplate t = probe;
    t.parts[0].passages["Main"].strategy.name = "no_such_passage";
    CHECK(compose_throws_naming(t, builtin, "no_such_passage"));
    t = probe;
    t.parts[0].passages["Main"].phrases[0].strategy.name = "no_such_phrase";
    CHECK(compose_throws_naming(t, builtin, "no_such_phrase"));
    t = probe;
    t.parts[1].passages["Main"].voicingSelector = "no_such_voicing";
    CHECK(compose_throws_naming(t, builtin, "no_such_voicing"));
    t = probe;
    t.parts[1].passages["Main"].voicingSelector = "smooth";
    t.parts[1].passages["Main"].voicingProfileSelector.name = "no_such_profile";
    CHECK(compose_throws_naming(t, builtin, "no_such_profile"));

    // A kit with the three defaults and the three shapes composes a melody
    // that names no strategy, and refuses one that names period_passage.
    ComposeKit small;
    small.strategies.add(std::make_unique<DefaultFigureStrategy>());
    small.strategies.add(std::make_unique<DefaultPhraseStrategy>());
    small.strategies.add(std::make_unique<DefaultPassageStrategy>());
    small.strategies.add(std::make_unique<ShapeCadentialApproachStrategy>());
    small.strategies.add(std::make_unique<ShapeSkippingStrategy>());
    small.strategies.add(std::make_unique<ShapeSteppingStrategy>());
    PieceTemplate melodyOnly = probe;
    melodyOnly.parts.erase(melodyOnly.parts.begin() + 1);
    const Composition c = compose(melodyOnly, small);
    CHECK(!c.piece.parts.empty() && !c.piece.parts[0].elementSequence.empty());
    melodyOnly.parts[0].passages["Main"].strategy.name = "period_passage";
    CHECK(compose_throws_naming(melodyOnly, small, "period_passage"));
}

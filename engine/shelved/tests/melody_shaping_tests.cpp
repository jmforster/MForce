#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/compose/anchor_selector.h"
#include "mforce/compose/compose.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/melody_profile.h"
#include "mforce/compose/passage_melody.h"
#include "mforce/compose/realization_strategy_json.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/core/randomizer.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/music_json.h"
#include "mforce/music/structure.h"
#include "mforce/shelved/connective_passage_strategy_json.h"
#include "mforce/shelved/elaborated_phrase_strategy_json.h"
#include "mforce/shelved/library_passage_strategy_json.h"
#include "mforce/shelved/pedal_buildup_strategy_json.h"
#include "mforce/shelved/period_passage_strategy_json.h"
#include "mforce/shelved/period_phrase_strategy_json.h"
#include "mforce/shelved/sentence_phrase_strategy_json.h"
#include "mforce/shelved/sequence_passage_strategy_json.h"
#include "mforce/shelved/two_figure_phrase_strategy_json.h"
#include "mforce/shelved/wandering_passage_strategy_json.h"
#include "mforce/voicing/voicing_json.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <vector>

using namespace mforce;


void run_walk1_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");
    MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");

    // --- Complexify: target note count, duration preserved, deterministic,
    //     JSON name round-trips.
    {
        MelodicFigure rep3;
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({2.0f, 0});
        auto a1 = figure_transforms::apply(rep3, TransformOp::Complexify, 4, 77u);
        CHECK(a1.note_count() == 4);
        CHECK(std::fabs(a1.total_duration() - 4.0f) < 1e-5f);
        auto a2 = figure_transforms::apply(rep3, TransformOp::Complexify, 4, 77u);
        bool same = a1.note_count() == a2.note_count();
        for (int k = 0; same && k < a1.note_count(); ++k)
            same = a1.units[k].duration == a2.units[k].duration
                && a1.units[k].step == a2.units[k].step;
        CHECK(same);
        json jt; to_json(jt, TransformOp::Complexify);
        CHECK(jt.get<std::string>() == std::string("complexify"));
        TransformOp back; from_json(jt, back);
        CHECK(back == TransformOp::Complexify);
    }

    // --- Derived motifs: chains resolve parent-first; missing parent throws
    //     by name; generationSeed makes a random op deterministic.
    {
        Motif a; a.name = "a"; a.userProvided = true;
        {
            MelodicFigure fa;
            fa.units.push_back({1.0f, 0});
            fa.units.push_back({1.0f, 1});
            a.content = fa;
        }
        Motif b; b.name = "b"; b.derivedFrom = "a";
        b.transform = TransformOp::Invert; b.content = MelodicFigure{};
        Motif d; d.name = "d"; d.derivedFrom = "b";
        d.transform = TransformOp::Reverse; d.content = MelodicFigure{};
        PieceTemplate tmpl; tmpl.motifs = {a, b, d};
        Randomizer rng(1u);
        realize_motifs(tmpl, rng);
        CHECK(tmpl.realizedMotifs.count("b") == 1);
        CHECK(tmpl.realizedMotifs.count("d") == 1);
        CHECK(tmpl.realizedMotifs.at("b").units[1].step == -1);

        PieceTemplate bad;
        Motif z; z.name = "z"; z.derivedFrom = "ghost";
        z.content = MelodicFigure{};
        bad.motifs = {z};
        bool threw = false;
        try { Randomizer r2(1u); realize_motifs(bad, r2); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("ghost") != std::string::npos;
        }
        CHECK(threw);

        Motif v; v.name = "v"; v.derivedFrom = "a";
        v.transform = TransformOp::VarySteps; v.transformParam = 1;
        v.generationSeed = 42u; v.content = MelodicFigure{};
        PieceTemplate t2; t2.motifs = {a, v};
        PieceTemplate t3; t3.motifs = {a, v};
        Randomizer r3(9u);    realize_motifs(t2, r3);
        Randomizer r4(1234u); realize_motifs(t3, r4);
        const auto& u2 = t2.realizedMotifs.at("v").units;
        const auto& u3 = t3.realizedMotifs.at("v").units;
        bool sameV = u2.size() == u3.size();
        for (size_t k = 0; sameV && k < u2.size(); ++k)
            sameV = u2[k].step == u3[k].step;
        CHECK(sameV);
    }

    // --- Anchor selector: R1/R2/R3 hold across seeds; dense connectors;
    //     the low-scoring start is rarely chosen; parallel pinning wins.
    {
        HarmonyTimeline tl;
        ChordProgression prog;
        ScaleChord c1; c1.degree = 0; c1.quality = &ChordDef::get("Major");
        ScaleChord g7; g7.degree = 4; g7.quality = &ChordDef::get("7");
        prog.add(c1, 4.0f);
        prog.add(g7, 2.0f);
        prog.add(c1, 2.0f);
        tl.set_segment(0.0f, 8.0f, prog, "test");

        MelodicFigure fA;   // 4 beats, net -1
        fA.units.push_back({1.0f, 0});
        fA.units.push_back({1.0f, -1});
        fA.units.push_back({2.0f, 0});
        MelodicFigure fB;   // 4 beats, notes at beats 4,5,6 (G7,G7,C)
        fB.units.push_back({1.0f, 0});
        fB.units.push_back({1.0f, 1});
        fB.units.push_back({2.0f, 0});
        std::vector<const MelodicFigure*> figs = {&fA, &fB};
        const Pitch reg = Pitch::from_note_number(64.0f);   // E5 register

        for (uint32_t s = 1; s <= 30; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "t";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           /*isPassageFinal*/ true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            CHECK(local.startingPitch.has_value());
            CHECK(local.connectors.size() == 2);
            CHECK(!local.connectors[0].has_value());
            CHECK(local.connectors[1].has_value());
            const int pc0 =
                ((int(local.startingPitch->note_number()) % 12) + 12) % 12;
            CHECK(pc0 == 0 || pc0 == 4 || pc0 == 7);        // R1
            const int g0 =
                scale_grid_index(local.startingPitch->note_number(), c);
            const int g1 = g0 + (-1) + local.connectors[1]->leadStep;
            const int last = g1 + 1;                        // fB net to last
            CHECK(((last % 7) + 7) % 7 == 0);               // R3 (tonic)
        }

        // Parallel pinning: a pinned start is taken verbatim.
        {
            Randomizer r(5u);
            PhraseTemplate local; local.name = "t2";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, true,
                           Pitch::from_note_number(67.0f), reg, r, prof, {}, 0.0f, false);
            CHECK(local.startingPitch
                  && int(local.startingPitch->note_number()) == 67);
        }

        // Weighting: single-figure phrase where anchors E/G keep both notes
        // chord tones and anchor C leaves its second (long, figure-final)
        // note off-chord — C should be a rare pick.
        {
            MelodicFigure fW;   // 4 beats: [0, -2], second note long+final
            fW.units.push_back({2.0f, 0});
            fW.units.push_back({2.0f, -2});
            std::vector<const MelodicFigure*> one = {&fW};
            int cPicks = 0;
            for (uint32_t s = 1; s <= 100; ++s) {
                Randomizer r(s);
                PhraseTemplate local; local.name = "w";
                select_anchors(local, one, tl, c, 0.0f, 4.0f, 1.0f,
                               /*isPassageFinal*/ false, std::nullopt, reg, r, prof, {}, 0.0f, false);
                const int pc0 =
                    ((int(local.startingPitch->note_number()) % 12) + 12) % 12;
                if (pc0 == 0) ++cPicks;
            }
            CHECK(cPicks < 15);
        }

        // Empty legal set throws with the phrase name: force R3 onto a
        // timeline whose final chord contains no tonic (B diminished-ish via
        // G7 only) — no chain can end on degree 1 as a chord tone.
        {
            HarmonyTimeline tl2;
            ChordProgression p2;
            p2.add(g7, 8.0f);
            tl2.set_segment(0.0f, 8.0f, p2, "test");
            Randomizer r(3u);
            PhraseTemplate local; local.name = "impossible";
            bool threw = false;
            try {
                select_anchors(local, figs, tl2, c, 0.0f, 4.0f, 1.0f,
                               true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            } catch (const std::exception& e) {
                threw = std::string(e.what()).find("impossible")
                        != std::string::npos;
            }
            CHECK(threw);
        }
    }

    // --- End to end: harmonic anchorMode through compose.
    {
        json tj = json::parse(R"({
          "keyName": "C", "scaleName": "Major", "bpm": 80, "masterSeed": 7,
          "sections": [{"name": "Main", "beats": 8,
            "chordProgression": [
              {"degree": 0, "beats": 4},
              {"degree": 4, "quality": "7", "beats": 2},
              {"degree": 0, "beats": 2}]}],
          "motifs": [
            {"name": "m1", "userProvided": true, "figure": {"units": [
              {"duration": 1.0, "step": 0}, {"duration": 1.0, "step": -1},
              {"duration": 2.0, "step": 0}]}},
            {"name": "m2", "userProvided": true, "figure": {"units": [
              {"duration": 1.0, "step": 0}, {"duration": 1.0, "step": 1},
              {"duration": 2.0, "step": 0}]}}
          ],
          "parts": [{"name": "melody", "role": "melody", "passages": {
            "Main": {"anchorMode": "harmonic", "melodyProfile": "nursery_v1",
                     "startingPitch": {"octave": 5, "pitch": "E"},
                     "phrases": [{
                        "name": "p1",
                        "startingPitch": {"octave": 5, "pitch": "E"},
                        "figures": [
                          {"source": "reference", "motifName": "m1"},
                          {"source": "reference", "motifName": "m2"}]}]}}}]
        })");
        PieceTemplate tmpl = template_from(tj);
        Piece piece = compose_builtin(tmpl).piece;
        CHECK(!piece.parts.empty());
        CHECK(piece.parts[0].elementSequence.size() == 6);
        // First event: R1 pitch class; last event: tonic (R3).
        const auto& els = piece.parts[0].elementSequence.elements;
        const int pcF = ((int(els.front().note().noteNumber) % 12) + 12) % 12;
        const int pcL = ((int(els.back().note().noteNumber) % 12) + 12) % 12;
        CHECK(pcF == 0 || pcF == 4 || pcF == 7);
        CHECK(pcL == 0);
    }
}

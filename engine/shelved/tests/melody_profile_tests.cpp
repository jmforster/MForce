#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/compose/anchor_selector.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/melody_profile.h"
#include "mforce/compose/melody_profile_json.h"
#include "mforce/compose/note_map.h"
#include "mforce/compose/passage_melody.h"
#include "mforce/compose/phrase_critic.h"
#include "mforce/compose/style_table.h"
#include "mforce/compose/templates.h"
#include "mforce/compose/templates_json.h"
#include "mforce/core/component.h"
#include "mforce/core/randomizer.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/music_json.h"
#include "mforce/music/structure.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


void run_walk3_tests() {
    using namespace mforce;
    // --- vary_steps reaches Mary's exact head_a from head ([0,-1,-1,+1] ->
    //     [0,0,+1,-1]) once the final step is perturbable (spec §4).
    {
        MelodicFigure head;
        head.units.push_back({1.0f, 0});
        head.units.push_back({1.0f, -1});
        head.units.push_back({1.0f, -1});
        head.units.push_back({1.0f, 1});
        bool reached = false;
        bool step0Touched = false;
        for (uint32_t s = 1; s <= 20000 && !reached; ++s) {
            auto f = figure_transforms::apply(head, TransformOp::VarySteps, 3, s);
            if (f.units[0].step != 0) step0Touched = true;
            if (f.units[1].step == 0 && f.units[2].step == 1
                && f.units[3].step == -1) reached = true;
        }
        CHECK(reached);
        CHECK(!step0Touched);
    }

    // --- MelodyProfile: strict parse of the shipped NRS v1 file.
    {
        MelodyProfile p = MelodyProfile::load_by_name("nursery_v1");
        CHECK(p.name == "nursery_v1");
        CHECK(p.tendencies.count("V7:7") == 1);
        CHECK(std::fabs(p.tendencies.at("V7:7").odds_per_100("step_down") - 96.0) < 1e-9);
        CHECK(std::fabs(p.tendencies.at("V7:7").odds_per_100("leap_up") - 4.0) < 1e-9); // "other"
        CHECK(std::fabs(p.nct.appoggiatura - 3.0) < 1e-9);
        CHECK(p.critic.departureBudget == 1);
        CHECK(p.search.phraseCandidates == 10 && p.search.topK == 3
              && p.search.passageCandidates == 10);
        // Missing key -> throws naming it.
        json j = json::parse(R"({"melody": {"tendencies": {}}})");
        bool threw = false;
        try { parse_melody_profile(j); }
        catch (const std::exception& e) {
            threw = std::string(e.what()).find("nct") != std::string::npos;
        }
        CHECK(threw);
        // StyleTable still parses a file carrying a "melody" block.
        StyleTable st = StyleTable::load_by_name("nursery_v1");
        CHECK(st.transitions.count("I") == 1);
        // Template round trip keeps melodyProfile.
        PassageTemplate pt; pt.melodyProfile = "nursery_v1";
        PieceTemplate holder; holder.parts.push_back({"melody", PartRole::Melody, ""});
        holder.parts[0].passages["Main"] = pt;
        const PassageTemplate back =
            template_from(json_of(holder)).parts[0].passages.at("Main");
        CHECK(back.melodyProfile == "nursery_v1");
    }

    // --- note_map: classification cases from Matt's annotations.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI;  cI.degree = 0; cI.quality = &ChordDef::get("Major");
        ScaleChord g7;  g7.degree = 4; g7.quality = &ChordDef::get("7");
        auto timeline = [&](std::vector<std::pair<ScaleChord, float>> segs) {
            HarmonyTimeline tl; ChordProgression pr; float tot = 0;
            for (auto& [sc, b] : segs) { pr.add(sc, b); tot += b; }
            tl.set_segment(0.0f, tot, pr, "t");
            return tl;
        };
        auto G = [&](float nn) { return scale_grid_index(nn, c); };
        // Quarter-note track from note numbers, starting at beat 0.
        auto track = [&](std::vector<float> nns) {
            std::vector<TrackNote> t; float b = 0;
            for (float nn : nns) { t.push_back({G(nn), b, 1.0f}); b += 1.0f; }
            return t;
        };
        auto kinds = [&](const NoteMapResult& r) {
            std::vector<std::string> k;
            for (auto& v : r.verdicts) k.push_back(v.kind);
            return k;
        };
        // Mary bar 7 over G7 then C: D D E D | C -> E is an upper NEIGHBOR (licensed).
        {
            HarmonyTimeline tl = timeline({{g7, 4.0f}, {cI, 4.0f}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({74, 74, 76, 74, 72}), 0, cl, c, 4.0f, prof, true);
            auto k = kinds(r);
            CHECK(std::find(k.begin(), k.end(), "neighbor") != k.end());
            CHECK(r.departures == 0);
        }
        // D D D E | C -> E leaves by leap: ESCAPE tone (departure).
        {
            HarmonyTimeline tl = timeline({{g7, 4.0f}, {cI, 4.0f}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({74, 74, 74, 76, 72}), 0, cl, c, 4.0f, prof, true);
            auto k = kinds(r);
            CHECK(std::find(k.begin(), k.end(), "escape") != k.end());
            CHECK(r.departures == 1);
        }
        // Passing tone: E D C over C -> D passing, odds 100, contributes 0.
        {
            HarmonyTimeline tl = timeline({{cI, 4.0f}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({76, 74, 72, 72}), 0, cl, c, 4.0f, prof, true);
            CHECK(kinds(r).size() >= 1 && kinds(r)[0] == "passing");
            CHECK(std::fabs(r.logScore) < 1e-9);
        }
        // Suspension (walk1 s101): F F F F over G7 | F E over C -> F held into C,
        // resolves down by step = suspension; F G instead = unresolved (departure).
        {
            HarmonyTimeline tl = timeline({{g7, 4.0f}, {cI, 4.0f}});
            ChordLookup cl(tl, c);
            auto good = evaluate_note_map(track({77, 77, 77, 77, 77, 76, 76, 76}), 0, cl, c, 4.0f, prof, true);
            auto kg = kinds(good);
            CHECK(std::find(kg.begin(), kg.end(), "suspension") != kg.end());
            CHECK(good.departures == 0);
            auto bad = evaluate_note_map(track({77, 77, 77, 77, 77, 79, 79, 79}), 0, cl, c, 4.0f, prof, true);
            auto kb = kinds(bad);
            CHECK(std::find(kb.begin(), kb.end(), "unresolvedSuspension") != kb.end());
            CHECK(bad.departures == 1);
        }
        // Tendency: 7th of V7 (F) stepping down vs up -> 96:4 = ln ratio ln(24).
        {
            HarmonyTimeline tl = timeline({{g7, 4.0f}});
            ChordLookup cl(tl, c);
            auto down = evaluate_note_map(track({77, 76}), 0, cl, c, 4.0f, prof, true);
            auto up   = evaluate_note_map(track({77, 79}), 0, cl, c, 4.0f, prof, true);
            // F->E: E is an NCT over G7 left with no next -> unscored; only F's
            // tendency differs between the two tracks.
            CHECK(std::fabs((down.logScore - up.logScore) - std::log(24.0)) < 1e-6);
            CHECK(up.departures == 1 && down.departures == 0);
        }
        // Leading tone B over G7 leaping down to F (walk2 s101): tendency
        // "other" (1/100) -> departure.
        {
            HarmonyTimeline tl = timeline({{g7, 4.0f}, {cI, 4.0f}});
            ChordLookup cl(tl, c);
            auto r = evaluate_note_map(track({71, 71, 71, 71, 77, 79}), 0, cl, c, 4.0f, prof, true);
            CHECK(r.departures >= 2);   // B leap-down + F appoggiatura over C
        }
        // Accented modifier: neighbor on the bar downbeat.
        {
            HarmonyTimeline tl = timeline({{cI, 4.0f}, {cI, 4.0f}});
            ChordLookup cl(tl, c);
            // beats 0..: C E G G | A G ... -> A at beat 4 (downbeat), step in, step out opposite
            auto r = evaluate_note_map(track({72, 76, 79, 79, 81, 79}), 0, cl, c, 4.0f, prof, true);
            bool found = false;
            for (auto& v : r.verdicts)
                if (v.kind == "neighbor" && std::fabs(v.odds - 30.0) < 1e-9) found = true;
            CHECK(found);
        }
        // Long NCT: E held 2 beats over G7 -> odds 1.
        {
            HarmonyTimeline tl = timeline({{g7, 4.0f}, {cI, 4.0f}});
            ChordLookup cl(tl, c);
            std::vector<TrackNote> t = {{G(74), 0, 1}, {G(76), 1, 2}, {G(74), 3, 1}, {G(72), 4, 4}};
            auto r = evaluate_note_map(t, 0, cl, c, 4.0f, prof, true);
            bool found = false;
            for (auto& v : r.verdicts) if (std::fabs(v.odds - 1.0) < 1e-9) found = true;
            CHECK(found);
        }
        // Scope: firstScoredNote excludes earlier fully-determined events.
        {
            HarmonyTimeline tl = timeline({{cI, 8.0f}});
            ChordLookup cl(tl, c);
            auto all  = evaluate_note_map(track({76, 74, 72, 74, 76, 74, 72}), 0, cl, c, 4.0f, prof, true);
            auto tail = evaluate_note_map(track({76, 74, 72, 74, 76, 74, 72}), 4, cl, c, 4.0f, prof, true);
            CHECK(all.verdicts.size() > tail.verdicts.size());
        }
    }

    // --- phrase critic + top-k.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI; cI.degree = 0; cI.quality = &ChordDef::get("Major");
        HarmonyTimeline tl; ChordProgression pr; pr.add(cI, 16.0f);
        tl.set_segment(0.0f, 16.0f, pr, "t");
        ChordLookup cl(tl, c);
        auto ph = [&](std::vector<int> grids) {
            std::vector<PhraseNote> v; float b = 0;
            for (int g : grids) { v.push_back({g, b, 1.0f, 0}); b += 1.0f; }
            return v;
        };
        // Range: span 4 costs nothing; span 10 costs 1.5 * 3.
        auto small = score_phrase_critic(ph({35, 36, 37, 38, 39}), {}, {}, false, 0.0f, cl, prof.critic);
        auto wide  = score_phrase_critic(ph({30, 32, 34, 36, 38, 40}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(small.range) < 1e-9);
        CHECK(std::fabs(wide.range - (-4.5)) < 1e-9);
        // Motion: 3 repeats of 4 moves (0.75) -> -4 * 0.25 = -1.
        auto rep = score_phrase_critic(ph({35, 35, 35, 35, 36}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(rep.motion - (-1.0)) < 1e-9);
        // Gap-fill: leap up 3 then step down -> +1.5.
        auto gap = score_phrase_critic(ph({35, 38, 37}), {}, {}, false, 0.0f, cl, prof.critic);
        CHECK(std::fabs(gap.gap - 1.5) < 1e-9);
        // pick_top_k: in-budget always outranks over-budget; k bounds the pool.
        std::vector<RankItem> items = {{false, 100.0}, {true, 1.0}, {true, 0.5}, {true, -50.0}};
        for (uint32_t s = 1; s <= 200; ++s) {
            Randomizer r(s);
            size_t p = pick_top_k(items, 2, r);
            CHECK(p == 1 || p == 2);
        }
        std::vector<RankItem> none = {{false, 3.0}, {false, 1.0}};
        Randomizer r1(1u);
        CHECK(pick_top_k(none, 1, r1) == 0);
    }

    // --- select_anchors: the 7th of V7 now resolves down ~24:1 (was ~400:1),
    //     measured end to end on a 2-figure phrase over G7 -> C.
    {
        Scale c = Scale::get("C", "Major");
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        ScaleChord cI; cI.degree = 0; cI.quality = &ChordDef::get("Major");
        ScaleChord g7; g7.degree = 4; g7.quality = &ChordDef::get("7");
        HarmonyTimeline tl; ChordProgression pr;
        pr.add(g7, 2.0f); pr.add(cI, 2.0f);
        tl.set_segment(0.0f, 4.0f, pr, "t");
        MelodicFigure a; a.units.push_back({2.0f, 0});          // over G7
        MelodicFigure b; b.units.push_back({2.0f, 0});          // over C
        std::vector<const MelodicFigure*> figs = {&a, &b};
        const Pitch reg = Pitch::from_note_number(77.0f);       // F5
        int down = 0, up = 0;
        for (uint32_t s = 1; s <= 400; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "sev";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, false,
                                      Pitch::from_note_number(77.0f), reg, r,
                                      prof, {}, 0.0f, false);
            (void)res;
            const int lead = local.connectors[1]->leadStep;
            if (lead == -1) ++down;
            if (lead > 0) ++up;
        }
        CHECK(down > up);     // the 96-row dominates; exact ratio is mixed with placement
        // Decision log names a departure when the chosen chain takes one:
        // the 7th of V7 stepping UP (4/100, below departureBelow 25).
        bool named = false;
        for (uint32_t s = 1; s <= 400 && !named; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "sev";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f, false,
                                      Pitch::from_note_number(77.0f), reg, r,
                                      prof, {}, 0.0f, true);
            if (local.connectors[1]->leadStep == 1) {
                CHECK(res.departures == 1);
                named = res.log.find("[departure] phrase 'sev' bar 1 beat 1 nn 77: "
                                     "tendency V7:7 step_up odds 4.0") != std::string::npos;
            }
        }
        CHECK(named);
    }

    // --- best-of-N end to end on the Mary walk template: deterministic per
    //     seed; legality (R3); variety across seeds.
    {
        std::ifstream f("scores/baselines/template_mary_walk.json");
        json base = json::parse(f);
        auto melody = [&](uint32_t seed) {
            json tj = base; tj["masterSeed"] = seed;
            PieceTemplate tmpl = template_from(tj);
            Piece piece = compose_builtin(tmpl).piece;
            std::vector<int> nns;
            for (auto& part : piece.parts) {
                if (part.name != "melody") continue;
                for (auto& el : part.elementSequence.elements)
                    nns.push_back(int(el.note().noteNumber));
            }
            return nns;
        };
        auto a = melody(100), b = melody(100);
        CHECK(a == b);                          // deterministic
        CHECK(!a.empty() && (a.back() % 12) == 0);   // R3
        std::set<std::vector<int>> distinct;
        for (uint32_t s = 100; s < 110; ++s) distinct.insert(melody(s));
        CHECK(distinct.size() >= 3);            // search didn't collapse to one tune
    }
}

#include "mforce/testing/check.h"
#include "mforce/compose/anchor_selector.h"
#include "mforce/compose/figure_transforms.h"
#include "mforce/compose/melody_profile.h"
#include "mforce/compose/note_map.h"
#include "mforce/compose/passage_melody.h"
#include "mforce/compose/templates.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

using namespace mforce;


void run_walk2_tests() {
    using namespace mforce;
    Scale c = Scale::get("C", "Major");

    // --- elaborate(): rung 1 always splits the long note evenly; totals
    //     preserved; nothing below a sixteenth; late-placement bias; fine
    //     splits rare and mostly dotted.
    {
        MelodicFigure rep3;
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({1.0f, 0});
        rep3.units.push_back({2.0f, 0});
        for (uint32_t s = 1; s <= 20; ++s) {
            Randomizer r(s);
            auto e4 = figure_transforms::elaborate(rep3, r, 4);
            CHECK(e4.note_count() == 4);
            bool allQ = true;
            for (auto& u : e4.units) if (u.duration != 1.0f) allQ = false;
            CHECK(allQ);                       // h -> q q, Mary's own move
            CHECK(e4.net_step() == 0);
        }
        int fineCount = 0, dottedCount = 0;
        double smallStartSum = 0.0;
        int smallCount = 0;
        for (uint32_t s = 1; s <= 400; ++s) {
            Randomizer r(s);
            auto e5 = figure_transforms::elaborate(rep3, r, 5);
            CHECK(std::fabs(e5.total_duration() - 4.0f) < 1e-4f);
            float minDur = 99.0f, minStart = 0.0f, start = 0.0f;
            bool hasDotted = false, hasFine = false;
            for (auto& u : e5.units) {
                CHECK(u.duration >= 0.25f - 1e-6f);
                if (u.duration < minDur) { minDur = u.duration; minStart = start; }
                if (u.duration == 0.75f) hasDotted = true;
                if (u.duration <= 0.25f + 1e-6f) hasFine = true;
                start += u.duration;
            }
            if (hasFine || hasDotted) { ++fineCount; if (hasDotted) ++dottedCount; }
            if (minDur < 1.0f) { smallStartSum += minStart; ++smallCount; }
        }
        // Depth policy: fine splits rare (~20% of the ONE post-rung split).
        CHECK(fineCount > 20 && fineCount < 160);
        // Dotted dominates fine (~80%).
        CHECK(dottedCount * 2 > fineCount);
        // Late-placement: mean start beat of the smallest unit is past the
        // bar's first half (uniform would be ~1.5 on a 4-beat figure).
        CHECK(smallCount > 0 && smallStartSum / smallCount > 1.6);
    }

    // --- Selector R4 (cadential register memory) + regression plumbing:
    //     final note must be a visited pitch when a visited set exists.
    {
        HarmonyTimeline tl;
        ChordProgression prog;
        ScaleChord c1; c1.degree = 0; c1.quality = &ChordDef::get("Major");
        prog.add(c1, 4.0f);
        tl.set_segment(0.0f, 4.0f, prog, "test");
        MelodicFigure whole;
        whole.units.push_back({4.0f, 0});
        std::vector<const MelodicFigure*> figs = {&whole};
        const Pitch reg = Pitch::from_note_number(64.0f);
        MelodyProfile prof = MelodyProfile::load_by_name("nursery_v1");
        // Prior track: C5 only (a whole note in the bar before).
        const std::vector<TrackNote> prior = {{scale_grid_index(60.0f, c), -4.0f, 4.0f}};
        for (uint32_t s = 1; s <= 20; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "r4";
            auto res = select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           true, std::nullopt, reg, r, prof, prior, 4.0f, false);
            CHECK(local.startingPitch
                  && int(local.startingPitch->note_number()) == 60);
            CHECK(res.notes.size() == 1);      // the chain's notes, returned
        }
        // Without any visited set, C5 and C6 both stay legal (guard).
        bool saw60 = false, saw72 = false;
        for (uint32_t s = 1; s <= 60; ++s) {
            Randomizer r(s);
            PhraseTemplate local; local.name = "r4b";
            select_anchors(local, figs, tl, c, 0.0f, 4.0f, 1.0f,
                           true, std::nullopt, reg, r, prof, {}, 0.0f, false);
            const int nn = int(local.startingPitch->note_number());
            CHECK(nn == 60 || nn == 72);
            if (nn == 60) saw60 = true;
            if (nn == 72) saw72 = true;
        }
        CHECK(saw60);
        (void)saw72;   // 72 may be rare; only legality is asserted
    }
}

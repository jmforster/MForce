#include "mforce/testing/check.h"
#include "mforce/compose/realization_strategy.h"
#include "mforce/compose/strategy_choice.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/structure.h"
#include <cstdio>

using namespace mforce;


// Realizes `strategy` over `harmony` in an 8-beat C major section starting at
// piece beat 10; each voiced chord is a C major triad at the length asked.
static ElementSequence realize_on(RealizationStrategy& strategy, const HarmonyTimeline& harmony,
                                  const StrategyChoice& choice) {
    const Section sec("Main", 8.0f, 120.0f, Meter::M_4_4, Scale::get("C", "Major"));
    RealizationContext ctx{harmony, sec, 10.0f, choice,
        [](const ScaleChord&, float, float beats) {
            return Chord::create("C", 4, "Major", beats);
        }};
    ElementSequence out;
    strategy.realize(ctx, out);
    return out;
}

void run_realization_strategy_tests() {
    std::printf("realization: block one event per chord; rhythm_pattern by the bar\n");
    const ChordDef* major = &ChordDef::get("Major");
    ChordProgression prog;
    prog.add(ScaleChord{0, 0, major}, 3.0f);
    prog.add(ScaleChord{3, 0, major}, 0.0f);   // no length: no event
    prog.add(ScaleChord{4, 0, major}, 2.0f);
    HarmonyTimeline full;
    full.set_segment(0.0f, 8.0f, prog, "test");
    BlockRealizationStrategy block;
    const ElementSequence b = realize_on(block, full, StrategyChoice{});
    CHECK(b.elements.size() == 2);
    if (b.elements.size() == 2) {
        CHECK(b.elements[0].startBeats == 10.0f && b.elements[0].chord().dur == 3.0f);
        CHECK(b.elements[1].startBeats == 13.0f && b.elements[1].chord().dur == 2.0f);
    }

    // Harmony only over the first bar; the pattern strikes 2, rests 1,
    // strikes 1, in both bars: the second bar's strikes find no chord.
    ChordProgression one;
    one.add(ScaleChord{0, 0, major}, 4.0f);
    HarmonyTimeline firstBar;
    firstBar.set_segment(0.0f, 4.0f, one, "test");
    RhythmPattern rp;
    rp.defaultPattern = {2.0f, -1.0f, 1.0f};
    RhythmPatternRealizationStrategy pattern;
    const ElementSequence r = realize_on(pattern, firstBar, StrategyChoice{"rhythm_pattern", rp});
    CHECK(r.elements.size() == 2);
    if (r.elements.size() == 2) {
        CHECK(r.elements[0].startBeats == 10.0f && r.elements[0].chord().dur == 2.0f);
        CHECK(r.elements[1].startBeats == 13.0f && r.elements[1].chord().dur == 1.0f);
    }
}

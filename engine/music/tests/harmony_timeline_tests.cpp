#include "mforce/testing/check.h"
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include <cstdio>

using namespace mforce;


void run_harmony_timeline_tests() {
    std::printf("the harmony timeline: the whole progression; the chord at a beat or the last\n");
    const ChordDef* major = &ChordDef::get("Major");
    ChordProgression first;
    first.add(ScaleChord{0, 0, major}, 2.0f);
    first.add(ScaleChord{4, 0, major}, 2.0f);
    ChordProgression second;
    second.add(ScaleChord{5, 0, major}, 4.0f);

    HarmonyTimeline one;
    one.set_segment(0.0f, 4.0f, first, "test");
    const ChordProgression whole1 = one.progression();
    CHECK(whole1.count() == 2 && whole1.chords.get(1).degree == 4
          && whole1.pulses.get(0) == 2.0f && whole1.pulses.get(1) == 2.0f);
    CHECK(one.chord_at_or_last(1.0f)->degree == 0);
    CHECK(one.chord_at_or_last(2.0f)->degree == 4);    // the boundary: the later chord
    CHECK(one.chord_at_or_last(10.0f)->degree == 4);   // past the end: the last chord
    CHECK(one.chord_at(10.0f) == nullptr);             // chord_at keeps its own rule

    HarmonyTimeline two = one;
    two.set_segment(4.0f, 8.0f, second, "test");
    const ChordProgression whole2 = two.progression();
    CHECK(whole2.count() == 3 && whole2.chords.get(2).degree == 5
          && whole2.pulses.get(2) == 4.0f);
    CHECK(two.chord_at_or_last(3.5f)->degree == 4);
    CHECK(two.chord_at_or_last(4.0f)->degree == 5);
    CHECK(two.chord_at_or_last(100.0f)->degree == 5);

    const HarmonyTimeline none;
    CHECK(none.chord_at_or_last(0.0f) == nullptr && none.progression().count() == 0);
}

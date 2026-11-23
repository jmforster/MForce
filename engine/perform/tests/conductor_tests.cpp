#include "mforce/testing/check.h"
#include "mforce/contract/performance.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/perform/conductor.h"
#include "mforce/perform/time_map.h"
#include <cstdio>
#include <iterator>
#include <vector>

using namespace mforce;


// The part the Conductor's data test performs at 120 bpm (half a second a
// beat): two plain notes, a staccato note, a mordent, a bend, a two-note slide
// run, a block chord and a hit, one beat apart.
static Part conductor_test_part(const Chord& chord) {
    Part part;
    part.add_note(0.0f, 60.0f, 0.5f, 1.0f);
    part.add_note(1.0f, 62.0f, 0.7f, 1.0f);
    part.add_note(2.0f, 64.0f, 0.6f, 1.0f, articulations::Staccato{});
    part.add_note(3.0f, 65.0f, 0.5f, 1.0f, articulations::Default{}, Mordent{1, 2, {}});
    part.add_note(4.0f, 67.0f, 0.5f, 1.0f, articulations::Bend{});
    part.add_note(5.0f, 69.0f, 0.4f, 1.0f);
    part.add_note(6.0f, 71.0f, 0.4f, 1.0f, articulations::Slide{0.2f});
    part.add_chord(7.0f, chord);
    part.add_hit(8.0f, 3, 0.7f, 0.5f);
    return part;
}

// The Conductor's performance of one part, as data, in delivery order. A
// plain note waits for the next note (it may anchor a slide run), so the
// chord, played at once, comes before the run. A one-section map at the same
// tempo performs the part the same.
void run_conductor_performance_tests() {
    std::printf("the Conductor's performance as data: count, order, seconds, velocity, curves, hits\n");
    const Chord chord = Chord::create("C", 4, "Major", 1.0f);
    Conductor conductor;
    const Performance perf = conductor.perform(conductor_test_part(chord), TimeMap(120.0f));
    const Performance viaSection = conductor.perform(
        conductor_test_part(chord), TimeMap(std::vector<Section>{Section("A", 9.0f, 120.0f, Meter::M_4_4, Scale::get("C", "Major"))}));
    bool sameAsSection = viaSection.notes.size() == perf.notes.size() && viaSection.hits.size() == perf.hits.size();
    for (size_t k = 0; sameAsSection && k < perf.notes.size(); ++k)
        sameAsSection = viaSection.notes[k].start == perf.notes[k].start && viaSection.notes[k].duration == perf.notes[k].duration;
    CHECK(sameAsSection);
    const int chordNotes = chord.pitch_count();
    CHECK(chordNotes == 3);
    CHECK(perf.notes.size() == size_t(8 + chordNotes));
    if (perf.notes.size() != size_t(8 + chordNotes) || chordNotes != 3) return;
    struct Want { float start, duration, noteNumber, velocity; bool curve; };
    const Want want[] = {
        {0.0f,   0.5f,   60.0f, 0.5f, false},   // plain
        {0.5f,   0.5f,   62.0f, 0.7f, false},   // plain
        {1.0f,   0.25f,  64.0f, 0.6f, false},   // staccato: half the length
        {1.5f,   0.125f, 65.0f, 0.5f, false},   // mordent: main, a quarter beat
        {1.625f, 0.125f, 67.0f, 0.5f, false},   //          neighbour above
        {1.75f,  0.25f,  65.0f, 0.5f, false},   //          main, the remainder
        {2.0f,   0.5f,   67.0f, 0.5f, true},    // bend
    };
    for (size_t k = 0; k < std::size(want); ++k) {
        const PerformedNote& n = perf.notes[k];
        CHECK(n.start == want[k].start && n.duration == want[k].duration);
        CHECK(n.noteNumber == want[k].noteNumber && n.velocity == want[k].velocity);
        CHECK(n.curve.has_value() == want[k].curve);
        CHECK(n.onset == 0 && !n.hold);
    }
    for (int i = 0; i < chordNotes; ++i) {                 // block chord: 10 ms spread
        const PerformedNote& n = perf.notes[std::size(want) + size_t(i)];
        CHECK_NEAR(n.start, 3.5f + 0.01f * float(i), 1e-6f);
        CHECK_NEAR(n.duration, 0.5f - 0.01f * float(i), 1e-6f);
        CHECK(n.noteNumber == chord.pitches[size_t(i)].note_number());
        CHECK(n.velocity == 1.0f && !n.curve);
    }
    const PerformedNote& run = perf.notes.back();       // the slide run: one note
    CHECK(run.start == 2.5f && run.duration == 1.0f && run.noteNumber == 69.0f);
    CHECK(run.velocity == 0.4f && run.curve.has_value());
    CHECK(perf.hits.size() == 1);
    if (perf.hits.size() == 1) {
        const PerformedHit& h = perf.hits[0];
        CHECK(h.start == 4.0f && h.duration == 0.25f && h.drumNumber == 3 && h.velocity == 0.7f);
    }
}

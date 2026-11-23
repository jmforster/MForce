#include "mforce/testing/check.h"
#include "music/tests/fixtures.h"
#include "mforce/music/basics.h"
#include "mforce/music/realize.h"
#include "mforce/music/structure.h"
#include <cstdio>

using namespace mforce;


void run_realize_events_tests() {
    std::printf("realize_events: a hand-built piece plays; a part with events is left alone\n");
    Piece piece = realize_probe_piece("melody");
    const float c4 = Pitch::from_name("C", 4).note_number();
    realize_events(piece);
    const auto& els = piece.parts[0].elementSequence.elements;
    CHECK(els.size() == 4);
    if (els.size() == 4) {
        const float starts[] = {0.0f, 1.0f, 2.0f, 4.0f};
        const float durs[]   = {1.0f, 1.0f, 2.0f, 1.0f};
        const float semis[]  = {0.0f, 2.0f, 5.0f, 4.0f};   // C D F E
        bool same = true;
        for (int k = 0; k < 4; ++k) {
            same = same && els[k].is_note() && els[k].startBeats == starts[k]
                && els[k].note().durationBeats == durs[k]
                && els[k].note().noteNumber == c4 + semis[k];
        }
        CHECK(same);
    }

    Piece held = realize_probe_piece("melody");
    held.parts[0].add_note(0.0f, 72.0f, 0.8f, 3.0f);
    realize_events(held);
    CHECK(held.parts[0].elementSequence.elements.size() == 1);
}

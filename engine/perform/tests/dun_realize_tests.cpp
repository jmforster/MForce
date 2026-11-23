#include "mforce/testing/check.h"
#include "mforce/music/dun_parser.h"
#include "mforce/music/structure.h"
#include "mforce/perform/time_map.h"
#include <cstdio>

using namespace mforce;


void run_dun_realize_tests() {
    std::printf("dun_to_piece plays; a truncated section pulls the next one earlier\n");
    const Piece four = dun_to_piece(parse_dun(
        "key:C scale:Major meter:4/4 bpm:120 start:0:4\nqn qu1 qu1 qd2\n"));
    const auto& e4 = four.parts[0].elementSequence.elements;
    CHECK(e4.size() == 4);
    CHECK(!e4.empty() && e4.back().startBeats + e4.back().note().durationBeats
                         == four.sections[0].beats);

    // Section 1 is composed 5 beats long (1 + 1 + 3) and plays 3; its third
    // note starts at 2 and is cut to end at 3, where section 2 starts.
    const Piece cut = dun_to_piece(parse_dun(
        "key:C scale:Major meter:4/4 bpm:120 start:0:4\n"
        "qn qu1 h.u1 truncate:2 //\nqn qu1\n"));
    CHECK(cut.sections.size() == 2);
    if (cut.sections.size() != 2) return;
    CHECK(cut.sections[0].beats == 5.0f && played_beats(cut.sections[0]) == 3.0f);
    CHECK(section_start_beat(cut.sections, 1) == 3.0f);
    CHECK(TimeMap(cut.sections).seconds_at(3.0f) == 1.5f);
    CHECK(TimeMap(cut.sections).bpm_at(3.0f) == 120.0f);
    const auto& ec = cut.parts[0].elementSequence.elements;
    CHECK(ec.size() == 5);
    if (ec.size() == 5) {
        CHECK(ec[2].startBeats == 2.0f && ec[2].note().durationBeats == 1.0f);
        CHECK(ec[3].startBeats == 3.0f && ec[4].startBeats == 4.0f);
    }
}

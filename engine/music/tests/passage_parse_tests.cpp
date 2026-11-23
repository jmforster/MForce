#include "mforce/testing/check.h"
#include "mforce/music/parse_util.h"

using namespace mforce;


void run_passage_parse_tests() {
    // No bars: every note starts its own phrase (compat: one-note phrases).
    auto plain = parse_passage("Cq Dq Eq", 4, 60.0f);
    CHECK(plain.size() == 3);
    for (auto& n : plain) CHECK(n.phraseStart);

    // Bars group; leading/doubled/trailing bars are no-ops.
    auto p = parse_passage("| Cq Dq | | Eq Fq Gq |", 4, 60.0f);
    CHECK(p.size() == 5);
    CHECK(p[0].phraseStart);  CHECK(!p[1].phraseStart);
    CHECK(p[2].phraseStart);  CHECK(!p[3].phraseStart);  CHECK(!p[4].phraseStart);

    // A rest ends the phrase: the note after it starts fresh (spec §2 v1
    // rule — silence breaks the breath; `|` is the explicit marker).
    // Grouping is active because the string contains a bar.
    auto r = parse_passage("| Cq Dq Rq Eq", 4, 60.0f);
    CHECK(r.size() == 4);
    CHECK(r[0].phraseStart);  CHECK(!r[1].phraseStart);
    CHECK(r[2].noteNumber == kRestNote);
    CHECK(r[3].phraseStart);
}

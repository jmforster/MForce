#include "mforce/testing/check.h"
#include "test_support.h"
#include "music/tests/fixtures.h"
#include "mforce/music/structure.h"
#include "mforce/perform/conductor.h"
#include "mforce/play/play.h"
#include <cstdio>
#include <string>

using namespace mforce;


void run_play_unrealized_warning_test() {
    std::printf("play: a part with passages and no events is named in a warning\n");
    Piece piece = realize_probe_piece("lead");
    RecordingInstrument rec;
    Conductor conductor;
    const PlayResult r = play(piece, Ensemble{{{"lead", &rec}}}, conductor);
    bool named = false;
    for (const auto& w : r.warnings) named = named || w.find("'lead'") != std::string::npos;
    CHECK(named);
    CHECK(rec.played.empty());
}

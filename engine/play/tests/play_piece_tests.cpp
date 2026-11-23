#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/perform/conductor.h"
#include "mforce/perform/time_map.h"
#include "mforce/play/play.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


// A section of `beats` at `bpm`, in C major 4/4.
static Section section_of(const char* name, float beats, float bpm) {
    return Section(name, beats, bpm, Meter::M_4_4, Scale::get("C", "Major"));
}

// A piece of one section per tempo in `sections`, and one part per entry of
// `parts`: its instrument type and the beats its one-beat notes start on.
static Piece piece_of(std::vector<Section> sections,
                      const std::vector<std::pair<std::string, std::vector<float>>>& parts) {
    Piece piece;
    for (auto& s : sections) piece.add_section(std::move(s));
    for (const auto& [type, beats] : parts) {
        Part part;
        part.instrumentType = type;
        for (float b : beats) part.add_note(b, 60.0f, 0.8f, 1.0f);
        piece.add_part(std::move(part));
    }
    return piece;
}

// play(piece): each part performed once over the whole piece at the map's
// seconds; the length the map's seconds plus the tail; a sum of two
// instruments past 0.98 scaled, one instrument never; instruments cleared.
void run_play_piece_tests() {
    std::printf("play(piece): each part once at the map's seconds, the length, the scale rule, cleared\n");
    {
        const Piece piece = piece_of({section_of("A", 4.0f, 120.0f), section_of("B", 4.0f, 60.0f)},
                                     {{"lead", {0.0f, 2.0f, 4.0f, 6.0f}}});
        RecordingInstrument rec;
        Conductor conductor;
        const PlayResult r = play(piece, Ensemble{{{"lead", &rec}}}, conductor);
        CHECK(rec.played.size() == 1 && rec.played[0].notes.size() == 4);
        const float want[] = {0.0f, 1.0f, 2.0f, 4.0f};
        bool at = rec.played.size() == 1 && rec.played[0].notes.size() == 4;
        for (size_t k = 0; at && k < 4; ++k)
            at = rec.played[0].notes[k].start == want[k] && r.stereo[size_t(int(want[k] * 48000.0f)) * 2] == 0.5f;
        CHECK(at);
        CHECK(std::count(r.stereo.begin(), r.stereo.end(), 0.5f) == 8);
        CHECK(r.sampleRate == 48000 && r.frames == int((TimeMap(piece.sections).seconds_at(8.0f) + 2.0f) * 48000.0f));
        CHECK(r.frames == 8 * 48000 && r.stereo.size() == size_t(r.frames) * 2);
        CHECK(rec.rendered().empty());
        CHECK(r.scale == 1.0f && r.peak == 0.5f);
    }
    {
        const Piece piece = piece_of({section_of("A", 4.0f, 120.0f)}, {{"a", {0.0f}}, {"b", {0.0f}}});
        RecordingInstrument a, b;
        a.level = b.level = 0.7f;
        Conductor conductor;
        const PlayResult r = play(piece, Ensemble{{{"a", &a}, {"b", &b}}}, conductor, 1.0f);
        CHECK(r.mixPeak == 0.7f + 0.7f && r.scale == 0.98f / r.mixPeak);
        CHECK_NEAR(r.peak, 0.98f, 1e-6f);
        CHECK(r.frames == 3 * 48000 && a.rendered().empty() && b.rendered().empty());
    }
    {
        const Piece piece = piece_of({section_of("A", 4.0f, 120.0f)}, {{"a", {0.0f}}, {"b", {}}});
        RecordingInstrument loud;
        loud.level = 1.5f;
        loud.peakGuard = false;
        Conductor conductor;
        const PlayResult r = play(piece, Ensemble{{{"a", &loud}, {"b", &loud}}}, conductor);
        CHECK(r.mixPeak == 1.5f && r.scale == 1.0f && r.peak == 1.5f);   // one instrument: never scaled
        CHECK(loud.played.size() == 1);                                   // a part with no events is not performed
    }
}

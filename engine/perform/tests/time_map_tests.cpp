#include "mforce/testing/check.h"
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/perform/time_map.h"
#include <bit>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace mforce;


// A section of `beats` at `bpm`, in C major 4/4.
static Section section_of(const char* name, float beats, float bpm) {
    return Section(name, beats, bpm, Meter::M_4_4, Scale::get("C", "Major"));
}

// One tempo maps beats to seconds bit for bit as b * 60 / bpm; across two
// tempi a beat belongs to the last section starting at or before it, the
// boundary beat to the later section, and a beat past the end to the last.
void run_time_map_tests() {
    std::printf("the time map: one tempo bit for bit, two tempi, the boundary, past the end\n");
    bool same = true;
    for (float bpm : {60.0f, 97.3f, 120.0f, 132.0f, 177.7f}) {
        const TimeMap one(std::vector<Section>{section_of("A", 64.0f, bpm)}), endless(bpm);
        for (int i = 0; i <= 2000; ++i) {
            const float b = float(i) * 0.0371f;               // to 74.2 beats: past the end too
            const auto want = std::bit_cast<std::uint32_t>(b * 60.0f / bpm);
            same = same && std::bit_cast<std::uint32_t>(one.seconds_at(b)) == want
                        && std::bit_cast<std::uint32_t>(endless.seconds_at(b)) == want
                        && one.bpm_at(b) == bpm && endless.bpm_at(b) == bpm;
        }
    }
    CHECK(same);
    const std::vector<Section> two{section_of("A", 8.0f, 120.0f), section_of("B", 6.0f, 60.0f)};
    const TimeMap time(two);
    CHECK(section_start_beat(two, 0) == 0.0f && section_start_beat(two, 1) == 8.0f);
    CHECK(time.seconds_at(4.0f) == 2.0f && time.bpm_at(4.0f) == 120.0f);
    CHECK(time.bpm_at(7.5f) == 120.0f);
    CHECK(time.seconds_at(8.0f) == 4.0f && time.bpm_at(8.0f) == 60.0f);      // the boundary: the later section
    CHECK(time.seconds_at(11.0f) == 7.0f);
    CHECK(time.seconds_at(20.0f) == 16.0f && time.bpm_at(20.0f) == 60.0f);   // past the end: the last section
}

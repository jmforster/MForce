#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "mforce/core/dsp/segment_walk.h"
#include "mforce/core/randomizer.h"
#include <cstdint>
#include <cstdio>

using namespace mforce;

namespace partials_twin {
inline void init(W& w, int baseLen, float evolve, Randomizer& rng) {
    w.cur = rng.valuePN();
    w.target = rng.valuePN();
    w.len = seg_len(baseLen, evolve, rng);
    w.pos = int(rng.range(0.0f, float(w.len)));
}
}  // namespace partials_twin

void run_segment_walk_twin_tests() {
    std::printf("segment walk: the primitive against the deleted walk\n");
    int bad = 0, rows = 0;
    for (int baseLen : {1, 7, 1600, 48000})
        for (float evolve : {0.0f, 0.1f, 0.3f, 1.0f})
            for (uint32_t seed = 1; seed <= 8; ++seed) {
                Randomizer ra(seed), rb(seed);
                partials_twin::W a;
                dsp::SegmentWalk b;
                partials_twin::init(a, baseLen, evolve, ra);
                b.init(baseLen, evolve, rb);
                if (!same_bits(a.cur, b.cur) || !same_bits(a.target, b.target) || a.pos != b.pos || a.len != b.len) ++bad;
                for (int k = 0; k < 500; ++k) {
                    float x = partials_twin::advance(a, baseLen, evolve, ra);
                    float y = b.advance(baseLen, evolve, rb);
                    ++rows;
                    if (!same_bits(x, y)) ++bad;
                }
            }
    CHECK(bad == 0);
    CHECK(rows == 4 * 4 * 8 * 500);
}

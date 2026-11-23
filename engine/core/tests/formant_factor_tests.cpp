#include "mforce/testing/check.h"
#include "test_support.h"
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

using namespace mforce;


// The consumer's factor `floor + weight × gain` against the gated form it
// replaces, `contains ? floor + weight × gain : floor`, over floor in
// {+0, −0, 0.2, 1}, weight in {0, 0.5, 1}, and (gain, contains) in {(0, in),
// (0.3, in), (1, in), (0, out)}: identical except the (−0, out-of-band) rows,
// one per weight, which differ in the sign of the zero only — the one
// departure, counted here.
void run_formant_factor_twin_tests() {
    std::printf("formant factor twin: floor + weight x gain against the gated form\n");
    std::vector<float> floors = {0.0f, -0.0f, 0.2f, 1.0f};
    std::vector<float> weights = {0.0f, 0.5f, 1.0f};
    std::vector<std::pair<float, bool>> gains = {{0.0f, true}, {0.3f, true}, {1.0f, true}, {0.0f, false}};
    int rows = 0, differ = 0, signOnly = 0, negZeroOut = 0;
    for (float fl : floors)
        for (float wt : weights)
            for (const auto& [g, in] : gains) {
                ++rows;
                const float old = in ? fl + wt * g : fl;
                const float now = fl + wt * g;
                if (!same_bits(old, now)) {
                    ++differ;
                    if (same_value(old, now)) ++signOnly;
                    if (std::signbit(fl) && fl == 0.0f && !in) ++negZeroOut;
                }
            }
    CHECK(rows == 48);
    CHECK(differ == signOnly);                                              // never a value, only a sign
    CHECK(differ == 3 && negZeroOut == 3);                                  // −0 floor, out of band, each weight
}

#pragma once
// Helpers the figure tests of this program share.
#include "mforce/testing/check.h"
#include "mforce/music/figures.h"
#include <cmath>
#include <iostream>

using namespace mforce;

namespace {

inline int expect_figures_equal(const MelodicFigure& a, const MelodicFigure& b, const char* tag) {
    if (a.units.size() != b.units.size()) {
        std::cerr << "  FAIL: " << tag << " unit count " << a.units.size()
                  << " vs " << b.units.size() << "\n";
        return 1;
    }
    for (size_t i = 0; i < a.units.size(); ++i) {
        if (a.units[i].step != b.units[i].step) {
            std::cerr << "  FAIL: " << tag << " step[" << i << "] " << a.units[i].step
                      << " vs " << b.units[i].step << "\n";
            return 1;
        }
        if (std::fabs(a.units[i].duration - b.units[i].duration) > 1e-5f) {
            std::cerr << "  FAIL: " << tag << " dur[" << i << "] " << a.units[i].duration
                      << " vs " << b.units[i].duration << "\n";
            return 1;
        }
    }
    return 0;
}
}  // namespace

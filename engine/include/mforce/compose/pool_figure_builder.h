#pragma once
#include "mforce/music/figures.h"
#include "mforce/compose/figure_constraints.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace mforce {

// One pre-generated, pre-tagged atom from the Markov pool. `count` is the
// generative weight (how often the model produced it) and drives selection.
struct PoolAtom {
    MelodicFigure figure;
    int     noteCount;
    float   totalBeats;
    Contour contour;
    long    count;
};

// Selects a constraint-matching atom from a tagged Markov pool, weighted by the
// atom's generative count. Sibling to RandomFigureBuilder (same build() shape);
// it SELECTS rather than procedurally generates. Throws when nothing matches —
// that throw is how a caller learns to relax a constraint.
class PoolFigureBuilder {
public:
    PoolFigureBuilder(std::vector<PoolAtom> atoms, uint32_t seed)
        : atoms_(std::move(atoms)), rng_(seed) {}

    // The pool in the JSON file at `path`; throws when it cannot be opened
    // or read. Defined in pool_figure_builder_json.cpp; pool_from_json
    // (pool_figure_builder_json.h) reads one from a JSON value.
    static PoolFigureBuilder load(const std::string& path, uint32_t seed);

    // Filter by whichever of count / length / contour are set (absent = wildcard),
    // then weighted-select by count. Throws if no atom matches.
    // The seeded overload makes selection reproducible from the caller's seed.
    MelodicFigure build(const Constraints& c) { return select_(c, rng_); }

    MelodicFigure build(const Constraints& c, uint32_t seed) {
        Randomizer r(seed);
        return select_(c, r);
    }

private:
    MelodicFigure select_(const Constraints& c, Randomizer& rng) {
        std::vector<const PoolAtom*> matches;
        long total = 0;
        for (const auto& a : atoms_) {
            if (c.count   && a.noteCount != *c.count) continue;
            if (c.length  && std::fabs(a.totalBeats - *c.length) > 0.01f) continue;
            if (c.contour && a.contour != *c.contour) continue;
            matches.push_back(&a);
            total += a.count;
        }
        if (matches.empty())
            throw std::runtime_error("PoolFigureBuilder::build: no atom matches constraints");

        long r = long(rng.int_range(0, int(total - 1)));
        long acc = 0;
        for (const auto* a : matches) {
            acc += a->count;
            if (r < acc) return a->figure;
        }
        return matches.back()->figure;
    }

    std::vector<PoolAtom> atoms_;
    Randomizer rng_;
};

} // namespace mforce

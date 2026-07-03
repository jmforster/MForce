#pragma once
#include "mforce/music/figures.h"
#include "mforce/music/figure_constraints.h"
#include "mforce/core/randomizer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>
#include <stdexcept>

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
    PoolFigureBuilder(const nlohmann::json& poolJson, uint32_t seed) : rng_(seed) {
        for (const auto& aj : poolJson.at("atoms")) {
            PoolAtom a;
            for (const auto& uj : aj.at("units")) {
                a.figure.units.push_back(FigureUnit{
                    uj.at("duration").get<float>(), uj.at("step").get<int>()});
            }
            a.noteCount  = aj.at("noteCount").get<int>();
            a.totalBeats = aj.at("totalBeats").get<float>();
            a.contour    = contour_from_string(aj.at("contour").get<std::string>());
            a.count      = aj.at("count").get<long>();
            atoms_.push_back(std::move(a));
        }
    }

    static PoolFigureBuilder load(const std::string& path, uint32_t seed) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("PoolFigureBuilder::load: cannot open " + path);
        nlohmann::json j; f >> j;
        return PoolFigureBuilder(j, seed);
    }

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

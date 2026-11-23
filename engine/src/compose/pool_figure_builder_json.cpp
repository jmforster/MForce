// The Markov atom pool's file form: JSON (lib/figures/markov_pool.json).
#include "mforce/compose/pool_figure_builder_json.h"
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

PoolFigureBuilder pool_from_json(const nlohmann::json& poolJson, uint32_t seed) {
    std::vector<PoolAtom> atoms;
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
        atoms.push_back(std::move(a));
    }
    return PoolFigureBuilder(std::move(atoms), seed);
}

PoolFigureBuilder PoolFigureBuilder::load(const std::string& path, uint32_t seed) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("PoolFigureBuilder::load: cannot open " + path);
    nlohmann::json j; f >> j;
    return pool_from_json(j, seed);
}

} // namespace mforce

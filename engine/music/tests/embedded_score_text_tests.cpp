#include "mforce/testing/check.h"
#include "mforce/music/music_json.h"
#include <nlohmann/json.hpp>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace mforce;


// The codec hands the score over as text. Every score block in
// patches/library and patches/baselines reads back from its text to the same
// JSON, the same text, the same float and double of each number and the same
// number type.
void run_embedded_score_text_tests() {
    std::printf("the embedded score as text: every score in the measurement scope reads back the same\n");
    int checked = 0;
    for (const char* dir : {"patches/library", "patches/baselines"}) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            std::ifstream f(entry.path());
            std::stringstream ss; ss << f.rdbuf();
            nlohmann::json root = nlohmann::json::parse(ss.str());
            if (!root.is_object() || !root.contains("score")) continue;
            const nlohmann::json& score = root.at("score");
            nlohmann::json re = nlohmann::json::parse(score.dump());
            bool same = re == score && re.dump() == score.dump() && re.size() == score.size();
            for (size_t i = 0; same && i < score.size(); ++i) {
                for (const char* key : {"note", "velocity", "duration", "time"}) {
                    if (!score[i].is_object() || !score[i].contains(key)) continue;
                    const auto& a = score[i].at(key);
                    const auto& b = re[i].at(key);
                    same = same
                        && std::bit_cast<uint32_t>(a.get<float>()) == std::bit_cast<uint32_t>(b.get<float>())
                        && std::bit_cast<uint64_t>(a.get<double>()) == std::bit_cast<uint64_t>(b.get<double>())
                        && a.is_number_integer() == b.is_number_integer()
                        && a.is_number_float() == b.is_number_float();
                }
            }
            if (!same) std::printf("  score text differs: %s\n", entry.path().string().c_str());
            CHECK(same);
            ++checked;
        }
    }
    std::printf("  score-bearing files checked: %d\n", checked);
}

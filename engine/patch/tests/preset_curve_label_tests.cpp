#include "mforce/testing/check.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/source/envelope/envelope.h"
#include <nlohmann/json.hpp>
#include <cctype>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

using namespace mforce;


// The preset form's curve keys: a label of the stage type, in any case, reads
// as its index; Hold, any other word and an index outside 0 to 3 are refused.
void run_preset_curve_label_tests() {
    using J = nlohmann::json;
    std::printf("the preset form: a curve key as a label or as its index\n");
    auto columns = [](const J& p) {
        std::vector<std::vector<float>> out;
        try {
            auto env = envelope_from_preset_json(p);
            for (const auto& d : env->array_descriptors())
                if (d.groupName && std::string_view(d.groupName) == "stages") out.push_back(env->get_array(d.name));
        } catch (const std::exception& e) { std::printf("  %s: %s\n", p.dump().c_str(), e.what()); }
        return out;
    };
    struct Preset { const char* name; std::vector<std::string> keys; };
    const Preset presets[] = {{"ar", {"attack", "release"}}, {"as", {"attack"}}, {"asr", {"attack", "release"}},
                              {"ads", {"attack", "decay"}}, {"adr", {"attack", "decay", "release"}},
                              {"adsr", {"attack", "decay", "release"}}};
    for (const auto& pr : presets)
        for (const auto& key : pr.keys)
            for (int i = 0; i < 4; ++i) {
                const std::string label = Envelope::kStageTypeLabels[i];
                std::string lower = label;
                for (auto& c : lower) c = char(std::tolower(static_cast<unsigned char>(c)));
                auto with = [&](const J& v) { return J{{"preset", pr.name}, {key + "Curve", v}, {key + "Power", 0.5}}; };
                const auto byIndex = columns(with(i));
                CHECK(byIndex.size() == 8);
                CHECK(columns(with(label)) == byIndex);
                CHECK(columns(with(lower)) == byIndex);
            }
    CHECK(columns(J{{"preset", "ar"}, {"attackCurve", "Expo"}}) != columns(J{{"preset", "ar"}, {"attackCurve", "Linear"}}));
    auto refusal = [](const J& v) {
        try { (void)envelope_from_preset_json(J{{"preset", "ar"}, {"attackCurve", v}}); }
        catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    for (const char* word : {"Hold", "bogus"})
        CHECK(refusal(word) == "envelope preset: attackCurve is \"" + std::string(word)
                               + "\"; a curve is Linear, Expo, InverseExpo or Sine, or its index 0 to 3");
    CHECK(refusal(4).find("attackCurve is 4; a curve is 0 (Linear)") != std::string::npos);
    CHECK(refusal(-1).find("attackCurve is -1; a curve is 0 (Linear)") != std::string::npos);
}

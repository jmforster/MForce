#include "mforce/testing/check.h"
#include "core/tests/fixtures.h"
#include "mforce/core/descriptors.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;


void run_string_setting_tests() {
    using namespace three_kinds;
    std::printf("the string setting: by name, structural, untouched by the float door\n");
    StringProbe p;
    const auto sd = p.setting_descriptors();
    CHECK(sd.size() == 2);
    CHECK(std::string(sd[0].name) == "gain" && sd[0].type == SettingType::Float && !sd[0].structural);
    CHECK(std::string(sd[1].name) == "label" && sd[1].type == SettingType::String && sd[1].structural);
    CHECK(sd[1].default_value == 0.0f && sd[1].min_value == 0.0f && sd[1].max_value == 0.0f && sd[1].enum_labels == nullptr);
    CHECK(p.get_string("label") == "x");
    p.set_string("label", "y");
    CHECK(p.label.value == "y" && p.get_string("label") == "y");
    CHECK((p.changed == std::vector<std::string>{"label"}));
    p.set_setting("label", 1.0f);                                  // the float door finds no string
    CHECK(p.label.value == "y" && p.get_setting("label") == 0.0f && p.changed.size() == 1);
    CHECK(!p.structural_setting("label"));                         // the float door's question; the descriptor says structural
    CHECK(p.get_string("nosuch").empty() && p.get_string("gain").empty());
    p.set_string("nosuch", "z");
    p.set_string("gain", "z");
    CHECK(p.changed.size() == 1 && p.get_setting("gain") == 1.0f);
    p.set_setting("gain", 0.5f);
    CHECK((p.changed == std::vector<std::string>{"label", "gain"}) && p.label.value == "y");
}

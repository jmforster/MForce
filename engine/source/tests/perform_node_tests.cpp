#include "mforce/testing/check.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/modulator/perform_node.h"
#include "mforce/source/register_all_sources.h"
#include <cstdio>
#include <memory>
#include <string>

using namespace mforce;


// The registered type: a Generator with one enum setting, the six labels.
void run_perform_node_type_tests() {
    std::printf("PerformNode: the registered declaration\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    auto n = reg.has("PerformNode") ? reg.create("PerformNode") : nullptr;
    CHECK(std::dynamic_pointer_cast<Generator>(n) != nullptr);
    if (!n) return;
    const auto settings = n->setting_descriptors();
    CHECK(settings.size() == 1);
    if (settings.size() == 1) {
        const auto& d = settings[0];
        CHECK(std::string(d.name) == "field" && d.type == SettingType::Int && d.default_value == 0.0f
              && d.min_value == 0.0f && d.max_value == 5.0f && d.enum_labels != nullptr);
        const char* want[] = {"frequency", "velocity", "wheel", "pressure", "duration", "onset"};
        bool same = d.enum_labels != nullptr;
        for (int k = 0; same && k < 6; ++k) same = d.enum_labels[k] && std::string(d.enum_labels[k]) == want[k];
        CHECK(same && d.enum_labels[6] == nullptr);
    }
    CHECK(n->param_descriptors().empty() && n->input_descriptors().empty() && n->array_descriptors().empty());
    PerformNode p;
    CHECK(p.compute() == 0.0f);
}

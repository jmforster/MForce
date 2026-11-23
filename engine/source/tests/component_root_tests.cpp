#include "mforce/testing/check.h"
#include "source/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/register_all_sources.h"
#include <cstdio>
#include <memory>
#include <string>

using namespace mforce;


// The audit: every registered type is created once; exactly the named parts
// are not signals, every other type is. The twenty-three parts are the four
// Partials types and the expand rule, the three formant types, the
// thirteen evolutions and the two output parts.
void run_component_root_tests() {
    std::printf("the root: parts and signals\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    CHECK(kPartTypes.size() == 23);
    int seen = 0, parts = 0;
    for (const auto& entry : reg.registered_types()) {
        const std::string& name = entry.name;
        auto c = reg.create(name);
        const bool isPart = std::dynamic_pointer_cast<ValueSource>(c) == nullptr;
        if (isPart != (kPartTypes.count(name) == 1))
            std::printf("  %s: %s\n", name.c_str(), isPart ? "a part the list lacks" : "a signal the list names");
        CHECK(isPart == (kPartTypes.count(name) == 1));
        ++seen; parts += isPart;
    }
    CHECK(parts == 23);
    CHECK(seen == int(reg.registered_types().size()));
}

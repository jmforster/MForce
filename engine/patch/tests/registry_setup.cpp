// The type-declaration tests check every registered type, the test probes
// included: the probes the render tests register (ReseedProbe, EnumTableProbe
// and the four of three_kinds). Those tests run in another program, so this
// one registers the same probes, in the same order and the same way, before
// the type-declaration tests read the table.
#include "mforce/testing/check.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/register_all_sources.h"
#include "core/tests/fixtures.h"
#include "patch/tests/fixtures.h"
#include <memory>

using namespace mforce;

void register_earlier_tests_probes() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    if (!reg.has("ReseedProbe"))
        reg.register_type("ReseedProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<ReseedProbe>(); });
    if (!reg.has("EnumTableProbe"))
        reg.register_type("EnumTableProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<enum_column::EnumTableProbe>(); });
    three_kinds::register_probes();
}

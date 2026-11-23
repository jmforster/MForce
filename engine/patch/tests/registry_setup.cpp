// Registers every source and the probe types the test fixtures define
// (ReseedProbe and EnumTableProbe from core/tests/fixtures.h, the four of
// three_kinds). The type-declaration tests listed after it check every
// registered type, so with the probes registered first they check these
// test-made types the same way as the shipped sources.
#include "mforce/testing/check.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/register_all_sources.h"
#include "core/tests/fixtures.h"
#include "patch/tests/fixtures.h"
#include <memory>

using namespace mforce;

void register_test_probes() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    if (!reg.has("ReseedProbe"))
        reg.register_type("ReseedProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<ReseedProbe>(); });
    if (!reg.has("EnumTableProbe"))
        reg.register_type("EnumTableProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<enum_column::EnumTableProbe>(); });
    three_kinds::register_probes();
}

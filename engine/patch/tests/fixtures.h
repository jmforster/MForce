#pragma once
// Helpers the patch tests share with the test programs of the modules above it.
#include "core/tests/fixtures.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/source/register_all_sources.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

using namespace mforce;

namespace patch_door {
inline mforce::Patch read_text(const std::string& text) {
    mforce::Patch p;
    from_json(nlohmann::json::parse(text), p);
    return p;
}
}  // namespace patch_door
namespace three_kinds {
inline void register_probes() {
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    const TypeTag tag{.family = Family::Utility, .shelf = Shelf::Experimental};
    if (!reg.has("StringProbe")) reg.register_type("StringProbe", tag, [](auto) { return std::make_shared<StringProbe>(); });
    if (!reg.has("ListProbe"))   reg.register_type("ListProbe", tag, [](auto) { return std::make_shared<ListProbe>(); });
    if (!reg.has("CheckProbe"))
        reg.register_type("CheckProbe", tag, [](std::optional<uint32_t> seed) {
            auto p = std::make_shared<CheckProbe>();
            p->seeded = seed.has_value();
            return p;
        });
    if (!reg.has("RefusingProbe")) reg.register_type("RefusingProbe", tag, [](auto) { return std::make_shared<RefusingProbe>(); });
}
}  // namespace three_kinds

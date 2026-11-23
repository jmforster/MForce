#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "source/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/source/filter/shaper_source.h"
#include "mforce/source/modulator/name_gate.h"
#include "mforce/source/register_all_sources.h"
#include <nlohmann/json.hpp>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace node_twins {
static std::shared_ptr<ValueSource> create(const char* type, std::optional<uint32_t> seed = std::nullopt) {
    register_all_sources();
    return std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type, seed));
}
}  // namespace node_twins

void run_name_gate_name_tests() {
    std::printf("NameGate: the name is a declared string, resolved against the onsets at build\n");
    NameGate ng;
    const auto sd = ng.setting_descriptors();
    CHECK(sd.size() == 1);
    CHECK(sd.size() == 1 && std::string(sd[0].name) == "name" && sd[0].type == SettingType::String && sd[0].structural);
    CHECK(ng.get_string("name").empty());
    ng.set_string("name", "tongue");
    CHECK(ng.name.value == "tongue" && ng.get_string("name") == "tongue");
    CHECK(ng.targetId == -1.0f);                                  // a set resolves nothing; the builder does
    const auto gate_doc = [](const nlohmann::json& name) {
        auto doc = nlohmann::json::parse(enum_column::one_node("NameGate", {{"name", name}}));
        doc["instrument"]["onsets"] = {"tongue", "pluck"};
        return doc.dump();
    };
    for (const auto& [name, id] : {std::pair<const char*, float>{"tongue", 1.0f}, {"pluck", 2.0f}, {"slur", -1.0f}}) {
        LoadedInstrument ip;
        bool loaded = true;
        try { ip = load_inst(gate_doc(name)); }
        catch (const std::exception& e) { std::printf("  %s: %s\n", name, e.what()); loaded = false; }
        CHECK(loaded);
        if (!loaded) continue;
        auto* g = dynamic_cast<NameGate*>(ip.instrument->pool().voice(0).nodes().at("n").get());
        CHECK(g && g->get_string("name") == name && g->targetId == id);   // 1-based; outside the vocabulary -1
    }
    CHECK(three_kinds::refused_naming(gate_doc(3), false, {"node 'n' (NameGate): 'name' takes a string, not 3"}));
    // A plain sound has no vocabulary: the gate loads and never matches.
    CHECK(three_kinds::loads(three_kinds::plain(gate_doc("tongue")), true));
}

void run_shaper_validate_tests() {
    std::printf("the Shaper checks itself: validate() on its arrays, warnings() under hysteresis\n");
    using Arrays = std::vector<std::pair<const char*, std::vector<float>>>;
    const auto check = [](const Arrays& arrays) {
        ShaperSource s;
        for (const auto& [n, v] : arrays) s.set_array(n, v);
        return s.validate();
    };
    const std::vector<float> two = {-1.0f, -1.0f, 1.0f, 1.0f}, three = {-1.0f, -1.0f, 0.0f, 0.5f, 1.0f, 1.0f};
    CHECK(check({}).empty());
    CHECK(check({{"values", two}, {"values2", three}}) == "values2 length must match values");
    CHECK(check({{"values", three}, {"values2", three}}).empty());
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {2.0f, 1.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 1.0f, 1.0f}}}) == "segs2 segment types must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 2.0f}}, {"segs2", {0.0f, 0.0f, 3.0f, 2.0f}}}).empty());   // Expo and InverseExpo
    CHECK(check({{"segs", {0.0f, 0.0f, 3.0f, 2.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}).empty());
    CHECK(check({{"segs2", {0.0f, 0.0f, 2.0f, 3.0f}}}).empty());                                       // no segs beside segs2
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}}).empty());
    // A list in which no segment is overridden is held empty, and the check
    // reads the decoded lists, which keep their length: an all-default list
    // is compared as the file's.
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}) == "segs2 segment types must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}) == "segs2 length must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}) == "segs2 segment types must match segs");
    CHECK(check({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}).empty());
    // values2 beside no values: four floats match the default curve the node
    // holds and load; any other length is refused.
    CHECK(check({{"values2", two}}).empty());
    CHECK(check({{"values2", three}}) == "values2 length must match values");
    // Through a load: the node, its type and the words.
    nlohmann::json bad = {{"values", two}, {"values2", three}};
    CHECK(three_kinds::refused_naming(enum_column::one_node("Shaper", bad), false,
                                      {"node 'n' (Shaper): values2 length must match values"}));
    nlohmann::json types = {{"segs", {0.0f, 0.0f, 4.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}};
    CHECK(three_kinds::refused_naming(enum_column::one_node("Shaper", types), true,
                                      {"node 'n' (Shaper): segs2 segment types must match segs"}));
    nlohmann::json pair = {{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 3.0f, 1.0f}}};
    CHECK(three_kinds::loads(enum_column::one_node("Shaper", pair), false));
    for (const bool file : {false, true}) {
        const auto doc = [file](const nlohmann::json& params) {
            const std::string d = enum_column::one_node("Shaper", params);
            return file ? three_kinds::plain(d) : d;
        };
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 length must match segs"}));
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 segment types must match segs"}));
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 2.0f, 1.0f, 0.0f, 0.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 length must match segs"}));
        CHECK(three_kinds::refused_naming(doc({{"segs", {0.0f, 0.0f, 2.0f, 1.0f}}, {"segs2", {0.0f, 0.0f, 0.0f, 0.0f}}}),
                                          file, {"node 'n' (Shaper): segs2 segment types must match segs"}));
        CHECK(three_kinds::loads(doc({{"segs2", {0.0f, 0.0f, 2.0f, 1.0f}}}), file));
        CHECK(three_kinds::loads(doc({{"values2", two}}), file));
        CHECK(three_kinds::refused_naming(doc({{"values2", three}}), file, {"node 'n' (Shaper): values2 length must match values"}));
    }
    // warnings(): one line under hysteresis without a slip curve, none otherwise.
    ShaperSource h;
    CHECK(h.warnings().empty());
    h.set_setting("hysteresis", 1.0f);
    const auto w = h.warnings();
    CHECK(w.size() == 1 && w[0] == "hysteresis without values2 is inert (no slip curve)");
    h.set_array("values2", two);
    CHECK(h.warnings().empty());
}

void run_declared_default_load_tests() {
    using namespace node_twins;
    std::printf("the five deleted branches' types load at their declared defaults\n");
    // Settings, Params and arrays against the registry's own node of the type.
    const auto at_defaults = [](const Component& got, const Component& fresh) {
        bool ok = true;
        for (const auto& d : fresh.setting_descriptors())
            ok = ok && got.get_setting(d.name) == d.default_value && got.get_setting(d.name) == fresh.get_setting(d.name);
        for (const auto& d : fresh.param_descriptors())
            ok = ok && !got.get_param(d.name) && !fresh.get_param(d.name);
        for (const auto& d : fresh.array_descriptors())
            ok = ok && got.get_array(d.name) == fresh.get_array(d.name);
        return ok;
    };
    for (const char* type : {"RangeSource", "SegmentSource", "Vibrato", "BWLowpassFilter",
                             "BWHighpassFilter", "BWBandpassFilter", "HybridKSSource"})
        for (bool withParams : {true, false}) {
            LoadedInstrument ip;
            auto n = enum_column::load_node(type, withParams ? nlohmann::json::object() : nlohmann::json(), ip);
            CHECK(n != nullptr);
            if (!n) continue;
            const bool ok = at_defaults(*n, *create(type));
            if (!ok) std::printf("  %s %s: not at its declared defaults\n", type, withParams ? "{}" : "(no params)");
            CHECK(ok);
        }
    // A RepeatingSource's gapDuration holds what the file gives, or its default.
    for (const auto& [params, want] : std::initializer_list<std::pair<const char*, float>>{
             {R"({"gapDuration":0.5})", 0.5f}, {R"({})", 0.2f}}) {
        LoadedInstrument rip;
        auto r = enum_column::load_node("RepeatingSource", nlohmann::json::parse(params), rip);
        CHECK(r && r->get_setting("gapDuration") == want);
    }
}
namespace list_twins {
// The render of node `id` of a loaded voice graph, or 0 with the refusal printed.
static uint64_t loaded_render(const std::string& doc, const char* id, std::vector<float>* arrays = nullptr,
                              std::initializer_list<const char*> names = {}) {
    try {
        LoadedInstrument ip = load_inst(doc);
        auto n = ip.instrument->pool().voice(0).nodes().at(id);
        if (arrays) for (const char* a : names) for (float v : n->get_array(a)) arrays->push_back(v);
        return node_twins::render(*std::dynamic_pointer_cast<ValueSource>(n), 4800, 2);
    } catch (const std::exception& e) {
        std::printf("  load failed: %s\n", e.what());
        return 0;
    }
}
}  // namespace list_twins

void run_list_node_loader_tests() {
    using namespace list_twins;
    using nlohmann::json;
    std::printf("the list nodes load their feeds and arrays from the file\n");
    std::vector<float> newArrays;
    const json columns = {{"id", "n"}, {"type", "MultiSource"}, {"params", {
        {"source", json::array({json{{"ref", "a"}}, json{{"ref", "b"}}})},
        {"weights", {0.5, 1.0}}, {"delays", {0.0, 100.0}}}}};
    const json plainFeeds = {{"id", "n"}, {"type", "MultiSource"}, {"params", {
        {"source", json::array({json{{"ref", "a"}}, json{{"ref", "b"}}})}}}};
    const uint64_t mNew = loaded_render(voice_doc(json::array({columns})), "n", &newArrays, {"weights", "delays"});
    CHECK(mNew != 0 && newArrays == std::vector<float>({0.5f, 1.0f, 0.0f, 100.0f}));
    CHECK(mNew != loaded_render(voice_doc(json::array({plainFeeds})), "n"));
    newArrays.clear();
    const json table = {{"id", "n"}, {"type", "PhasedValueSource"}, {"params", {{"overlap", 0.01},
        {"sources", json::array({json{{"ref", "a"}}, json{{"ref", "b"}}})},
        {"percent", {0.3, 0.0}}, {"minSec", {0.01, 0.0}}, {"maxSec", {0.02, 0.0}}, {"gain", {0.5, 1.0}}}}};
    const char* cols[] = {"percent", "minSec", "maxSec", "gain"};
    const uint64_t pNew = loaded_render(voice_doc(json::array({table})), "n", &newArrays, {cols[0], cols[1], cols[2], cols[3]});
    CHECK(pNew != 0 && newArrays == std::vector<float>({0.3f, 0.0f, 0.01f, 0.0f, 0.02f, 0.0f, 0.5f, 1.0f}));
    json ragged = table;
    ragged["params"].erase("gain");
    CHECK(three_kinds::refused_naming(voice_doc(json::array({ragged})), false,
        {"node 'n' (PhasedValueSource): table 'stages' takes its columns at one length, all present or all absent",
         "gain absent"}));
}

void run_expand_rule_loader_tests() {
    using namespace list_twins;
    using nlohmann::json;
    std::printf("the expand rule loads as a node by ref; a ref that is not a rule fails\n");
    const json rule = {{"count", 3}, {"spacing1", 0.3}, {"spacing2", 0.7}, {"dt1", 0.05}, {"power1", 2.0}, {"po2", 0.25}};
    const auto partials_doc = [&](const json& expandRule, bool withNode) {
        json nodes = json::array();
        if (withNode) nodes.push_back(json{{"id", "rule"}, {"type", "ExpandRule"}, {"params", rule}});
        json p = {{"id", "p"}, {"type", "FullPartials"}, {"params", {{"seed", 3902}, {"maxPartials", 6}}}};
        if (!expandRule.is_null()) p["params"]["expandRule"] = expandRule;
        nodes.push_back(p);
        nodes.push_back(json{{"id", "n"}, {"type", "AdditiveSource"},
                             {"params", {{"frequency", 220.0}, {"partials", {{"ref", "p"}}}}}});
        return voice_doc(nodes);
    };
    const uint64_t refRule = loaded_render(partials_doc(json{{"ref", "rule"}}, true), "n");
    CHECK(refRule != 0 && refRule != loaded_render(partials_doc(json(), false), "n"));
    // A ref to no node, and to a node that is not an ExpandRule.
    CHECK(three_kinds::refused_naming(partials_doc(json{{"ref", "nope"}}, false), false, {"nope"}));
    CHECK(three_kinds::refused_naming(partials_doc(json{{"ref", "a"}}, false), false,
        {"node 'p' (FullPartials): 'expandRule' does not accept a SineSource; 'expandRule' takes an ExpandRule part"}));
}

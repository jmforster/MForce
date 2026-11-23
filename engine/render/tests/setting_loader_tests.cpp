#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/build_instrument.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

using namespace mforce;

namespace three_kinds {
// A voice graph of three nodes: two sines `a` and `b` beside the node `n`.
static std::string list_doc(const nlohmann::json& feeds, const char* extraType = nullptr) {
    nlohmann::json nodes = nlohmann::json::array({
        nlohmann::json{{"id", "a"}, {"type", "SineSource"}},
        nlohmann::json{{"id", "b"}, {"type", "SineSource"}},
        nlohmann::json{{"id", "n"}, {"type", "ListProbe"}, {"params", {{"feeds", feeds}}}}});
    if (extraType) nodes.insert(nodes.begin(), nlohmann::json{{"id", "pk"}, {"type", extraType}});
    return nlohmann::json{{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
                          {"graph", {{"output", "a"}, {"nodes", nodes}}}}.dump();
}
}  // namespace three_kinds

void run_string_setting_loader_tests() {
    using namespace three_kinds;
    std::printf("the string setting through a load: a string read, a number and a pin refused\n");
    register_probes();
    {
        LoadedInstrument ip;
        auto n = enum_column::load_node("StringProbe", {{"label", "hello"}}, ip);
        CHECK(n != nullptr && n->get_string("label") == "hello");
        LoadedInstrument absent;
        auto d = enum_column::load_node("StringProbe", nlohmann::json::object(), absent);
        CHECK(d != nullptr && d->get_string("label") == "x");      // absent: the default
        StringProbe::lastLabel.clear();
        CHECK(loads(plain(enum_column::one_node("StringProbe", {{"label", "plain"}})), true));
        CHECK(StringProbe::lastLabel == "plain");
    }
    for (const bool file : {false, true}) {
        const std::string doc = enum_column::one_node("StringProbe", {{"label", 3}});
        CHECK(refused_naming(file ? plain(doc) : doc, file, {"node 'n' (StringProbe): 'label' takes a string, not 3"}));
        const std::string flag = enum_column::one_node("StringProbe", {{"label", true}});
        CHECK(refused_naming(file ? plain(flag) : flag, file, {"node 'n' (StringProbe): 'label' takes a string, not true"}));
    }
    {
        auto doc = nlohmann::json::parse(enum_column::one_node("StringProbe", nlohmann::json::object()));
        doc["graph"]["nodes"][1]["dynamicPins"] = {{"label", {{"ref", "out"}}}};
        for (const bool file : {false, true})
            CHECK(refused_naming(doc.dump(), file, {"graph.nodes[1].dynamicPins.label: node 'n' (StringProbe): 'label' is a structural setting on StringProbe"}));
    }
}

void run_input_list_loader_tests() {
    using namespace three_kinds;
    std::printf("the list of signal inputs through a load: refs and taps in file order\n");
    register_probes();
    auto feeds_of = [](const std::string& doc, LoadedInstrument& ip) -> ListProbe* {
        try { ip = load_inst(doc); }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return nullptr; }
        return dynamic_cast<ListProbe*>(ip.instrument->pool().voice(0).nodes().at("n").get());
    };
    {
        LoadedInstrument ip;
        auto* n = feeds_of(list_doc(nlohmann::json::array({{{"ref", "a"}}, {{"tap", "b"}}, {{"ref", "b"}}})), ip);
        CHECK(n != nullptr);
        if (n) {
            const auto& nodes = ip.instrument->pool().voice(0).nodes();
            CHECK(n->feeds.sources.size() == 3);
            auto* tap = n->feeds.sources.size() == 3 ? dynamic_cast<TapSource*>(n->feeds.sources[1].get()) : nullptr;
            CHECK(n->feeds.sources.size() == 3 && n->feeds.sources[0] == nodes.at("a") && n->feeds.sources[2] == nodes.at("b"));
            CHECK(tap != nullptr && tap->source == nodes.at("b"));
        }
    }
    {
        LoadedInstrument ip;
        auto* n = feeds_of(list_doc({{"ref", "b"}}), ip);
        CHECK(n != nullptr && n->feeds.sources.size() == 1);
        if (n && n->feeds.sources.size() == 1) CHECK(n->feeds.sources[0] == ip.instrument->pool().voice(0).nodes().at("b"));
    }
    for (const bool file : {false, true}) {
        const std::string inList = list_doc(nlohmann::json::array({{{"ref", "a"}}, {{"ref", "pk"}}}), "FormantPeaks");
        const std::string alone = list_doc({{"ref", "pk"}}, "FormantPeaks");
        CHECK(refused_naming(inList, file, {"node 'n' (ListProbe): 'feeds' does not accept a FormantPeaks, a part, not a signal"}));
        CHECK(refused_naming(alone, file, {"node 'n' (ListProbe): 'feeds' does not accept a FormantPeaks, a part, not a signal"}));
    }
}

void run_validate_tests() {
    using namespace three_kinds;
    std::printf("a node's own check: a refusal fails the load naming the node, a warning does not\n");
    register_probes();
    CheckProbe p;
    CHECK(p.validate().empty() && p.warnings().empty());
    p.refuse.value = true;
    p.warn.value = true;
    CHECK(p.validate() == "probe says no");
    CHECK((p.warnings() == std::vector<std::string>{"probe warns once", "probe warns twice"}));
    // a voice's node and a plain sound's node
    for (const bool file : {false, true}) {
        const std::string doc = enum_column::one_node("CheckProbe", {{"refuse", true}});
        CHECK(refused_naming(file ? plain(doc) : doc, file, {"node 'n' (CheckProbe): probe says no"}));
        const std::string warned = enum_column::one_node("CheckProbe", {{"warn", true}});
        CHECK(loads(file ? plain(warned) : warned, file));
        CHECK(loads(file ? plain(enum_column::one_node("CheckProbe", nullptr)) : enum_column::one_node("CheckProbe", nullptr), file));
        // a node with no params key is asked too
        const std::string bare = enum_column::one_node("RefusingProbe", nullptr);
        CHECK(nlohmann::json::parse(bare)["graph"]["nodes"][1].count("params") == 0);
        CHECK(refused_naming(file ? plain(bare) : bare, file, {"node 'n' (RefusingProbe): probe refuses as built"}));
    }
    // asked once per built node: one voice builds the probe once
    const auto module_builds = [](const std::string& doc) {
        try { (void)build_instrument(patch_door::read_text(doc)); return true; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    };
    {
        const std::string doc = enum_column::one_node("CheckProbe", {{"warn", true}});
        const int v0 = CheckProbe::validated, w0 = CheckProbe::warned;
        CHECK(module_builds(doc));
        CHECK(CheckProbe::validated - v0 == 1 && CheckProbe::warned - w0 == 1);
    }
    // inside a Multiplex template: each clone's build asks its own node
    auto mux_doc = [](const nlohmann::json& params) {
        return nlohmann::json{{"sampleRate", 48000}, {"instrument", {{"polyphony", 1}}},
            {"graph", {{"output", "mux"}, {"nodes", nlohmann::json::array({
                nlohmann::json{{"id", "n"}, {"type", "CheckProbe"}, {"params", params}},
                nlohmann::json{{"id", "mux"}, {"type", "MultiplexSource"},
                               {"params", {{"count", 3}, {"source", {{"ref", "n"}}}}}}})}}}}.dump();
    };
    {
        const int v0 = CheckProbe::validated, w0 = CheckProbe::warned;
        CHECK(module_builds(mux_doc({{"warn", true}})));
        if (CheckProbe::validated - v0 != 4) std::printf("  validated %d times, want 4\n", CheckProbe::validated - v0);
        CHECK(CheckProbe::validated - v0 == 4 && CheckProbe::warned - w0 == 4);   // the template's node, then three clones
    }
    for (const bool file : {false, true}) {
        const std::string refused = mux_doc({{"refuse", true}}), seeded = mux_doc({{"refuseSeeded", true}});
        CHECK(refused_naming(file ? plain(refused) : refused, file, {"node 'n' (CheckProbe): probe says no"}));
        // seeded only in a clone: the main pass passes, the clone's build refuses
        const std::string alone = enum_column::one_node("CheckProbe", {{"refuseSeeded", true}});
        CHECK(loads(file ? plain(alone) : alone, file));
        CHECK(refused_naming(file ? plain(seeded) : seeded, file, {"node 'n' (CheckProbe): probe says no"}));
    }
}

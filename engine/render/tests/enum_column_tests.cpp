#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/source/additive/formant_peaks.h"
#include "mforce/source/register_all_sources.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


// A FormantPeaks loads from four arrays of one length and renders; arrays of
// unequal length fail the load naming the node; an edited row reaches the
// next note through the owner's prepare, as the peak node's pin did.
void run_formant_peaks_load_tests() {
    std::printf("formant peaks: the load\n");
    const char* ragged = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks","params":{"frequency":[500.0,1500.0],"gain":[1.0],"width":[300.0,400.0],"power":[2.0,2.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0}}]}})";
    CHECK(load_fails_naming(ragged, "node 'pk' (FormantPeaks)"));
    CHECK(load_fails_naming(ragged, "table 'peaks' takes its columns at one length"));
    CHECK(load_fails_naming(ragged, "frequency 2, gain 1, width 2, power 2"));
    const char* missing = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks","params":{"frequency":[500.0],"gain":[1.0],"width":[300.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0}}]}})";
    CHECK(load_fails_naming(missing, "node 'pk' (FormantPeaks)"));            // a missing array is a length of none
    const char* rows = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks","params":{"frequency":[440.0,1320.0],"gain":[1.0,0.5],"width":[300.0,400.0],"power":[2.0,2.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0,"formantFloor":0.0}}]}})";
    auto ip = load_inst(rows);
    auto* pk = dynamic_cast<FormantPeaks*>(ip.instrument->pool().voice(0).nodes().at("pk").get());
    CHECK(pk != nullptr && pk->peak_count() == 2);
    const auto before = render_one_live_note(*ip.instrument, 60.0f);
    CHECK(std::any_of(before.begin(), before.end(), [](float v) { return v != 0.0f; }));
    pk->set_array("gain", {0.0f, 0.5f});
    ip.instrument->prepare(RenderContext{48000});
    const auto after = render_one_live_note(*ip.instrument, 60.0f);
    CHECK(!same_bits(before, after));
    const char* none = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"pk","type":"FormantPeaks"},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"pk"},"formantWeight":1.0}}]}})";
    auto empty = load_inst(none);                            // no rows: a formant that gains nothing
    auto* pk0 = dynamic_cast<FormantPeaks*>(empty.instrument->pool().voice(0).nodes().at("pk").get());
    CHECK(pk0 != nullptr && pk0->peak_count() == 0 && same_bits(pk0->gain(1000.0f), 0.0f));
}
namespace enum_column {
// The probe's `kind` column after a load of `params`, or {-1} on a refusal.
static std::vector<float> load_kind(const nlohmann::json& params) {
    LoadedInstrument ip;
    auto n = load_node("EnumTableProbe", params, ip);
    return n ? n->get_array("kind") : std::vector<float>{-1.0f};
}
// Whether the probe's load of `params` is refused with every one of `needles`.
static bool probe_fails_naming(const nlohmann::json& params, std::initializer_list<const char*> needles) {
    if (const std::string msg = load_refused(one_node("EnumTableProbe", params)); !msg.empty()) {
        bool all = true;
        for (const char* n : needles) all = all && msg.find(n) != std::string::npos;
        if (!all) std::printf("  refused for another reason: %s\n", msg.c_str());
        return all;
    }
    std::printf("  loaded: %s\n", params.dump().c_str());
    return false;
}
}  // namespace enum_column
void run_enum_column_tests() {
    using namespace enum_column;
    std::printf("the enum column: labels on disk, indices in memory; a ragged table refused\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    if (!reg.has("EnumTableProbe"))
        reg.register_type("EnumTableProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<EnumTableProbe>(); });
    // the one label match: case-insensitive, whole-string; null outside the labels
    CHECK(enum_index(kKinds, "Expo") == 1 && enum_index(kKinds, "expo") == 1 && enum_index(kKinds, "EXPO") == 1);
    CHECK(enum_index(kKinds, "Linear") == 0 && enum_index(kKinds, "hold") == 2);
    CHECK(enum_index(kKinds, "Lin") == -1 && enum_index(kKinds, "Linears") == -1 && enum_index(kKinds, "") == -1);
    CHECK(enum_index(kKinds, "Sine") == -1 && enum_index(nullptr, "Linear") == -1);
    CHECK(std::string(enum_label(kKinds, 0)) == "Linear" && std::string(enum_label(kKinds, 1)) == "Expo"
          && std::string(enum_label(kKinds, 2)) == "Hold");
    CHECK(enum_label(kKinds, -1) == nullptr && enum_label(kKinds, 3) == nullptr && enum_label(nullptr, 0) == nullptr);
    // the column read through a load: labels, indices and a mix load to the same indices
    {
        const auto byLabel = load_kind({{"level", {0.1, 0.2, 0.3}}, {"kind", {"Linear", "Expo", "Hold"}}});
        const auto byIndex = load_kind({{"level", {0.1, 0.2, 0.3}}, {"kind", {0, 1, 2}}});
        const auto mixed   = load_kind({{"level", {0.1, 0.2, 0.3}}, {"kind", {"Expo", 0, "Hold"}}});
        CHECK((byLabel == std::vector<float>{0.0f, 1.0f, 2.0f}));
        CHECK(byIndex == byLabel);
        CHECK((mixed == std::vector<float>{1.0f, 0.0f, 2.0f}));
        CHECK((load_kind({{"level", nlohmann::json::array({0.5})}, {"kind", nlohmann::json::array({2.0})}}) == std::vector<float>{2.0f}));   // a whole float is an index
    }
    // refused, naming the node, the column, the value and the labels
    {
        const nlohmann::json bad[] = {"Sine", 3, -1, 1.5, true, nullptr, nlohmann::json::object()};
        const char* shown[] = {"\"Sine\"", "holds 3,", "holds -1,", "holds 1.5,", "holds true,", "holds null,", "holds {},"};
        for (size_t k = 0; k < std::size(bad); ++k)
            CHECK(probe_fails_naming({{"level", {0.1, 0.2}}, {"kind", {"Linear", bad[k]}}},
                                     {"node 'n' (EnumTableProbe)", "'kind'", shown[k], "(Linear, Expo, Hold)"}));
    }
    // a ragged table refused for every type, naming the node, the group and each column's length
    {
        CHECK(probe_fails_naming({{"level", {0.1, 0.2, 0.3}}, {"kind", {0, 1}}},
                                 {"node 'n' (EnumTableProbe)", "table 'rows'", "(got level 3, kind 2)"}));
        CHECK(probe_fails_naming({{"level", {0.1, 0.2}}}, {"node 'n' (EnumTableProbe)", "table 'rows'", "(got level 2, kind absent)"}));
        CHECK(probe_fails_naming({{"kind", nlohmann::json::array({"Expo"})}}, {"table 'rows'", "(got level absent, kind 1)"}));
        CHECK(probe_fails_naming({{"level", 0.5}, {"kind", nlohmann::json::array({"Expo"})}}, {"node 'n' (EnumTableProbe): 'level' takes a list of numbers, not 0.5"}));   // not an array: refused
        CHECK(probe_fails_naming({{"level", nlohmann::json::array()}, {"kind", nlohmann::json::array({0})}}, {"(got level 0, kind 1)"}));
        CHECK(load_kind(nlohmann::json::object()).empty());                                 // both absent
        CHECK(load_kind({{"level", nlohmann::json::array()}, {"kind", nlohmann::json::array()}}).empty());   // both empty
        CHECK((load_kind({{"level", {0.1, 0.2}}, {"kind", {"Hold", 1}}}) == std::vector<float>{2.0f, 1.0f}));   // one length
    }
    // CombinedSource reads its own labels and the ordinals; a label in another
    // case and "add", a word that is no label, are refused
    {
        for (const char* word : {"mix", "MULTIPLY", "add"}) {
            const std::string msg = load_refused(one_node("CombinedSource", {{"operation", word}}));
            const bool named = msg.find(std::string("'operation' holds \"") + word + "\", which is not one of its labels") != std::string::npos;
            if (!named) std::printf("  CombinedSource operation %s: %s\n", word, msg.c_str());
            CHECK(named);
        }
        const std::pair<nlohmann::json, int> ops[] = {{"Mix", 0}, {"Multiply", 1}, {"Fade", 2}, {"Sum", 3},
                                                      {0, 0}, {1, 1}, {2, 2}, {3, 3}};
        for (const auto& [v, want] : ops) {
            const float got = load_setting("CombinedSource", "operation", v);
            if (got != float(want)) std::printf("  CombinedSource operation %s: got %g, want %d\n", v.dump().c_str(), got, want);
            CHECK(got == float(want));
        }
    }
    // every enum setting of every registered type loads from each of its own labels
    {
        int walked = 0;
        std::vector<std::string> skipped;
        for (const auto& e : reg.registered_types()) {
            if (e.name == "PerformNode") { skipped.push_back(e.name + " (loads as the voice's adapter, which declares no setting; run_perform_node_field_tests loads each label)"); continue; }
            std::shared_ptr<Component> s;
            try { s = reg.create(e.name); } catch (const std::exception&) { skipped.push_back(e.name + " (the registry cannot build it alone)"); continue; }
            if (!s) { skipped.push_back(e.name + " (the registry cannot build it alone)"); continue; }
            for (const auto& d : s->setting_descriptors()) {
                if (!d.enum_labels || d.type == SettingType::String) continue;   // a string setting takes no label
                for (int i = 0; d.enum_labels[i]; ++i) {
                    const float got = load_setting(e.name.c_str(), d.name, d.enum_labels[i]);
                    if (got != float(i)) std::printf("  %s.%s \"%s\": got %g, want %d\n", e.name.c_str(), d.name, d.enum_labels[i], got, i);
                    CHECK(got == float(i));
                    ++walked;
                }
            }
        }
        std::printf("  enum labels walked: %d; skipped: %zu\n", walked, skipped.size());
        for (const auto& s : skipped) std::printf("  skipped: %s\n", s.c_str());
        CHECK(walked > 0);
    }
}

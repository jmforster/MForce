#include "mforce/testing/check.h"
#include "mforce/core/component.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;

namespace patch_table {
static std::vector<std::string> labels(const char* const* l) {
    std::vector<std::string> out;
    for (int i = 0; l && l[i]; ++i) out.emplace_back(l[i]);
    return out;
}
static std::string text(const char* s) { return s ? s : ""; }
// Every field of every descriptor of `fresh` against the entry `d`, in
// declaration order; the first difference is printed.
static bool same_declarations(const Component& fresh, const TypeDeclarations& d) {
    const auto differ = [&](const char* what, const char* name) {
        std::printf("  %s: %s '%s' differs from a fresh instance's\n", d.type.c_str(), what, name);
        return false;
    };
    const auto ps = fresh.param_descriptors();
    if (ps.size() != d.params.size()) return differ("the param count", "");
    for (size_t i = 0; i < ps.size(); ++i) {
        const auto& e = d.params[i];
        if (e.name != ps[i].name || e.defaultValue != ps[i].default_value || e.min != ps[i].min_value ||
            e.max != ps[i].max_value || e.hint != text(ps[i].hint)) return differ("param", ps[i].name);
    }
    const auto is = fresh.input_descriptors();
    if (is.size() != d.inputs.size()) return differ("the input count", "");
    for (size_t i = 0; i < is.size(); ++i) {
        const auto& e = d.inputs[i];
        if (e.name != is[i].name || e.list != is[i].multi || e.slot != is[i].slot ||
            e.hint != text(is[i].hint) || (!e.slot && !e.accepts.empty())) return differ("input", is[i].name);
    }
    const auto ss = fresh.setting_descriptors();
    if (ss.size() != d.settings.size()) return differ("the setting count", "");
    for (size_t i = 0; i < ss.size(); ++i) {
        const auto& e = d.settings[i];
        const std::string defaultText = ss[i].type == SettingType::String ? fresh.get_string(ss[i].name) : "";
        if (e.name != ss[i].name || e.type != ss[i].type || e.defaultValue != ss[i].default_value ||
            e.min != ss[i].min_value || e.max != ss[i].max_value || e.defaultText != defaultText ||
            e.labels != labels(ss[i].enum_labels) || e.structural != ss[i].structural)
            return differ("setting", ss[i].name);
    }
    const auto as = fresh.array_descriptors();
    if (as.size() != d.arrays.size()) return differ("the array count", "");
    for (size_t i = 0; i < as.size(); ++i) {
        const auto& e = d.arrays[i];
        if (e.name != as[i].name || e.group != text(as[i].groupName) || e.rowDefault != as[i].default_value ||
            e.min != as[i].min_value || e.max != as[i].max_value || e.labels != labels(as[i].enum_labels))
            return differ("array", as[i].name);
    }
    return true;
}
// A probe registered only after the table has been asked.
struct LateProbe final : Generator {
    Param level{this, "level", 0.5f, 0.0f, 1.0f};
    float compute() override { return level.current(); }
    const char* type_name() const override { return "LateDeclarationProbe"; }
};
}  // namespace patch_table

// The table's first ask, its pointers' lifetime, and an unknown name. The
// whole table's build time is printed.
void run_type_declaration_identity_tests() {
    std::printf("the declarations table: read a type at a time, pointers stable, unknown names null\n");
    const auto start = std::chrono::steady_clock::now();
    const TypeDeclarations* sine = type_declarations("SineSource");
    const Component* sineProto = prototype("SineSource");
    CHECK(sine != nullptr);
    CHECK(sineProto != nullptr);
    const auto types = declared_types();
    for (const auto& t : types) CHECK(type_declarations(t) != nullptr);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::printf("  %zu types declared; the table read whole in %.1f ms\n", types.size(), ms);
    CHECK(type_declarations("SineSource") == sine);
    CHECK(prototype("SineSource") == sineProto);
    CHECK(std::find(types.begin(), types.end(), "SineSource") != types.end());
    CHECK(sine && sine->type == "SineSource" && sine->signal);
    if (sine) {
        CHECK(sine->param("frequency") && sine->param("frequency")->name == "frequency");
        CHECK(sine->param("nope") == nullptr);
        CHECK(sine->input("nope") == nullptr && sine->setting("nope") == nullptr && sine->array("nope") == nullptr);
    }
    if (const auto* env = type_declarations("Envelope")) {
        CHECK(env->setting("timeMode") && env->setting("timeMode")->labels ==
                                              std::vector<std::string>({"fraction", "seconds"}));
        CHECK(env->array("type") && env->array("type")->group == "stages" && env->array("type")->labels.size() == 5);
        CHECK(env->input("trigger") == nullptr && env->param("trigger") != nullptr);
    }
    if (const auto* gate = type_declarations("NameGate"))
        CHECK(gate->setting("name") && gate->setting("name")->type == SettingType::String &&
              gate->setting("name")->structural && gate->input("in") != nullptr);
    CHECK(type_declarations("NoSuchSource") == nullptr);
    CHECK(prototype("NoSuchSource") == nullptr);
    const auto again = declared_types();
    CHECK(std::find(again.begin(), again.end(), "NoSuchSource") == again.end());
    CHECK(again == types);
}

// Every field of every descriptor of a fresh instance made through the
// registry, the string settings' defaults, and whether the type is a signal.
void run_type_declaration_field_tests() {
    using namespace patch_table;
    std::printf("the declarations table: every descriptor field, as a fresh instance says\n");
    const auto& reg = SourceRegistry::instance();
    int types = 0, strings = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        CHECK(d != nullptr);
        if (!d) continue;
        ++types;
        const auto fresh = reg.create(t);
        CHECK(d->type == t);
        CHECK(same_declarations(*fresh, *d));
        const bool signal = dynamic_cast<ValueSource*>(fresh.get()) != nullptr;
        if (d->signal != signal) std::printf("  %s: signal is %d, the cast says %d\n", t.c_str(), d->signal, signal);
        CHECK(d->signal == signal);
        for (const auto& s : d->settings) strings += s.type == SettingType::String;
    }
    std::printf("  %d types compared; %d string settings among them\n", types, strings);
    const auto* gate = type_declarations("NameGate");
    CHECK(gate && gate->setting("name") && gate->setting("name")->defaultText == "");
    const auto* mixer = type_declarations("StereoMixer");
    CHECK(mixer && !mixer->signal);
    const auto* channel = type_declarations("SoundChannel");
    CHECK(channel && !channel->signal);
}

// A slot's accepted types against the door: for every slot of every type and
// every registered type, listed exactly when a fresh owner's set_param takes
// a fresh instance; and three slots by name.
void run_type_declaration_accept_tests() {
    std::printf("the declarations table: a slot accepts what the door takes\n");
    const auto& reg = SourceRegistry::instance();
    const auto types = declared_types();
    int slots = 0, probes = 0, disagreements = 0;
    for (const auto& owner : types) {
        const TypeDeclarations* d = type_declarations(owner);
        if (!d) continue;
        for (const auto& in : d->inputs) {
            if (!in.slot) continue;
            ++slots;
            for (const auto& candidate : types) {
                ++probes;
                const bool door = reg.create(owner)->set_param(in.name, reg.create(candidate));
                const bool listed = std::find(in.accepts.begin(), in.accepts.end(), candidate) != in.accepts.end();
                if (door == listed) continue;
                ++disagreements;
                std::printf("  %s.%s and %s: the door says %d, the table %d\n",
                            owner.c_str(), in.name.c_str(), candidate.c_str(), door, listed);
            }
        }
    }
    std::printf("  %d slots, %d probes\n", slots, probes);
    CHECK(slots > 0);
    CHECK(disagreements == 0);
    const auto accepts = [](const char* type, const char* slot) {
        const auto* d = type_declarations(type);
        const auto* in = d ? d->input(slot) : nullptr;
        return in ? in->accepts : std::vector<std::string>{"(no such slot)"};
    };
    CHECK(accepts("StereoMixer", "channels") == std::vector<std::string>({"SoundChannel"}));
    CHECK(accepts("AdditiveSource", "partials") ==
          std::vector<std::string>({"FullPartials", "SequencePartials", "ExplicitPartials", "CompositePartials"}));
    CHECK(accepts("WavetableSource", "evolution") ==
          std::vector<std::string>({"EKSEvolution", "PluckEvolution", "AveragingEvolution", "BowedStringEvolution",
                                    "ReedEvolution", "BrassEvolution", "ReactionDiffusionEvolution",
                                    "SortErosionEvolution", "CellularAutomatonEvolution",
                                    "HistogramEqualizeEvolution", "BezierPullEvolution", "BitRotateEvolution",
                                    "TargetEvolution"}));
    const auto* sine = type_declarations("SineSource");
    CHECK(sine && sine->input("frequency") == nullptr && sine->param("frequency") != nullptr);
}

// A fresh instance holds its declared defaults: every Param's constant and
// every scalar setting's stored value. Envelope.sustainLevel is the one
// exception: its get_setting answers from the stage table.
void run_type_declaration_default_tests() {
    std::printf("the declarations table: a fresh instance holds its declared defaults\n");
    const auto& reg = SourceRegistry::instance();
    int differing = 0, compared = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        if (!d) continue;
        const auto fresh = reg.create(t);
        for (const auto& p : d->params) {
            ++compared;
            const Param* pin = fresh->param(p.name);
            if (pin && pin->constant == p.defaultValue) continue;
            ++differing;
            std::printf("  %s.%s: the constant is %g, declared %g\n", t.c_str(), p.name.c_str(),
                        pin ? pin->constant : -1.0f, p.defaultValue);
        }
        for (const auto& s : d->settings) {
            if (s.type == SettingType::String) continue;
            const float got = fresh->get_setting(s.name);
            if (t == "Envelope" && s.name == "sustainLevel") {
                std::printf("  Envelope.sustainLevel (the view): reads %g, declared %g\n", got, s.defaultValue);
                continue;
            }
            ++compared;
            if (got == s.defaultValue) continue;
            ++differing;
            std::printf("  %s.%s: holds %g, declared %g\n", t.c_str(), s.name.c_str(), got, s.defaultValue);
        }
    }
    std::printf("  %d values compared, %d differ\n", compared, differing);
    CHECK(compared > 0);
    CHECK(differing == 0);
}

// The table is read from each type's prototype and asks slot acceptance of a
// scratch owner, so no slot of any type's prototype holds a part.
void run_type_declaration_prototype_tests() {
    std::printf("the declarations table: no slot of any prototype holds a part\n");
    int slots = 0, holding = 0;
    for (const auto& t : declared_types()) {
        const TypeDeclarations* d = type_declarations(t);
        const Component* proto = prototype(t);
        if (!d || !proto) continue;
        for (const auto& in : d->inputs) {
            if (!in.slot) continue;
            ++slots;
            const auto part = proto->get_part(in.name);
            if (!part) continue;
            ++holding;
            std::printf("  %s.%s holds a %s\n", t.c_str(), in.name.c_str(), part->type_name());
        }
    }
    std::printf("  %d slots over every type's prototype, %d hold a part\n", slots, holding);
    CHECK(slots > 0);
    CHECK(holding == 0);
}

// A name the registry does not have caches nothing: registered after it was
// asked, and after the table was first read, it is found.
void run_type_declaration_late_tests() {
    using namespace patch_table;
    std::printf("the declarations table: a type registered after the first ask is found\n");
    auto& reg = SourceRegistry::instance();
    CHECK(type_declarations("SineSource") != nullptr);
    if (!reg.has("LateDeclarationProbe")) {
        CHECK(type_declarations("LateDeclarationProbe") == nullptr);
        CHECK(prototype("LateDeclarationProbe") == nullptr);
        reg.register_type("LateDeclarationProbe", {.family = Family::Utility, .shelf = Shelf::Experimental},
                          [](auto) { return std::make_shared<LateProbe>(); });
    }
    const auto* late = type_declarations("LateDeclarationProbe");
    CHECK(late && late->signal && late->params.size() == 1 && late->param("level") &&
          late->param("level")->defaultValue == 0.5f);
    CHECK(prototype("LateDeclarationProbe") != nullptr);
    const auto types = declared_types();
    CHECK(!types.empty() && types.back() == "LateDeclarationProbe");
}

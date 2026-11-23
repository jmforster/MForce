#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/combiner/multi_source.h"
#include "mforce/source/modulator/name_gate.h"
#include "mforce/source/modulator/range_source.h"
#include "mforce/source/modulator/var_source.h"
#include <cmath>
#include <memory>
#include <string>

using namespace mforce;


void run_range_var_tests() {
    RenderContext ctx{48000};

    // RangeSource: the declared default of `normalized` is false; the
    // setting turns it on.
    {
        auto zero = std::make_shared<ConstantSource>(0.0f);
        RangeSource a;
        a.set_param("min", std::make_shared<ConstantSource>(10.0f));
        a.set_param("max", std::make_shared<ConstantSource>(20.0f));
        a.set_param("var", zero);
        CHECK(a.normalized.value == false);
        CHECK(!a.setting_descriptors().empty());
        CHECK_NEAR(a.setting_descriptors()[0].default_value, 0.0f, 1e-6f);
        a.prepare(ctx); a.start(16);
        CHECK_NEAR(a.next(), 15.0f, 1e-5f);        // var 0 remapped (0+1)/2
        RangeSource b;
        b.set_param("min", std::make_shared<ConstantSource>(10.0f));
        b.set_param("max", std::make_shared<ConstantSource>(20.0f));
        b.set_param("var", zero);
        b.set_setting("normalized", 1.0f);
        b.prepare(ctx); b.start(16);
        CHECK_NEAR(b.next(), 10.0f, 1e-5f);        // already normalized
        CHECK(a.param_descriptors().size() == 3);
        CHECK(std::string(a.param_descriptors()[2].name) == "var");
        CHECK(a.get_param("max") != nullptr);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(a.next()));
    }

    // VarSource: absolute and relative modes; wired by name, default
    // absolute.
    {
        VarSource v;
        v.set_param("val", std::make_shared<ConstantSource>(2.0f));
        v.set_param("var", std::make_shared<ConstantSource>(0.5f));
        v.set_param("varPct", std::make_shared<ConstantSource>(0.5f));
        CHECK(v.absolute.value == true);
        v.prepare(ctx); v.start(16);
        CHECK_NEAR(v.next(), 2.5f, 1e-5f);         // 2 * (1 + 0.5 * 0.5)
        v.set_setting("absolute", 0.0f);
        CHECK_NEAR(v.next(), 2.5f + (2.0f - 2.5f) * 1.25f, 1e-5f);
        for (int i = 0; i < 10; ++i) CHECK(std::isfinite(v.next()));
        CHECK(v.param_descriptors().size() == 3);
    }
}

void run_multi_name_gate_tests() {
    RenderContext ctx{48000};

    // MultiSource: `source` is a declared list with its weights and delays
    // beside it, by position; clear_param empties it.
    {
        MultiSource m;
        m.set_param("source", std::make_shared<ConstantSource>(1.0f));
        m.add_param("source", std::make_shared<ConstantSource>(2.0f));
        CHECK(m.source.sources.size() == 2);
        CHECK(m.input_descriptors().size() == 1);
        CHECK(m.input_descriptors()[0].multi);
        CHECK(m.pins().size() == 3);               // the list and the two arrays
        CHECK(m.get_param("source") == nullptr);
        m.set_array("weights", {1.0f, 0.5f});
        m.set_array("delays", {0.0f, 1.0f});
        m.prepare(ctx); m.start(16);
        CHECK_NEAR(m.next(), 1.0f, 1e-6f);         // the delayed feed is not yet in
        CHECK_NEAR(m.next(), 2.0f, 1e-6f);         // 1 + 2 * 0.5
        m.clear_param("source");
        CHECK(m.source.sources.empty());
    }

    // NameGate: `in` is an optional signal input, advanced once per sample.
    {
        NameGate ng;
        CHECK(ng.param_descriptors().empty());
        CHECK(ng.input_descriptors().size() == 1);
        CHECK(std::string(ng.input_descriptors()[0].name) == "in");
        ng.targetId = 0.0f;
        CHECK(ng.next() == 0.0f);                  // unwired never matches, even id 0
        auto c = std::make_shared<PinCounter>();
        c->level = 3.0f;
        ng.set_param("in", c);
        ng.targetId = 3.0f;
        ng.prepare(ctx); ng.start(16);
        CHECK(c->starts == 1);
        CHECK(ng.next() == 1.0f);
        CHECK(ng.next() == 1.0f);
        CHECK(c->nexts == 2);
    }
}

#include "mforce/testing/check.h"
#include "test_support.h"
#include "source/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/ramp.h"
#include "mforce/source/envelope/envelope.h"
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


void run_envelope_pin_tests() {
    RenderContext ctx{48000};

    // Envelope: minValue/maxValue are pulled and prepared by the base;
    // `trigger` is Manual, so it is never prepared or advanced.
    {
        auto env = std::make_shared<Envelope>();
        env->add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        env->add_stage({{1.0f, 1.0f, RampType::Linear, 0.0f}, 0.5f, 0.0f, 0.0f});
        auto lo = std::make_shared<PinCounter>();
        auto hi = std::make_shared<PinCounter>();
        auto trig = std::make_shared<PinCounter>();
        env->set_param("minValue", lo);
        env->set_param("maxValue", hi);
        env->set_param("trigger", trig);
        env->prepare(ctx); env->start(4800);
        for (int i = 0; i < 5; ++i) env->next();
        CHECK(lo->starts == 1 && hi->starts == 1 && trig->starts == 0);
        CHECK(lo->nexts == 5 && hi->nexts == 5 && trig->nexts == 0);
        CHECK(env->trigger.source == trig);
        CHECK(env->range() == Generator::Range::Unipolar);
        CHECK(env->param_descriptors().size() == 3);
        CHECK(std::string(env->param_descriptors()[2].name) == "trigger");
    }

    // Envelope settings: the knobs clamp; sustainLevel is a live view of the
    // four-slot ADSR shape and reads 0 on any other layout.
    {
        auto adsr = Envelope::make_adsr(0.1f, 0.1f, 0.4f, 0.2f);
        CHECK_NEAR(adsr->get_setting("sustainLevel"), 0.4f, 1e-6f);
        adsr->set_setting("sustainLevel", 0.2f);
        CHECK_NEAR(adsr->get_setting("sustainLevel"), 0.2f, 1e-6f);
        CHECK_NEAR(adsr->stage(3).ramp.startVal, 0.2f, 1e-6f);
        adsr->set_setting("stage_accuracy", 7.0f);
        CHECK_NEAR(adsr->get_setting("stage_accuracy"), 1.0f, 1e-6f);
        adsr->set_setting("timeScale", 0.0f);
        CHECK_NEAR(adsr->get_setting("timeScale"), 0.01f, 1e-6f);
        auto ar = Envelope::make_ar(0.2f);
        ar->set_setting("sustainLevel", 0.9f);
        CHECK_NEAR(ar->get_setting("sustainLevel"), 0.0f, 1e-6f);
        CHECK_NEAR(ar->stage(1).ramp.startVal, 1.0f, 1e-6f);   // not rewritten
    }
}

// replace_stages assigns a freshly laid-out envelope over this one (the
// editor's preset combo). The pin registry belongs to the object, not to the
// assignment, so the pins stay the same and a wired one is still prepared
// and advanced afterwards.
void run_envelope_rebuild_pin_tests() {
    RenderContext ctx{48000};
    auto env = Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.1f);
    const std::vector<PinBase*> pinsBefore = env->pins();
    auto lo = std::make_shared<PinCounter>();
    auto hi = std::make_shared<PinCounter>();
    env->set_param("minValue", lo);
    env->set_param("maxValue", hi);
    env->set_setting("stage_accuracy", 0.5f);
    env->replace_stages(std::move(*Envelope::make_adsr(0.2f, 0.1f, 0.3f, 0.25f)));   // each of these replaces the layout
    env->replace_stages(std::move(*Envelope::make_as(0.05f)));
    CHECK_NEAR(env->get_setting("sustainLevel"), 0.0f, 1e-6f);        // the view follows the layout: no four-slot shape
    env->replace_stages(std::move(*Envelope::make_adsr(0.2f, 0.1f, 0.7f, 0.0f)));
    CHECK_NEAR(env->get_setting("sustainLevel"), 0.7f, 1e-6f);        // and reads the new layout's slot (what the editor re-pulls)
    CHECK_NEAR(env->stage(2).ramp.startVal, 0.7f, 1e-6f);
    env->replace_stages(std::move(*Envelope::make_as(0.05f)));
    CHECK(env->pins() == pinsBefore);
    CHECK(env->get_param("minValue") == lo && env->get_param("maxValue") == hi);
    CHECK_NEAR(env->get_setting("stage_accuracy"), 0.5f, 1e-6f);      // a knob outside the layout survives
    CHECK(env->stage_count() == 2);

    env->prepare(ctx); env->start(4800);
    const int n = 25;
    for (int i = 0; i < n; ++i) env->next();
    CHECK(lo->starts == 1 && hi->starts == 1);
    CHECK(lo->nexts == n && hi->nexts == n);
}

void run_envelope_factory_tests() {
    const RampType L = RampType::Linear, S = RampType::Sine;
    CHECK(stages_are(*Envelope::make_as(0.3f),
                     {{0.0f, 1.0f, L, 0.0f, 0.3f, 0.0f, 1.0f}, {1.0f, 1.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_as(0.3f, 0.6f, true),
                     {{0.6f, 0.0f, L, 0.0f, 0.3f, 0.0f, 1.0f}, {0.0f, 0.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_asr(0.15f, 0.4f, 0.3f),
                     {{0.0f, 0.4f, L, 0.0f, 0.15f, 0.0f, 1.0f}, {0.4f, 0.4f, L, 0.0f, 0.0f, 0.0f, 0.0f},
                      {0.4f, 0.0f, S, 0.0f, 0.3f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_ads(0.1f, 0.25f, 0.3f),
                     {{0.0f, 1.0f, L, 0.0f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.3f, L, 0.0f, 0.25f, 0.0f, 0.5f},
                      {0.3f, 0.3f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*Envelope::make_adr(0.1f, 0.2f, 0.55f),
                     {{0.0f, 1.0f, L, 0.0f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.55f, L, 0.0f, 0.2f, 0.0f, 0.5f},
                      {0.55f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    for (const auto& e : {Envelope::make_as(0.3f), Envelope::make_asr(0.15f, 0.4f, 0.3f),
                          Envelope::make_ads(0.1f, 0.25f, 0.3f), Envelope::make_adr(0.1f, 0.2f, 0.55f)})
        CHECK(!e->has_adsr_shape_() && !e->absolute_time());      // none is the four-slot shape, none in seconds
}

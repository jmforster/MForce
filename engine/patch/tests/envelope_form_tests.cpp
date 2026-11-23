#include "mforce/testing/check.h"
#include "source/tests/fixtures.h"
#include "mforce/core/ramp.h"
#include "mforce/patch/envelope_preset.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace mforce;


// The six preset forms, stage by stage, at their defaults and with every
// knob off its default. A bare adsr form releases over 0.0 (a patch that
// wants another release writes the key).
void run_envelope_form_tests() {
    const RampType L = RampType::Linear, E = RampType::Expo, I = RampType::InverseExpo, S = RampType::Sine;
    using J = nlohmann::json;
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "ar"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"ar","attack":0.4,"attackCurve":2,"attackPower":1.2,"releaseCurve":0,"releasePower":3.0})")),
                     {{0.0f, 1.0f, I, 1.2f, 0.4f, 0.0f, 1.0f}, {1.0f, 0.0f, L, 3.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "as"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 1.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"as","attack":0.35,"attackCurve":1,"attackPower":2.5,"reverse":true,"sustainLevel":0.6})")),
                     {{0.6f, 0.0f, E, 2.5f, 0.35f, 0.0f, 1.0f}, {0.0f, 0.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "asr"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 1.0f, L, 0.0f, 0.0f, 0.0f, 0.0f},
                      {1.0f, 0.0f, S, 0.0f, 0.1f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"asr","attack":0.15,"attackCurve":2,"attackPower":1.5,"release":0.3,"releaseCurve":2,"releasePower":0.5,"sustainLevel":0.4})")),
                     {{0.0f, 0.4f, I, 1.5f, 0.15f, 0.0f, 1.0f}, {0.4f, 0.4f, L, 0.0f, 0.0f, 0.0f, 0.0f},
                      {0.4f, 0.0f, I, 0.5f, 0.3f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "ads"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.0f, L, 0.0f, 0.1f, 0.0f, 0.5f},
                      {0.0f, 0.0f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"ads","attack":0.1,"attackCurve":3,"decay":0.25,"decayCurve":1,"decayPower":2.0,"sustainLevel":0.3})")),
                     {{0.0f, 1.0f, S, 0.0f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.3f, E, 2.0f, 0.25f, 0.0f, 0.5f},
                      {0.3f, 0.3f, L, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "adr"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.7f, L, 0.0f, 0.1f, 0.0f, 0.5f},
                      {0.7f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"adr","attack":0.1,"attackPower":0.7,"decay":0.2,"decayCurve":3,"decayLevel":0.55,"releaseCurve":1,"releasePower":1.5})")),
                     {{0.0f, 1.0f, L, 0.7f, 0.1f, 0.0f, 1.0f}, {1.0f, 0.55f, S, 0.0f, 0.2f, 0.0f, 0.5f},
                      {0.55f, 0.0f, E, 1.5f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J{{"preset", "adsr"}}),
                     {{0.0f, 1.0f, L, 0.0f, 0.2f, 0.0f, 1.0f}, {1.0f, 0.7f, L, 0.0f, 0.1f, 0.0f, 0.5f},
                      {0.7f, 0.7f, L, 0.0f, 0.0f, 0.0f, 0.0f}, {0.7f, 0.0f, S, 0.0f, 0.0f, 0.0f, 0.0f}}));
    CHECK(stages_are(*envelope_from_preset_json(J::parse(R"({"preset":"adsr","attack":0.12,"attackCurve":1,"attackPower":0.8,"decay":0.2,"decayCurve":2,"decayPower":1.1,"sustainLevel":0.5,"release":0.25,"releaseCurve":0,"releasePower":2.0})")),
                     {{0.0f, 1.0f, E, 0.8f, 0.12f, 0.0f, 1.0f}, {1.0f, 0.5f, I, 1.1f, 0.2f, 0.0f, 0.5f},
                      {0.5f, 0.5f, L, 0.0f, 0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, L, 2.0f, 0.25f, 0.0f, 0.0f}}));
    // adsr is the four-slot shape (the live sustain rewrite applies); the
    // others are not, and report the four-slot view of sustainLevel, 0; none
    // is in seconds.
    CHECK(envelope_from_preset_json(J{{"preset", "adsr"}})->has_adsr_shape_());
    for (const char* n : {"ar", "as", "asr", "ads", "adr"}) {
        auto e = envelope_from_preset_json(J{{"preset", n}});
        CHECK(!e->has_adsr_shape_() && !e->absolute_time());
        CHECK_NEAR(e->get_setting("sustainLevel"), 0.0f, 1e-6f);
    }
    // A curve outside 0..3 and an unknown preset are refused by name.
    std::string msg;
    try { envelope_from_preset_json(J{{"preset", "as"}, {"attackCurve", 4}}); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("attackCurve is 4") != std::string::npos);
    msg.clear();
    try { envelope_from_preset_json(J{{"preset", "adsx"}}); } catch (const std::runtime_error& e) { msg = e.what(); }
    CHECK(msg.find("Unknown envelope preset: adsx") != std::string::npos);
    // The key list is what the builders read; the table is the six in menu order.
    const auto& keys = envelope_preset_keys();
    CHECK(std::find(keys.begin(), keys.end(), "reverse") != keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "decayLevel") != keys.end());
    CHECK(std::find(keys.begin(), keys.end(), "stages") == keys.end());
    CHECK(std::size(kEnvelopePresets) == 6 && std::string(kEnvelopePresets[0].name) == "ar"
          && std::string(kEnvelopePresets[5].name) == "adsr");
}

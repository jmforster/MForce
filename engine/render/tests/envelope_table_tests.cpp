#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/ramp.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/source/envelope/envelope.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


// ---------------------------------------------------------------------------
// The preset form against the preset classes: for each of the six layouts a
// class built through set_setting (what the registry path's generic settings
// pass does) lays out the same stages as the form built from the same keys
// at once.
// ---------------------------------------------------------------------------
static bool same_layout(const Envelope& a, const Envelope& b) {
    if (a.stage_count() != b.stage_count()) return false;
    for (int i = 0; i < a.stage_count(); ++i) {
        const auto& s = a.stage(i);
        const auto& t = b.stage(i);
        if (s.percent != t.percent || s.minSec != t.minSec || s.maxSec != t.maxSec || s.nominal != t.nominal)
            return false;
        if (s.ramp.startVal != t.ramp.startVal || s.ramp.endVal != t.ramp.endVal
            || s.ramp.type != t.ramp.type || s.ramp.power != t.ramp.power)
            return false;
    }
    return a.absolute_time() == b.absolute_time() && a.has_adsr_shape_() == b.has_adsr_shape_();
}

// Through a load: a form node and the stage table it lays out, given as
// columns, render the same bytes; a seed and the accuracy knobs reach the
// form node as they reach the table node.
static std::vector<float> render_loaded_note(const char* json, int n) {
    auto ip = load_inst(json);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    play_notes(*pi, {{.start = 0.0f, .duration = 0.2f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
    std::vector<float> out(static_cast<size_t>(n));
    ip.instrument->render(out.data(), n);
    return out;
}

void run_envelope_form_loader_tests() {
    const char* form = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"Env","type":"Envelope","params":{"preset":"as","attack":0.3,"attackCurve":1,"attackPower":2.0,"reverse":true,"sustainLevel":0.6,"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8}},
        {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
    const char* stages = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
        {"id":"Env","type":"Envelope","params":{"startVal":[0.6,0.0],"endVal":[0.0,0.0],"power":[2.0,0.0],"type":["Expo","Linear"],"percent":[0.3,0.0],"minSec":[0.0,0.0],"maxSec":[1.0,0.0],"nominal":[0.0,0.0],"stage_accuracy":0.9,"ramp_accuracy":0.95,"seed":77,"minValue":0.0,"maxValue":0.8}},
        {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
    const auto a = render_loaded_note(form, 9600), b = render_loaded_note(stages, 9600);
    CHECK(same_bits(a, b));
    CHECK(a[1000] != 0.0f);                       // inside the attack: reversed, the shape falls to 0 and holds there
    auto env_of = [](const char* json) {
        auto ip = load_inst(json);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        return std::dynamic_pointer_cast<Envelope>(pi->pool().voice(0).nodes().at("Env"));
    };
    auto ef = env_of(form), es = env_of(stages);
    CHECK(ef && es && same_layout(*ef, *es));
    CHECK(ef->get_seed() == es->get_seed() && ef->get_seed() != 0u);
    CHECK_NEAR(ef->get_setting("stage_accuracy"), 0.9f, 1e-6f);
    CHECK_NEAR(ef->get_setting("ramp_accuracy"), 0.95f, 1e-6f);
}

// ---------------------------------------------------------------------------
// The Envelope's stage table: the eight columns of the group "stages" are what
// the node holds, and the stage layout the render walks is derived from them.
// ---------------------------------------------------------------------------
static const char* const kStageColumns[] = {"startVal", "endVal", "power", "type",
                                            "percent", "minSec", "maxSec", "nominal"};

// Field `c` of a stage, in the columns' order; the type as its index.
static float stage_field(const Envelope::Stage& s, int c) {
    switch (c) {
        case 0:  return s.ramp.startVal;
        case 1:  return s.ramp.endVal;
        case 2:  return s.ramp.power;
        case 3:  return float(int(s.ramp.type));
        case 4:  return s.percent;
        case 5:  return s.minSec;
        case 6:  return s.maxSec;
        default: return s.nominal;
    }
}

void run_envelope_table_tests() {
    std::printf("the Envelope's stage table: the twin, the doors, the order, timeMode, the mapping, the load\n");
    using Stage = Envelope::Stage;
    using J = nlohmann::json;
    const RampType L = RampType::Linear, E = RampType::Expo, I = RampType::InverseExpo,
                   S = RampType::Sine, H = RampType::Hold;

    // Stage count and every field of every stage, the floats by their bits.
    auto same_layout = [](const Envelope& a, const Envelope& b) {
        if (a.stage_count() != b.stage_count()) return false;
        for (int i = 0; i < a.stage_count(); ++i) {
            const Stage& s = a.stage(i);
            const Stage& t = b.stage(i);
            if (!same_bits(s.ramp.startVal, t.ramp.startVal) || !same_bits(s.ramp.endVal, t.ramp.endVal)
                || s.ramp.type != t.ramp.type || !same_bits(s.ramp.power, t.ramp.power)
                || !same_bits(s.percent, t.percent) || !same_bits(s.minSec, t.minSec)
                || !same_bits(s.maxSec, t.maxSec) || !same_bits(s.nominal, t.nominal))
                return false;
        }
        return true;
    };
    // Each of the eight columns equals the layout field for field.
    auto columns_match_layout = [](const Envelope& e) {
        for (int c = 0; c < 8; ++c) {
            const std::vector<float> col = e.get_array(kStageColumns[c]);
            if (int(col.size()) != e.stage_count()) return false;
            for (int i = 0; i < e.stage_count(); ++i)
                if (!same_bits(col[size_t(i)], stage_field(e.stage(i), c))) return false;
        }
        return true;
    };
    auto rows_of = [](const Envelope& e) {
        std::vector<Stage> rows;
        for (int i = 0; i < e.stage_count(); ++i) rows.push_back(e.stage(i));
        return rows;
    };
    auto by_add = [](const std::vector<Stage>& rows, float timeMode) {
        auto e = std::make_shared<Envelope>();
        e->set_setting("timeMode", timeMode);
        for (const Stage& s : rows) e->add_stage(s);
        return e;
    };
    // The same rows written as the eight columns, in declaration order or reversed.
    auto by_columns = [](const std::vector<Stage>& rows, float timeMode, bool reverse) {
        auto e = std::make_shared<Envelope>();
        e->set_setting("timeMode", timeMode);
        for (int k = 0; k < 8; ++k) {
            const int c = reverse ? 7 - k : k;
            std::vector<float> col;
            for (const Stage& s : rows) col.push_back(stage_field(s, c));
            e->set_array(kStageColumns[c], col);
        }
        return e;
    };
    auto note = [](Envelope& e, int n) {
        e.prepare(RenderContext{48000});
        e.start(n);
        return render_samples(e, n);
    };

    // (1) The twin for the stage layout: add_stage and set_array lay out the
    // same stages and render the same samples.
    std::vector<std::pair<std::vector<Stage>, float>> lists;
    for (float a : {0.0f, 0.15f, 0.4f})
        for (float k : {0.0f, 0.3f, 1.0f}) {
            lists.push_back({rows_of(*Envelope::make_ar(a, k * 0.1f, 1.0f - k * 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_as(a, k + 0.2f, k > 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_asr(a, k + 0.2f, 0.3f - a * 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_ads(a, k * 0.2f, k)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_adr(a, k * 0.2f, k)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_adsr(a, k * 0.2f, k, 0.3f - a * 0.5f)), 0.0f});
            lists.push_back({rows_of(*Envelope::make_adsr_abs(a * 0.1f, k * 0.05f, k, 0.02f)), 1.0f});
        }
    lists.push_back({{{{0.0f, 1.0f, L, 0.0f}, 0.1f, 0.0f, 0.5f},
                      {{1.0f, 0.4f, E, 2.0f}, 0.15f, 0.01f, 0.2f},
                      {{0.4f, 0.9f, I, 2.0f}, 0.1f, 0.0f, 0.0f},
                      {{0.9f, 0.5f, S, 0.0f}, 0.0f, 0.0f, 0.0f},
                      {{0.5f, 0.1f, S, 3.0f}, 0.2f, 0.0f, 1.0f},
                      {{0.1f, 0.7f, H, 0.0f}, 0.1f, 0.0f, 0.0f, 0.05f}}, 0.0f});
    lists.push_back({{{{-0.5f, 1.5f, E, 0.5f}, 0.05f, 0.0f, 0.0f}, {{1.5f, -0.25f, I, 3.0f}, 0.0f, 0.0f, 0.0f},
                      {{-0.25f, 0.0f, H, 0.0f}, 0.03f, 0.0f, 0.0f, 0.2f}}, 1.0f});
    lists.push_back({{}, 0.0f});
    lists.push_back({{Stage{}, Stage{}, Stage{}}, 0.0f});
    for (const auto& [rows, timeMode] : lists) {
        auto a = by_add(rows, timeMode);
        auto b = by_columns(rows, timeMode, false);
        auto c = by_columns(rows, timeMode, true);
        CHECK(same_layout(*a, *b) && same_layout(*a, *c));
        CHECK(a->stage_count() == int(rows.size()));
        CHECK(a->absolute_time() == b->absolute_time() && a->absolute_time() == c->absolute_time());
        const auto ra = note(*a, 4800), rb = note(*b, 4800), rc = note(*c, 4800);
        CHECK(same_bits(ra, rb) && same_bits(ra, rc));
    }
    // A factory and the add_stage build of its rows render alike (the factory
    // writes its columns by the same door).
    {
        auto f = Envelope::make_adsr_abs(0.01f, 0.05f, 0.6f, 0.02f);
        auto a = by_add(rows_of(*f), 1.0f);
        CHECK(same_layout(*f, *a) && same_bits(note(*f, 4800), note(*a, 4800)));
    }

    // (2) The columns follow every door.
    {
        auto e = Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f);
        CHECK(columns_match_layout(*e));
        e->add_stage({{0.0f, 0.3f, E, 1.5f}, 0.1f, 0.01f, 0.4f, 0.02f});
        CHECK(e->stage_count() == 5 && columns_match_layout(*e));
        e->remove_stage(4);
        CHECK(e->stage_count() == 4 && columns_match_layout(*e));
        e->remove_stage(0);
        CHECK(e->stage_count() == 3 && columns_match_layout(*e));
        e->remove_stage(7);                       // out of range: nothing moves
        CHECK(e->stage_count() == 3 && columns_match_layout(*e));
        e->set_curve(1, I, 2.5f);
        CHECK(e->stage(1).ramp.type == I && e->stage(1).ramp.power == 2.5f && columns_match_layout(*e));
        e->set_curve(9, S, 1.0f);                 // out of range: nothing moves
        CHECK(columns_match_layout(*e));
        e->replace_stages(std::move(*Envelope::make_asr(0.1f, 0.5f, 0.2f)));
        CHECK(e->stage_count() == 3 && columns_match_layout(*e));
        e->replace_stages(std::move(*Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f)));
        e->set_setting("sustainLevel", 0.35f);
        CHECK(same_bits(e->stage(2).ramp.startVal, 0.35f) && columns_match_layout(*e));
        for (const auto& f : {Envelope::make_ar(0.2f), Envelope::make_as(0.3f), Envelope::make_asr(0.15f, 0.4f, 0.3f),
                              Envelope::make_ads(0.1f, 0.25f, 0.3f), Envelope::make_adr(0.1f, 0.2f, 0.55f),
                              Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f),
                              Envelope::make_adsr_abs(0.01f, 0.05f, 0.6f, 0.02f)})
            CHECK(columns_match_layout(*f));
        auto shaped = envelope_from_preset_json(J::parse(R"({"preset":"adsr","attackCurve":1,"attackPower":0.8,"releaseCurve":2,"releasePower":1.5})"));
        CHECK(shaped->stage(0).ramp.type == E && shaped->stage(3).ramp.type == I && columns_match_layout(*shaped));
    }

    // (3) The order fact: sustainLevel set before the columns leaves the
    // table as written; set after, it rewrites the four slots.
    {
        const auto slots09 = rows_of(*Envelope::make_adsr(0.1f, 0.2f, 0.9f, 0.25f));
        auto slots_are = [](const Envelope& e, float v) {
            return same_bits(e.stage(1).ramp.endVal, v) && same_bits(e.stage(2).ramp.startVal, v)
                && same_bits(e.stage(2).ramp.endVal, v) && same_bits(e.stage(3).ramp.startVal, v);
        };
        Envelope before;
        before.set_setting("sustainLevel", 0.3f);
        for (int c = 0; c < 8; ++c) {
            std::vector<float> col;
            for (const Stage& s : slots09) col.push_back(stage_field(s, c));
            before.set_array(kStageColumns[c], col);
        }
        CHECK(slots_are(before, 0.9f));
        CHECK(same_bits(before.get_setting("sustainLevel"), 0.9f));
        Envelope after;
        for (int c = 0; c < 8; ++c) {
            std::vector<float> col;
            for (const Stage& s : slots09) col.push_back(stage_field(s, c));
            after.set_array(kStageColumns[c], col);
        }
        after.set_setting("sustainLevel", 0.3f);
        CHECK(slots_are(after, 0.3f) && columns_match_layout(after));
        CHECK(after.get_array("endVal")[1] == 0.3f && after.get_array("startVal")[2] == 0.3f
              && after.get_array("endVal")[2] == 0.3f && after.get_array("startVal")[3] == 0.3f);
        auto three = by_columns(rows_of(*Envelope::make_asr(0.15f, 0.4f, 0.3f)), 0.0f, false);
        const auto threeRows = rows_of(*three);
        three->set_setting("sustainLevel", 0.3f);
        CHECK(same_layout(*three, *by_add(threeRows, 0.0f)) && columns_match_layout(*three));
    }

    // (4) timeMode is the one state behind absolute_time().
    {
        Envelope e;
        CHECK(!e.absolute_time());
        e.set_setting("timeMode", 1.0f);
        CHECK(e.absolute_time());
        e.set_setting("timeMode", 0.0f);
        CHECK(!e.absolute_time());
        auto abs = Envelope::make_adsr_abs(0.01f, 0.05f, 0.6f, 0.02f);
        CHECK(abs->absolute_time() && abs->get_setting("timeMode") == 1.0f);
        CHECK(!Envelope::make_adsr(0.1f, 0.2f, 0.6f, 0.25f)->absolute_time());
    }

    // (5) The mapping applies always: with nothing wired 0 + (1 - 0) * raw is
    // raw, bit for bit, but for a raw -0.0, which leaves as +0.0.
    {
        auto hold_at = [&](float v, bool wired) {
            Envelope e;
            e.add_stage({{v, v, H, 0.0f}, 0.0f, 0.0f, 0.0f});
            if (wired) {
                e.set_param("minValue", std::make_shared<ConstantSource>(0.25f));
                e.set_param("maxValue", std::make_shared<ConstantSource>(0.75f));
            }
            e.prepare(RenderContext{48000});
            e.start(4800);
            return e.next();
        };
        const float denormal = std::numeric_limits<float>::denorm_min() * 7.0f;
        for (float v : {0.0f, 1.0f, 0.5f, -0.3f, -1.0f, -250.0f, 1.25f, 2.6f, 40000.0f, denormal, -denormal,
                        0.1f, 0.7f, 1e-30f})
        {
            CHECK(same_bits(hold_at(v, false), v));
            const float lo = 0.25f, hi = 0.75f;
            CHECK(same_bits(hold_at(v, true), lo + (hi - lo) * v));
        }
        const float negZero = hold_at(-0.0f, false);
        CHECK(negZero == 0.0f && !std::signbit(negZero));
    }

    // (6) Both adsr factories have the four-slot shape by the invariant alone.
    for (float s : {0.0f, 0.3f, 1.0f, 2.6f})
        for (float d : {0.0f, 0.2f})
            for (float r : {0.0f, 0.25f})
                for (const auto& e : {Envelope::make_adsr(0.1f, d, s, r), Envelope::make_adsr_abs(0.01f, d * 0.1f, s, r * 0.1f)}) {
                    CHECK(e->has_adsr_shape_());
                    CHECK(same_bits(e->get_setting("sustainLevel"), s));
                    e->set_setting("sustainLevel", 0.45f);
                    CHECK(same_bits(e->get_setting("sustainLevel"), 0.45f));
                    CHECK(same_bits(e->stage(1).ramp.endVal, 0.45f) && same_bits(e->stage(2).ramp.startVal, 0.45f)
                          && same_bits(e->stage(2).ramp.endVal, 0.45f) && same_bits(e->stage(3).ramp.startVal, 0.45f));
                    CHECK(columns_match_layout(*e));
                }

    // (7) The load: the table as columns; labels exactly (another case
    // refused) and indices; the refusals; an empty table; timeMode on either form.
    {
        auto patch = [](const std::string& envParams) {
            return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
                {"id":"Env","type":"Envelope","params":{)") + envParams + R"(}},
                {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"Env"}}}]}})";
        };
        auto env_of = [](const std::string& json) {
            auto ip = load_inst(json);
            auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            return std::dynamic_pointer_cast<Envelope>(pi->pool().voice(0).nodes().at("Env"));
        };
        auto refusal = [](const std::string& json) {
            return load_refused(json);
        };
        const std::string table = R"("startVal":[0.0,1.0,0.2],"endVal":[1.0,0.2,0.0],"power":[2.0,0.0,0.0],)"
                                  R"("type":["Expo","Sine","Hold"],"percent":[0.3,0.0,0.2],"minSec":[0.0,0.0,0.0],)"
                                  R"("maxSec":[1.0,0.0,0.0],"nominal":[0.0,0.0,0.1])";
        const auto fromTable = env_of(patch(table));
        const std::string cased = refusal(patch(R"("startVal":[0.0,1.0,0.2],"endVal":[1.0,0.2,0.0],"power":[2.0,0.0,0.0],)"
                                                R"("type":["expo","SINE","Hold"],"percent":[0.3,0.0,0.2],"minSec":[0.0,0.0,0.0],)"
                                                R"("maxSec":[1.0,0.0,0.0],"nominal":[0.0,0.0,0.1])"));
        CHECK(cased.find(R"('type' holds "expo", which is not one of its labels)") != std::string::npos);
        CHECK(fromTable && fromTable->stage_count() == 3);
        CHECK(fromTable->stage(0).ramp.type == E && fromTable->stage(1).ramp.type == S && fromTable->stage(2).ramp.type == H);
        const std::string indices = R"("startVal":[0.0,1.0,0.2],"endVal":[1.0,0.2,0.0],"power":[2.0,0.0,0.0],)"
                                    R"("type":[1,3,4],"percent":[0.3,0.0,0.2],"minSec":[0.0,0.0,0.0],)"
                                    R"("maxSec":[1.0,0.0,0.0],"nominal":[0.0,0.0,0.1])";
        CHECK(same_layout(*fromTable, *env_of(patch(indices))));

        const std::string unknown = refusal(patch(R"("startVal":[0.0],"endVal":[1.0],"power":[0.0],"type":["Squiggle"],)"
                                                  R"("percent":[0.0],"minSec":[0.0],"maxSec":[0.0],"nominal":[0.0])"));
        CHECK(unknown.find("node 'Env' (Envelope): 'type' holds \"Squiggle\"") != std::string::npos);
        const std::string presetAndColumn = refusal(patch(R"("preset":"ar","type":["Linear"])"));
        CHECK(presetAndColumn.find("node 'Env' (Envelope): holds both 'preset' and 'type'") != std::string::npos);
        const std::string ragged = refusal(patch(R"("startVal":[0.0],"endVal":[1.0],"power":[0.0],"type":["Linear"],)"
                                                 R"("percent":[0.0],"minSec":[0.0],"maxSec":[0.0])"));
        CHECK(ragged.find("node 'Env' (Envelope): table 'stages' takes its columns at one length, all present or all absent")
              != std::string::npos);
        CHECK(ragged.find("nominal absent") != std::string::npos);

        const auto empty = env_of(patch(R"("startVal":[],"endVal":[],"power":[],"type":[],"percent":[],"minSec":[],"maxSec":[],"nominal":[])"));
        CHECK(empty && empty->stage_count() == 0);
        CHECK(env_of(patch(table + R"(,"timeMode":"seconds")"))->absolute_time());
        CHECK(!env_of(patch(table))->absolute_time());
        CHECK(env_of(patch(R"("preset":"adsr","timeMode":"seconds")"))->absolute_time());

        // The order of a load: the settings pass runs before the array pass,
        // so a sustainLevel beside a four-slot table leaves the table as the
        // file wrote it.
        const std::string slots09 = R"("startVal":[0.0,1.0,0.9,0.9],"endVal":[1.0,0.9,0.9,0.0],"power":[0.0,0.0,0.0,0.0],)"
                                    R"("type":["Linear","Linear","Linear","Sine"],"percent":[0.2,0.1,0.0,0.2],)"
                                    R"("minSec":[0.0,0.0,0.0,0.0],"maxSec":[1.0,0.5,0.0,0.0],"nominal":[0.0,0.0,0.0,0.0],)"
                                    R"("sustainLevel":0.3)";
        const auto ordered = env_of(patch(slots09));
        const std::vector<float> start = ordered->get_array("startVal"), end = ordered->get_array("endVal");
        CHECK(start.size() == 4 && end.size() == 4);
        if (start.size() == 4 && end.size() == 4)
            CHECK(end[1] == 0.9f && start[2] == 0.9f && end[2] == 0.9f && start[3] == 0.9f);
        CHECK(ordered->get_setting("sustainLevel") == 0.9f);
    }

    // (8) A rebuild sizes the layout by the shortest column and never writes
    // a column: startVal at three cells and endVal at two leave no stage
    // while the default's six other columns are empty, and two stages once
    // those six hold two cells each; neither startVal nor endVal is resized.
    {
        Envelope e;
        e.set_array("startVal", {0.0f, 1.0f, 2.0f});
        e.set_array("endVal", {1.0f, 0.0f});
        CHECK(e.stage_count() == 0);
        for (const char* c : {"power", "type", "percent", "minSec", "maxSec", "nominal"})
            e.set_array(c, {0.0f, 0.0f});
        CHECK(e.stage_count() == 2);
        CHECK(e.get_array("startVal").size() == 3);
        CHECK(e.get_array("endVal").size() == 2);
    }

    // (9) A table set shorter under a sounding note keeps the walk inside the
    // layout. The note is in its last stage (the adsr's release, from 0.8)
    // when the table becomes two Sine rows between -1 and -3, which stay in
    // their range at any position; past the last stage the walk reads 0. So
    // every value must be finite and in [-3, 0]. What this can see: a walk
    // that reads a stage past the layout through the cells the shrink left in
    // the vectors' capacity, the old release's positive values. What it
    // cannot: a read past the end whose value happens to fall in the range.
    {
        auto e = Envelope::make_adsr(0.1f, 0.1f, 0.8f, 0.2f);
        e->prepare(RenderContext{48000});
        e->start(4800);
        for (int i = 0; i < 3500; ++i) e->next();
        e->set_array("startVal", {-1.0f, -2.0f});
        e->set_array("endVal", {-2.0f, -3.0f});
        e->set_array("power", {0.0f, 0.0f});
        e->set_array("type", {float(int(S)), float(int(S))});
        e->set_array("percent", {0.2f, 0.2f});
        e->set_array("minSec", {0.0f, 0.0f});
        e->set_array("maxSec", {0.0f, 0.0f});
        e->set_array("nominal", {0.0f, 0.0f});
        CHECK(e->stage_count() == 2);
        int outside = 0;
        for (int i = 3500; i < 4800; ++i) {
            const float v = e->next();
            if (!std::isfinite(v) || v < -3.0f || v > 0.0f) ++outside;
        }
        if (outside) std::printf("  shorter table under a note: %d of 1300 values outside [-3, 0]\n", outside);
        CHECK(outside == 0);
    }
}

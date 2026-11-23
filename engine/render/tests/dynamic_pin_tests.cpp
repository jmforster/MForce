#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/table_rule.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/render/fault_report.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/formant_sequence.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/modulator/curve_node.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/register_all_sources.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;


// A dynamic pin: a setting the patch drives once per note, here an envelope's
// timeScale through a CurveNode reading the note's frequency. The setting takes
// the curve's value at the note's frequency at Setup; a held line's
// continuation leaves it at the line's first note; and the curve, which nothing
// but the binding reaches, reports no fault after a note.
static std::string dynamic_pin_patch_json(int polyphony) {
    return R"({"sampleRate":48000,"instrument":{"polyphony":)" + std::to_string(polyphony)
         + R"(,"sustaining":true,"glideMs":15.0},"graph":{"output":"sine1","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"ts","type":"CurveNode","params":{"source":{"ref":"pf"},"interp":"logx","x":[100.0,1000.0],"y":[0.5,2.0]}},
      {"id":"env1","type":"Envelope","params":{"startVal":[0.0,1.0,0.7,0.7],"endVal":[1.0,0.7,0.7,0.0],
        "power":[0.0,0.0,0.0,0.0],"type":["Linear","Linear","Linear","Linear"],"percent":[0.05,0.1,0.0,0.25],
        "minSec":[0.0,0.0,0.0,0.0],"maxSec":[0.0,0.0,0.0,0.0],"nominal":[0.0,0.0,0.0,0.0]},
       "dynamicPins":{"timeScale":{"ref":"ts"}}},
      {"id":"sine1","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"env1"},"phase":0.0}}]}})";
}

void run_dynamic_pin_tests() {
    std::printf("dynamic pins: the note's value at Setup, held across a continuation, no fault\n");
    CurveNode curve; curve.interp.value = int(CurveNode::CurveInterp::LogX);
    curve.set_knots(CurveKnots{{100.0f, 0.5f}, {1000.0f, 2.0f}});
    const float at60 = curve.map(note_to_freq(60.0f));
    const float at72 = curve.map(note_to_freq(72.0f));
    CHECK(std::fabs(at60 - at72) > 0.1f);              // the two notes give two values
    auto timescale = [](PitchedInstrument& pi, size_t voice = 0) {
        return pi.pool().voice(int(voice)).nodes().at("env1")->get_setting("timeScale");
    };
    // 1. Each fresh note sets the setting to the curve at its own frequency,
    //    on its own voice: with two voices, play_note's rotation hands the
    //    first fresh note voice 0 and the second voice 1.
    {
        auto ip = load_inst(dynamic_pin_patch_json(2));
        PitchedInstrument& pi = *ip.instrument;
        CHECK(pi.pool().size() == 2);
        play_notes(pi, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        CHECK_NEAR(timescale(pi, 0), at60, 1e-5f);
        play_notes(pi, {{.start = 1.0f, .duration = 0.4f, .noteNumber = 72.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        CHECK_NEAR(timescale(pi, 1), at72, 1e-5f);
        CHECK_NEAR(timescale(pi, 0), at60, 1e-5f);     // the first voice keeps its note's value
        // 3. Each voice's curve was stepped at Setup with that voice's context:
        //    no fault on any voice (collect_faults walks the whole pool).
        std::vector<FaultReport> faults;
        pi.collect_faults(faults);
        bool curveFault = false;
        for (const auto& f : faults)
            if (f.nodeId == "ts") { curveFault = true; std::printf("  ts faults: %u\n", unsigned(f.faults)); }
        CHECK(!curveFault);
    }
    // 2. A held line: the second note continues the first, and the setting
    //    keeps the first note's value.
    {
        auto ip = load_inst(dynamic_pin_patch_json(1));
        PitchedInstrument& pi = *ip.instrument;
        play_notes(pi, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                        {.start = 0.4f, .duration = 0.4f, .noteNumber = 72.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        CHECK(pi.rendered().size() == 1);            // one line: the second note continued the first
        CHECK_NEAR(timescale(pi), at60, 1e-5f);
    }
}

// One wiring door for every pin kind: a Param or Input takes a signal and
// refuses a part, a Slot takes its interface and refuses a signal; get_param
// serves the signal pins, get_part the slots. The registry hands out
// Components. A part named where a signal is expected fails the load naming
// the node, the pin and the kind; the voice keeps a list of its signal nodes.
struct Plug final : Component {   // a bare part: a Component that is no signal
    const char* type_name() const override { return "Plug"; }
};
void run_wiring_door_tests() {
    std::printf("the wiring door: one call for every pin kind\n");
    AdditiveSource add;
    CHECK(add.set_param("frequency", std::make_shared<ConstantSource>(220.0f)));   // a signal into a Param
    CHECK(!add.set_param("frequency", std::make_shared<Plug>()));                  // a part into a Param: refused
    CHECK(add.set_param("partials", std::make_shared<FullPartials>()));            // a part into its slot
    CHECK(!add.set_param("partials", std::make_shared<SineSource>()));             // a signal into a slot: refused
    CHECK(!add.set_param("partials", std::make_shared<Plug>()));                   // the wrong part: refused
    CHECK(add.get_part("frequency") == nullptr);                                   // a Param serves no part
    CHECK(add.get_param("nothing") == nullptr && add.get_part("nothing") == nullptr);
    CHECK(add.get_part("partials") != nullptr);
    CHECK(add.get_param("partials") == nullptr);                                   // a slot serves no signal
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    std::shared_ptr<Component> c = reg.create("SineSource");
    CHECK(std::dynamic_pointer_cast<ValueSource>(c) != nullptr);
    CHECK(std::dynamic_pointer_cast<ValueSource>(reg.create("FullPartials")) == nullptr);   // a part
    // a part where a signal is expected fails the load naming node, pin and kind
    const char* partAsSignal = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"fm","type":"FormantPeaks","params":{"frequency":[440.0],"gain":[1.0],"width":[300.0],"power":[2.0]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"fm"},"amplitude":0.5,"partials":{"ref":"parts"}}}]}})";
    CHECK(load_fails_naming(partAsSignal, "add"));
    CHECK(load_fails_naming(partAsSignal, "frequency"));
    CHECK(load_fails_naming(partAsSignal, "not a signal"));
    const char* additive = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    auto ip = load_inst(additive);
    const auto& vg = ip.instrument->pool().voice(0);
    CHECK(vg.signals().size() == 1 && vg.signals()[0].id == "add");   // the part is no signal
    CHECK(vg.nodes().count("parts") == 1 && vg.nodes().count("add") == 1);
}

// The formant family are parts: a FormantSequence holds its formants in a
// multi slot that clears and refills, set replaces and add appends, a signal
// is refused, no formant type is a ValueSource, and a formant edited on a
// loaded instrument reaches the next note through the owner's prepare.
void run_formant_part_tests() {
    std::printf("formants: parts in slots\n");
    {
        FormantSequence seq;
        auto f1 = make_one_peak(500.0f), f2 = make_one_peak(2000.0f);
        CHECK(seq.add_param("spectra", f1) && seq.add_param("spectra", f2));
        CHECK(seq.clear_param("spectra"));
        CHECK(seq.add_param("spectra", f2));
        seq.prepare(RenderContext{48000}); seq.start(4800); seq.next();
        CHECK(seq.gain(500.0f) == 0.0f && seq.gain(2000.0f) > 0.0f);                 // exactly the refilled set
        CHECK(!seq.add_param("spectra", std::make_shared<SineSource>()));             // a signal into the slot: refused
        CHECK(seq.set_param("spectra", f1));                                          // set replaces on a multi slot
        CHECK(seq.get_part("spectra") == f1);
        CHECK(seq.spectra.views.size() == 1);
        register_all_sources();
        for (const char* type : {"FormantPeaks", "FormantBands", "FormantSequence"})
            CHECK(std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type)) == nullptr);
    }
    // a peak's gain changed on a loaded instrument, then prepare: the next note hears it
    {
        const char* shaped = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
          {"id":"fm","type":"FormantPeaks","params":{"frequency":[440.0],"gain":[1.0],"width":[300.0],"power":[2.0]}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"fm"},"formantWeight":1.0}}]}})";
        auto ip = load_inst(shaped);
        const auto before = render_one_live_note(*ip.instrument, 60.0f);
        ip.instrument->pool().voice(0).nodes().at("fm")->set_array("gain", {0.0f});
        ip.instrument->prepare(RenderContext{48000});
        const auto after = render_one_live_note(*ip.instrument, 60.0f);
        CHECK(!same_bits(before, after));
    }
}

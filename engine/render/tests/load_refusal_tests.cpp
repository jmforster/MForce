#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/patch/retired_spellings.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/additive/sequence_partials.h"
#include <nlohmann/json.hpp>
#include <cstdio>
#include <exception>
#include <string>
#include <string_view>

using namespace mforce;


// A structural setting — one the node sizes or reshapes itself by: every int
// and bool, and a float the node declares so — takes no per-note push. The
// pin and its descriptor say so, and one changed after a note is honoured by
// the next prepare.
void run_structural_setting_tests() {
    std::printf("structural settings: the mark, and the rebuild at prepare\n");
    {
        FullPartials full;
        CHECK(full.structural_setting("maxPartials"));      // an int: structural by type
        CHECK(full.structural_setting("evenWeight1"));      // a float the node declares
        CHECK(!full.structural_setting("rolloff1"));        // a value setting
        CHECK(!full.structural_setting("noSuchSetting"));
        bool declared = false, plain = false;
        for (const auto& d : full.setting_descriptors()) {
            if (std::string_view(d.name) == "unitPO2")   declared = d.structural;
            if (std::string_view(d.name) == "decayRate") plain = !d.structural;
        }
        CHECK(declared);
        CHECK(plain);
        SequencePartials seq;
        CHECK(seq.structural_setting("incr1"));
        CHECK(seq.structural_setting("minMult2"));
        CHECK(!seq.structural_setting("rolloff2"));
    }
    // A shaping float set after a note (the UI's edit, then Play prepares
    // again) rebuilds the arrays at that prepare: the next note is the one a
    // patch loaded with the value renders.
    {
        const char* even = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        const char* odd = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4,"evenWeight1":0.0}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto edited = load_inst(even);
        const auto before = render_one_live_note(*edited.instrument, 60.0f);
        edited.instrument->pool().voice(0).nodes().at("parts")->set_setting("evenWeight1", 0.0f);
        edited.instrument->prepare(RenderContext{48000});
        const auto after = render_one_live_note(*edited.instrument, 60.0f);
        auto loaded = load_inst(odd);
        (void)render_one_live_note(*loaded.instrument, 60.0f);
        const auto asLoaded = render_one_live_note(*loaded.instrument, 60.0f);
        CHECK(!same_bits(before, after));
        CHECK(same_bits(after, asLoaded));
    }
}

void run_structural_push_refusal_tests() {
    std::printf("structural settings: the load refuses the wire\n");
    auto loads = [](const char* patch) {
        try { (void)load_inst(patch); return true; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    };
    // The wiring format's dynamic pin into a shaping float (refused), an int
    // (refused: structural by type) and a value float (taken).
    const char* pinShaping = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"SequencePartials","params":{"maxPartials":4},"dynamicPins":{"incr1":{"ref":"pf"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(load_fails_naming(pinShaping, "graph.nodes[1].dynamicPins.incr1: node 'parts' (SequencePartials)"));
    CHECK(load_fails_naming(pinShaping, "structural"));
    const char* pinCount = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4},"dynamicPins":{"maxPartials":{"ref":"pf"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(load_fails_naming(pinCount, "structural"));
    const char* pinValue = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4},"dynamicPins":{"decayRate":{"ref":"pf"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(loads(pinValue));
}

// An instrument block that carries a paramMap is refused by name, through the
// text entry point and the file entry point alike, and the message names the
// converter that rewrites it, in the one sentence kParamMapRefusal holds; an
// empty map and a null one are refused too. The same document without the key
// loads.
void run_param_map_refusal_tests() {
    std::printf("instrument.paramMap: refused by name, naming the converter\n");
    const char* withMap = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":{"frequency":"osc.frequency"}},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"amplitude":0.5}}]}})";
    const char* emptyMap = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":{}},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5}}]}})";
    const char* nullMap = R"({"sampleRate":48000,"instrument":{"polyphony":1,"paramMap":null},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5}}]}})";
    const char* without = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5}}]}})";
    CHECK(load_fails_naming(withMap, "paramMap"));
    CHECK(load_fails_naming(withMap, "tools/convert_patch.py"));
    CHECK(load_fails_naming(emptyMap, "paramMap"));
    CHECK(load_fails_naming(emptyMap, "tools/convert_patch.py"));
    CHECK(load_fails_naming(nullMap, "paramMap"));
    CHECK(load_fails_naming(nullMap, "tools/convert_patch.py"));
    {
        const std::string msg = load_refused(withMap);
        if (!refusal_says(msg, kParamMapRefusal)) std::printf("  paramMap: %s\n", msg.c_str());
        CHECK(refusal_says(msg, kParamMapRefusal));
    }
    {
        ScratchPatch file("renders/scratch/_param_map_refusal.json", withMap);
        std::string msg;
        try { (void)load_sound(file.path); }
        catch (const std::exception& e) { msg = e.what(); }
        CHECK(msg.find("paramMap") != std::string::npos);
        CHECK(msg.find("tools/convert_patch.py") != std::string::npos);
    }
    bool loaded = true;
    try { (void)load_inst(without); }
    catch (const std::exception& e) { loaded = false; std::printf("  load failed: %s\n", e.what()); }
    CHECK(loaded);
}

// A key of a table's row form — an Envelope's stages, a CurveNode's knots and
// exprKnots — fails the load through the text entry point and the file entry
// point alike, naming the node, the key and the converter: the message is the
// node's prefix and the sentence retired_key_refusal holds for the pair. The
// refusal is per type and key, and the table forms of the three load.
void run_retired_key_refusal_tests() {
    std::printf("retired keys: an Envelope's stages, a CurveNode's knots and exprKnots refused by name\n");
    auto patch = [](const std::string& node) {
        return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      )") + node + R"(,
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"n"}}}]}})";
    };
    auto text_refusal = [](const std::string& json) {
        return load_refused(json);
    };
    auto file_refusal = [](const char* path, const std::string& json) {
        ScratchPatch file(path, json);
        try { (void)load_sound(file.path); } catch (const std::exception& e) { return std::string(e.what()); }
        return std::string();
    };
    auto loads = [](const std::string& json) {
        try { (void)load_inst(json); return true; }
        catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); return false; }
    };

    struct Retired { const char* type; const char* key; const char* file; std::string node; };
    const Retired retired[] = {
        {"Envelope", "stages", "renders/scratch/_retired_stages.json",
         R"({"id":"n","type":"Envelope","params":{"stages":[{"startVal":0.0,"endVal":1.0,"type":"Linear","percent":0.5},)"
         R"({"startVal":1.0,"endVal":0.0,"type":"Linear","percent":0.5}]}})"},
        {"CurveNode", "knots", "renders/scratch/_retired_knots.json",
         R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"knots":[[100.0,0.2],[1000.0,0.8]]}})"},
        {"CurveNode", "exprKnots", "renders/scratch/_retired_expr_knots.json",
         R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"mode":"expressions",)"
         R"("exprKnots":[{"x":440.0,"form":"linear","a":0.001,"b":0.0}]}})"},
    };
    for (const auto& r : retired) {
        const std::string sentence = retired_key_refusal(r.type, r.key);
        const std::string want = std::string("node 'n' (") + r.type + "): " + sentence;
        for (const std::string& msg : {text_refusal(patch(r.node)), file_refusal(r.file, patch(r.node))}) {
            if (!refusal_says(msg, want)) std::printf("  %s.%s: got \"%s\"\n", r.type, r.key, msg.c_str());
            CHECK(msg.find(std::string("node 'n' (") + r.type + ")") != std::string::npos);
            CHECK(msg.find(std::string("'") + r.key + "'") != std::string::npos);
            CHECK(msg.find("no longer read") != std::string::npos);
            CHECK(msg.find("python tools/convert_patch.py") != std::string::npos);
            CHECK(msg.size() >= sentence.size() && msg.compare(msg.size() - sentence.size(), sentence.size(), sentence) == 0);
            CHECK(refusal_says(msg, want));
        }
    }
    // The sentence for each retired pair, and none for any other pair.
    CHECK(retired_key_refusal("Envelope", "stages")
          == "'stages' is no longer read: python tools/convert_patch.py <patch> rewrites it as the eight stage columns");
    CHECK(retired_key_refusal("CurveNode", "knots")
          == "'knots' is no longer read: python tools/convert_patch.py <patch> rewrites it as the columns x and y");
    CHECK(retired_key_refusal("CurveNode", "exprKnots")
          == "'exprKnots' is no longer read: python tools/convert_patch.py <patch> rewrites it as the columns exprX, exprForm, exprA and exprB");
    CHECK(retired_key_refusal("Envelope", "knots").empty());
    CHECK(retired_key_refusal("CurveNode", "stages").empty());
    CHECK(retired_key_refusal("Envelope", "startVal").empty());

    // The table forms beside them load.
    CHECK(loads(patch(R"({"id":"n","type":"Envelope","params":{"startVal":[0.0,1.0],"endVal":[1.0,0.0],"power":[0.0,0.0],)"
                      R"("type":["Linear","Linear"],"percent":[0.5,0.5],"minSec":[0.0,0.0],"maxSec":[0.0,0.0],"nominal":[0.0,0.0]}})")));
    CHECK(loads(patch(R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"x":[100.0,1000.0],"y":[0.2,0.8]}})")));
    CHECK(loads(patch(R"({"id":"n","type":"CurveNode","params":{"source":{"ref":"pf"},"mode":"expressions",)"
                      R"("exprX":[440.0],"exprForm":["linear"],"exprA":[0.001],"exprB":[0.0]}})")));

    // A stage list beside a preset gets the retired key's sentence, not the
    // two forms'; a preset beside a stage column gets the two forms'.
    const std::string listAndPreset = text_refusal(patch(
        R"({"id":"n","type":"Envelope","params":{"preset":"adsr","stages":[{"startVal":0.0,"endVal":1.0,"type":"Linear","percent":0.5}]}})"));
    CHECK(refusal_says(listAndPreset, "node 'n' (Envelope): " + retired_key_refusal("Envelope", "stages")));
    CHECK(listAndPreset.find("one form") == std::string::npos);
    const std::string presetAndColumn = text_refusal(patch(R"({"id":"n","type":"Envelope","params":{"preset":"ar","type":["Linear"]}})"));
    CHECK(presetAndColumn.find("node 'n' (Envelope): holds both 'preset' and 'type'; an Envelope takes one form: "
                               "the stage columns or a preset") != std::string::npos);
}

#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/core/pins.h"
#include "mforce/core/render_context.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/live_player.h"
#include "mforce/render/ring_out.h"
#include <nlohmann/json.hpp>
#include "mforce/testing/alloc_probe.h"
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;


// Allocation-free start() and next(): one note through the live player —
// note_on (the note's Setup and start on the control side) and fill in
// 256-frame buffers over the note and the fixed tail — armed from note-on to
// the last sample, must count nothing. Returns the count.
static std::size_t allocations_in_one_note(const char* patchJson, float noteNumber = 60.0f) {
    LivePlayer player(patch_door::read_text(patchJson), 1);
    std::vector<float> buf(256);
    const int most = int((0.1f + RingOut::kTailSeconds) * float(player.sample_rate())) / 256 + 1;
    alloc_probe::reset();
    alloc_probe::arm();
    (void)player.note_on({.start = 0.0f, .duration = 0.1f, .noteNumber = noteNumber, .velocity = 0.8f});
    for (int k = 0; k < most; ++k) player.fill(buf.data(), 256);
    alloc_probe::disarm();
    if (alloc_probe::count())
        std::printf("  [alloc] %zu allocation(s), %zu bytes, in one note of the patch beginning %.60s\n",
                    alloc_probe::count(), alloc_probe::bytes(), patchJson);
    return alloc_probe::count();
}

void run_allocation_tests_partials() {
    // Partials: a plain FullPartials, and one with every per-partial feature on
    // (decay, bandwidth noise, motion, shimmer, trade, onset spread, an expand
    // rule) so every array the Partials' start sizes is exercised; two pitches on
    // the pool's two voices.
    const char* additive = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":16}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(allocations_in_one_note(additive) == 0);
    CHECK(allocations_in_one_note(additive, 72.0f) == 0);
    const char* everything = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"add","nodes":[
      {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":1,"spacing1":0.3,"spacing2":0.3}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8,"decayRate":1.0,"bandwidthHz":30.0,
         "motionDepth1":10.0,"shimmerDepth1":0.2,"tradeDepth":0.2,"onsetSpread":0.1,"expandRule":{"ref":"rule"}}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
    CHECK(allocations_in_one_note(everything) == 0);
    CHECK(allocations_in_one_note(everything, 48.0f) == 0);
    // A second prepare after a note — the UI prepares every node again at
    // every Play — changes nothing a render can see: the expand rule starts
    // each note from the same originals (one voice, so the second note lands
    // on the voice the first note expanded).
    {
        const char* expanded = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}},
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4,"expandRule":{"ref":"rule"}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto once = load_inst(expanded);
        (void)render_one_live_note(*once.instrument, 60.0f);
        const auto second = render_one_live_note(*once.instrument, 60.0f);
        auto again = load_inst(expanded);
        (void)render_one_live_note(*again.instrument, 60.0f);
        again.instrument->prepare(RenderContext{48000});
        const auto secondAfterPrepare = render_one_live_note(*again.instrument, 60.0f);
        CHECK(same_bits(second, secondAfterPrepare));
    }
    // An ExplicitPartials row edited after the first prepare (the UI's path:
    // set_array, then Play prepares again) sounds on the next note with an
    // expand rule: the originals the rule starts from follow every rebuild.
    {
        const char* rows = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}},
          {"id":"parts","type":"ExplicitPartials","params":{"mult1":[1.0,2.0,3.0,4.0],"ampl1":[1.0,0.5,0.25,0.125],"mult2":[1.0,2.0,3.0,4.0],"ampl2":[1.0,0.5,0.25,0.125],"evolve":false,
             "expandRule":{"ref":"rule"}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        const char* editedRows = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
          {"id":"rule","type":"ExpandRule","params":{"count":1,"recurse":0,"spacing1":0.3,"spacing2":0.3}},
          {"id":"parts","type":"ExplicitPartials","params":{"mult1":[1.0,2.0,3.0,4.0],"ampl1":[1.0,0.1,0.1,0.1],"mult2":[1.0,2.0,3.0,4.0],"ampl2":[1.0,0.1,0.1,0.1],"evolve":false,
             "expandRule":{"ref":"rule"}}},
          {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}}]}})";
        auto edited = load_inst(rows);
        (void)render_one_live_note(*edited.instrument, 60.0f);
        edited.instrument->pool().voice(0).nodes().at("parts")->set_array("ampl1", {1.0f, 0.1f, 0.1f, 0.1f});
        edited.instrument->prepare(RenderContext{48000});
        const auto afterEdit = render_one_live_note(*edited.instrument, 60.0f);
        auto loaded = load_inst(editedRows);
        (void)render_one_live_note(*loaded.instrument, 60.0f);
        const auto asLoaded = render_one_live_note(*loaded.instrument, 60.0f);
        CHECK(same_bits(afterEdit, asLoaded));
    }
}

// The frequency-dependent tables and their evolutions.
void run_allocation_tests_tables() {
    // WavetableSource: the table is reserved at prepare for the floor frequency
    // (kMinTableHz); a note at the floor, below it, and at the pin's maximum
    // fit, and each evolution's buffers are reserved with it.
    const char* wavetable = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5}}]}})";
    CHECK(allocations_in_one_note(wavetable) == 0);
    CHECK(allocations_in_one_note(wavetable, 15.49f) == 0);     // 20 Hz: the floor's length
    CHECK(allocations_in_one_note(wavetable, 0.0f) == 0);       // 8 Hz: below the floor, the floor's length
    CHECK(allocations_in_one_note(wavetable, 135.08f) == 0);    // 20 kHz: the pin's maximum, a two-sample table
    const auto withEvolution = [](const char* evolutionNodes) {
        return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},)")
             + evolutionNodes
             + R"(,{"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"evolution":{"ref":"ev"}}}]}})";
    };
    const char* evolutions[] = {
        R"({"id":"ev","type":"HistogramEqualizeEvolution","params":{"bins":64,"rate":5e-05}})",
        R"({"id":"ev","type":"ReactionDiffusionEvolution","params":{"feed":0.0367,"kill":0.0649,"diffU":0.16,"diffV":0.08,"dt":1.0,"subSteps":1}})",
        R"({"id":"ev","type":"CellularAutomatonEvolution","params":{"rule":110,"threshold":0.0,"levelHi":0.6,"levelLo":-0.6,"stepEvery":12}})",
        R"({"id":"ev","type":"BezierPullEvolution","params":{"p0":-1.0,"p1":0.8,"p2":-0.8,"p3":1.0,"rate":5e-05}})",
        R"({"id":"ev","type":"SortErosionEvolution","params":{"swapsPerSample":4,"descending":0}})",
        R"({"id":"bow","type":"RedNoiseSource","params":{"amplitude":1.0,"frequency":350.0,"density":0.5,"smoothness":0.5,"rampVariation":0.2}},
      {"id":"ev","type":"BowedStringEvolution","params":{"bow":{"ref":"bow"},"bowPosition":0.14,"bowSpeed":0.3,"frictionGain":4.0,"tubeLoss":0.005}})",
        R"({"id":"ev","type":"BrassEvolution","params":{"brassiness":1.5,"breath":0.8,"tubeLoss":0.01,"lipTension":0.92,"lipFreqRatio":1.2,"lipQ":3.0}})",
        R"({"id":"ev","type":"ReedEvolution","params":{"breath":0.5,"reedStiffness":1.8,"tubeLoss":0.01,"loopFilter":0.35}})",
        R"({"id":"ev","type":"EKSEvolution","params":{"pickPosition":0.5,"pickDirection":0.3,"stiffness":0.1,"decayStretch":0.5,"drumBlend":1.0}})",
    };
    for (const char* ev : evolutions) {
        const std::string json = withEvolution(ev);
        CHECK(allocations_in_one_note(json.c_str()) == 0);
        CHECK(allocations_in_one_note(json.c_str(), 36.0f) == 0);   // a second, lower pitch on the other voice
    }
    // The pluck under a wavetable, its seed given on the node.
    const char* pluck = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"ev","type":"PluckEvolution","params":{"muting":0.0,"seed":42}},
      {"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"evolution":{"ref":"ev"}}}]}})";
    CHECK(allocations_in_one_note(pluck) == 0);
    // The morph target: synthesised at prepare, resampled per note within the reserve.
    const char* target = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"src","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"ev","type":"TargetEvolution","params":{"morphRate":0.0005,"holdCycles":5,"decayFactor":0.999,"targetPartials":[1.0,0.5,0.33,0.25],"targetLength":2048}},
      {"id":"src","type":"WavetableSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"evolution":{"ref":"ev"}}}]}})";
    CHECK(allocations_in_one_note(target) == 0);
    CHECK(allocations_in_one_note(target, 36.0f) == 0);
}

// The remaining families: the hybrid table, the stage, segment and basic
// additive nodes, a Multiplex voice whose clones read the voice's pitch.
void run_allocation_tests_nodes() {
    // HybridKSSource: the table is reserved at prepare for the floor, the
    // partial arrays sized with the targets; a short hold so the transition
    // (the DFT) happens inside the note's life.
    const char* hybrid = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"hks","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"hks","type":"HybridKSSource","params":{"seed":12345,"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"holdCycles":2,"morphDuration":0.05,"numPartials":30,
         "targetPartials":[0.4,1.0,0.75,0.45,0.28,0.15,0.1,0.05,0.02,0.01]}}]}})";
    CHECK(allocations_in_one_note(hybrid) == 0);
    CHECK(allocations_in_one_note(hybrid, 15.49f) == 0);    // the floor
    CHECK(allocations_in_one_note(hybrid, 36.0f) == 0);     // a second pitch on the other voice

    // PhasedValueSource: the stage counters and boundaries are sized at prepare.
    const char* phased = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"phased","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"saw","type":"SawSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":1.0}},
      {"id":"pulse","type":"PulseSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":1.0,"dutyCycle":0.3,"bend":0.4}},
      {"id":"sin","type":"SineSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":1.0}},
      {"id":"phased","type":"PhasedValueSource","params":{"overlap":0.15,"amplitude":0.8,
        "sources":[{"ref":"saw"},{"ref":"pulse"},{"ref":"sin"}],
        "percent":[0.33,0.33,0.0],"minSec":[0.5,0.5,0.0],"maxSec":[3.0,3.0,0.0],"gain":[1.0,1.0,1.0]}}]}})";
    CHECK(allocations_in_one_note(phased) == 0);
    CHECK(allocations_in_one_note(phased, 48.0f) == 0);
    // SegmentSource: the segment list is reserved at prepare for the shape's size.
    const char* segment = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"seg","nodes":[
      {"id":"seg","type":"SegmentSource","params":{"values":[8.0,0.65,8.0,-0.54,8.0,0.74,9.0,0.2],"oneShot":false,"seed":42,"smoothness":0.5,"gap":0.0}}]}})";
    CHECK(allocations_in_one_note(segment) == 0);
    CHECK(allocations_in_one_note(segment, 48.0f) == 0);

    // A note on a Multiplex voice whose clones read the voice's pitch (each
    // clone's Note node wired to Var1.val) allocates nothing.
    const char* multiplex = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"Multiplex1","nodes":[
      {"id":"__perf_freq","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"Var1","type":"VarSource","params":{"absolute":true,"val":{"ref":"__perf_freq"},"varPct":0.0}},
      {"id":"Triangle1","type":"TriangleSource","params":{"amplitude":0.5,"frequency":{"ref":"Var1"}}},
      {"id":"Multiplex1","type":"MultiplexSource","params":{"count":4,"source":{"ref":"Triangle1"}}}]}})";
    CHECK(allocations_in_one_note(multiplex) == 0);
    CHECK(allocations_in_one_note(multiplex, 48.0f) == 0);
}

// The paths no library or baselines patch drives: a repetition boundary, a
// held note's release, a drum hit.
void run_allocation_tests_pins() {
    // A RepeatingSource boundary inside next() restarts its child — a
    // Partials-bearing one — and allocates nothing; the note's life holds
    // several repetitions.
    const char* repeating = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"rep","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"parts","type":"FullPartials","params":{"maxPartials":8}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":0.5,"partials":{"ref":"parts"},"formantWeight":0.0}},
      {"id":"rep","type":"RepeatingSource","params":{"seed":77,"source":{"ref":"add"},"duration":0.05,"gapDuration":0.01}}]}})";
    CHECK(allocations_in_one_note(repeating) == 0);

    // A held note's release — the envelopes gated at note-on, released
    // mid-life with the releasing note's length as the reference, the way a
    // line's end does — re-lays the release stage in place and allocates
    // nothing.
    {
        const char* held = R"({"sampleRate":48000,"instrument":{"polyphony":2,"sustaining":true},"graph":{"output":"osc","nodes":[{"id":"__perf_frequency","type":"PerformNode","params":{"field":"frequency"}},
          {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.05,"decay":0.1,"sustainLevel":0.8,"release":0.2}},
          {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"__perf_frequency"},"amplitude":{"ref":"env"}}}]}})";
        CHECK(load_inst(held).instrument->pool().voice(0).can_hold());
        LivePlayer player(patch_door::read_text(held), 1);
        std::vector<float> buf(256);
        alloc_probe::reset();
        alloc_probe::arm();
        const LivePlayer::NoteId id = player.note_on({.start = 0.0f, .duration = 0.5f, .noteNumber = 60.0f,
                                                      .velocity = 0.8f, .hold = true});
        for (int k = 0; k < 47; ++k) player.fill(buf.data(), 256);   // half the note
        player.note_off(id);                                          // released against that length
        for (int k = 0; k < 2000 && player.sounding() > 0; ++k) player.fill(buf.data(), 256);
        alloc_probe::disarm();
        CHECK(alloc_probe::count() == 0 && player.sounding() == 0);
    }

    // The drum kit has no library or baselines patch: one hit's engine path
    // is its drum's start and one next per sample on a clock of the drum's
    // own, as the kit pulls it. The kit adds only its pre-render buffer, the
    // offline host's, outside the arm.
    {
        const char* drum = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
          {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.001,"decay":0.1,"sustainLevel":0.0,"release":0.05}},
          {"id":"osc","type":"SineSource","params":{"frequency":180.0,"amplitude":{"ref":"env"}}}]}})";
        auto ip = load_inst(drum);
        const auto drumSource = output_of(ip.instrument->pool().voice(0));
        SampleClock clock;
        drumSource->prepare(RenderContext{48000}.with_clock(&clock));
        alloc_probe::reset();
        alloc_probe::arm();
        drumSource->start(4800);
        for (int i = 0; i < 4800; ++i) { clock.tick(); (void)drumSource->next(); }
        alloc_probe::disarm();
        CHECK(alloc_probe::count() == 0);
    }
}

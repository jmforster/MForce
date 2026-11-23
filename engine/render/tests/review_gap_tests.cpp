#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/equal_temperament.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/patch.h"
#include "mforce/render/perform_source.h"
#include "mforce/render/voice.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/formant_bands.h"
#include "mforce/source/additive/formant_sequence.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/register_all_sources.h"
#include <cstdio>
#include <memory>

using namespace mforce;


// Load refusals: a refused slot wire says what the slot takes; an alias key
// no reader takes is refused by name; a part in every signal position fails
// the load with its path's message.
void run_review_gap_tests() {
    std::printf("the review's gaps: messages, the alias, the output positions\n");
    // a signal into a slot: the refusal names what the slot takes
    const char* sineIntoPartials = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"osc"}}}]}})";
    CHECK(load_fails_naming(sineIntoPartials, "does not accept a SineSource"));
    CHECK(load_fails_naming(sineIntoPartials, "takes a PartialSet part"));
    // a deleted additive type fails the load by name, with the hint
    const char* deletedType = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"as2","nodes":[
      {"id":"as2","type":"AdditiveSource2","params":{"frequency":220.0}}]}})";
    CHECK(load_fails_naming(deletedType, "Unknown node type: AdditiveSource2"));
    CHECK(load_fails_naming(deletedType, "author an AdditiveSource"));
    // a FormantSequence keyed "formants" (an alias no reader takes) is refused by name
    const char* seqAlias = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"add","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
      {"id":"spec","type":"FormantPeaks","params":{"frequency":[500.0],"gain":[1.0],"width":[500.0],"power":[2.0]}},
      {"id":"seq","type":"FormantSequence","params":{"blend":0.5,"formants":[{"ref":"spec"}]}},
      {"id":"add","type":"AdditiveSource","params":{"frequency":220.0,"amplitude":0.5,"partials":{"ref":"parts"},"formant":{"ref":"seq"},"formantWeight":1.0}}]}})";
    CHECK(load_fails_naming(seqAlias, "node 'seq' (FormantSequence): unknown key 'formants'"));
    // a part as the instrument's output, as a mixer-mode output, as a channel's source
    const char* partAsVoiceOutput = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"parts","nodes":[
      {"id":"parts","type":"FullPartials","params":{"maxPartials":4}}]}})";
    CHECK(load_fails_naming(partAsVoiceOutput, "not a signal"));
    CHECK(file_fails_naming("renders/scratch/_part_as_output.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"parts","nodes":[{"id":"parts","type":"FullPartials","params":{"maxPartials":4}}]}})",
        "'parts' is a FullPartials, a part, not a signal"));
    CHECK(file_fails_naming("renders/scratch/_part_as_channel.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"parts"},"volume":1.0,"pan":0.0}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})",
        "node 'ch1' (SoundChannel): 'source' does not accept a FullPartials, a part, not a signal"));
    // a channel as the output, and a channel whose source names no node
    CHECK(file_fails_naming("renders/scratch/_channel_as_output.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"ch1","nodes":[
          {"id":"osc","type":"SineSource","params":{"frequency":220.0}},
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"osc"},"volume":1.0,"pan":0.0}}]}})",
        "'ch1' is a SoundChannel, a part, not a signal"));
    CHECK(file_fails_naming("renders/scratch/_channel_source_missing.json",
        R"({"sampleRate":48000,"seconds":0.1,"graph":{"output":"mix","nodes":[
          {"id":"ch1","type":"SoundChannel","params":{"source":{"ref":"nowhere"},"volume":1.0,"pan":0.0}},
          {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch1"}],"gainL":1.0,"gainR":1.0}}]}})",
        "node 'ch1' (SoundChannel): 'source' reads 'nowhere', which names no node"));
}

// Lifecycle: parts nested three deep are prepared and
// started through their owners; the live note-on's reseed reaches a signal.
void run_review_gap_lifecycle_tests() {
    std::printf("the review's gaps: nesting, the live reseed\n");
    // parts three deep: the owner's prepare, start and next reach the innermost pins
    {
        RenderContext ctx{48000};
        AdditiveSource f;
        auto fm = std::make_shared<FormantBands>();
        auto freq = std::make_shared<PinCounter>();
        fm->set_param("startFreq", freq);
        auto inner = std::make_shared<FormantSequence>();                     // a sequence is a Formant: it nests
        CHECK(inner->add_param("spectra", fm));
        auto seq = std::make_shared<FormantSequence>();
        CHECK(seq->add_param("spectra", inner));
        CHECK(f.set_param("partials", std::make_shared<FullPartials>()));
        CHECK(f.set_param("formant", seq));
        f.set_param("formantWeight", std::make_shared<ConstantSource>(1.0f));
        f.prepare(ctx); f.start(4800);
        for (int i = 0; i < 3; ++i) f.next();
        CHECK(freq->rate == 48000 && freq->starts == 1 && freq->nexts == 3);
    }
    // a carried note's reseed reaches the voice's signals
    {
        register_all_sources();
        auto& reg = SourceRegistry::instance();
        if (!reg.has("ReseedProbe"))
            reg.register_type("ReseedProbe", {.family = Family::Utility, .shelf = Shelf::Experimental}, [](auto) { return std::make_shared<ReseedProbe>(); });
        const char* probed = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"p","nodes":[
          {"id":"parts","type":"FullPartials","params":{"maxPartials":4}},
          {"id":"p","type":"ReseedProbe"}]}})";
        const Patch patch = patch_door::read_text(probed);
        Voice voice(patch.graph, *patch.instrument, std::make_shared<InstrumentState>(), nullptr);
        voice.prepare(RenderContext{48000});
        voice.start({note_to_freq(60.0f), 0.8f, 4800, 0.1f, 0, nullptr}, false);
        voice.carry_on({note_to_freq(62.0f), 0.8f, 4800, 0.1f, 0, nullptr}, 261.6f, 0.0f);
        auto* probe = dynamic_cast<ReseedProbe*>(voice.nodes().at("p").get());
        CHECK(probe != nullptr && probe->reseeds == 1);
    }
}

#include "mforce/testing/check.h"
#include "mforce/core/component.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/additive/formant_peaks.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/source/output/sound_channel.h"
#include "mforce/source/output/stereo_mixer.h"
#include "mforce/source/register_all_sources.h"
#include <cstdio>
#include <memory>
#include <string>

using namespace mforce;


// The two output parts as declared: each pin with its default, and the
// wiring door — a mixer's channels take a SoundChannel and nothing else, a
// channel's source a signal and nothing else.
void run_output_part_tests() {
    std::printf("the output parts: their pins and the wiring door\n");
    register_all_sources();
    auto& reg = SourceRegistry::instance();
    auto ch = reg.has("SoundChannel") ? std::dynamic_pointer_cast<SoundChannel>(reg.create("SoundChannel")) : nullptr;
    auto mix = reg.has("StereoMixer") ? std::dynamic_pointer_cast<StereoMixer>(reg.create("StereoMixer")) : nullptr;
    CHECK(ch != nullptr && mix != nullptr);
    if (!ch || !mix) return;
    CHECK(dynamic_cast<ValueSource*>(static_cast<Component*>(ch.get())) == nullptr);   // neither is a signal
    CHECK(dynamic_cast<ValueSource*>(static_cast<Component*>(mix.get())) == nullptr);
    auto param_is = [](const ParamDescriptor& d, const char* name, float def, float mn, float mx) {
        return std::string(d.name) == name && d.default_value == def && d.min_value == mn && d.max_value == mx;
    };
    const auto chParams = ch->param_descriptors();
    CHECK(chParams.size() == 2 && param_is(chParams[0], "volume", 1.0f, 0.0f, 4.0f) && param_is(chParams[1], "pan", 0.0f, -1.0f, 1.0f));
    const auto chInputs = ch->input_descriptors();
    CHECK(chInputs.size() == 1 && std::string(chInputs[0].name) == "source" && !chInputs[0].multi && !chInputs[0].slot);
    const auto mixParams = mix->param_descriptors();
    CHECK(mixParams.size() == 2 && param_is(mixParams[0], "gainL", 1.0f, 0.0f, 4.0f) && param_is(mixParams[1], "gainR", 1.0f, 0.0f, 4.0f));
    const auto mixInputs = mix->input_descriptors();
    CHECK(mixInputs.size() == 1 && std::string(mixInputs[0].name) == "channels" && mixInputs[0].multi && mixInputs[0].slot
          && mixInputs[0].hint && std::string(mixInputs[0].hint) == "a SoundChannel");
    CHECK(ch->setting_descriptors().empty() && ch->array_descriptors().empty());
    CHECK(mix->setting_descriptors().empty() && mix->array_descriptors().empty());
    // channels: a channel by set_param, a second appended, a signal and another part refused
    auto ch2 = std::make_shared<SoundChannel>();
    auto sine = std::make_shared<SineSource>();
    auto peaks = std::make_shared<FormantPeaks>();
    CHECK(mix->set_param("channels", ch));
    CHECK(mix->add_param("channels", ch2));
    auto holds_both = [&] {
        return mix->channels.views.size() == 2 && mix->channels.views[0] == ch.get() && mix->channels.views[1] == ch2.get();
    };
    CHECK(holds_both());
    CHECK(!mix->set_param("channels", sine) && holds_both());
    CHECK(!mix->add_param("channels", sine) && holds_both());
    CHECK(!mix->add_param("channels", peaks) && holds_both());
    // source: a signal taken, a part refused
    CHECK(ch->set_param("source", sine) && ch->source.source == sine);
    CHECK(!ch->set_param("source", peaks) && ch->source.source == sine);
}

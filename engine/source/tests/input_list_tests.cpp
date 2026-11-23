#include "mforce/testing/check.h"
#include "core/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/additive/formant_peaks.h"
#include "mforce/source/combiner/crossfade_source.h"
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;


void run_input_list_tests() {
    using namespace three_kinds;
    std::printf("the list of signal inputs: set replaces, add appends, clear empties, a part refused\n");
    ListProbe p;
    const auto id = p.input_descriptors();
    CHECK(id.size() == 1 && std::string(id[0].name) == "feeds" && id[0].multi && !id[0].slot);
    CHECK(p.param_descriptors().empty() && p.setting_descriptors().empty());
    CHECK(p.pins().size() == 1 && p.pins()[0]->pull == Pull::Manual);
    CrossfadeSource xf;                                                    // a single Input is not a list
    CHECK(!xf.input_descriptors().empty() && !xf.input_descriptors()[0].multi);
    auto a = std::make_shared<CountingFeed>(), b = std::make_shared<CountingFeed>(), c = std::make_shared<CountingFeed>();
    using Feeds = std::vector<std::shared_ptr<ValueSource>>;
    CHECK(p.set_param("feeds", a));
    CHECK((p.feeds.sources == Feeds{a}) && p.hooks == 1);
    CHECK(p.add_param("feeds", b) && p.add_param("feeds", c));
    CHECK((p.feeds.sources == Feeds{a, b, c}) && p.hooks == 3);
    CHECK(p.set_param("feeds", b));
    CHECK((p.feeds.sources == Feeds{b}) && p.hooks == 4);
    CHECK(p.set_param("feeds", nullptr));
    CHECK(p.feeds.sources.empty() && p.hooks == 5);
    CHECK(p.add_param("feeds", a) && p.add_param("feeds", nullptr));
    CHECK(p.feeds.sources.empty() && p.hooks == 7);
    CHECK(p.add_param("feeds", a) && p.clear_param("feeds"));
    CHECK(p.feeds.sources.empty() && p.hooks == 9);
    CHECK(p.add_param("feeds", a) && p.hooks == 10);
    auto part = std::make_shared<FormantPeaks>();
    CHECK(!p.set_param("feeds", part));
    CHECK(!p.add_param("feeds", part));
    CHECK((p.feeds.sources == Feeds{a}) && p.hooks == 10);
    CHECK(p.get_param("feeds") == nullptr && p.get_part("feeds") == nullptr);
    // the owner's sweep does not prepare, start or pull a list's feeds
    CHECK(p.add_param("feeds", b));
    p.prepare(RenderContext{48000});
    p.start(16);
    for (int i = 0; i < 8; ++i) (void)p.next();
    CHECK(a->nexts == 0 && b->nexts == 0 && a->prepares == 0 && b->prepares == 0 && a->starts == 0 && b->starts == 0);
}

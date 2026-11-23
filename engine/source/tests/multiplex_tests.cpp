#include "mforce/testing/check.h"
#include "test_support.h"
#include "core/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/source/combiner/combined_source.h"
#include "mforce/source/combiner/multiplex_source.h"
#include <cstdio>
#include <memory>
#include <vector>

using namespace mforce;


// A Multiplex given a one-argument builder calls it once per clone, with the
// clone's index, at every rebuild, and emits the clones' mean; a clone's inner
// node that only the root's pin holds lives as long as the Multiplex.
void run_multiplex_builder_tests() {
    std::printf("MultiplexSource: a builder that returns the clone's root\n");
    std::vector<int> calls;
    std::vector<std::weak_ptr<ValueSource>> inner;
    auto allAlive = [&inner] {
        bool alive = !inner.empty();
        for (const auto& w : inner) alive = alive && !w.expired();
        return alive;
    };
    MultiplexSource mux;
    mux.set_setting("count", 3.0f);
    mux.set_template([&calls, &inner](int i) -> std::shared_ptr<ValueSource> {
        calls.push_back(i);
        auto leaf = konst(float(i + 1));
        inner.push_back(leaf);
        return std::make_shared<CombinedSource>(leaf, konst(0.0f), CombineOp::Sum);
    });
    TestHost h;
    mux.prepare(h.ctx);
    CHECK(calls == std::vector<int>({0, 1, 2}));
    CHECK(allAlive());
    mux.start(64); h.clock.tick();
    CHECK_NEAR(mux.next(), 2.0f, 1e-6f);                  // (1 + 2 + 3) / 3
    calls.clear(); inner.clear();
    mux.mark_dirty();
    mux.prepare(h.ctx);
    CHECK(calls == std::vector<int>({0, 1, 2}));
    CHECK(allAlive());
    calls.clear(); inner.clear();
    mux.set_setting("count", 5.0f);
    mux.prepare(h.ctx);
    CHECK(calls == std::vector<int>({0, 1, 2, 3, 4}));
    CHECK(allAlive());
    mux.start(64); h.clock.tick();
    CHECK_NEAR(mux.next(), 3.0f, 1e-6f);                  // (1 + 2 + 3 + 4 + 5) / 5
}

// A builder given by set_template is the one the next prepare() asks: a
// Multiplex prepared and pulled with builder A, then given builder B and
// prepared and pulled again, asks B for every clone and emits B's mean.
void run_multiplex_template_change_tests() {
    std::printf("MultiplexSource: the next prepare asks the builder set_template gave it\n");
    int askedA = 0, askedB = 0;
    MultiplexSource mux;
    mux.set_setting("count", 2.0f);
    mux.set_template([&askedA](int) -> std::shared_ptr<ValueSource> { ++askedA; return konst(1.0f); });
    TestHost h;
    mux.prepare(h.ctx);
    mux.start(64); h.clock.tick();
    CHECK_NEAR(mux.next(), 1.0f, 1e-6f);
    mux.set_template([&askedB](int i) -> std::shared_ptr<ValueSource> { ++askedB; return konst(float(10 + i)); });
    mux.prepare(h.ctx);
    mux.start(64); h.clock.tick();
    CHECK(askedA == 2 && askedB == 2);
    CHECK_NEAR(mux.next(), 10.5f, 1e-6f);                 // (10 + 11) / 2
}

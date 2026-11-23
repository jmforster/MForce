#pragma once
// Helpers the core tests share with the test programs of the modules above it.
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/randomizer.h"
#include "mforce/core/render_context.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace mforce;

// n samples in (-1, 1) from the engine's own generator, with an exact zero at
// every 97th index and a negative zero at every 101st, so a comparison meets
// both.
inline std::vector<float> twin_signal(uint32_t seed, int n) {
    Randomizer rng(seed);
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        float x = rng.valuePN();
        if (i % 97 == 0) x = 0.0f;
        if (i % 101 == 0) x = -0.0f;
        v[size_t(i)] = x;
    }
    return v;
}
namespace {
struct TestHost { SampleClock clock; RenderContext ctx{48000, &clock}; };
}  // namespace
namespace partials_twin {
struct W { float cur{0.0f}, target{0.0f}; int pos{0}, len{1}; };
inline int seg_len(int baseLen, float evolve, Randomizer& rng) {
    float f = (evolve <= 0.0f) ? 1.0f
                               : std::pow(1.0f + 3.0f * evolve, rng.valuePN());
    int len = int(float(baseLen) * f);
    return len < 1 ? 1 : len;
}
inline float advance(W& w, int baseLen, float evolve, Randomizer& rng) {
    if (w.pos >= w.len) {
        w.cur = w.target;
        float amp = 1.0f + 0.7f * evolve * rng.valuePN();
        w.target = rng.valuePN() * amp;
        w.len = seg_len(baseLen, evolve, rng);
        w.pos = 0;
    }
    float u = float(w.pos) / float(w.len);
    float s = u * u * (3.0f - 2.0f * u);
    ++w.pos;
    return w.cur + (w.target - w.cur) * s;
}
}  // namespace partials_twin
namespace enum_column {
static constexpr const char* kKinds[] = {"Linear", "Expo", "Hold", nullptr};
struct EnumTableProbe final : Generator {
    Setting<int> shape{this, "shape", 0, 0, 2, kKinds};
    Array level{this, "level", "rows", 0.0f, -10.0f, 10.0f};
    Array kind {this, "kind",  "rows", 0.0f, 0.0f,   2.0f, kKinds};
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "EnumTableProbe"; }
};
}  // namespace enum_column
namespace three_kinds {
struct StringProbe final : Generator {
    Setting<float> gain{this, "gain", 1.0f, 0.0f, 2.0f};
    StringSetting label{this, "label", "x"};
    std::vector<std::string> changed;                 // each name on_setting_changed saw, in order
    static inline std::string lastLabel;              // the label after the last change, for a load whose node the test cannot reach
    void on_setting_changed(std::string_view n) override { changed.emplace_back(n); lastLabel = label.value; }
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "StringProbe"; }
};
struct ListProbe final : Generator {
    InputList feeds{this, "feeds"};
    int hooks{0};                                     // on_input_list_changed calls for "feeds"
    void on_input_list_changed(std::string_view n) override { if (n == "feeds") ++hooks; }
    float compute() override {
        float sum = 0.0f;
        for (const auto& f : feeds.sources) sum += f->current();
        return sum;
    }
    const char* type_name() const override { return "ListProbe"; }
};
// Counts its prepares, starts and computes: a feed the owner's sweep must not reach.
struct CountingFeed final : Generator {
    int prepares{0}, starts{0}, nexts{0};
    void on_prepare(const RenderContext&) override { ++prepares; }
    void on_start(int) override { ++starts; }
    float compute() override { ++nexts; return 1.0f; }
    const char* type_name() const override { return "CountingFeed"; }
};
// Refuses itself when `refuse` is on, or when `refuseSeeded` is on and the
// registry built it with a seed (a Multiplex clone always is); two warnings
// when `warn` is on. Counts the calls of both.
struct CheckProbe final : Generator {
    Setting<bool> refuse{this, "refuse", false, false, true};
    Setting<bool> refuseSeeded{this, "refuseSeeded", false, false, true};
    Setting<bool> warn{this, "warn", false, false, true};
    bool seeded{false};
    static inline int validated{0}, warned{0};
    std::string validate() const override {
        ++validated;
        return refuse.value || (refuseSeeded.value && seeded) ? "probe says no" : "";
    }
    std::vector<std::string> warnings() const override {
        ++warned;
        if (!warn.value) return {};
        return {"probe warns once", "probe warns twice"};
    }
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "CheckProbe"; }
};
// Refuses itself as built, whatever the file gives it.
struct RefusingProbe final : Generator {
    std::string validate() const override { return "probe refuses as built"; }
    float compute() override { return 0.0f; }
    const char* type_name() const override { return "RefusingProbe"; }
};
}  // namespace three_kinds

// A node that counts the reseeds the live note-on delivers: the voice's reseed
// walk must reach every signal, on the audio thread, with no cast.
struct ReseedProbe final : Generator {
    int reseeds{0};
    float compute() override { return 1.0f; }
    void reseed() override { ++reseeds; }
    const char* type_name() const override { return "ReseedProbe"; }
};

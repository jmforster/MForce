#include "mforce/testing/check.h"
#include "test_support.h"
#include "source/tests/fixtures.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/source_registry.h"
#include "mforce/source/envelope/vibrato.h"
#include "mforce/source/filter/bw_bandpass_filter.h"
#include "mforce/source/filter/bw_highpass_filter.h"
#include "mforce/source/filter/bw_lowpass_filter.h"
#include "mforce/source/modulator/var_source.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/source/utility/static_range_source.h"
#include "mforce/source/utility/static_var_source.h"
#include "mforce/source/wavetable/hybrid_ks_source.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace mforce;

namespace node_twins {
// A line from a to b over n samples of each note, then held at b.
struct LineFeed final : Generator {
    float a, b;
    int n, k{0};
    LineFeed(float a0, float b0, int n0) : a(a0), b(b0), n(n0) {}
    void on_start(int) override { k = 0; }
    float compute() override { const float t = k < n ? float(k) / float(n) : 1.0f; ++k; return a + (b - a) * t; }
    const char* type_name() const override { return "LineFeed"; }
};
static std::shared_ptr<ValueSource> line(float a, float b) { return std::make_shared<LineFeed>(a, b, 4800); }
static constexpr const char* kFilterKinds[] = {"lp", "hp", "bp"};
static constexpr int kSectionCounts[] = {1, 2, 3, 5};
// The filter `f` of kind 0 (lowpass), 1 (highpass) or 2 (bandpass), fed a
// fresh seeded noise, its cutoff(s) fixed or moving, 4,800 samples.
static uint64_t filter_hash(ValueSource& f, int kind, bool moving) {
    f.set_param("source", noise(0xB0B0u));
    if (kind == 2) {
        f.set_param("lowCutoff",  moving ? line(100.0f, 800.0f) : c(300.0f));
        f.set_param("highCutoff", moving ? line(2000.0f, 6000.0f) : c(3000.0f));
    } else if (kind == 0) {
        f.set_param("cutoffFreq", moving ? line(200.0f, 8000.0f) : c(1200.0f));
    } else {
        f.set_param("cutoffFreq", moving ? line(3000.0f, 100.0f) : c(800.0f));
    }
    return render(f, 4800);
}
// The hybrid's target spectrum of n values.
static std::vector<float> targets(int n) {
    std::vector<float> t;
    for (int k = 0; k < n; ++k) t.push_back((k % 3 == 1 ? 0.5f : 1.0f) / float(k + 1));
    return t;
}
static constexpr int kTargetCounts[] = {10, 30, 40};
// A 220 Hz note of 9,600 samples: the attack and well into the additive phase.
static uint64_t hybrid_hash(ValueSource& h) {
    h.set_param("frequency", c(220.0f));
    return render(h, 9600);
}
// Each Static node's three numbers, a grid of four, and two seeds.
static constexpr float kStaticVarGrid[4][3]   = {{1.0f, 0.0f, 0.0f}, {2.0f, 0.5f, 0.0f}, {2.0f, 0.5f, 0.3f}, {-3.0f, 0.2f, -0.5f}};
static constexpr float kStaticRangeGrid[4][3] = {{0.0f, 1.0f, 0.0f}, {0.2f, 0.4f, 0.3f}, {-5.0f, 5.0f, -0.7f}, {100.0f, 200.0f, 0.0f}};
static constexpr uint32_t kStaticVarSeeds[]   = {0x57A70001u, 12345u};
static constexpr uint32_t kStaticRangeSeeds[] = {0x57A70002u, 12345u};
// Four notes of two samples: the value drawn at each start.
static uint64_t static_hash(ValueSource& s) { return render(s, 2, 4); }
// Vibrato's seven settings over six points; the last holds every note
// under its threshold.
static constexpr const char* kVibratoKeys[7] = {"speed", "depth", "attack", "threshold", "speedVar", "depthVar", "zeroCrossTendency"};
static constexpr float kVibratoGrid[6][7] = {
    {5.0f, 0.02f, 0.3f, 0.0f,  0.0f, 0.0f, 1.0f},
    {6.0f, 0.05f, 0.2f, 0.0f,  0.3f, 0.2f, 1.0f},
    {3.5f, 0.1f,  0.5f, 0.05f, 0.0f, 0.5f, 0.3f},
    {8.0f, 0.01f, 0.0f, 0.0f,  0.8f, 0.0f, 0.0f},
    {5.0f, 0.03f, 1.0f, 0.08f, 0.1f, 0.1f, 0.7f},
    {5.0f, 0.05f, 0.3f, 0.5f,  0.0f, 0.0f, 1.0f}};
static constexpr uint32_t kVibratoSeeds[] = {0xF1B00000u, 777u};
// Two notes of 4,800 samples on a 440 Hz carrier.
static uint64_t vibrato_hash(ValueSource& v, const float (&point)[7]) {
    for (int k = 0; k < 7; ++k) v.set_setting(kVibratoKeys[k], point[k]);
    v.set_param("frequency", c(440.0f));
    return render(v, 4800, 2);
}
static const std::vector<float> kSegmentValues = {0.01f, 1.0f, 0.02f, -0.5f, 0.005f, 0.0f};
// Two notes of 2,400 samples, the widths and values varied so the seed is heard.
static uint64_t segment_hash(ValueSource& s) {
    s.set_param("widthVarPct", c(0.3f));
    s.set_param("valVarPct", c(0.2f));
    return render(s, 2400, 2);
}
static std::string filter_case(int kind, int n, bool moving) {
    return std::string("bw ") + kFilterKinds[kind] + " s" + std::to_string(n) + (moving ? " moving" : " fixed");
}
static std::shared_ptr<ValueSource> bw_filter(int kind) {
    if (kind == 0) return std::make_shared<BWLowpassFilter>();
    if (kind == 1) return std::make_shared<BWHighpassFilter>();
    return std::make_shared<BWBandpassFilter>();
}
static std::shared_ptr<ValueSource> create(const char* type, std::optional<uint32_t> seed = std::nullopt) {
    register_all_sources();
    return std::dynamic_pointer_cast<ValueSource>(SourceRegistry::instance().create(type, seed));
}
}  // namespace node_twins

void run_bw_section_twin_tests() {
    using namespace node_twins;
    std::printf("the Butterworth section count: the setting against the old constructor\n");
    int rows = 0;
    for (int kind = 0; kind < 3; ++kind)
        for (int n : kSectionCounts)
            for (bool moving : {false, true}) {
                auto f = bw_filter(kind);
                f->set_setting("sections", float(n));
                CHECK(filter_hash(*f, kind, moving) == base(filter_case(kind, n, moving)));
                ++rows;
            }
    CHECK(rows == 24);
    for (int kind = 0; kind < 3; ++kind) {
        auto f = bw_filter(kind);
        CHECK(f->get_setting("sections") == 2.0f);
        CHECK(filter_hash(*f, kind, true) == base(filter_case(kind, 2, true)));
        // The count changed between two prepares: the rebuild starts from
        // clear histories, so fresh feeds give the fresh three-section render.
        f->set_setting("sections", 3.0f);
        CHECK(filter_hash(*f, kind, true) == base(filter_case(kind, 3, true)));
        // The same rate and count again: the histories carry on.
        CHECK(filter_hash(*f, kind, true) != base(filter_case(kind, 3, true)));
    }
    BWBandpassFilter bp;
    const auto* d = find_setting(bp, "sections");
    CHECK(d != nullptr && d->type == SettingType::Int && d->structural);
    CHECK(d && d->default_value == 2.0f && d->min_value == 1.0f && d->max_value == 8.0f);
}

void run_hybrid_target_twin_tests() {
    using namespace node_twins;
    std::printf("the hybrid's targets: the array against set_target_partials\n");
    for (int n : kTargetCounts) {
        HybridKSSource h;
        h.set_array("targetPartials", targets(n));
        CHECK(hybrid_hash(h) == base("hybrid t" + std::to_string(n)));
        CHECK(h.get_array("targetPartials") == targets(n));      // the store keeps what was set
        CHECK(h.target_count() == std::max(n, 30));               // the working copy is padded to numPartials
    }
    HybridKSSource late;
    late.prepare(RenderContext{48000});
    late.set_array("targetPartials", targets(10));
    CHECK(late.target_count() == 30);                             // sized at once on a prepared node
    CHECK(hybrid_hash(late) == base("hybrid t10 after prepare"));
    CHECK(base("hybrid t10") != base("hybrid t30"));              // the targets reach the render
    bool declared = false;
    for (const auto& d : late.array_descriptors())
        declared = declared || std::string(d.name) == "targetPartials";
    CHECK(declared);
}

void run_static_pair_twin_tests() {
    using namespace node_twins;
    std::printf("the Static pair: the settings against the old constructors\n");
    for (int g = 0; g < 4; ++g)
        for (int s = 0; s < 2; ++s) {
            const std::string at = " g" + std::to_string(g) + " seed" + std::to_string(s);
            const auto& p = kStaticVarGrid[g];
            StaticVarSource sv(kStaticVarSeeds[s]);
            sv.set_setting("baseValue", p[0]);
            sv.set_setting("varPct", p[1]);
            sv.set_setting("bias", p[2]);
            CHECK(static_hash(sv) == base("staticvar" + at));
            const auto& q = kStaticRangeGrid[g];
            StaticRangeSource sr(kStaticRangeSeeds[s]);
            sr.set_setting("min", q[0]);
            sr.set_setting("max", q[1]);
            sr.set_setting("bias", q[2]);
            CHECK(static_hash(sr) == base("staticrange" + at));
        }
    // The default seeds are the old ones.
    StaticVarSource dv;
    dv.set_setting("baseValue", 2.0f);
    dv.set_setting("varPct", 0.5f);
    CHECK(static_hash(dv) == base("staticvar g1 seed0"));
    StaticRangeSource dr;
    CHECK(static_hash(dr) == base("staticrange g0 seed0"));
    StaticVarSource fv;
    StaticRangeSource fr;
    CHECK(fv.get_setting("baseValue") == 1.0f && fv.get_setting("varPct") == 0.0f && fv.get_setting("bias") == 0.0f);
    CHECK(fr.get_setting("min") == 0.0f && fr.get_setting("max") == 1.0f && fr.get_setting("bias") == 0.0f);
    CHECK(fv.setting_descriptors().size() == 3 && fr.setting_descriptors().size() == 3);
    const auto* b = find_setting(fv, "bias");
    CHECK(b && b->type == SettingType::Float && b->min_value == -1.0f && b->max_value == 1.0f);
    const auto* mx = find_setting(fr, "max");
    CHECK(mx && mx->default_value == 1.0f && mx->min_value == -10000.0f && mx->max_value == 10000.0f);
}

void run_vibrato_twin_tests() {
    using namespace node_twins;
    std::printf("Vibrato: its LFO's two ranges built by their pins against the old constructor\n");
    int rows = 0;
    for (int g = 0; g < 6; ++g)
        for (int s = 0; s < 2; ++s) {
            Vibrato v(kVibratoSeeds[s]);
            CHECK(vibrato_hash(v, kVibratoGrid[g]) == base("vibrato g" + std::to_string(g) + " seed" + std::to_string(s)));
            ++rows;
        }
    CHECK(rows == 12);
    CHECK(base("vibrato g1 seed0") != base("vibrato g1 seed1"));  // the seed reaches the render
}

void run_registry_built_twin_tests() {
    using namespace node_twins;
    std::printf("Var, Range and Segment built by the registry and filled by name\n");
    for (bool abs : {true, false}) {
        auto v = create("VarSource");
        v->set_param("val", line(1.0f, 3.0f));
        v->set_param("var", noise(7u));
        v->set_param("varPct", c(0.25f));
        v->set_setting("absolute", abs ? 1.0f : 0.0f);
        CHECK(render(*v, 4800) == base(std::string("var ") + (abs ? "absolute" : "relative")));
    }
    for (bool norm : {true, false}) {
        auto r = create("RangeSource");
        r->set_param("min", c(10.0f));
        r->set_param("max", c(20.0f));
        r->set_param("var", noise(9u));
        r->set_setting("normalized", norm ? 1.0f : 0.0f);
        CHECK(render(*r, 4800) == base(std::string("range ") + (norm ? "normalized" : "raw")));
    }
    for (bool once : {false, true}) {
        auto a = create("SegmentSource");
        auto b = create("SegmentSource", 99u);
        for (auto* s : {a.get(), b.get()}) {
            s->set_array("values", kSegmentValues);
            s->set_setting("oneShot", once ? 1.0f : 0.0f);
        }
        CHECK(segment_hash(*a) == base(std::string("segment default seed") + (once ? " oneShot" : "")));
        CHECK(segment_hash(*b) == base(std::string("segment seed 99") + (once ? " oneShot" : "")));
    }
    // The registry's own nodes: nothing wired, each pin at its declared constant.
    auto v = create("VarSource");
    CHECK(!v->get_param("val") && !v->get_param("var") && !v->get_param("varPct"));
    auto* vs = dynamic_cast<VarSource*>(v.get());
    CHECK(vs && vs->val.constant == 1.0f && vs->absolute.value);
    (void)render(*v, 4);
    CHECK(v->current() == 1.0f);                                  // 1 * (1 + 0 * 0)
    auto r = create("RangeSource");
    CHECK(r->get_setting("normalized") == 0.0f);
    CHECK(!r->get_param("min") && !r->get_param("max") && !r->get_param("var"));
}

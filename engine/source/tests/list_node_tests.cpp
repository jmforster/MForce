#include "mforce/testing/check.h"
#include "test_support.h"
#include "source/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/additive/additive_source.h"
#include "mforce/source/additive/expand_rule.h"
#include "mforce/source/additive/expand_rule_node.h"
#include "mforce/source/additive/explicit_partials.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/additive/sequence_partials.h"
#include "mforce/source/combiner/multi_source.h"
#include "mforce/source/envelope/phased_value_source.h"
#include "mforce/source/oscillator/sine_source.h"
#include "mforce/testing/alloc_probe.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace list_twins {
using node_twins::c;
using node_twins::fnv;
using node_twins::noise;
constexpr float kAbsent = std::numeric_limits<float>::quiet_NaN();
// One feed of a case: a constant, a seeded noise, or a sine at a frequency,
// with its weight and delay, either of which may be absent.
struct Feed {
    int kind;                                   // 0 constant, 1 noise (seed), 2 sine (Hz)
    float value;
    float weight = kAbsent;
    float delay = kAbsent;
};
static std::shared_ptr<ValueSource> signal(int kind, float value) {
    if (kind == 0) return c(value);
    if (kind == 1) return noise(uint32_t(value));
    auto s = std::make_shared<SineSource>();
    s->set_param("frequency", c(value));
    return s;
}
struct MultiCase { const char* name; std::vector<Feed> feeds; };
static const std::vector<MultiCase> kMultiCases = {
    {"multi one",            {{0, 0.3f}}},
    {"multi two all",        {{0, 0.3f, 0.5f}, {1, 11.0f, 2.0f, 1.0f}}},
    {"multi three some",     {{0, 0.3f, 0.5f}, {0, -0.7f}, {1, 11.0f, 0.25f}}},
    {"multi four delays",    {{0, 0.3f}, {1, 11.0f, kAbsent, 100.0f}, {1, 23.0f, kAbsent, 1.0f}, {0, 0.6f}}},
    {"multi five all",       {{0, 0.1f, 0.5f, 100.0f}, {0, 0.2f, 0.25f}, {1, 11.0f, 1.5f, 1.0f},
                              {2, 440.0f, 0.8f}, {0, 0.4f, -1.0f, 100.0f}}},
    {"multi three delayed",  {{2, 440.0f, kAbsent, 100.0f}, {1, 23.0f, kAbsent, 100.0f}, {0, 0.2f, kAbsent, 100.0f}}},
    // "multi five all" with its arrays cut to two weights and one delay, the
    // defaults written out past the cut.
    {"multi five defaults",  {{0, 0.1f, 0.5f, 100.0f}, {0, 0.2f, 0.25f, 0.0f}, {1, 11.0f, 1.0f, 0.0f},
                              {2, 440.0f, 1.0f, 0.0f}, {0, 0.4f, 1.0f, 0.0f}}},
};
// A case's weights or delays as the node's array: one per feed, the default
// where a feed has none; empty when no feed has one.
static std::vector<float> column(const std::vector<Feed>& feeds, float Feed::*field, float def) {
    bool any = false;
    for (const auto& f : feeds) any = any || !std::isnan(f.*field);
    std::vector<float> out;
    if (any)
        for (const auto& f : feeds) out.push_back(std::isnan(f.*field) ? def : f.*field);
    return out;
}
constexpr int kMultiFrames = 4800;

// One stage of a Phased case: its signal and its four numbers.
struct Row { int kind; float value; float percent, minSec, maxSec, gain; };
struct PhasedCase { const char* name; std::vector<Row> rows; };
static const std::vector<PhasedCase> kPhasedCases = {
    {"expand first",  {{2, 220.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {1, 31.0f, 0.3f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.25f, 0.0f, 0.0f, 1.0f}}},
    {"expand middle", {{2, 220.0f, 0.3f, 0.0f, 0.0f, 1.0f}, {1, 31.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.25f, 0.0f, 0.0f, 1.0f}}},
    {"expand last",   {{2, 220.0f, 0.3f, 0.0f, 0.0f, 1.0f}, {1, 31.0f, 0.25f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.0f, 0.0f, 0.0f, 1.0f}}},
    {"clamped",       {{2, 220.0f, 0.5f, 0.01f, 0.02f, 1.0f}, {1, 31.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.2f, 0.03f, 0.04f, 1.0f}}},
    {"gain",          {{2, 220.0f, 0.3f, 0.0f, 0.0f, 0.5f}, {1, 31.0f, 0.0f, 0.0f, 0.0f, 1.0f}, {2, 550.0f, 0.25f, 0.0f, 0.0f, 0.5f}}},
};
static constexpr float kOverlaps[] = {0.0f, 0.05f, 0.2f};
static constexpr int kNoteFrames[] = {9600, 48000};
static std::string phased_name(const PhasedCase& pc, float overlap, int frames) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "phased %s ov%.2f n%d", pc.name, overlap, frames);
    return buf;
}
static uint64_t zeros(int frames) { return fnv(std::vector<float>(size_t(frames), 0.0f)); }

// The rule of twelve values, every one off its default, and the host it
// expands: a FullPartials under an AdditiveSource.
static ExpandRule rule_values(int recurse) {
    ExpandRule r;
    r.count = 3; r.recurse = recurse;
    r.spacing1 = 0.3f; r.spacing2 = 0.7f; r.dt1 = 0.05f; r.dt2 = 0.02f;
    r.loPct1 = 0.2f; r.loPct2 = 0.3f; r.power1 = 2.0f; r.power2 = 1.5f;
    r.po1 = 0.1f; r.po2 = 0.25f;
    return r;
}
static std::shared_ptr<FullPartials> host_partials() {
    auto p = std::make_shared<FullPartials>(0x3902u);
    p->set_setting("maxPartials", 6.0f);
    return p;
}
static std::shared_ptr<AdditiveSource> host(std::shared_ptr<FullPartials> parts) {
    auto a = std::make_shared<AdditiveSource>();
    a->set_param("frequency", c(220.0f));
    a->set_param("partials", std::move(parts));
    return a;
}
constexpr int kRuleFrames = 4800, kRuleNotes = 2;
// Notes on a node already prepared: start and pull, no prepare.
static uint64_t render_prepared(ValueSource& n, int frames, int notes) {
    std::vector<float> out;
    out.reserve(size_t(frames) * size_t(notes));
    for (int k = 0; k < notes; ++k) {
        n.start(frames);
        for (int i = 0; i < frames; ++i) out.push_back(n.next());
    }
    return fnv(out);
}
// The recorded answers, one per case: the hash each case rendered when filled
// through add with a weight and a delay, add_stage and the struct handed to
// set_expand_rule.
static const std::pair<const char*, uint64_t> kBase[] = {
    {"multi one", 0x5A7D7C04F2769CA5ull},
    {"multi two all", 0x88045BDA2BF5BDE7ull},
    {"multi three some", 0x110682BC88AFB980ull},
    {"multi four delays", 0xCA65543BF7A95376ull},
    {"multi five all", 0x816A53E6C9ADEC03ull},
    {"multi three delayed", 0x6BC2C0800CFB6075ull},
    {"multi five defaults", 0x306EBA191BF9E239ull},
    {"phased expand first ov0.00 n9600", 0x3452D7490CAE6CBEull},
    {"phased expand first ov0.00 n48000", 0x0A8F84C9382AC0FAull},
    {"phased expand first ov0.05 n9600", 0xC5ABB93F4E52304Bull},
    {"phased expand first ov0.05 n48000", 0x52648E35AC5E4CC1ull},
    {"phased expand first ov0.20 n9600", 0x5BA977C1A46B91FBull},
    {"phased expand first ov0.20 n48000", 0x2D8C356CBE7DA3FBull},
    {"phased expand middle ov0.00 n9600", 0xD1B46C44DA175B3Aull},
    {"phased expand middle ov0.00 n48000", 0xD7FEBB876E0211ADull},
    {"phased expand middle ov0.05 n9600", 0xDAA3AE9FA76DC163ull},
    {"phased expand middle ov0.05 n48000", 0x20F654881D2C5835ull},
    {"phased expand middle ov0.20 n9600", 0x30BEEF910CE130A4ull},
    {"phased expand middle ov0.20 n48000", 0x1C16DEAA0BA7B3DCull},
    {"phased expand last ov0.00 n9600", 0x7200B562F373D661ull},
    {"phased expand last ov0.00 n48000", 0x9B2CC538F2522A07ull},
    {"phased expand last ov0.05 n9600", 0x70E42EE60889830Full},
    {"phased expand last ov0.05 n48000", 0x398DE6EC34BEC562ull},
    {"phased expand last ov0.20 n9600", 0xB836D8CC0AAE5A24ull},
    {"phased expand last ov0.20 n48000", 0x2C49EBB889185456ull},
    {"phased clamped ov0.00 n9600", 0x599D3203CD1D728Aull},
    {"phased clamped ov0.00 n48000", 0xE394F9ED5BFC9096ull},
    {"phased clamped ov0.05 n9600", 0x8A4BBA7F04A10D92ull},
    {"phased clamped ov0.05 n48000", 0xE9EED33E1FFD8554ull},
    {"phased clamped ov0.20 n9600", 0xF865214F16F3313Eull},
    {"phased clamped ov0.20 n48000", 0x2252C62EEC19AE0Eull},
    {"phased gain ov0.00 n9600", 0xF0C512D67C2C0D6Full},
    {"phased gain ov0.00 n48000", 0xF23E6BBB266C4F43ull},
    {"phased gain ov0.05 n9600", 0x6CC31B7FD7FCBA17ull},
    {"phased gain ov0.05 n48000", 0x5E8B76AB7519107Bull},
    {"phased gain ov0.20 n9600", 0x30BEEF910CE130A4ull},
    {"phased gain ov0.20 n48000", 0x48A26FB28712CBD6ull},
    {"rule recurse0", 0x42208CB55733894Bull},
    {"rule recurse1", 0xE2A810BB90A821E7ull},
    {"rule none", 0xC3B3C2CA5142A1A5ull},
};
static uint64_t base(const std::string& name) {
    for (const auto& [n, h] : kBase) if (name == n) return h;
    std::printf("  no recorded case '%s'\n", name.c_str());
    return 0;
}
static const MultiCase& multi_case(const char* name) {
    for (const auto& mc : kMultiCases) if (std::string(mc.name) == name) return mc;
    std::printf("  no multi case '%s'\n", name);
    return kMultiCases.front();
}
static const PhasedCase& phased_case(const char* name) {
    for (const auto& pc : kPhasedCases) if (std::string(pc.name) == name) return pc;
    std::printf("  no phased case '%s'\n", name);
    return kPhasedCases.front();
}
static void add_feeds(MultiSource& m, const std::vector<Feed>& feeds) {
    for (const auto& f : feeds) CHECK(m.add_param("source", signal(f.kind, f.value)));
}
// The case's arrays, set only where a feed holds a value.
static void set_columns(MultiSource& m, const std::vector<Feed>& feeds) {
    if (auto w = column(feeds, &Feed::weight, 1.0f); !w.empty()) m.set_array("weights", w);
    if (auto d = column(feeds, &Feed::delay, 0.0f); !d.empty()) m.set_array("delays", d);
}
// The first `n` rows of the case's table, column by column.
static void set_table(PhasedValueSource& p, const std::vector<Row>& rows, size_t n) {
    std::vector<float> percent, minSec, maxSec, gain;
    for (size_t i = 0; i < n && i < rows.size(); ++i) {
        percent.push_back(rows[i].percent); minSec.push_back(rows[i].minSec);
        maxSec.push_back(rows[i].maxSec);   gain.push_back(rows[i].gain);
    }
    p.set_array("percent", percent); p.set_array("minSec", minSec);
    p.set_array("maxSec", maxSec);   p.set_array("gain", gain);
}
static void add_stages(PhasedValueSource& p, const std::vector<Row>& rows) {
    for (const auto& r : rows) CHECK(p.add_param("sources", signal(r.kind, r.value)));
}
// The rule of `rule_values(recurse)` as a node, its pins wired to constants
// as a load wires them.
static std::shared_ptr<ExpandRuleNode> rule_node(int recurse) {
    const ExpandRule r = rule_values(recurse);
    auto n = std::make_shared<ExpandRuleNode>();
    n->set_setting("count", float(r.count));
    n->set_setting("recurse", float(r.recurse));
    const std::pair<const char*, float> pins[] = {
        {"spacing1", r.spacing1}, {"spacing2", r.spacing2}, {"dt1", r.dt1}, {"dt2", r.dt2},
        {"loPct1", r.loPct1}, {"loPct2", r.loPct2}, {"power1", r.power1}, {"power2", r.power2},
        {"po1", r.po1}, {"po2", r.po2}};
    for (const auto& [name, v] : pins) CHECK(n->set_param(name, c(v)));
    return n;
}
}  // namespace list_twins

void run_multi_list_twin_tests() {
    using namespace list_twins;
    std::printf("MultiSource: the list and its two arrays against add(src, weight, delay)\n");
    int rows = 0;
    for (const auto& mc : kMultiCases)
        for (bool arraysFirst : {true, false}) {
            MultiSource m;
            if (arraysFirst) set_columns(m, mc.feeds);
            add_feeds(m, mc.feeds);
            if (!arraysFirst) set_columns(m, mc.feeds);
            CHECK(m.source.sources.size() == mc.feeds.size());
            CHECK(node_twins::render(m, kMultiFrames) == base(mc.name));
            ++rows;
        }
    CHECK(rows == 14);
    const auto& five = multi_case("multi five all");
    {   // Arrays shorter than the feeds: each feed past an end takes the default.
        MultiSource m;
        add_feeds(m, five.feeds);
        m.set_array("weights", {0.5f, 0.25f});
        m.set_array("delays", {100.0f});
        CHECK(node_twins::render(m, kMultiFrames) == base("multi five defaults"));
    }
    {   // Arrays longer than the feeds: the entries past the last feed are not read.
        MultiSource m;
        add_feeds(m, multi_case("multi two all").feeds);
        m.set_array("weights", {0.5f, 2.0f, 7.0f, 7.0f});
        m.set_array("delays", {0.0f, 1.0f, 50.0f});
        CHECK(node_twins::render(m, kMultiFrames) == base("multi two all"));
    }
    {   // clear_param empties the list: silence, with the arrays kept.
        MultiSource m;
        add_feeds(m, five.feeds);
        set_columns(m, five.feeds);
        CHECK(m.clear_param("source"));
        CHECK(m.source.sources.empty() && m.get_array("weights").size() == 5);
        CHECK(node_twins::render(m, kMultiFrames) == zeros(kMultiFrames));
    }
    {   // set_param replaces the list with its one feed.
        MultiSource m;
        add_feeds(m, multi_case("multi three some").feeds);
        CHECK(m.set_param("source", signal(0, 0.3f)));
        CHECK(m.source.sources.size() == 1);
        CHECK(node_twins::render(m, kMultiFrames) == base("multi one"));
    }
}

void run_phased_list_twin_tests() {
    using namespace list_twins;
    std::printf("PhasedValueSource: the list and its table against add_stage\n");
    int rows = 0;
    for (const auto& pc : kPhasedCases)
        for (float ov : kOverlaps)
            for (int frames : kNoteFrames)
                for (bool tableFirst : {true, false}) {
                    PhasedValueSource p;
                    p.set_setting("overlap", ov);
                    if (tableFirst) set_table(p, pc.rows, pc.rows.size());
                    add_stages(p, pc.rows);
                    if (!tableFirst) set_table(p, pc.rows, pc.rows.size());
                    CHECK(node_twins::render(p, frames) == base(phased_name(pc, ov, frames)));
                    ++rows;
                }
    CHECK(rows == 60);
    {   // No stage: silence.
        PhasedValueSource p;
        CHECK(node_twins::render(p, 4800) == zeros(4800));
    }
    {   // A table of two rows under three feeds: the third takes 0, 0, 0, 1,
        // which is the last row of "expand last".
        const auto& last = phased_case("expand last");
        PhasedValueSource p;
        add_stages(p, last.rows);
        set_table(p, last.rows, 2);
        CHECK(node_twins::render(p, 9600) == base(phased_name(last, 0.05f, 9600)));
    }
    const auto& middle = phased_case("expand middle");
    {   // The list emptied after a prepare and a start: zeros, inside the list.
        PhasedValueSource p;
        add_stages(p, middle.rows);
        set_table(p, middle.rows, 3);
        p.prepare(RenderContext{48000});
        p.start(9600);
        for (int i = 0; i < 100; ++i) (void)p.next();
        CHECK(p.clear_param("sources"));
        bool silent = true;
        for (int i = 0; i < 9600; ++i) silent = silent && p.next() == 0.0f;
        p.start(9600);
        for (int i = 0; i < 100; ++i) silent = silent && p.next() == 0.0f;
        CHECK(silent);
    }
    {   // Three stages cut to one mid-note: the walk reads the one stage.
        PhasedValueSource p;
        add_stages(p, middle.rows);
        set_table(p, middle.rows, 3);
        p.prepare(RenderContext{48000});
        p.start(9600);
        for (int i = 0; i < 5000; ++i) (void)p.next();
        CHECK(p.set_param("sources", c(0.5f)));
        CHECK(p.sources.sources.size() == 1);
        bool one = true;
        for (int i = 0; i < 6000; ++i) one = one && p.next() == 0.5f;
        CHECK(one);
    }
    // A list or table change never moves the walk a note laid out.
    const auto pull = [](PhasedValueSource& p, int n, std::vector<float>& out) {
        for (int i = 0; i < n; ++i) out.push_back(p.next());
    };
    const auto untouched = [&](const std::vector<Row>& rows, float overlap, int frames) {
        PhasedValueSource p;
        p.set_setting("overlap", overlap);
        add_stages(p, rows);
        set_table(p, rows, rows.size());
        std::vector<float> out;
        p.prepare(RenderContext{48000});
        p.start(frames);
        pull(p, frames, out);
        return out;
    };
    const auto feeds_of = [](const std::vector<Row>& rows) {
        std::vector<std::shared_ptr<ValueSource>> feeds;
        for (const auto& r : rows) feeds.push_back(signal(r.kind, r.value));
        return feeds;
    };
    const std::vector<float> twin = untouched(middle.rows, 0.05f, 9600);
    for (int k : {100, 3000, 9000}) {   // in the first stage, the expand stage, and its fade into the last
        // The list cleared and refilled with the same feeds mid-note, as the editor's rewire does.
        PhasedValueSource p;
        p.set_setting("overlap", 0.05f);
        const auto feeds = feeds_of(middle.rows);
        for (const auto& f : feeds) CHECK(p.add_param("sources", f));
        set_table(p, middle.rows, 3);
        std::vector<float> out;
        p.prepare(RenderContext{48000});
        p.start(9600);
        pull(p, k, out);
        CHECK(p.clear_param("sources"));
        for (const auto& f : feeds) CHECK(p.add_param("sources", f));
        pull(p, 9600 - k, out);
        if (out != twin) std::printf("  refilled list at %d: the walk moved\n", k);
        CHECK(out == twin);
        // The four columns set again to the same values mid-note.
        PhasedValueSource q;
        q.set_setting("overlap", 0.05f);
        add_stages(q, middle.rows);
        set_table(q, middle.rows, 3);
        std::vector<float> outQ;
        q.prepare(RenderContext{48000});
        q.start(9600);
        pull(q, k, outQ);
        set_table(q, middle.rows, 3);
        pull(q, 9600 - k, outQ);
        if (outQ != twin) std::printf("  table set again at %d: the walk moved\n", k);
        CHECK(outQ == twin);
    }
    {   // Prepared and started with no feed, then a feed: zeros until the
        // next start, then the feed plays as in a node that held it throughout.
        PhasedValueSource p;
        p.prepare(RenderContext{48000});
        p.start(4800);
        std::vector<float> out;
        pull(p, 100, out);
        auto feed = signal(2, 220.0f);
        feed->prepare(RenderContext{48000});              // prepared here: the walk prepares nothing mid-note
        CHECK(p.add_param("sources", feed));
        pull(p, 4700, out);
        CHECK(fnv(out) == zeros(4800));
        p.start(4800);
        std::vector<float> played;
        pull(p, 4800, played);
        CHECK(played == untouched({{2, 220.0f, 0.0f, 0.0f, 0.0f, 1.0f}}, 0.05f, 4800));
    }
    {   // Three stages cut to two mid-note (cleared, then two added): the walk
        // keeps the boundaries the note laid out (2400, 4800, 9600) and reads
        // the two stages, each sample against the hand-computed walk.
        PhasedValueSource p;
        p.set_setting("overlap", 0.01f);
        p.set_array("percent", {0.25f, 0.25f, 0.5f});
        auto one = c(1.0f), two = c(2.0f), four = c(4.0f);
        for (const auto& f : {one, two, four}) CHECK(p.add_param("sources", f));
        std::vector<float> out;
        p.prepare(RenderContext{48000});
        p.start(9600);
        pull(p, 1000, out);
        CHECK(p.clear_param("sources"));
        CHECK(p.add_param("sources", one) && p.add_param("sources", two));
        pull(p, 8600, out);
        const int halfOl = int(0.01f * float(48000)) / 2;
        const auto fade = [&](int pos) {            // stage 1 into stage 2 over the half overlap
            const float t = float(pos) / float(halfOl);
            return (1.0f * (1.0f - t) + 2.0f * 1.0f * t) * 1.0f;
        };
        const auto want = [&](int ptr) {
            if (ptr < 2400) return 2400 - ptr <= halfOl ? fade(halfOl - (2400 - ptr)) : 1.0f;
            const int pos = ptr - (ptr < 4800 ? 2400 : 4800);   // past the last boundary walked, the last stage
            return pos < halfOl ? fade(pos) : 2.0f;
        };
        bool finite = true, walked = true;
        int firstOff = -1;
        for (int i = 0; i < int(out.size()); ++i) {
            finite = finite && std::isfinite(out[size_t(i)]);
            const bool same = std::fabs(out[size_t(i)] - want(i)) <= 1e-6f;
            if (!same && firstOff < 0) firstOff = i;
            walked = walked && same;
        }
        if (firstOff >= 0) std::printf("  cut to two: sample %d is %g, want %g\n", firstOff, out[size_t(firstOff)], want(firstOff));
        CHECK(out.size() == 9600 && finite && walked);
    }
    {   // The next start after the list grew allocates nothing: the counters
        // and boundaries were reserved for the list when it changed.
        PhasedValueSource p;
        add_stages(p, {middle.rows[0]});
        p.prepare(RenderContext{48000});
        const auto later = feeds_of({middle.rows[1], middle.rows[2]});
        for (const auto& f : later) {
            f->prepare(RenderContext{48000});
            CHECK(p.add_param("sources", f));
        }
        alloc_probe::reset();
        alloc_probe::arm();
        p.start(9600);
        for (int i = 0; i < 9600; ++i) (void)p.next();
        alloc_probe::disarm();
        if (alloc_probe::count()) std::printf("  the start after the list grew: %zu allocation(s)\n", alloc_probe::count());
        CHECK(alloc_probe::count() == 0);
        alloc_probe::reset();
    }
}

void run_expand_rule_slot_tests() {
    using namespace list_twins;
    std::printf("Partials: the expandRule slot against set_expand_rule\n");
    for (int recurse : {0, 1}) {
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(recurse)));
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule recurse" + std::to_string(recurse)));
    }
    {   // The slot emptied: no expansion, by clear_param and by a null.
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(0)));
        CHECK(parts->clear_param("expandRule"));
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule none"));
        auto parts2 = host_partials();
        CHECK(parts2->set_param("expandRule", rule_node(0)));
        CHECK(parts2->set_param("expandRule", nullptr));
        auto a2 = host(parts2);
        CHECK(node_twins::render(*a2, kRuleFrames, kRuleNotes) == base("rule none"));
    }
    {   // A signal is refused and the rule stays.
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(0)));
        CHECK(!parts->set_param("expandRule", c(1.0f)));
        CHECK(parts->expandRule.get() != nullptr);
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule recurse0"));
    }
    {   // A rule wired after the host's prepare: the note arrays are sized again.
        auto parts = host_partials();
        auto a = host(parts);
        a->prepare(RenderContext{48000});
        CHECK(parts->set_param("expandRule", rule_node(0)));
        CHECK(render_prepared(*a, kRuleFrames, kRuleNotes) == base("rule recurse0"));
    }
    {   // The slot emptied after a note: the next notes play the unexpanded
        // partials, as a host that never had the rule plays its second and third.
        auto parts = host_partials();
        CHECK(parts->set_param("expandRule", rule_node(0)));
        auto a = host(parts);
        a->prepare(RenderContext{48000});
        (void)render_prepared(*a, kRuleFrames, 1);
        CHECK(parts->clear_param("expandRule"));
        auto never = host(host_partials());
        never->prepare(RenderContext{48000});
        (void)render_prepared(*never, kRuleFrames, 1);
        CHECK(render_prepared(*a, kRuleFrames, kRuleNotes) == render_prepared(*never, kRuleFrames, kRuleNotes));
    }
    {   // The rule is read when the slot changes: a later edit to the node is not seen.
        auto parts = host_partials();
        auto rule = rule_node(0);
        CHECK(parts->set_param("expandRule", rule));
        rule->set_setting("count", 5.0f);
        auto a = host(parts);
        CHECK(node_twins::render(*a, kRuleFrames, kRuleNotes) == base("rule recurse0"));
    }
}

void run_list_node_descriptor_tests() {
    std::printf("the list nodes' and the slot's descriptors\n");
    MultiSource m;
    CHECK(m.input_descriptors().size() == 1 && std::string(m.input_descriptors()[0].name) == "source");
    CHECK(m.input_descriptors()[0].multi && !m.input_descriptors()[0].slot);
    const auto ma = m.array_descriptors();
    CHECK(ma.size() == 2);
    CHECK(ma.size() == 2 && std::string(ma[0].name) == "weights" && !ma[0].groupName && ma[0].default_value == 1.0f);
    CHECK(ma.size() == 2 && std::string(ma[1].name) == "delays" && !ma[1].groupName && ma[1].default_value == 0.0f);
    CHECK(m.param_descriptors().empty() && m.setting_descriptors().empty());
    CHECK(m.pins().size() == 3);
    PhasedValueSource p;
    CHECK(p.input_descriptors().size() == 1 && std::string(p.input_descriptors()[0].name) == "sources");
    CHECK(p.input_descriptors()[0].multi && !p.input_descriptors()[0].slot);
    CHECK(p.setting_descriptors().size() == 1 && std::string(p.setting_descriptors()[0].name) == "overlap");
    CHECK(p.setting_descriptors()[0].type == SettingType::Float && p.setting_descriptors()[0].default_value == 0.05f);
    const auto pa = p.array_descriptors();
    const char* columns[] = {"percent", "minSec", "maxSec", "gain"};
    const float defaults[] = {0.0f, 0.0f, 0.0f, 1.0f};
    CHECK(pa.size() == 4);
    for (size_t k = 0; k < 4 && k < pa.size(); ++k) {
        CHECK(std::string(pa[k].name) == columns[k]);
        CHECK(pa[k].groupName && std::string(pa[k].groupName) == "stages");
        CHECK(pa[k].default_value == defaults[k]);
    }
    CHECK(p.param_descriptors().size() == 1 && std::string(p.param_descriptors()[0].name) == "amplitude");
    const std::shared_ptr<Component> hosts[] = {std::make_shared<FullPartials>(), std::make_shared<SequencePartials>(),
                                                std::make_shared<ExplicitPartials>()};
    for (const auto& h : hosts) {
        int found = 0;
        for (const auto& d : h->input_descriptors())
            if (std::string(d.name) == "expandRule") {
                ++found;
                CHECK(d.slot && !d.multi && d.hint && std::string(d.hint) == "an ExpandRule part");
            }
        CHECK(found == 1);
    }
}

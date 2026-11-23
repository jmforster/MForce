#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/source_registry.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/source/modulator/curve_node.h"
#include <nlohmann/json.hpp>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace mforce;

using CurveRows = std::vector<CurveNode::ExprKnot>;

// The curves the table tests read, each under one interp in one mode, and
// the grid of x it is read over: below the first knot, at each knot, between,
// above the last, 0 and a negative x.
struct CurveCase {
    const char* name;
    int interp;                                    // CurveNode::CurveInterp
    bool expr;                                     // expressions mode
    CurveKnots pts;
    CurveRows rows;
};
static std::vector<CurveCase> curve_cases() {
    using KF = CurveNode::KnotForm;
    const CurveKnots p2 = {{100.0f, 0.5f}, {1000.0f, 2.0f}};
    const CurveKnots p5 = {{20.0f, 0.1f}, {200.0f, 0.8f}, {1000.0f, 0.3f}, {5000.0f, 1.5f}, {16000.0f, 4.0f}};
    const CurveKnots p1 = {{440.0f, 3.0f}};
    const CurveKnots pz = {{100.0f, 0.0f}, {1000.0f, 1.0f}, {10000.0f, 4.0f}};
    const CurveRows e1 = {{440.0f, KF::Linear, 8.0f, 0.0f}};
    const CurveRows e4 = {{0.0f, KF::Linear, 0.0f, 0.1f},
                          {200.0f, KF::Power, 0.01f, 1.5f},
                          {2000.0f, KF::Linear, -2e-5f, 0.5f},
                          {12000.0f, KF::Power, 3.0f, 0.25f}};
    static const char* const in[] = {"linear", "logx", "loglog"};
    std::vector<CurveCase> cases;
    for (int i = 0; i < 3; ++i) {
        cases.push_back({in[i], i, false, p2, {}});
        cases.push_back({in[i], i, false, p5, {}});
        cases.push_back({in[i], i, false, p1, {}});
        cases.push_back({in[i], i, false, {}, {}});
    }
    cases.push_back({"loglog, a y of 0", 2, false, pz, {}});
    cases.push_back({"linear", 0, true, {}, e1});
    for (int i = 0; i < 3; ++i) cases.push_back({in[i], i, true, {}, e4});
    cases.push_back({"linear", 0, true, {}, {}});
    return cases;
}
static constexpr float kCurveGrid[] = {-50.0f, 0.0f, 10.0f, 20.0f, 50.0f, 100.0f, 150.0f, 200.0f, 440.0f,
                                       500.0f, 1000.0f, 1500.0f, 2000.0f, 5000.0f, 7500.0f, 10000.0f,
                                       12000.0f, 16000.0f, 20000.0f};
// map(x) over kCurveGrid for each of curve_cases(), as float bits, recorded
// from the node that held its knots as public vectors.
static constexpr uint32_t kCurveBaseMap[][std::size(kCurveGrid)] = {
    {0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F155555u, 0x3F2AAAABu, 0x3F888888u, 0x3F955556u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u},  // 0: points, linear, 2 knots, 0 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3E5DDDDEu, 0x3ED27D28u, 0x3F1B05B0u, 0x3F4CCCCDu, 0x3F266666u, 0x3F1CCCCDu, 0x3E99999Au, 0x3EE66667u, 0x3F19999Au, 0x3FC00000u, 0x40045D17u, 0x4028BA2Eu, 0x4045D174u, 0x40800000u, 0x40800000u},  // 1: points, linear, 5 knots, 0 rows
    {0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u},  // 2: points, linear, 1 knots, 0 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 3: points, linear, 0 knots, 0 rows
    {0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F439E7Au, 0x3F739874u, 0x3FBB8AFDu, 0x3FC633C6u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u},  // 4: points, logx, 2 knots, 0 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3EC1D25Bu, 0x3F16DAFDu, 0x3F366936u, 0x3F4CCCCDu, 0x3F0E17E2u, 0x3F03ED36u, 0x3E99999Au, 0x3F1A315Au, 0x3F511A96u, 0x3FC00000u, 0x4017C654u, 0x403F58F8u, 0x40586D5Eu, 0x40800000u, 0x40800000u},  // 5: points, logx, 5 knots, 0 rows
    {0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u},  // 6: points, logx, 1 knots, 0 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 7: points, logx, 0 knots, 0 rows
    {0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F236408u, 0x3F4249F8u, 0x3F9C29A2u, 0x3FA8A7F1u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u, 0x40000000u},  // 8: points, loglog, 2 knots, 0 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DCCCCCDu, 0x3E6A3F73u, 0x3EDB0793u, 0x3F1DF149u, 0x3F4CCCCDu, 0x3EFD536Bu, 0x3EEA56FAu, 0x3E99999Au, 0x3EE66666u, 0x3F19999Au, 0x3FC00000u, 0x400721FAu, 0x402C3B8Du, 0x4048DB2Cu, 0x40800000u, 0x40800000u},  // 9: points, loglog, 5 knots, 0 rows
    {0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u, 0x40400000u},  // 10: points, loglog, 1 knots, 0 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 11: points, loglog, 0 knots, 0 rows
    {0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3E345144u, 0x3E9A209Au, 0x3F24B951u, 0x3F32EFB3u, 0x3F800000u, 0x3FA36408u, 0x3FC249F8u, 0x4028A7F1u, 0x405749ABu, 0x40800000u, 0x40800000u, 0x40800000u, 0x40800000u},  // 12: points, loglog, a y of 0, 3 knots, 0 rows
    {0xC3C80000u, 0x00000000u, 0x42A00000u, 0x43200000u, 0x43C80000u, 0x44480000u, 0x44960000u, 0x44C80000u, 0x455C0000u, 0x457A0000u, 0x45FA0000u, 0x463B8000u, 0x467A0000u, 0x471C4000u, 0x476A6000u, 0x479C4000u, 0x47BB8000u, 0x47FA0000u, 0x481C4000u},  // 13: expressions, linear, 0 knots, 1 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DE2F115u, 0x3E37BFD5u, 0x3F757964u, 0x40A19999u, 0x415CDAA4u, 0x41E24630u, 0x42A01BFAu, 0x42BA8098u, 0x432FE53Au, 0x4321B6B8u, 0x3EEB8800u, 0x40FB2360u, 0x4178331Eu, 0x41C07AE1u, 0x41FB3144u, 0x4206F640u, 0x420EB471u},  // 14: expressions, linear, 0 knots, 4 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DE2F115u, 0x3E37BFD5u, 0x3F757964u, 0x40A19999u, 0x415CDAA4u, 0x41E24630u, 0x4273700Au, 0x428703C2u, 0x42BF0F20u, 0x4291FCFCu, 0x3EEB8800u, 0x41518A4Cu, 0x41A57E3Au, 0x41D7D2A7u, 0x41FB3144u, 0x4206F640u, 0x420EB471u},  // 15: expressions, logx, 0 knots, 4 rows
    {0x3DCCCCCDu, 0x3DCCCCCDu, 0x3DD8EF67u, 0x3DFEF7A0u, 0x3E79B27Du, 0x3F800000u, 0x409FAE47u, 0x41E24630u, 0x4175D7ADu, 0x414E1FD9u, 0x4058BF1Bu, 0x3F926D7Eu, 0x3EEB851Fu, 0x40552143u, 0x410D9F69u, 0x419635DCu, 0x41FB3144u, 0x4206F640u, 0x420EB471u},  // 16: expressions, loglog, 0 knots, 4 rows
    {0xC2480000u, 0x00000000u, 0x41200000u, 0x41A00000u, 0x42480000u, 0x42C80000u, 0x43160000u, 0x43480000u, 0x43DC0000u, 0x43FA0000u, 0x447A0000u, 0x44BB8000u, 0x44FA0000u, 0x459C4000u, 0x45EA6000u, 0x461C4000u, 0x463B8000u, 0x467A0000u, 0x469C4000u},  // 17: expressions, linear, 0 knots, 0 rows
};

void run_curve_table_tests() {
    std::printf("the Curve's tables: the twin for the knot lookup, the columns, interp and mode, the load\n");
    using KF = CurveNode::KnotForm;
    static const char* const kColumns[] = {"x", "y", "exprX", "exprForm", "exprA", "exprB"};
    auto expr_cell = [](const CurveNode::ExprKnot& e, int c) {
        return c == 0 ? e.x : c == 1 ? float(int(e.form)) : c == 2 ? e.a : e.b;
    };
    // Column c of a case's rows: 0 and 1 the knots, 2 to 5 the expression rows.
    auto column = [&](const CurveCase& k, int c) {
        std::vector<float> col;
        if (c < 2) for (const auto& p : k.pts) col.push_back(c == 0 ? p.first : p.second);
        else       for (const auto& e : k.rows) col.push_back(expr_cell(e, c - 2));
        return col;
    };
    auto by_setters = [](const CurveCase& k) {
        auto n = std::make_shared<CurveNode>();
        n->set_setting("interp", float(k.interp));
        n->set_setting("mode", k.expr ? 1.0f : 0.0f);
        n->set_knots(k.pts);
        n->set_expr_knots(k.rows);
        return n;
    };
    // The same curve written as the six columns, in declaration order or reversed.
    auto by_columns = [&](const CurveCase& k, bool reverse) {
        auto n = std::make_shared<CurveNode>();
        n->set_setting("interp", float(k.interp));
        n->set_setting("mode", k.expr ? 1.0f : 0.0f);
        for (int j = 0; j < 6; ++j) {
            const int c = reverse ? 5 - j : j;
            n->set_array(kColumns[c], column(k, c));
        }
        return n;
    };
    auto same_knots = [](const CurveKnots& a, const CurveKnots& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!same_bits(a[i].first, b[i].first) || !same_bits(a[i].second, b[i].second)) return false;
        return true;
    };
    auto same_rows = [](const CurveRows& a, const CurveRows& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!same_bits(a[i].x, b[i].x) || a[i].form != b[i].form || !same_bits(a[i].a, b[i].a)
                || !same_bits(a[i].b, b[i].b))
                return false;
        return true;
    };
    auto same_map = [](const CurveNode& a, const CurveNode& b) {
        for (float x : kCurveGrid)
            if (!same_bits(a.map(x), b.map(x))) return false;
        return true;
    };

    // (1) The knot lookup: the setters and set_array give the
    // recorded map(x), bit for bit, and next() through a wired constant agrees.
    const auto cases = curve_cases();
    CHECK(cases.size() == std::size(kCurveBaseMap));
    for (size_t c = 0; c < cases.size() && c < std::size(kCurveBaseMap); ++c) {
        const auto a = by_setters(cases[c]), b = by_columns(cases[c], false), r = by_columns(cases[c], true);
        int bad = 0;
        for (size_t i = 0; i < std::size(kCurveGrid); ++i) {
            const float x = kCurveGrid[i];
            const float v = a->map(x);
            if (std::bit_cast<uint32_t>(v) != kCurveBaseMap[c][i]) ++bad;
            if (!same_bits(v, b->map(x)) || !same_bits(v, r->map(x))) ++bad;
            auto wired = by_columns(cases[c], false);
            wired->set_param("source", std::make_shared<ConstantSource>(x));
            wired->next();
            if (!same_bits(wired->current(), v)) ++bad;
        }
        if (bad) std::printf("  curve case %zu (%s, %s): %d of %zu differ\n", c,
                             cases[c].expr ? "expressions" : "points", cases[c].name, bad, std::size(kCurveGrid));
        CHECK(bad == 0);
    }

    // (2) The columns follow the setters, and the rows follow the columns.
    for (const CurveCase& k : cases) {
        const auto a = by_setters(k);
        bool ok = true;
        for (int c = 0; c < 6; ++c) {
            const std::vector<float> want = column(k, c), got = a->get_array(kColumns[c]);
            ok = ok && want.size() == got.size();
            for (size_t i = 0; ok && i < want.size(); ++i) ok = same_bits(want[i], got[i]);
        }
        CHECK(ok);
        CHECK(same_knots(a->knots(), k.pts) && same_rows(a->expr_knots(), k.rows));
        const auto b = by_columns(k, true);
        CHECK(same_knots(b->knots(), k.pts) && same_rows(b->expr_knots(), k.rows));
    }
    {
        // A form cell is truncated and clamped: 1 and above Power, below 1 Linear.
        CurveNode n;
        n.set_array("exprX", {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f});
        n.set_array("exprForm", {1.0f, 0.0f, 2.7f, -1.0f, 0.9f, 1.5f});
        n.set_array("exprA", {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f});
        n.set_array("exprB", {2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f});
        const auto& rows = n.expr_knots();
        CHECK(rows.size() == 6);
        if (rows.size() == 6)
            CHECK(rows[0].form == KF::Power && rows[1].form == KF::Linear && rows[2].form == KF::Power
                  && rows[3].form == KF::Linear && rows[4].form == KF::Linear && rows[5].form == KF::Power);
        // A group's rows are as many as its shortest column has cells.
        CurveNode m;
        m.set_array("x", {1.0f, 2.0f, 3.0f});
        m.set_array("y", {4.0f, 5.0f});
        CHECK(same_knots(m.knots(), CurveKnots{{1.0f, 4.0f}, {2.0f, 5.0f}}));
        m.set_array("exprX", {1.0f, 2.0f});
        CHECK(m.expr_knots().empty());
        // The setters replace the columns whole.
        m.set_knots(CurveKnots{{7.0f, 8.0f}});
        CHECK(m.get_array("x") == std::vector<float>{7.0f} && m.get_array("y") == std::vector<float>{8.0f});
        m.set_knots(CurveKnots{});
        CHECK(m.knots().empty() && m.get_array("x").empty() && m.get_array("y").empty());
    }

    // (3) interp and mode are settings; their labels are the words the files hold.
    {
        CurveNode n;
        CHECK(n.interp.value == int(CurveNode::CurveInterp::Linear) && n.mode.value == 0);
        n.set_setting("interp", 2.0f);
        CHECK(n.interp.value == int(CurveNode::CurveInterp::LogLog));
        n.set_setting("mode", 1.0f);
        CHECK(n.mode.value == 1 && n.get_setting("mode") == 1.0f);
        auto labels_are = [](const char* const* got, std::initializer_list<const char*> want) {
            if (!got) return false;
            size_t i = 0;
            for (const char* w : want) {
                if (!got[i] || std::strcmp(got[i], w) != 0) return false;
                ++i;
            }
            return got[i] == nullptr;
        };
        int found = 0;
        for (const auto& d : n.setting_descriptors()) {
            if (std::strcmp(d.name, "interp") == 0) { ++found; CHECK(labels_are(d.enum_labels, {"linear", "logx", "loglog"})); }
            if (std::strcmp(d.name, "mode") == 0)   { ++found; CHECK(labels_are(d.enum_labels, {"points", "expressions"})); }
        }
        CHECK(found == 2);
        std::string arrays;
        for (const auto& d : n.array_descriptors()) {
            arrays += std::string(arrays.empty() ? "" : " ") + d.name + "/" + (d.groupName ? d.groupName : "-");
            if (std::strcmp(d.name, "exprForm") == 0) CHECK(labels_are(d.enum_labels, {"linear", "power"}));
            else CHECK(d.enum_labels == nullptr);
        }
        CHECK(arrays == "x/knots y/knots exprX/exprKnots exprForm/exprKnots exprA/exprKnots exprB/exprKnots");

        // An interp outside the labels' range reads as linear in both modes.
        for (const CurveCase& k : cases) {
            if (k.interp != 0) continue;
            const auto linear = by_setters(k);
            auto five = by_setters(k);
            five->set_setting("interp", 5.0f);
            CHECK(five->interp.value == 5);
            CHECK(same_map(*five, *linear));
        }
    }

    // (4) The load: each table as its columns; labels exactly, another case refused; the refusals.
    {
        auto patch = [](const std::string& curveParams) {
            return std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
                {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
                {"id":"cv","type":"CurveNode","params":{"source":{"ref":"pf"},)") + curveParams + R"(}},
                {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"cv"}}}]}})";
        };
        auto curve_of = [](const std::string& json) {
            auto ip = load_inst(json);
            auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
            return std::dynamic_pointer_cast<CurveNode>(pi->pool().voice(0).nodes().at("cv"));
        };
        auto refusal = [](const std::string& json) {
            return load_refused(json);
        };
        const std::string xy = R"("interp":"logx","x":[100.0,1000.0,5000.0],"y":[0.002,0.2,0.05])";
        const auto fromXY = curve_of(patch(xy));
        CHECK(fromXY && fromXY->knots().size() == 3);
        CHECK(same_knots(fromXY->knots(), CurveKnots{{100.0f, 0.002f}, {1000.0f, 0.2f}, {5000.0f, 0.05f}}));
        CHECK(fromXY->interp.value == int(CurveNode::CurveInterp::LogX) && fromXY->mode.value == 0);

        const std::string labels = R"("mode":"expressions","interp":"loglog","exprX":[0.0,200.0,2000.0,12000.0],)"
                                   R"("exprForm":["linear","power","linear","power"],"exprA":[0.0,0.0001,-2e-5,0.03],)"
                                   R"("exprB":[0.001,1.5,0.05,0.25])";
        const std::string indices = R"("mode":"expressions","interp":"loglog","exprX":[0.0,200.0,2000.0,12000.0],)"
                                    R"("exprForm":[0,1,0,1],"exprA":[0.0,0.0001,-2e-5,0.03],"exprB":[0.001,1.5,0.05,0.25])";
        const auto fromLabels = curve_of(patch(labels)), fromIndices = curve_of(patch(indices));
        CHECK(fromLabels && fromIndices && fromLabels->expr_knots().size() == 4);
        CHECK(same_rows(fromLabels->expr_knots(), fromIndices->expr_knots()));
        CHECK(same_map(*fromLabels, *fromIndices));
        CHECK(fromLabels->mode.value == 1 && fromLabels->interp.value == int(CurveNode::CurveInterp::LogLog));
        CHECK(fromLabels->expr_knots().size() == 4 && fromLabels->expr_knots()[2].form == KF::Linear
              && fromLabels->expr_knots()[1].form == KF::Power);

        // a column's label, interp and mode in another case are refused, and so is a word that matches no label
        const std::string casedForm = refusal(patch(R"("mode":"expressions","interp":"loglog","exprX":[0.0,200.0],)"
                                                    R"("exprForm":["linear","POWER"],"exprA":[0.0,0.0001],"exprB":[0.001,1.5])"));
        CHECK(casedForm.find(R"('exprForm' holds "POWER", which is not one of its labels)") != std::string::npos);
        const std::string cased = refusal(patch(R"("interp":"LogLog","mode":"EXPRESSIONS")"));
        CHECK(cased.find(R"('interp' holds "LogLog", which is not one of its labels)") != std::string::npos);
        const std::string unknownSetting = refusal(patch(R"("interp":"cubic","mode":"spline")"));
        CHECK(unknownSetting.find(R"('interp' holds "cubic", which is not one of its labels)") != std::string::npos);
        const auto absent = curve_of(patch(R"("x":[0.0,1.0],"y":[0.0,1.0])"));
        CHECK(absent->interp.value == 0 && absent->mode.value == 0);

        // A table missing a column is ragged.
        const std::string ragged = refusal(patch(R"("x":[100.0,1000.0])"));
        CHECK(ragged.find("node 'cv' (CurveNode): table 'knots' takes its columns at one length, all present or all absent")
              != std::string::npos);
        CHECK(ragged.find("y absent") != std::string::npos);
        // An exprForm label outside the two is refused.
        const std::string badForm = refusal(patch(R"("exprX":[0.0],"exprForm":["cubic"],"exprA":[1.0],"exprB":[0.0])"));
        CHECK(badForm.find("node 'cv' (CurveNode): 'exprForm' holds \"cubic\"") != std::string::npos);
    }
}

// A retired type name (one with a hint) fails the load naming the tool that
// rewrites it; an unknown name with no hint fails without naming it.
void run_retired_type_message_tests() {
    auto message_for = [](const char* type) {
        const std::string json = std::string(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"n","nodes":[
            {"id":"n","type":")") + type + R"(","params":{}}]}})";
        return load_refused(json);
    };
    for (const char* t : {"ASEnvelope", "ADSREnvelope", "KSPianoString"}) {
        const std::string msg = message_for(t);
        CHECK(msg.find(std::string("Unknown node type: ") + t) != std::string::npos);
        CHECK(msg.find("tools/convert_patch.py") != std::string::npos);
    }
    const std::string none = message_for("NoSuchType");
    CHECK(none.find("Unknown node type: NoSuchType") != std::string::npos);
    CHECK(none.find("convert_patch") == std::string::npos);
    CHECK(retired_type_hint("Envelope") == nullptr && retired_type_hint("ASREnvelope") != nullptr);
    CHECK(retired_type_hint("AdditiveSource2") != nullptr && retired_type_hint("BasicAdditiveSource") != nullptr);
    CHECK(std::string(retired_type_hint("AdditiveSource2")).find("author an AdditiveSource") != std::string::npos);
    for (const char* t : {"Formant", "FormantSpectrum"})
        CHECK(std::string(retired_type_hint(t)).find("rows of a FormantPeaks") != std::string::npos);
    CHECK(std::string(retired_type_hint("FixedSpectrum")).find("FormantBands or FormantPeaks") != std::string::npos);
    CHECK(message_for("FormantSpectrum").find("convert_patch.py") != std::string::npos);
    CHECK(std::string(retired_type_hint("BandSpectrum")).find("renamed FormantBands") != std::string::npos);
    CHECK(message_for("BandSpectrum").find("Unknown node type: BandSpectrum") != std::string::npos);
}

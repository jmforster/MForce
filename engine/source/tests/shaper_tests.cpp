#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/core/curve.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/source/filter/shaper_source.h"
#include "mforce/source/modulator/curve_node.h"
#include "mforce/source/utility/wormhole_source.h"
#include <cmath>
#include <memory>

using namespace mforce;


void run_shaper_tests() {
    RenderContext ctx{48000};
    // Default identity curve passes input through
    auto sh = std::make_shared<ShaperSource>();
    auto in = std::make_shared<ConstantSource>(0.5f);
    sh->set_param("source", in);
    sh->prepare(ctx); sh->start(100);
    CHECK_NEAR(sh->next(), 0.5f, 1e-3f);

    // Clamp past the drawn ends
    in->set(3.0f); in->next();
    CHECK_NEAR(sh->next(), 1.0f, 1e-3f);
    in->set(-3.0f); in->next();
    CHECK_NEAR(sh->next(), -1.0f, 1e-3f);

    // drive scales input BEFORE the lookup: drive 2 pushes 0.4 to 0.8
    sh->set_param("drive", std::make_shared<ConstantSource>(2.0f));
    in->set(0.4f); in->next();
    CHECK_NEAR(sh->next(), 0.8f, 1e-3f);

    // A drawn dead-zone curve: flat 0 across [-0.5, 0.5], ramps outside.
    // smoothness 0.5 = linear segments (0 = hold step, 1 = sine).
    auto dz = std::make_shared<ShaperSource>();
    dz->set_array("values", {-1.0f, -1.0f, -0.5f, 0.0f, 0.5f, 0.0f, 1.0f, 1.0f});
    dz->set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    auto in2 = std::make_shared<ConstantSource>(0.2f);
    dz->set_param("source", in2);
    dz->prepare(ctx); dz->start(100);
    float mid = dz->next();
    CHECK(std::fabs(mid) < 0.05f);           // inside the dead zone
    in2->set(0.75f); in2->next();
    float hi = dz->next();
    CHECK_NEAR(hi, 0.5f, 1e-3f);             // linear outer ramp midpoint

    // smoothness 0 steps: same input holds the segment's START value (0)
    dz->set_param("smoothness", std::make_shared<ConstantSource>(0.0f));
    CHECK_NEAR(dz->next(), 0.0f, 1e-6f);

    // values round-trip via get_array (UI re-pull path)
    auto back = dz->get_array("values");
    CHECK(back.size() == 8 && back[2] == -0.5f);
}

// Shaper per-segment overrides. "segs" array:
// flat [typeIdx, power] per segment; typeIdx 0 = default (smoothness),
// else 1 + int(RampType).
void run_shaper_seg_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-1.0f, 0.0f,0.0f, 1.0f,1.0f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.prepare(ctx); sh.start(8);
    sh.next();
    // Segment 1 (0->1) overridden to Hold (typeIdx 5): y stays 0 across it.
    sh.set_array("segs", {0.0f,0.0f, 5.0f,0.0f});
    CHECK_NEAR(sh.map(0.5f), 0.0f, 1e-9f);
    // Segment 0 default: smoothness 0.5 == lerp
    CHECK_NEAR(sh.map(-0.5f), -0.5f, 1e-6f);
    // Expo override with power 2 on segment 1 (typeIdx 2 = 1 + Expo)
    sh.set_array("segs", {0.0f,0.0f, 2.0f,2.0f});
    CHECK_NEAR(sh.map(0.5f), 0.25f, 1e-6f);   // t^2 at t=0.5
    // Round-trip: get_array returns what was set (Expo override live)
    auto back = sh.get_array("segs");
    CHECK(back.size() == 4);
    // A fresh node has no segs and an all-default set encodes to empty
    ShaperSource fresh;
    CHECK(fresh.get_array("segs").empty());
    sh.set_array("segs", {0.0f,0.0f, 0.0f,0.0f});
    CHECK(sh.get_array("segs").empty());
    // Smoothness must NOT bend an overridden Linear segment (typeIdx 1)
    sh.set_array("segs", {0.0f,0.0f, 1.0f,0.0f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(1.0f));
    sh.next();
    CHECK_NEAR(sh.map(0.5f), 0.5f, 1e-6f);
}

// Wormhole: pure pass-through, including chained pairs (out-half refs
// in-half — the hidden-wire configuration the UI creates).
void run_wormhole_tests() {
    RenderContext ctx{48000};
    auto in = std::make_shared<ConstantSource>(0.42f);
    WormholeSource wh;
    wh.set_param("source", in);
    wh.prepare(ctx); wh.start(8);
    CHECK_NEAR(wh.next(), 0.42f, 1e-9f);
    auto whIn = std::make_shared<WormholeSource>();
    whIn->set_param("source", in);
    WormholeSource whOut;
    whOut.set_param("source", whIn);
    whOut.prepare(ctx); whOut.start(8);
    CHECK_NEAR(whOut.next(), 0.42f, 1e-9f);
    // Unwired = silence, not garbage
    WormholeSource bare;
    bare.prepare(ctx); bare.start(8);
    CHECK_NEAR(bare.next(), 0.0f, 1e-9f);
}

// Shaper morph pin: point-space A/B blend.
void run_shaper_morph_tests() {
    RenderContext ctx{48000};
    ShaperSource sh;
    sh.set_array("values",  {-1.0f,-1.0f, 0.4f,0.2f, 1.0f,1.0f});
    sh.set_array("values2", {-1.0f,-1.0f, 0.1f,0.6f, 1.0f,1.0f});
    auto morph = std::make_shared<ConstantSource>(0.0f);
    sh.set_param("morph", morph);
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.5f));
    sh.prepare(ctx); sh.start(8);
    sh.next();
    // m=0: exactly curve A (knee at 0.4)
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-7f);
    // m=1: exactly curve B (knee at 0.1)
    morph->set(1.0f); morph->next(); sh.next();
    CHECK_NEAR(sh.map(0.1f), 0.6f, 1e-7f);
    // m=0.5: the knee SLIDES to (0.25, 0.4) — point-space, not output blend
    morph->set(0.5f); morph->next(); sh.next();
    CHECK_NEAR(sh.map(0.25f), 0.4f, 1e-6f);
    // Segment powers lerp when both sides override the same segment
    sh.set_array("segs",  {0.0f,0.0f, 2.0f,1.0f});   // seg1 Expo p1
    sh.set_array("segs2", {0.0f,0.0f, 2.0f,3.0f});   // seg1 Expo p3
    // blended seg1: Expo p2 between blended points (0.25,0.4)->(1,1):
    // at x=0.625 (t=0.5): 0.4 + 0.6*0.25 = 0.55
    CHECK_NEAR(sh.map(0.625f), 0.55f, 1e-5f);
    // Signed-curvature lerp across the Expo/InverseExpo axis: A Expo p2
    // (bulge down) vs B InverseExpo p2 (bulge up) at m=0.5 cancels to
    // linear — segment midpoint lands on the chord midpoint.
    sh.set_array("segs",  {0.0f,0.0f, 2.0f,2.0f});
    sh.set_array("segs2", {0.0f,0.0f, 3.0f,2.0f});
    CHECK_NEAR(sh.map(0.625f), 0.7f, 1e-5f);
    // Mismatched point count: morph ignored (B degenerates to A)
    sh.set_array("segs", {}); sh.set_array("segs2", {});
    sh.set_array("values2", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(sh.map(0.4f), 0.2f, 1e-6f);
    // No values2 at all: byte-identity path
    ShaperSource plain;
    plain.set_array("values", {-1.0f,-1.0f, 1.0f,1.0f});
    CHECK_NEAR(plain.map(0.3f), 0.3f, 1e-7f);
}

// Shared Curve evaluator parity: Curve::eval gives CurveNode::map (all three
// domains, smoothness 0.5 == exact lerp) and ShaperSource::map
// (SmoothnessInterpolator path).
void run_curve_shared_tests() {
    RenderContext ctx{48000};

    // Parity vs CurveNode::map — Linear domain
    CurveNode lin;
    lin.set_knots(CurveKnots{{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}});
    for (float x : {-1.0f, 0.0f, 0.1f, 0.2f, 0.5f, 0.925f, 1.0f, 2.0f})
        CHECK_NEAR(Curve::eval(lin.knots(), {}, Curve::Domain::Linear, 0.5f, x),
                   lin.map(x), 1e-7f);

    // Parity vs CurveNode::map — LogX and LogLog domains
    CurveNode logx; logx.interp.value = int(CurveNode::CurveInterp::LogX);
    logx.set_knots(CurveKnots{{100.0f, 0.0f}, {10000.0f, 2.0f}});
    CHECK_NEAR(Curve::eval(logx.knots(), {}, Curve::Domain::LogX, 0.5f, 1000.0f),
               logx.map(1000.0f), 1e-6f);
    CurveNode ll; ll.interp.value = int(CurveNode::CurveInterp::LogLog);
    ll.set_knots(CurveKnots{{10.0f, 100.0f}, {100.0f, 10000.0f}});
    CHECK_NEAR(Curve::eval(ll.knots(), {}, Curve::Domain::LogLog, 0.5f,
                           31.6227766f),
               ll.map(31.6227766f), 0.5f);

    // Parity vs ShaperSource::map — smoothness 0.6, asymmetric curve
    ShaperSource sh;
    sh.set_array("values", {-1.0f,-0.76f, -0.87f,-0.73f, -0.1f,-0.2f,
                             0.0f,0.0f, 0.11f,0.19f, 0.78f,0.57f,
                             0.93f,0.66f});
    sh.set_param("smoothness", std::make_shared<ConstantSource>(0.6f));
    sh.set_param("drive", std::make_shared<ConstantSource>(1.0f));
    sh.set_param("source", std::make_shared<ConstantSource>(0.0f));
    sh.prepare(ctx); sh.start(8);
    sh.next();  // latches smoothness into the evaluator
    auto vals = sh.get_array("values");
    for (float x : {-1.5f, -0.9f, -0.3f, 0.0f, 0.05f, 0.5f, 0.9f, 1.5f})
        CHECK_NEAR(Curve::eval_flat(vals, {}, Curve::Domain::Linear, 0.6f, x),
                   sh.map(x), 1e-6f);

    // Identity on empty
    CHECK_NEAR(Curve::eval({}, {}, Curve::Domain::Linear, 0.5f, 123.0f),
               123.0f, 1e-9f);
}

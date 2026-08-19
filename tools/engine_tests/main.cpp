// Assert-style engine unit tests (PerformSource P1, plan_perform_source_p1.md).
// House testing is render-based; this exe covers the value-level semantics
// renders can't isolate (curve interp identity, envelope mapping, adapters).
#include <cstdio>
#include <cmath>
#include <cstdlib>

static int g_checks = 0, g_fails = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, eps) do { ++g_checks; float _a=(a), _b=(b); \
  if (std::fabs(_a-_b) > (eps)) { ++g_fails; \
  std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", __FILE__, __LINE__, #a, _a, #b, _b); } } while (0)

#include "mforce/core/curve_node.h"
#include <memory>
using namespace mforce;

static void run_curve_node_tests() {
    // Linear (vcurve semantics): clamp + linear interp
    CurveNode lin;
    lin.knots = {{0.0f, 0.3f}, {0.2f, 0.3f}, {0.85f, 1.0f}, {1.0f, 1.6f}};
    CHECK_NEAR(lin.map(-1.0f), 0.3f, 1e-6f);   // clamp low
    CHECK_NEAR(lin.map(0.1f),  0.3f, 1e-6f);   // flat segment
    CHECK_NEAR(lin.map(1.0f),  1.6f, 1e-6f);   // clamp high
    CHECK_NEAR(lin.map(0.925f), 1.3f, 1e-3f);  // midpoint of last segment

    // LogX (default paramMap curve semantics): value linear in log(x)
    CurveNode logx; logx.interp = CurveNode::CurveInterp::LogX;
    logx.knots = {{100.0f, 0.0f}, {10000.0f, 2.0f}};
    CHECK_NEAR(logx.map(1000.0f), 1.0f, 1e-4f);  // geometric midpoint

    // LogLog: 2 knots == exact power law (y = x^2 through (10,100),(100,10000))
    CurveNode ll; ll.interp = CurveNode::CurveInterp::LogLog;
    ll.knots = {{10.0f, 100.0f}, {100.0f, 10000.0f}};
    CHECK_NEAR(ll.map(31.6227766f), 1000.0f, 0.5f);

    // Pull path: source wired, evaluates on change
    auto cs = std::make_shared<ConstantSource>(0.5f);
    CurveNode wired;
    wired.knots = {{0.0f, 0.0f}, {1.0f, 10.0f}};
    wired.set_param("source", cs);
    wired.next();
    CHECK_NEAR(wired.current(), 5.0f, 1e-6f);

    // Empty knots = identity
    CurveNode ident;
    CHECK_NEAR(ident.map(123.0f), 123.0f, 1e-6f);
}

int main() {
    run_curve_node_tests();
    if (g_fails) { std::printf("%d/%d FAILED\n", g_fails, g_checks); return 1; }
    std::printf("ALL PASS (%d checks)\n", g_checks);
    return 0;
}

#include "mforce/testing/check.h"
#include <cmath>
#include <cstdio>
#include <exception>

namespace mforce::testing {

namespace {

// The process-wide count of checks made and failed: the tests' one
// file-level mutable variable. Every test reaches it through check().
Tally g_tally{0, 0};

}  // namespace

Tally tally() { return g_tally; }

bool check(bool passed, const char* file, int line, const char* text) {
  ++g_tally.checks;
  if (!passed) {
    ++g_tally.fails;
    std::printf("FAIL %s:%d  %s\n", file, line, text);
  }
  return passed;
}

bool check_near(float a, float b, double eps, const char* file, int line,
                const char* textA, const char* textB) {
  ++g_tally.checks;
  // Written so that a NaN on either side fails: every comparison with NaN is false.
  const bool passed = std::fabs(a - b) <= eps;
  if (!passed) {
    ++g_tally.fails;
    std::printf("FAIL %s:%d  %s=%g vs %s=%g\n", file, line, textA, double(a), textB, double(b));
  }
  return passed;
}

int run_tests(std::span<const Test> tests) {
  const Tally before = g_tally;
  for (const Test& t : tests) {
    try {
      t.run();
    } catch (const RequireFailed&) {
      // counted where it failed
    } catch (const std::exception& e) {
      std::printf("  %s threw: %s\n", t.name, e.what());
      check(false, __FILE__, __LINE__, t.name);
    } catch (...) {
      std::printf("  %s threw something other than a std::exception\n", t.name);
      check(false, __FILE__, __LINE__, t.name);
    }
  }
  const int checks = g_tally.checks - before.checks;
  const int fails = g_tally.fails - before.fails;
  if (fails) {
    std::printf("%d/%d FAILED\n", fails, checks);
    return 1;
  }
  if (checks == 0) {
    std::printf("NO CHECKS MADE\n");
    return 1;
  }
  std::printf("ALL PASS (%d checks)\n", checks);
  return 0;
}

}  // namespace mforce::testing

// The harness tests itself: every other test program's verdict rests on these
// promises. Each is a small set of tests, most failing on purpose, handed to
// run_tests; what run_tests returns and what it added to the tally are then
// compared with what the harness promises. The expected failures print their
// FAIL lines and a "FAILED" summary; this program's own verdict is the last
// line and its exit code.
#include "mforce/testing/check.h"
#include <cstdio>
#include <limits>
#include <span>
#include <stdexcept>

using mforce::testing::Tally;
using mforce::testing::Test;

namespace {

void failing_check() { CHECK(1 + 1 == 3); }
void passing_check() { CHECK(true); }
void throwing() { throw std::runtime_error("thrown on purpose"); }
void throwing_int() { throw 7; }
void failing_require() {
  REQUIRE(false);
  CHECK(true);   // not reached: REQUIRE ended the test
}
void near_inside() { CHECK_NEAR(1.0f, 1.05f, 0.1); }
void near_outside() { CHECK_NEAR(1.0f, 1.2f, 0.1); }
void near_nan() { CHECK_NEAR(std::numeric_limits<float>::quiet_NaN(), 1.0f, 0.1); }

// What one run of `tests` returned and added to the tally.
struct Outcome { int result; int checks; int fails; };

Outcome run_and_measure(std::span<const Test> tests) {
  const Tally before = mforce::testing::tally();
  const int result = mforce::testing::run_tests(tests);
  const Tally after = mforce::testing::tally();
  return {result, after.checks - before.checks, after.fails - before.fails};
}

bool holds(Outcome o, int result, int checks, int fails) {
  return o.result == result && o.checks == checks && o.fails == fails;
}

// One promise: its text and whether it held.
struct Promise { const char* text; bool held; };

constexpr Test kFailed[] = {{"failing_check", failing_check}};
constexpr Test kPassed[] = {{"passing_check", passing_check}, {"near_inside", near_inside}};
constexpr Test kThrown[] = {{"throwing", throwing}, {"passing_check", passing_check}};
constexpr Test kThrownInt[] = {{"throwing_int", throwing_int}, {"passing_check", passing_check}};
constexpr Test kRequired[] = {{"failing_require", failing_require}};
constexpr Test kNearOutside[] = {{"near_outside", near_outside}};
constexpr Test kNearNan[] = {{"near_nan", near_nan}};

}  // namespace

int main() {
  const Promise promises[] = {
    {"a failed CHECK makes run_tests return 1", holds(run_and_measure(kFailed), 1, 1, 1)},
    {"a set whose checks all pass makes run_tests return 0, CHECK_NEAR inside its tolerance included",
     holds(run_and_measure(kPassed), 0, 2, 0)},
    {"a test that throws is caught, counted as one failure, and the next test runs",
     holds(run_and_measure(kThrown), 1, 2, 1)},
    {"a test that throws something other than a std::exception is caught and counted too",
     holds(run_and_measure(kThrownInt), 1, 2, 1)},
    {"a failed REQUIRE ends its test and is counted once", holds(run_and_measure(kRequired), 1, 1, 1)},
    {"CHECK_NEAR fails outside its tolerance", holds(run_and_measure(kNearOutside), 1, 1, 1)},
    {"CHECK_NEAR fails when a side is NaN", holds(run_and_measure(kNearNan), 1, 1, 1)},
    {"a list that makes no check makes run_tests return 1",
     holds(run_and_measure(std::span<const Test>{}), 1, 0, 0)},
  };
  int held = 0;
  for (const Promise& p : promises) {
    std::printf("%s: %s\n", p.held ? "holds" : "BROKEN", p.text);
    held += p.held;
  }
  const int n = static_cast<int>(std::size(promises));
  std::printf("testing_tests: %d of %d hold\n", held, n);
  return held == n ? 0 : 1;
}

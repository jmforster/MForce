// The harness tests itself: every other test program's verdict rests on these
// three. Each is a small set of tests that fail on purpose, handed to
// run_tests; what run_tests returns and what it added to the tally are then
// compared with what the harness promises. The expected failures print their
// FAIL lines and a "FAILED" summary; this program's own verdict is the last
// line and its exit code.
#include "mforce/testing/check.h"
#include <cstdio>
#include <stdexcept>

using mforce::testing::Tally;
using mforce::testing::Test;

namespace {

void failing_check() { CHECK(1 + 1 == 3); }
void passing_check() { CHECK(true); }
void throwing() { throw std::runtime_error("thrown on purpose"); }
void failing_require() {
  REQUIRE(false);
  CHECK(true);   // not reached: REQUIRE ended the test
}

// What one run of `tests` returned and added to the tally.
struct Outcome { int result; int checks; int fails; };

Outcome run_and_measure(std::span<const Test> tests) {
  const Tally before = mforce::testing::tally();
  const int result = mforce::testing::run_tests(tests);
  const Tally after = mforce::testing::tally();
  return {result, after.checks - before.checks, after.fails - before.fails};
}

bool report(const char* promise, bool held) {
  std::printf("%s: %s\n", held ? "holds" : "BROKEN", promise);
  return held;
}

}  // namespace

int main() {
  const Test failed[] = {{"failing_check", failing_check}};
  const Test thrown[] = {{"throwing", throwing}, {"passing_check", passing_check}};
  const Test required[] = {{"failing_require", failing_require}};

  const Outcome a = run_and_measure(failed);
  const Outcome b = run_and_measure(thrown);
  const Outcome c = run_and_measure(required);

  int held = 0;
  held += report("a failed CHECK makes run_tests return 1",
                 a.result == 1 && a.checks == 1 && a.fails == 1);
  held += report("a test that throws is caught, counted as one failure, and the next test runs",
                 b.result == 1 && b.checks == 2 && b.fails == 1);
  held += report("a failed REQUIRE ends its test and is counted once",
                 c.result == 1 && c.checks == 1 && c.fails == 1);
  std::printf("testing_tests: %d of 3 hold\n", held);
  return held == 3 ? 0 : 1;
}

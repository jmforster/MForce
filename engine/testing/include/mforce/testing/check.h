#pragma once
#include <span>

// The one test harness. A test is a function that makes checks; a test
// program's main lists its tests and hands them to run_tests.
//
//   CHECK(cond)            counts a check; on failure prints file, line and the text
//   CHECK_NEAR(a, b, eps)  as CHECK, for |a - b| <= eps, both sides converted to float;
//                          a NaN on either side fails
//   REQUIRE(cond)          as CHECK, and on failure ends the test it is in
namespace mforce::testing {

// One test: the name its failures are reported under, and the function.
struct Test { const char* name; void (*run)(); };

// The checks made and failed so far in this process.
struct Tally { int checks; int fails; };
Tally tally();

// Counts one check and prints the failure; returns whether it passed.
bool check(bool passed, const char* file, int line, const char* text);
bool check_near(float a, float b, double eps, const char* file, int line,
                const char* textA, const char* textB);

// Thrown by a failed REQUIRE and caught by run_tests, which ends the test
// there without counting it a second time.
struct RequireFailed {};

// Runs each test inside a try/catch: a test that throws is reported by name
// and counted as one failed check, and the next test runs. Prints
// "ALL PASS (N checks)" and returns 0 when the tests made checks and none
// failed; otherwise prints "F/N FAILED", or "NO CHECKS MADE" when they made
// none, and returns 1.
int run_tests(std::span<const Test> tests);

}  // namespace mforce::testing

#define CHECK(cond) ::mforce::testing::check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)
#define CHECK_NEAR(a, b, eps) ::mforce::testing::check_near((a), (b), (eps), __FILE__, __LINE__, #a, #b)
#define REQUIRE(cond) do { if (!::mforce::testing::check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)) \
  throw ::mforce::testing::RequireFailed{}; } while (0)

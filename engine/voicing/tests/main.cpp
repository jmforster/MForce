// The voicing tests, in the order they run.
#include "mforce/testing/check.h"

void run_profile_selector_capture_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_profile_selector_capture_tests", run_profile_selector_capture_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

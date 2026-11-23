// The ui_control tests, in the order they run.
#include "mforce/testing/check.h"

void run_module_name_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_module_name_tests", run_module_name_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

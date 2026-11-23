// The ui_model tests, in the order they run.
#include "mforce/testing/check.h"

void run_open_patch_tests();
void run_save_tests();
void run_save_as_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_open_patch_tests", run_open_patch_tests},
  {"run_save_tests", run_save_tests},
  {"run_save_as_tests", run_save_as_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

// The ui_model tests, in the order they run.
#include "mforce/testing/check.h"

void run_open_patch_tests();
void run_save_tests();
void run_save_as_tests();
void run_placement_rule_tests();
void run_placement_block_tests();
void run_placement_census_tests();
void run_recent_files_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_open_patch_tests", run_open_patch_tests},
  {"run_save_tests", run_save_tests},
  {"run_save_as_tests", run_save_as_tests},
  {"run_placement_rule_tests", run_placement_rule_tests},
  {"run_placement_block_tests", run_placement_block_tests},
  {"run_placement_census_tests", run_placement_census_tests},
  {"run_recent_files_tests", run_recent_files_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

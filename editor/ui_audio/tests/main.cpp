// The ui_audio tests, in the order they run. None opens the sound card.
#include "mforce/testing/check.h"

void run_adapter_lines_tests();
void run_adapter_swap_tests();
void run_adapter_many_swaps_tests();
void run_adapter_allocation_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_adapter_lines_tests", run_adapter_lines_tests},
  {"run_adapter_swap_tests", run_adapter_swap_tests},
  {"run_adapter_many_swaps_tests", run_adapter_many_swaps_tests},
  {"run_adapter_allocation_tests", run_adapter_allocation_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

// The perform tests, in the order they run.
#include "mforce/testing/check.h"

void run_dun_realize_tests();
void run_conductor_performance_tests();
void run_time_map_tests();
void run_perform_embedded_score_tests();

int main() {
  static constexpr mforce::testing::Test kTests[] = {
    {"run_dun_realize_tests", run_dun_realize_tests},
    {"run_conductor_performance_tests", run_conductor_performance_tests},
    {"run_time_map_tests", run_time_map_tests},
    {"run_perform_embedded_score_tests", run_perform_embedded_score_tests},
  };
  return mforce::testing::run_tests(kTests);
}

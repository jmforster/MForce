// The compose tests, in the order they run.
#include "mforce/testing/check.h"

void run_walk2_tests();
void run_realization_strategy_tests();
void test_contour_roundtrip();
void test_pool_select();
void test_build_approach_steps();
void test_settle_tail();
void test_invert();
void test_retrograde_steps();
void test_prune_end();
void test_prune_start();
void test_set_last_pulse();
void test_adjust_last_pulse();
void test_stretch();
void test_compress();
void test_combine_basic();
void test_combine_with_elide_and_adjust();
void test_combine_sugar();
void test_replicate_repeats();
void test_replicate_connectors();
void test_replicate_and_prune();
void test_split();
void test_add_neighbor_up();
void test_add_turn_up();
void test_vary_rhythm_preserves_length();
void test_vary_steps_changes_interior();
void test_vary_composite();
void test_complexify_grows_unit_count();
void test_embellish_marks_articulation();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_walk2_tests", run_walk2_tests},
  {"run_realization_strategy_tests", run_realization_strategy_tests},
  {"test_contour_roundtrip", test_contour_roundtrip},
  {"test_pool_select", test_pool_select},
  {"test_build_approach_steps", test_build_approach_steps},
  {"test_settle_tail", test_settle_tail},
  {"test_invert", test_invert},
  {"test_retrograde_steps", test_retrograde_steps},
  {"test_prune_end", test_prune_end},
  {"test_prune_start", test_prune_start},
  {"test_set_last_pulse", test_set_last_pulse},
  {"test_adjust_last_pulse", test_adjust_last_pulse},
  {"test_stretch", test_stretch},
  {"test_compress", test_compress},
  {"test_combine_basic", test_combine_basic},
  {"test_combine_with_elide_and_adjust", test_combine_with_elide_and_adjust},
  {"test_combine_sugar", test_combine_sugar},
  {"test_replicate_repeats", test_replicate_repeats},
  {"test_replicate_connectors", test_replicate_connectors},
  {"test_replicate_and_prune", test_replicate_and_prune},
  {"test_split", test_split},
  {"test_add_neighbor_up", test_add_neighbor_up},
  {"test_add_turn_up", test_add_turn_up},
  {"test_vary_rhythm_preserves_length", test_vary_rhythm_preserves_length},
  {"test_vary_steps_changes_interior", test_vary_steps_changes_interior},
  {"test_vary_composite", test_vary_composite},
  {"test_complexify_grows_unit_count", test_complexify_grows_unit_count},
  {"test_embellish_marks_articulation", test_embellish_marks_articulation},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

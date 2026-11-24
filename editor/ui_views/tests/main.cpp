// The ui_views tests, in the order they run. Every view is drawn with no window.
#include "mforce/testing/check.h"

void run_headless_census_tests();
void run_headless_prompt_and_empty_tests();
void run_canvas_table_tests();
void run_canvas_rewire_tests();
void run_create_menu_tests();
void run_canvas_selection_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_headless_census_tests", run_headless_census_tests},
  {"run_headless_prompt_and_empty_tests", run_headless_prompt_and_empty_tests},
  {"run_canvas_table_tests", run_canvas_table_tests},
  {"run_canvas_rewire_tests", run_canvas_rewire_tests},
  {"run_create_menu_tests", run_create_menu_tests},
  {"run_canvas_selection_tests", run_canvas_selection_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

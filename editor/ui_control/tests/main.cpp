// The ui_control tests, in the order they run. None opens a window or the sound card.
#include "mforce/testing/check.h"

void run_control_edit_tests();
void run_control_failed_rebuild_tests();
void run_control_file_tests();
void run_control_unsaved_tests();
void run_control_rate_tests();
void run_control_open_census_tests();
void run_control_key_tests();
void run_control_keyboard_tests();
void run_control_status_tests();
void run_control_held_keys_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_control_edit_tests", run_control_edit_tests},
  {"run_control_failed_rebuild_tests", run_control_failed_rebuild_tests},
  {"run_control_file_tests", run_control_file_tests},
  {"run_control_unsaved_tests", run_control_unsaved_tests},
  {"run_control_rate_tests", run_control_rate_tests},
  {"run_control_open_census_tests", run_control_open_census_tests},
  {"run_control_key_tests", run_control_key_tests},
  {"run_control_keyboard_tests", run_control_keyboard_tests},
  {"run_control_status_tests", run_control_status_tests},
  {"run_control_held_keys_tests", run_control_held_keys_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

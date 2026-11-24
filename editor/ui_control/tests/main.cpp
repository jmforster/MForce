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
void run_control_string_tests();
void run_control_arrange_tests();
void run_control_opened_tests();
void run_control_keep_up_tests();
void run_control_key_act_tests();
void run_step_tests();
void run_sound_follows_tests();
void run_undo_redo_tests();
void run_wire_tests();
void run_unwire_tests();
void run_delete_tests();
void run_rename_tests();
void run_create_tests();
void run_offer_tests();
void run_new_patch_tests();
void run_history_census_tests();
void run_onsets_text_tests();

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
  {"run_control_string_tests", run_control_string_tests},
  {"run_control_arrange_tests", run_control_arrange_tests},
  {"run_control_opened_tests", run_control_opened_tests},
  {"run_control_keep_up_tests", run_control_keep_up_tests},
  {"run_control_key_act_tests", run_control_key_act_tests},
  {"run_step_tests", run_step_tests},
  {"run_sound_follows_tests", run_sound_follows_tests},
  {"run_undo_redo_tests", run_undo_redo_tests},
  {"run_wire_tests", run_wire_tests},
  {"run_unwire_tests", run_unwire_tests},
  {"run_delete_tests", run_delete_tests},
  {"run_rename_tests", run_rename_tests},
  {"run_create_tests", run_create_tests},
  {"run_offer_tests", run_offer_tests},
  {"run_new_patch_tests", run_new_patch_tests},
  {"run_history_census_tests", run_history_census_tests},
  {"run_onsets_text_tests", run_onsets_text_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

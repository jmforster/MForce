// The ui_model tests, in the order they run.
#include "mforce/testing/check.h"

void run_open_patch_tests();
void run_save_tests();
void run_save_as_tests();
void run_placement_rule_tests();
void run_placement_block_tests();
void run_placement_census_tests();
void run_recent_files_tests();
void run_canvas_view_tests();
void run_placement_slot_tests();
void run_arrange_tests();
void run_canvas_count_tests();
void run_history_tests();
void run_history_cap_tests();
void run_note_faces_tests();
void run_note_faces_census_tests();
void run_new_patch_tests();
void run_placement_face_tests();
void run_placement_fault_tests();
void run_centred_panning_tests();
void run_folder_memory_tests();
void run_drop_unread_face_node_tests();
void run_drop_unread_face_node_census_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_open_patch_tests", run_open_patch_tests},
  {"run_save_tests", run_save_tests},
  {"run_save_as_tests", run_save_as_tests},
  {"run_placement_rule_tests", run_placement_rule_tests},
  {"run_placement_block_tests", run_placement_block_tests},
  {"run_placement_census_tests", run_placement_census_tests},
  {"run_recent_files_tests", run_recent_files_tests},
  {"run_canvas_view_tests", run_canvas_view_tests},
  {"run_placement_slot_tests", run_placement_slot_tests},
  {"run_arrange_tests", run_arrange_tests},
  {"run_canvas_count_tests", run_canvas_count_tests},
  {"run_history_tests", run_history_tests},
  {"run_history_cap_tests", run_history_cap_tests},
  {"run_note_faces_tests", run_note_faces_tests},
  {"run_note_faces_census_tests", run_note_faces_census_tests},
  {"run_new_patch_tests", run_new_patch_tests},
  {"run_placement_face_tests", run_placement_face_tests},
  {"run_placement_fault_tests", run_placement_fault_tests},
  {"run_centred_panning_tests", run_centred_panning_tests},
  {"run_folder_memory_tests", run_folder_memory_tests},
  {"run_drop_unread_face_node_tests", run_drop_unread_face_node_tests},
  {"run_drop_unread_face_node_census_tests", run_drop_unread_face_node_census_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

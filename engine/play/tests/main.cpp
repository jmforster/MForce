// The play tests, in the order they run.
#include "mforce/testing/check.h"

void run_channel_source_tests();
void run_channel_clock_tests();
void run_instrument_mixer_unity_tests();
void run_plain_part_twin_tests();
void run_plain_part_build_tests();
void run_instrument_holding_parts_tests();
void run_plain_sharing_load_tests();
void run_plain_sharing_multiplex_tests();
void run_play_unrealized_warning_test();
void run_play_piece_tests();
void run_embedded_score_tests();
void run_embedded_score_end_tests();
void run_embedded_score_delivery_tests();
void run_retired_form_table_tests();
void run_build_sound_tests();

int main() {
  static constexpr mforce::testing::Test kTests[] = {
    {"run_channel_source_tests", run_channel_source_tests},
    {"run_channel_clock_tests", run_channel_clock_tests},
    {"run_instrument_mixer_unity_tests", run_instrument_mixer_unity_tests},
    {"run_plain_part_twin_tests", run_plain_part_twin_tests},
    {"run_plain_part_build_tests", run_plain_part_build_tests},
    {"run_instrument_holding_parts_tests", run_instrument_holding_parts_tests},
    {"run_plain_sharing_load_tests", run_plain_sharing_load_tests},
    {"run_plain_sharing_multiplex_tests", run_plain_sharing_multiplex_tests},
    {"run_play_unrealized_warning_test", run_play_unrealized_warning_test},
    {"run_play_piece_tests", run_play_piece_tests},
    {"run_embedded_score_tests", run_embedded_score_tests},
    {"run_embedded_score_end_tests", run_embedded_score_end_tests},
    {"run_embedded_score_delivery_tests", run_embedded_score_delivery_tests},
    {"run_retired_form_table_tests", run_retired_form_table_tests},
    {"run_build_sound_tests", run_build_sound_tests},
  };
  return mforce::testing::run_tests(kTests);
}

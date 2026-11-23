// The shelved tests, in the order they run.
#include "mforce/testing/check.h"

void run_walk3_tests();
void run_walk1_tests();
void run_compose_request_tests();
void run_compose_kit_tests();
void run_settings_of_tests();
void run_settings_codec_tests();
void run_template_refusal_tests();
void run_settings_required_tests();
void run_shelved_registration_tests();
void run_default_passage_place_tests();
void run_kit_reuse_tests();
void run_wrong_pairing_tests();
void run_period_phrase_refusal_tests();
void run_passage_melody_tests();
void test_smoke_round_trip();
void integ_invert();
void integ_retrograde();
void integ_combine();
void integ_replicate();
void integ_split();
void integ_add_neighbor();
void integ_add_turn();
void integ_stretch();
void integ_pitch_realization();
void integ_two_figure_count_invert();
void integ_two_figure_length_retrograde();
void integ_two_figure_singleton_stretch();
void integ_two_figure_json_round_trip();
void integ_elab_all_leave();
void integ_elab_all_generate();
void integ_elab_json_round_trip();

int main() {
  static constexpr mforce::testing::Test kTests[] = {
    {"run_walk3_tests", run_walk3_tests},
    {"run_walk1_tests", run_walk1_tests},
    {"run_compose_request_tests", run_compose_request_tests},
    {"run_compose_kit_tests", run_compose_kit_tests},
    {"run_settings_of_tests", run_settings_of_tests},
    {"run_settings_codec_tests", run_settings_codec_tests},
    {"run_template_refusal_tests", run_template_refusal_tests},
    {"run_settings_required_tests", run_settings_required_tests},
    {"run_shelved_registration_tests", run_shelved_registration_tests},
    {"run_default_passage_place_tests", run_default_passage_place_tests},
    {"run_kit_reuse_tests", run_kit_reuse_tests},
    {"run_wrong_pairing_tests", run_wrong_pairing_tests},
    {"run_period_phrase_refusal_tests", run_period_phrase_refusal_tests},
    {"run_passage_melody_tests", run_passage_melody_tests},
    {"test_smoke_round_trip", test_smoke_round_trip},
    {"integ_invert", integ_invert},
    {"integ_retrograde", integ_retrograde},
    {"integ_combine", integ_combine},
    {"integ_replicate", integ_replicate},
    {"integ_split", integ_split},
    {"integ_add_neighbor", integ_add_neighbor},
    {"integ_add_turn", integ_add_turn},
    {"integ_stretch", integ_stretch},
    {"integ_pitch_realization", integ_pitch_realization},
    {"integ_two_figure_count_invert", integ_two_figure_count_invert},
    {"integ_two_figure_length_retrograde", integ_two_figure_length_retrograde},
    {"integ_two_figure_singleton_stretch", integ_two_figure_singleton_stretch},
    {"integ_two_figure_json_round_trip", integ_two_figure_json_round_trip},
    {"integ_elab_all_leave", integ_elab_all_leave},
    {"integ_elab_all_generate", integ_elab_all_generate},
    {"integ_elab_json_round_trip", integ_elab_json_round_trip},
  };
  return mforce::testing::run_tests(kTests);
}

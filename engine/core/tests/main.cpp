// The core tests, in the order they run.
#include "mforce/testing/check.h"

void run_alloc_probe_tests();
void run_segment_walk_twin_tests();
void run_driven_part_lifecycle_tests();
void run_formant_factor_twin_tests();
void run_string_setting_tests();
void run_pin_tests();
void run_base_tests();
void run_manual_pin_next_tests();
void run_setting_clamp_tests();
void run_manual_pin_prepare_tests();
void run_processor_manual_input_tests();
void run_tap_reads_live_tests();
void run_spsc_queue_tests();
void run_tap_guard_tests();
void run_ramp_hold_tests();
void run_dsp_scaffold_tests();
void run_dsp_dc_blocker_tests();
void run_dsp_tuning_allpass_tests();
void run_dsp_tuning_allpass_boundary_tests();
void run_dsp_one_pole_tests();
void run_dsp_bow_friction_tests();
void run_dsp_biquad_tests();
void run_dsp_reset_tests();

int main() {
  static constexpr mforce::testing::Test kTests[] = {
    {"run_alloc_probe_tests", run_alloc_probe_tests},
    {"run_segment_walk_twin_tests", run_segment_walk_twin_tests},
    {"run_driven_part_lifecycle_tests", run_driven_part_lifecycle_tests},
    {"run_formant_factor_twin_tests", run_formant_factor_twin_tests},
    {"run_string_setting_tests", run_string_setting_tests},
    {"run_pin_tests", run_pin_tests},
    {"run_base_tests", run_base_tests},
    {"run_manual_pin_next_tests", run_manual_pin_next_tests},
    {"run_setting_clamp_tests", run_setting_clamp_tests},
    {"run_manual_pin_prepare_tests", run_manual_pin_prepare_tests},
    {"run_processor_manual_input_tests", run_processor_manual_input_tests},
    {"run_tap_reads_live_tests", run_tap_reads_live_tests},
    {"run_spsc_queue_tests", run_spsc_queue_tests},
    {"run_tap_guard_tests", run_tap_guard_tests},
    {"run_ramp_hold_tests", run_ramp_hold_tests},
    {"run_dsp_scaffold_tests", run_dsp_scaffold_tests},
    {"run_dsp_dc_blocker_tests", run_dsp_dc_blocker_tests},
    {"run_dsp_tuning_allpass_tests", run_dsp_tuning_allpass_tests},
    {"run_dsp_tuning_allpass_boundary_tests", run_dsp_tuning_allpass_boundary_tests},
    {"run_dsp_one_pole_tests", run_dsp_one_pole_tests},
    {"run_dsp_bow_friction_tests", run_dsp_bow_friction_tests},
    {"run_dsp_biquad_tests", run_dsp_biquad_tests},
    {"run_dsp_reset_tests", run_dsp_reset_tests},
  };
  return mforce::testing::run_tests(kTests);
}

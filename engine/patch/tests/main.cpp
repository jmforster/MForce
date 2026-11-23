// The patch tests, in the order they run.
#include "mforce/testing/check.h"

void run_retired_spelling_table_tests();
void run_preset_curve_label_tests();
void run_envelope_form_tests();
void run_dsp_blep_tests();
void run_patch_record_tests();
void register_earlier_tests_probes();
void run_type_declaration_identity_tests();
void run_type_declaration_field_tests();
void run_type_declaration_accept_tests();
void run_type_declaration_default_tests();
void run_type_declaration_prototype_tests();
void run_patch_check_tests();
void run_codec_identity_tests();
void run_codec_complete_model_tests();
void run_codec_float_tests();
void run_codec_kind_tests();
void run_codec_preset_tests();
void run_codec_passenger_tests();
void run_codec_layout_fragment_tests();
void run_codec_refusal_tests();
void run_codec_untouched_tests();
void run_build_sort_tests();
void run_build_fill_tests();
void run_build_named_tests();
void run_build_template_seed_tests();
void run_build_list_tests();
void run_type_declaration_late_tests();

int main() {
  static constexpr mforce::testing::Test kTests[] = {
    {"run_retired_spelling_table_tests", run_retired_spelling_table_tests},
    {"run_preset_curve_label_tests", run_preset_curve_label_tests},
    {"run_envelope_form_tests", run_envelope_form_tests},
    {"run_dsp_blep_tests", run_dsp_blep_tests},
    {"run_patch_record_tests", run_patch_record_tests},
    {"register_earlier_tests_probes", register_earlier_tests_probes},
    {"run_type_declaration_identity_tests", run_type_declaration_identity_tests},
    {"run_type_declaration_field_tests", run_type_declaration_field_tests},
    {"run_type_declaration_accept_tests", run_type_declaration_accept_tests},
    {"run_type_declaration_default_tests", run_type_declaration_default_tests},
    {"run_type_declaration_prototype_tests", run_type_declaration_prototype_tests},
    {"run_patch_check_tests", run_patch_check_tests},
    {"run_codec_identity_tests", run_codec_identity_tests},
    {"run_codec_complete_model_tests", run_codec_complete_model_tests},
    {"run_codec_float_tests", run_codec_float_tests},
    {"run_codec_kind_tests", run_codec_kind_tests},
    {"run_codec_preset_tests", run_codec_preset_tests},
    {"run_codec_passenger_tests", run_codec_passenger_tests},
    {"run_codec_layout_fragment_tests", run_codec_layout_fragment_tests},
    {"run_codec_refusal_tests", run_codec_refusal_tests},
    {"run_codec_untouched_tests", run_codec_untouched_tests},
    {"run_build_sort_tests", run_build_sort_tests},
    {"run_build_fill_tests", run_build_fill_tests},
    {"run_build_named_tests", run_build_named_tests},
    {"run_build_template_seed_tests", run_build_template_seed_tests},
    {"run_build_list_tests", run_build_list_tests},
    {"run_type_declaration_late_tests", run_type_declaration_late_tests},
  };
  return mforce::testing::run_tests(kTests);
}

// The patch tests, in the order they run.
#include "mforce/testing/check.h"

void run_retired_spelling_table_tests();
void run_preset_curve_label_tests();
void run_envelope_form_tests();
void run_dsp_blep_tests();
void run_patch_record_tests();
void register_test_probes();
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
void run_edit_apply_tests();
void run_edit_value_refusal_tests();
void run_edit_setting_refusal_tests();
void run_edit_string_and_layout_refusal_tests();
void run_edit_codec_tests();
void run_edit_census_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_retired_spelling_table_tests", run_retired_spelling_table_tests},
  {"run_preset_curve_label_tests", run_preset_curve_label_tests},
  {"run_envelope_form_tests", run_envelope_form_tests},
  {"run_dsp_blep_tests", run_dsp_blep_tests},
  {"run_patch_record_tests", run_patch_record_tests},
  {"register_test_probes", register_test_probes},
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
  {"run_edit_apply_tests", run_edit_apply_tests},
  {"run_edit_value_refusal_tests", run_edit_value_refusal_tests},
  {"run_edit_setting_refusal_tests", run_edit_setting_refusal_tests},
  {"run_edit_string_and_layout_refusal_tests", run_edit_string_and_layout_refusal_tests},
  {"run_edit_codec_tests", run_edit_codec_tests},
  {"run_edit_census_tests", run_edit_census_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

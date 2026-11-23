mforce_module(source
  SOURCES src/additive/additive_source.cpp
          src/noise/red_noise_source.cpp
          src/source_registrations.cpp
          src/wavetable/hybrid_ks_source.cpp
          src/wavetable/wavetable_source.cpp
  USES    core)

mforce_tests(source
  SOURCES tests/main.cpp
          tests/additive_tests.cpp
          tests/combine_mod_pin_tests.cpp
          tests/combine_mod_tests.cpp
          tests/component_root_tests.cpp
          tests/curve_node_tests.cpp
          tests/delay_loop_tests.cpp
          tests/envelope_pin_tests.cpp
          tests/envelope_retrigger_tests.cpp
          tests/evolution_tests.cpp
          tests/filter_design_tests.cpp
          tests/filter_tests.cpp
          tests/formant_peaks_tests.cpp
          tests/formant_tests.cpp
          tests/input_list_tests.cpp
          tests/list_node_tests.cpp
          tests/memo_tap_tests.cpp
          tests/multiplex_tests.cpp
          tests/no_throw_tests.cpp
          tests/node_twin_tests.cpp
          tests/noise_tests.cpp
          tests/output_part_tests.cpp
          tests/partial_motion_tests.cpp
          tests/perform_node_tests.cpp
          tests/physical_tests.cpp
          tests/pierce_filter_tests.cpp
          tests/range_var_tests.cpp
          tests/registry_tests.cpp
          tests/shaper_tests.cpp
          tests/wavetable_tests.cpp)

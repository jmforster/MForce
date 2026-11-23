mforce_module(render
  SOURCES src/build_instrument.cpp
          src/build_sound.cpp
          src/drum_kit.cpp
          src/instrument.cpp
          src/live_player.cpp
          src/mixer.cpp
          src/node_capture.cpp
          src/pitched_instrument.cpp
          src/voice.cpp
          src/wav_reader.cpp
          src/wav_writer.cpp
  USES    core source patch contract)

mforce_tests(render
  SOURCES tests/main.cpp
          tests/allocation_tests.cpp
          tests/curve_table_tests.cpp
          tests/declared_load_tests.cpp
          tests/dynamic_pin_tests.cpp
          tests/enum_column_tests.cpp
          tests/envelope_table_tests.cpp
          tests/fault_tests.cpp
          tests/generated_reader_tests.cpp
          tests/instrument_build_tests.cpp
          tests/live_player_tests.cpp
          tests/load_refusal_tests.cpp
          tests/note_delivery_tests.cpp
          tests/onset_field_tests.cpp
          tests/perform_node_field_tests.cpp
          tests/performer_tests.cpp
          tests/plain_part_refusal_tests.cpp
          tests/prepare_start_combine_tests.cpp
          tests/prepare_start_filter_tests.cpp
          tests/prepare_start_host_tests.cpp
          tests/prepare_start_physical_tests.cpp
          tests/prepare_start_source_tests.cpp
          tests/prepare_start_tests.cpp
          tests/retired_form_load_tests.cpp
          tests/review_gap_tests.cpp
          tests/setting_loader_tests.cpp
          tests/shared_source_tests.cpp
          tests/voice_tests.cpp)

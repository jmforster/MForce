mforce_module(play
  SOURCES src/play.cpp
  USES    core source patch render music perform contract)

mforce_tests(play
  SOURCES tests/main.cpp
          tests/build_sound_tests.cpp
          tests/channel_tests.cpp
          tests/embedded_score_tests.cpp
          tests/offline_play_tests.cpp
          tests/plain_part_tests.cpp
          tests/play_piece_tests.cpp
          tests/retired_form_table_tests.cpp
          tests/unrealized_warning_tests.cpp)

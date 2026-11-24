mforce_module(music JSON
  SOURCES src/chord.cpp
          src/music.cpp
          src/realize.cpp
  USES    core)

mforce_tests(music
  SOURCES tests/main.cpp
          tests/chord_pitch_tests.cpp
          tests/drum_map_parse_tests.cpp
          tests/embedded_score_text_tests.cpp
          tests/harmony_timeline_tests.cpp
          tests/passage_parse_tests.cpp
          tests/realize_events_tests.cpp)

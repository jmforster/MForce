mforce_module(shelved JSON
  SOURCES src/pattern_library_json.cpp
          src/register_shelved.cpp
  USES    core music voicing compose)

mforce_tests(shelved
  SOURCES tests/main.cpp
          tests/compose_kit_tests.cpp
          tests/figure_compose_tests.cpp
          tests/figure_phrase_strategy_tests.cpp
          tests/melody_profile_tests.cpp
          tests/melody_shaping_tests.cpp
          tests/passage_melody_tests.cpp
          tests/strategy_settings_tests.cpp)

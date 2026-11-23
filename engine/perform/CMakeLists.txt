mforce_module(perform JSON
  SOURCES src/embedded_score.cpp
  USES    core music voicing contract)

mforce_tests(perform
  SOURCES tests/main.cpp
          tests/conductor_tests.cpp
          tests/dun_realize_tests.cpp
          tests/embedded_score_perform_tests.cpp
          tests/time_map_tests.cpp)

mforce_module(compose JSON
  SOURCES src/compose.cpp
          src/melody_profile_json.cpp
          src/pool_figure_builder_json.cpp
          src/register_builtin.cpp
          src/style_table_json.cpp
  USES    core music voicing)

mforce_tests(compose
  SOURCES tests/main.cpp
          tests/elaborate_tests.cpp
          tests/figure_transform_tests.cpp
          tests/realization_strategy_tests.cpp)

mforce_module(ui_control
  SOURCES src/module_name.cpp
  USES    ui_model ui_audio patch render contract)

mforce_tests(ui_control
  SOURCES tests/main.cpp
          tests/module_name_tests.cpp)

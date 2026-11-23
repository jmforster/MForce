mforce_module(ui_model
  SOURCES src/open_patch.cpp
  USES    patch)

mforce_tests(ui_model
  SOURCES tests/main.cpp
          tests/open_patch_tests.cpp)

mforce_module(ui_model
  SOURCES src/open_patch.cpp
          src/placement.cpp
          src/recent_files.cpp
  USES    patch)

mforce_tests(ui_model
  SOURCES tests/main.cpp
          tests/open_patch_tests.cpp
          tests/placement_tests.cpp
          tests/recent_files_tests.cpp)

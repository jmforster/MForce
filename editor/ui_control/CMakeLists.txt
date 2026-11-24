mforce_module(ui_control
  SOURCES src/building.cpp
          src/edits.cpp
          src/files.cpp
          src/keyboard.cpp
          src/live_play.cpp
          src/playing.cpp
          src/stepping.cpp
          src/undo_redo.cpp
          src/unsaved.cpp
          src/wiring.cpp
  USES    ui_model ui_audio patch render contract)

mforce_tests(ui_control
  SOURCES tests/main.cpp
          tests/edit_and_file_tests.cpp
          tests/play_tests.cpp
          tests/step_tests.cpp
          tests/building_tests.cpp
          tests/history_census_tests.cpp
          tests/keyboard_tests.cpp)

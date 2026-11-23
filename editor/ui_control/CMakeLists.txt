mforce_module(ui_control
  SOURCES src/edits.cpp
          src/files.cpp
          src/keyboard.cpp
          src/live_play.cpp
          src/playing.cpp
          src/unsaved.cpp
  USES    ui_model ui_audio patch render contract)

mforce_tests(ui_control
  SOURCES tests/main.cpp
          tests/edit_and_file_tests.cpp
          tests/play_tests.cpp)

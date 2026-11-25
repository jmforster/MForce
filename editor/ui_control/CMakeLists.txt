mforce_module(ui_control
  SOURCES src/building.cpp
          src/clipboard.cpp
          src/copies.cpp
          src/edits.cpp
          src/files.cpp
          src/generating.cpp
          src/grouping.cpp
          src/keyboard.cpp
          src/level.cpp
          src/listening.cpp
          src/live_play.cpp
          src/pane.cpp
          src/playing.cpp
          src/selecting.cpp
          src/sounds.cpp
          src/spectrum.cpp
          src/stepping.cpp
          src/transport_files.cpp
          src/undo_redo.cpp
          src/unsaved.cpp
          src/watchdog.cpp
          src/wave_extents.cpp
          src/wave_view.cpp
          src/waveforms.cpp
          src/wiring.cpp
          src/wormholes.cpp
  USES    ui_model ui_audio patch render contract core music perform play util)

mforce_tests(ui_control
  SOURCES tests/main.cpp
          tests/edit_and_file_tests.cpp
          tests/play_tests.cpp
          tests/step_tests.cpp
          tests/building_tests.cpp
          tests/history_census_tests.cpp
          tests/keyboard_tests.cpp
          tests/live_and_sounds_tests.cpp
          tests/generating_tests.cpp
          tests/parity_tests.cpp
          tests/waveforms_tests.cpp
          tests/spectrum_tests.cpp
          tests/listening_tests.cpp
          tests/watchdog_tests.cpp
          tests/grouping_tests.cpp
          tests/clipboard_tests.cpp)

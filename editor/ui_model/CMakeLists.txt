mforce_module(ui_model
  SOURCES src/canvas_view.cpp
          src/history.cpp
          src/new_patch.cpp
          src/note_faces.cpp
          src/open_patch.cpp
          src/placement.cpp
          src/recent_files.cpp
  USES    patch)

mforce_tests(ui_model
  SOURCES tests/main.cpp
          tests/canvas_view_tests.cpp
          tests/open_patch_tests.cpp
          tests/placement_tests.cpp
          tests/recent_files_tests.cpp
          tests/history_tests.cpp
          tests/note_faces_tests.cpp
          tests/placement_face_tests.cpp)

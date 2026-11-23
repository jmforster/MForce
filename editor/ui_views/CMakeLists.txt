mforce_module(ui_views
  SOURCES src/imgui_version.cpp
  USES    ui_model ui_control patch core
  LIBS    imgui)

mforce_tests(ui_views
  SOURCES tests/main.cpp
          tests/imgui_version_tests.cpp)

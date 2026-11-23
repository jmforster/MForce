mforce_module(ui_views
  SOURCES src/editor_views.cpp
          src/headless.cpp
          src/keyboard_view.cpp
          src/menu_bar.cpp
          src/node_canvas.cpp
          src/properties_panel.cpp
          src/status_line.cpp
          src/unsaved_prompt.cpp
  USES    ui_model ui_control patch core
  LIBS    imgui)

mforce_tests(ui_views
  SOURCES tests/main.cpp
          tests/headless_draw_tests.cpp)

mforce_module(ui_views
  SOURCES src/canvas_gestures.cpp
          src/canvas_menu.cpp
          src/canvas_nodes.cpp
          src/canvas_table.cpp
          src/create_menu.cpp
          src/editor_views.cpp
          src/headless.cpp
          src/keyboard_view.cpp
          src/menu_bar.cpp
          src/node_canvas.cpp
          src/panel_instrument.cpp
          src/panel_node.cpp
          src/properties_panel.cpp
          src/status_line.cpp
          src/unsaved_prompt.cpp
  USES    ui_model ui_control patch core
  LIBS    imgui)

mforce_tests(ui_views
  SOURCES tests/main.cpp
          tests/headless_draw_tests.cpp
          tests/canvas_table_tests.cpp
          tests/canvas_selection_tests.cpp)

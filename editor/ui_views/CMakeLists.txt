mforce_module(ui_views
  SOURCES src/bottom_pane.cpp
          src/canvas_gestures.cpp
          src/canvas_menu.cpp
          src/canvas_nodes.cpp
          src/canvas_table.cpp
          src/create_menu.cpp
          src/editor_views.cpp
          src/headless.cpp
          src/keyboard_view.cpp
          src/menu_bar.cpp
          src/node_canvas.cpp
          src/places.cpp
          src/spectrum_view.cpp
          src/panel_instrument.cpp
          src/panel_node.cpp
          src/properties_panel.cpp
          src/status_line.cpp
          src/transport_fields.cpp
          src/transport_view.cpp
          src/unsaved_prompt.cpp
          src/wave_pop_outs.cpp
          src/wave_strip.cpp
          src/waveforms_view.cpp
  USES    ui_model ui_control patch core
  LIBS    imgui)

mforce_tests(ui_views
  SOURCES tests/main.cpp
          tests/headless_draw_tests.cpp
          tests/canvas_table_tests.cpp
          tests/canvas_selection_tests.cpp
          tests/mouse_tests.cpp
          tests/transport_view_tests.cpp
          tests/pane_tests.cpp
          tests/strip_gesture_tests.cpp
          tests/listen_menu_tests.cpp)

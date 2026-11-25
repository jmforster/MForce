// The ui_views tests, in the order they run. Every view is drawn with no window.
#include "mforce/testing/check.h"

void run_headless_census_tests();
void run_headless_prompt_and_empty_tests();
void run_canvas_table_tests();
void run_canvas_rewire_tests();
void run_create_menu_tests();
void run_canvas_selection_tests();
void run_fit_key_tests();
void run_create_item_tests();
void run_pin_tooltip_tests();
void run_transport_tabs_draw_tests();
void run_transport_click_tests();
void run_canvas_click_tests();
void run_canvas_drag_tests();
void run_canvas_menu_tests();
void run_canvas_minimap_tests();
void run_canvas_wheel_tests();
void run_piano_mouse_tests();
void run_pane_tab_tests();
void run_pane_collapse_tests();
void run_spectrum_readout_tests();
void run_strip_toolbar_tests();
void run_strip_wheel_and_drag_tests();
void run_strip_key_tests();
void run_pop_out_tests();
void run_listen_menu_tests();
void run_restart_menu_tests();
void run_strip_limit_tests();
void run_pop_out_limit_tests();
void run_generate_again_tests();
void run_pop_out_key_tests();
void run_pop_out_mouse_tests();
void run_columns_typed_tests();
void run_pop_out_window_kept_tests();

namespace {
constexpr mforce::testing::Test kTests[] = {
  {"run_headless_census_tests", run_headless_census_tests},
  {"run_headless_prompt_and_empty_tests", run_headless_prompt_and_empty_tests},
  {"run_canvas_table_tests", run_canvas_table_tests},
  {"run_canvas_rewire_tests", run_canvas_rewire_tests},
  {"run_create_menu_tests", run_create_menu_tests},
  {"run_canvas_selection_tests", run_canvas_selection_tests},
  {"run_fit_key_tests", run_fit_key_tests},
  {"run_create_item_tests", run_create_item_tests},
  {"run_pin_tooltip_tests", run_pin_tooltip_tests},
  {"run_transport_tabs_draw_tests", run_transport_tabs_draw_tests},
  {"run_transport_click_tests", run_transport_click_tests},
  {"run_canvas_click_tests", run_canvas_click_tests},
  {"run_canvas_drag_tests", run_canvas_drag_tests},
  {"run_canvas_menu_tests", run_canvas_menu_tests},
  {"run_canvas_minimap_tests", run_canvas_minimap_tests},
  {"run_canvas_wheel_tests", run_canvas_wheel_tests},
  {"run_piano_mouse_tests", run_piano_mouse_tests},
  {"run_pane_tab_tests", run_pane_tab_tests},
  {"run_pane_collapse_tests", run_pane_collapse_tests},
  {"run_spectrum_readout_tests", run_spectrum_readout_tests},
  {"run_strip_toolbar_tests", run_strip_toolbar_tests},
  {"run_strip_wheel_and_drag_tests", run_strip_wheel_and_drag_tests},
  {"run_strip_key_tests", run_strip_key_tests},
  {"run_pop_out_tests", run_pop_out_tests},
  {"run_listen_menu_tests", run_listen_menu_tests},
  {"run_restart_menu_tests", run_restart_menu_tests},
  {"run_strip_limit_tests", run_strip_limit_tests},
  {"run_pop_out_limit_tests", run_pop_out_limit_tests},
  {"run_generate_again_tests", run_generate_again_tests},
  {"run_pop_out_key_tests", run_pop_out_key_tests},
  {"run_pop_out_mouse_tests", run_pop_out_mouse_tests},
  {"run_columns_typed_tests", run_columns_typed_tests},
  {"run_pop_out_window_kept_tests", run_pop_out_window_kept_tests},
};
}  // namespace

int main() { return mforce::testing::run_tests(kTests); }

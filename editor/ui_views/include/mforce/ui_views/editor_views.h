#pragma once
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/file_dialogs.h"
#include "mforce/ui_views/keyboard_view.h"
#include "mforce/ui_views/menu_bar.h"
#include "mforce/ui_views/node_canvas.h"
#include "mforce/ui_views/properties_panel.h"
#include "mforce/ui_views/status_line.h"
#include "mforce/ui_views/unsaved_prompt.h"

namespace mforce::ui {

// Every view of the one window, in a fixed arrangement: the menu bar at the
// top, the status line at the bottom, the keyboard above it, the properties
// panel down the right side, the canvas in the rest.
class EditorViews {
public:
  explicit EditorViews(FileDialogs& dialogs);
  ~EditorViews();
  EditorViews(const EditorViews&) = delete;
  EditorViews& operator=(const EditorViews&) = delete;
  void draw(Session& session);       // between the caller's ImGui::NewFrame() and ImGui::Render()

private:
  MenuBar menu_;
  NodeCanvas canvas_;
  PropertiesPanel properties_;
  KeyboardView keyboard_;
  StatusLine status_;
  UnsavedPrompt prompt_;
};

}  // namespace mforce::ui

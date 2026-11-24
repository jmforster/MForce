#pragma once
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/file_dialogs.h"
#include "mforce/ui_views/keyboard_view.h"
#include "mforce/ui_views/menu_bar.h"
#include "mforce/ui_views/node_canvas.h"
#include "mforce/ui_views/properties_panel.h"
#include "mforce/ui_views/status_line.h"
#include "mforce/ui_views/transport_view.h"
#include "mforce/ui_views/unsaved_prompt.h"

namespace mforce::ui {

// Every view of the one window, in a fixed arrangement: the menu bar at the
// top, the transport under it, the status line at the bottom, the keyboard
// above it, the properties panel down the right side, the canvas in the rest.
class EditorViews {
public:
  explicit EditorViews(FileDialogs& dialogs);
  ~EditorViews();
  EditorViews(const EditorViews&) = delete;
  EditorViews& operator=(const EditorViews&) = delete;
  void draw(Session& session);       // between the caller's ImGui::NewFrame() and ImGui::Render()
  Vec2 canvas_origin() const { return canvas_.origin(); }   // the canvas's top-left corner on screen
  Vec2 canvas_size() const { return canvas_.size(); }       // and its size
  Vec2 piano_origin() const { return keyboard_.piano_origin(); }   // the on-screen piano's top-left corner
  // The centre of the transport's tab or button with this label, as last drawn.
  std::optional<Vec2> transport_item(const std::string& label) const { return transport_.centre_of(label); }

private:
  MenuBar menu_;
  TransportView transport_;
  NodeCanvas canvas_;
  PropertiesPanel properties_;
  KeyboardView keyboard_;
  StatusLine status_;
  UnsavedPrompt prompt_;
};

}  // namespace mforce::ui

#pragma once
#include <optional>
#include <string>
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/bottom_pane.h"
#include "mforce/ui_views/breadcrumb.h"
#include "mforce/ui_views/clipboard.h"
#include "mforce/ui_views/file_dialogs.h"
#include "mforce/ui_views/menu_bar.h"
#include "mforce/ui_views/node_canvas.h"
#include "mforce/ui_views/places.h"
#include "mforce/ui_views/properties_panel.h"
#include "mforce/ui_views/status_line.h"
#include "mforce/ui_views/transport_view.h"
#include "mforce/ui_views/unsaved_prompt.h"

namespace mforce::ui {

// Every view of the one window, in a fixed arrangement: the menu bar at the
// top, the transport under it, the status line at the bottom, the bottom
// pane (the strips, the spectrum, the keyboard) across the window above it,
// the properties panel down the right side, the canvas in the rest, under
// the breadcrumb while the patch has a group; and the
// strips popped out, each a window of its own over them.
class EditorViews {
public:
  EditorViews(FileDialogs& dialogs, Clipboard& clipboard);
  ~EditorViews();
  EditorViews(const EditorViews&) = delete;
  EditorViews& operator=(const EditorViews&) = delete;
  void draw(Session& session);       // between the caller's ImGui::NewFrame() and ImGui::Render()
  Vec2 canvas_origin() const { return canvas_.origin(); }   // the canvas's top-left corner on screen
  Vec2 canvas_size() const { return canvas_.size(); }       // and its size
  Vec2 piano_origin() const { return pane_.piano_origin(); }   // the on-screen piano's top-left corner
  // The centre of the transport's tab or button with this label, as last drawn.
  std::optional<Vec2> transport_item(const std::string& label) const { return transport_.centre_of(label); }
  // Where the thing of this name was drawn last frame: a transport tab or
  // button by its words (its centre alone), a menu's item ("menu:<words>"),
  // every name the canvas and the breadcrumb keep (NodeCanvas::places,
  // Breadcrumb::places), and every name the pane keeps (BottomPane::area).
  std::optional<Area> area(const std::string& name) const;
  const std::string& spectrum_readout() const { return pane_.spectrum_readout(); }

private:
  MenuBar menu_;
  TransportView transport_;
  Breadcrumb breadcrumb_;
  NodeCanvas canvas_;
  PropertiesPanel properties_;
  BottomPane pane_;
  StatusLine status_;
  UnsavedPrompt prompt_;
};

}  // namespace mforce::ui

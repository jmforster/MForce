#pragma once
#include <memory>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// Draws every view with no window: makes an ImGui and an imnodes context
// with no backend, draws the frames a first showing needs, destroys both.
// The file dialogs answer as if cancelled.
void draw_once_headless(Session& session);

// The views drawn with no window for as long as it lives, with the few acts
// a test of the canvas needs: frames, a click, a key, and imnodes' selected
// wires read and set. One at a time: it holds the contexts while it lives.
class HeadlessEditor {
public:
  explicit HeadlessEditor(Session& session);
  ~HeadlessEditor();
  HeadlessEditor(const HeadlessEditor&) = delete;
  HeadlessEditor& operator=(const HeadlessEditor&) = delete;
  void frames(int n);
  void click_canvas_corner();       // a left click just inside the canvas's top-left: the canvas takes the keyboard
  void press_delete();
  void select_link(int id);         // imnodes' own SelectLink
  int  selected_links() const;      // imnodes' own NumSelectedLinks

private:
  struct Parts;                     // the contexts and the views, in the .cpp
  std::unique_ptr<Parts> parts_;
  Session& session_;
};

}  // namespace mforce::ui

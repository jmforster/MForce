#pragma once
#include <memory>
#include <string>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"

namespace mforce::ui {

// Draws every view with no window: makes an ImGui and an imnodes context
// with no backend, draws the frames a first showing needs, destroys both.
// The file dialogs answer as if cancelled.
void draw_once_headless(Session& session);

// The views drawn with no window for as long as it lives, with the few acts
// a test needs: frames (each followed by the requested render, as the
// program's loop runs it), a click, a key, a click on a transport tab or
// button by its label, and imnodes' selected wires read and set. One at a
// time: it holds the contexts while it lives.
class HeadlessEditor {
public:
  explicit HeadlessEditor(Session& session);
  ~HeadlessEditor();
  HeadlessEditor(const HeadlessEditor&) = delete;
  HeadlessEditor& operator=(const HeadlessEditor&) = delete;
  void frames(int n);
  void click_canvas_corner();       // a left click just inside the canvas's top-left: the canvas takes the keyboard
  void press_delete();
  void press_f();                   // the F key, with the mouse wherever it was left
  void press_space();               // the Space key
  // A left click on the transport's tab or button with this label, where the
  // last frame drew it; false when it was not drawn.
  bool click(const std::string& label);
  Vec2 canvas_size() const;         // the canvas's size on screen, from the last frame
  void select_link(int id);         // imnodes' own SelectLink
  int  selected_links() const;      // imnodes' own NumSelectedLinks

private:
  void press(int key);              // an ImGuiKey, as its number: no ImGui type in this header
  struct Parts;                     // the contexts and the views, in the .cpp
  std::unique_ptr<Parts> parts_;
  Session& session_;
};

}  // namespace mforce::ui

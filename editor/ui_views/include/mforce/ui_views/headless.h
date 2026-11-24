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
// a test needs: frames (each with the sound card watched on a clock of its
// own and followed by the requested render, as the program's loop runs it), a click, a key, a click on a transport tab or
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
  // Whether the last frame drew a transport tab or button with these words.
  bool drawn(const std::string& label) const;
  Vec2 canvas_size() const;         // the canvas's size on screen, from the last frame
  Vec2 canvas_origin() const;       // and its top-left corner
  Vec2 piano_origin() const;        // the on-screen piano's top-left corner, from the last frame
  // The mouse as a hand moves it, at a point on screen: it arrives, rests two
  // frames, then each change of a button is a frame of its own.
  void click_at(Vec2 at);           // the left button down and up
  void right_click_at(Vec2 at);     // the right button down and up
  void mouse_down_at(Vec2 at);      // the left button down, and kept down
  void mouse_up();                  // the left button let go where the mouse is
  void drag(Vec2 from, Vec2 to);    // the left button down at `from`, the mouse moved in steps to `to`, let go
  void wheel_at(Vec2 at, float notches);   // the wheel turned, a positive number away from the hand
  bool popup_open() const;          // whether a menu is open
  void select_link(int id);         // imnodes' own SelectLink
  int  selected_links() const;      // imnodes' own NumSelectedLinks

private:
  void press(int key);              // an ImGuiKey, as its number: no ImGui type in this header
  void rest_at(Vec2 at);            // the mouse moved there, then two frames
  void click_button(int button);    // an ImGuiMouseButton, as its number, down and up
  struct Parts;                     // the contexts and the views, in the .cpp
  std::unique_ptr<Parts> parts_;
  Session& session_;
  double clock_{0.0};               // the frames' own clock, a sixtieth of a second a frame
};

}  // namespace mforce::ui

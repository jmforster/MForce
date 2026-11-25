#pragma once
#include <memory>
#include <optional>
#include <string>
#include "mforce/patch/layout.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_views/clipboard.h"
#include "mforce/ui_views/places.h"

namespace mforce::ui {

// Draws every view with no window: makes an ImGui and an imnodes context
// with no backend, draws the frames a first showing needs, destroys both.
// The file dialogs answer as if cancelled.
void draw_once_headless(Session& session);

// The views drawn with no window for as long as it lives, with the few acts
// a test needs: frames (each with the sound card watched on a clock of its
// own and followed by the requested render, as the program's loop runs it), a click (with Ctrl, or two
// as a double-click), a key, a click on a thing by its name, and imnodes'
// selected wires read and set. Its clipboard, and ImGui's own, keep a
// string. One at a
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
  void click_at(Vec2 at);           // the left button down and up; two at one point within a third of a second are a double-click
  void ctrl_click_at(Vec2 at);      // the left button down and up with Ctrl held through it
  void double_click_at(Vec2 at);    // two clicks at one point, the second within ImGui's double-click time
  void settle();                    // frames enough that the next click is no double-click
  void right_click_at(Vec2 at);     // the right button down and up
  void mouse_down_at(Vec2 at);      // the left button down, and kept down
  void mouse_up();                  // the left button let go where the mouse is
  void drag(Vec2 from, Vec2 to);    // the left button down at `from`, the mouse moved in steps to `to`, let go
  void wheel_at(Vec2 at, float notches, bool ctrl = false);   // the wheel turned, a positive number away from the hand
  void hover(Vec2 at);              // the mouse moved there, then two frames
  // Where the thing of this name was drawn last frame (EditorViews::area);
  // a click or a hover at its centre, false when it was not drawn.
  std::optional<Area> area(const std::string& name) const;
  bool click_on(const std::string& name);
  bool right_click_on(const std::string& name);
  bool hover_on(const std::string& name);
  bool ctrl_click_on(const std::string& name);
  bool double_click_on(const std::string& name);
  Clipboard& clipboard();           // the views' clipboard: a string, never the system's
  // A key down for a frame and up, with Shift or Ctrl held through it: "Left",
  // "Right", "Up", "Down", "Home", "End", "=", "-", "0", "Keypad+", "Keypad-",
  // "Delete", "Space", "Enter", or a letter "A" to "Z".
  void press_key(const std::string& key, bool shift = false, bool ctrl = false);
  void type_text(const std::string& text);   // the characters typed, as into a field that has the keys, then a frame
  void advance_clock(double seconds) { clock_ += seconds; }   // the clock the frames watch the sound card by
  const std::string& spectrum_readout() const;   // the spectrum's words under the mouse, from the last frame
  bool popup_open() const;          // whether a menu is open
  void select_link(int id);         // imnodes' own SelectLink
  int  selected_links() const;      // imnodes' own NumSelectedLinks

private:
  void press(int key);              // an ImGuiKey, as its number: no ImGui type in this header
  void click_button(int button);    // an ImGuiMouseButton, as its number, down and up
  struct Parts;                     // the contexts and the views, in the .cpp
  std::unique_ptr<Parts> parts_;
  Session& session_;
  double clock_{0.0};               // the frames' own clock, a sixtieth of a second a frame
};

}  // namespace mforce::ui

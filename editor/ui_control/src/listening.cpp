#include "mforce/ui_control/listening.h"
#include <exception>
#include <string>
#include <utility>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/generating.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "stepping.h"

namespace mforce::ui {

namespace {

// `p` with `node` as its output, its instrument at volume 1: a point inside
// the chain never passed through the levelling the volume is set for. Throws
// check()'s sentence when the output is refused.
Patch tapped(const Patch& p, std::string_view node) {
  Patch copy = p;
  (void)set_output(copy, node);
  if (copy.instrument) copy.instrument->volume = 1.0f;
  return copy;
}

// Why `node` cannot be listened to before any reader is asked; empty when
// the readers decide.
std::string refused_by_name(const Session& s, std::string_view node) {
  if (node.empty()) return "the Output node is what the keys play already: choose a node to listen to";
  if (stepping::has_face(s.open.patch, node))
    return "'" + std::string(node) + "' is a Note: listen to a node it feeds instead";
  return {};
}

}  // namespace

Patch heard_patch(const Session& s) {
  if (s.listening.empty()) return s.open.patch;
  try {
    return tapped(s.open.patch, s.listening);
  } catch (const std::exception&) {               // listen_at accepted it; an edit since cannot make check() refuse it
    return s.open.patch;
  }
}

// The copy is asked of check() first; then the readers that build it: the
// keys' player for an instrument, a running plain sound's build for a plain
// one. Each keeps what played before when it refuses.
void listen_at(Session& s, std::string_view node) {
  if (const std::string why = refused_by_name(s, node); !why.empty()) {
    s.status = why;
    return;
  }
  if (node == s.listening) return;
  Patch heard;
  try {
    heard = tapped(s.open.patch, node);
  } catch (const std::exception& e) {
    s.status = e.what();
    return;
  }
  if (heard.instrument) {
    if (std::string why = s.live.rebuild(heard); !why.empty()) {
      s.status = std::move(why);
      return;
    }
  }
  std::string before = std::exchange(s.listening, std::string(node));
  if (!plain_follows(s)) s.listening = std::move(before);
}

void stop_listening(Session& s) {
  if (s.listening.empty()) return;
  s.listening.clear();
  sound_follows(s);
}

}  // namespace mforce::ui

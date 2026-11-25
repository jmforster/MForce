#pragma once
#include <cstddef>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_control/wiring.h"
#include "mforce/ui_model/history.h"

// The one way the control changes the patch, shared by the control's files.
namespace mforce::ui::stepping {

// Runs `edit` (any callable taking Patch& and returning a Change: a template
// parameter, as a C# method takes a Func<Patch, Change>) on the open patch as
// one step of `gesture`. A refusal's sentence goes to status and nothing
// changes. An edit that changes nothing leaves no step and answers none.
// Otherwise the patch before it is remembered, unless `gesture` is not empty
// and is the gesture already open (a drag is one step), status is cleared,
// and the answer is what the engine's edit returned.
//
// Inside the open gesture `edit` runs on the patch in place: the engine's
// edits leave it untouched on a throw, so a control function made of several
// engine edits uses an empty gesture. Otherwise a copy is taken first, and
// the patch is put back when a later part of `edit` throws after an earlier
// part applied: several engine edits in one step are one undo, all or nothing.
template <class Edit>
std::optional<Change> step(Session& s, const std::string& gesture, Edit edit) {
  Patch& patch = s.open.patch;
  if (!gesture.empty() && gesture == s.gesture) {
    try {
      const Change kind = edit(patch);
      s.status.clear();
      return kind;
    } catch (const std::exception& e) {
      s.status = e.what();
      return std::nullopt;
    }
  }
  Patch before = patch;
  Change kind{};
  try {
    kind = edit(patch);
  } catch (const std::exception& e) {
    patch = std::move(before);
    s.status = e.what();
    return std::nullopt;
  }
  if (gesture.empty()) s.gesture.clear();
  if (patch == before) return std::nullopt;
  remember(s.open.history, before);
  s.gesture = gesture;
  s.status.clear();
  return kind;
}

bool has_node(const Patch& p, std::string_view id);
bool has_face(const Patch& p, std::string_view label);

// The engine's edit that takes the feed `feed` of `reader` away (see unwire);
// a reader that takes nothing is refused.
Change unwire_in(Patch& p, const PinEnd& reader, std::size_t feed);

bool has_group(const Patch& p, std::string_view name);

// What the status says when a wire joins two Wormholes, by route_wire or by
// hand (wire): the wire between them is hidden at once, which without these
// words reads as the wire being eaten rather than the pair forming.
constexpr const char* kWormholePaired = "wormhole pair linked — span hidden; hover or select either half to see it";

// After a step that may have taken or moved things: the offers of nodes
// that went go, listening ends when its node went, the path is cut where it
// stops naming a level (level::follow_path), and then each selected name the
// level does not show (shown_at) leaves the selection: what is selected is
// always what the canvas shows selected. True when listening ended: the
// caller has the sound follow.
bool forget_what_went(Session& s);

}  // namespace mforce::ui::stepping

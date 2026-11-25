#include "mforce/ui_control/wiring.h"
#include <stdexcept>
#include <string>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

using Kind = PinEnd::Kind;

bool gives(Kind k) { return k == Kind::Output || k == Kind::Tap || k == Kind::NoteField; }
bool takes(Kind k) { return k == Kind::Input || k == Kind::SettingPin || k == Kind::PatchOutput; }

// The engine's edit that has `from` feed `to`.
Change feed(Patch& p, const PinEnd& from, const PinEnd& to) {
  const std::string source = from.kind == Kind::NoteField ? face_field(p, from.node, from.name) : from.node;
  if (to.kind == Kind::Input) return connect(p, to.node, to.name, source, from.kind == Kind::Tap);
  if (to.kind == Kind::SettingPin) return feed_setting(p, to.node, to.name, source);
  return set_output(p, source);
}

bool is_wormhole(const Patch& p, const std::string& id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return n.type == "Wormhole";
  return false;
}

}  // namespace

namespace stepping {

Change unwire_in(Patch& p, const PinEnd& reader, std::size_t feed) {
  if (reader.kind == Kind::Input) return disconnect(p, reader.node, reader.name, feed);
  if (reader.kind == Kind::SettingPin) return unfeed_setting(p, reader.node, reader.name);
  if (reader.kind == Kind::PatchOutput) return set_output(p, "");
  throw std::runtime_error("a wire is taken away at the pin it feeds, not at its source");
}

}  // namespace stepping

void wire(Session& s, const PinEnd& a, const PinEnd& b) {
  const bool forward = gives(a.kind);
  const PinEnd& from = forward ? a : b;
  const PinEnd& to = forward ? b : a;
  if (!gives(from.kind) || !takes(to.kind)) {
    s.status = "a wire runs from an output to an input";
    return;
  }
  if (from.kind == Kind::Tap && to.kind != Kind::Input) {
    s.status = "a tap feeds a pin: it cannot drive a setting or be the patch's output";
    return;
  }
  if (!step(s, "", [&](Patch& p) { return feed(p, from, to); })) return;
  if (to.kind == Kind::SettingPin) s.offered.erase({to.node, to.name});
  sound_follows(s);
  if (from.kind != Kind::NoteField && to.kind == Kind::Input && is_wormhole(s.open.patch, from.node) &&
      is_wormhole(s.open.patch, to.node))
    s.status = kWormholePaired;
}

void unwire(Session& s, const PinEnd& reader, std::size_t feed) {
  if (!step(s, "", [&](Patch& p) { return unwire_in(p, reader, feed); })) return;
  if (reader.kind == Kind::SettingPin) s.offered.insert({reader.node, reader.name});
  sound_follows(s);
}

}  // namespace mforce::ui

// A wire routed through a new pair of Wormholes (route_wire).
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "mforce/patch/edit.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/ui_control/clipboard.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/session.h"
#include "mforce/ui_model/groups.h"
#include "level.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

using Kind = PinEnd::Kind;

constexpr Vec2 kInBeside{230, 30};     // the in-half from the source
constexpr Vec2 kOutBeside{-160, 30};   // the out-half from the reader

const Node* node_named(const Patch& p, std::string_view id) {
  for (const auto& n : p.graph.nodes)
    if (n.id == id) return &n;
  return nullptr;
}

// The face that names node `id` for a field; empty when none does.
std::string face_of(const Patch& p, const std::string& id) {
  for (const auto& f : p.layout.faces)
    for (const auto& [field, node] : f.fields)
      if (node == id) return f.label;
  return {};
}

// The wire `reader` takes as its feed `feed`; none, with the words in
// `why`, when there is none to route.
std::optional<Wire> wire_into(const Patch& p, const PinEnd& reader, std::size_t feed, std::string& why) {
  if (reader.kind == Kind::PatchOutput) {
    if (p.graph.output.empty()) why = "the patch has no output: there is no wire to route";
    return p.graph.output.empty() ? std::nullopt : std::optional<Wire>(Wire{p.graph.output, false});
  }
  why = "'" + reader.node + "' has no feed number " + std::to_string(feed) + " on '" + reader.name + "' to route";
  const Node* n = node_named(p, reader.node);
  if (!n) return std::nullopt;
  const auto fed = n->wires.find(reader.name);
  if (fed == n->wires.end() || feed >= fed->second.size()) return std::nullopt;
  why.clear();
  return fed->second[feed];
}

Vec2 plus(std::optional<Vec2> at, Vec2 by) {
  const Vec2 from = at.value_or(Vec2{});
  return Vec2{from.x + by.x, from.y + by.y};
}

// The ends of a route: the two halves' names and places, and the groups
// they join.
struct Route {
  Wire wire;
  std::string in, out;
  Vec2 inAt, outAt;
  std::string inGroup, outGroup;
  bool list{false};             // the reader's pin takes several feeds
};

Route plan(const Patch& p, const PinEnd& reader, const Wire& w) {
  Route r;
  r.wire = w;
  const std::string face = face_of(p, w.source);
  const std::string source = face.empty() ? w.source : face;
  r.inAt = plus(level::place_of(p, source), kInBeside);
  r.inGroup = group_holding(p, source);
  const bool output = reader.kind == Kind::PatchOutput;
  r.outAt = plus(output ? p.layout.outputPos : level::place_of(p, reader.node), kOutBeside);
  r.outGroup = output ? std::string() : group_holding(p, reader.node);
  r.in = fresh_id(p, "Wormhole");
  Patch named = p;
  Node held;
  held.id = r.in;
  named.graph.nodes.push_back(held);
  r.out = fresh_id(named, "Wormhole");
  if (!output) {
    const TypeDeclarations* d = type_declarations(node_named(p, reader.node)->type);
    const auto* in = d ? d->input(reader.name) : nullptr;
    r.list = in && in->list;
  }
  return r;
}

// The reader's feed `feed` reads the out-half instead, the feeds after it
// kept in their order.
void lead_to(Patch& p, const PinEnd& reader, std::size_t feed, const Route& r) {
  if (reader.kind == Kind::PatchOutput) {
    (void)set_output(p, r.out);
    return;
  }
  if (!r.list) {
    (void)connect(p, reader.node, reader.name, r.out, false);
    return;
  }
  const std::vector<Wire> feeds = node_named(p, reader.node)->wires.at(reader.name);
  for (std::size_t k = feeds.size(); k-- > feed;) (void)disconnect(p, reader.node, reader.name, k);
  (void)connect(p, reader.node, reader.name, r.out, false);
  for (std::size_t k = feed + 1; k < feeds.size(); ++k)
    (void)connect(p, reader.node, reader.name, feeds[k].source, feeds[k].tap);
}

}  // namespace

void route_wire(Session& s, const PinEnd& reader, std::size_t feed) {
  if (reader.kind == Kind::SettingPin) {
    s.status = "a setting's wire is not routed through wormholes";
    return;
  }
  if (reader.kind != Kind::Input && reader.kind != Kind::PatchOutput) {
    s.status = "a wire is routed at the pin it feeds, not at its source";
    return;
  }
  const Patch& now = s.open.patch;
  std::string why;
  const std::optional<Wire> w = wire_into(now, reader, feed, why);
  if (!w) {
    s.status = why;
    return;
  }
  const Node* source = node_named(now, w->source);
  const Node* taker = reader.kind == Kind::Input ? node_named(now, reader.node) : nullptr;
  if ((source && source->type == "Wormhole") || (taker && taker->type == "Wormhole")) {
    s.status = "the wire runs through a wormhole already: it is not routed again";
    return;
  }
  const Route r = plan(now, reader, *w);
  const std::uint32_t inSeed = s.seeds(), outSeed = s.seeds();
  if (!step(s, "", [&](Patch& p) {
        (void)add_node(p, "Wormhole", r.in, inSeed, r.inAt);
        (void)add_node(p, "Wormhole", r.out, outSeed, r.outAt);
        (void)connect(p, r.in, "source", r.wire.source, r.wire.tap);
        (void)connect(p, r.out, "source", r.in, false);
        lead_to(p, reader, feed, r);
        if (!r.inGroup.empty()) (void)regroup(p, r.in, r.inGroup);
        if (!r.outGroup.empty()) (void)regroup(p, r.out, r.outGroup);
        return Change::Structure;
      }))
    return;
  sound_follows(s);
  s.status = kWormholePaired;
}

}  // namespace mforce::ui

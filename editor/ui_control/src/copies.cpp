// Duplicate and Replace with, and what they share with the clipboard.
#include <exception>
#include <string>
#include <vector>
#include "mforce/patch/edit.h"
#include "mforce/ui_control/clipboard.h"
#include "mforce/ui_control/listening.h"
#include "mforce/ui_control/playing.h"
#include "mforce/ui_control/selecting.h"
#include "mforce/ui_control/session.h"
#include "copying.h"
#include "stepping.h"

namespace mforce::ui {

using namespace stepping;   // the shared helpers by their short names (C#: a `using` directive)

namespace {

constexpr Vec2 kDuplicateStep{30, 30};   // where a duplicate lands from its original

std::string plural(int n, const char* word) { return std::to_string(n) + " " + word + (n == 1 ? "" : "s"); }

std::string joined(const std::vector<std::string>& names) {
  std::string out;
  for (const auto& n : names) out += (out.empty() ? "" : ", ") + n;
  return out;
}

// Why `node` is not duplicated or replaced (`verb`); empty when it may be.
std::string refused(const Patch& p, std::string_view node, const char* verb) {
  const std::string what = copying::uncopyable(p, node);
  if (what.empty()) return {};
  if (what == "nothing") return "'" + std::string(node) + "' names no node: there is nothing to " + verb;
  return "'" + std::string(node) + "' is " + what + ": it is not " + verb + "d";
}

}  // namespace

namespace copying {

std::string uncopyable(const Patch& p, std::string_view name) {
  for (const auto& f : p.layout.faces) {
    if (f.label == name) return "a Note";
    for (const auto& [field, id] : f.fields)
      if (id == name) return "a Note";
  }
  if (has_group(p, name)) return "a group";
  for (const auto& n : p.graph.nodes) {
    if (n.id != name) continue;
    if (n.type == "PerformNode") return "a Note";
    if (n.type == "SoundChannel") return "a Channel";
    if (n.type == "StereoMixer") return "a Mixer";
    return {};
  }
  return "nothing";
}

void join_open(Patch& p, const Session& s, const std::string& name) {
  if (!s.groupPath.empty()) (void)regroup(p, name, s.groupPath.back());
}

}  // namespace copying

void duplicate(Session& s, std::string_view node) {
  if (const std::string why = refused(s.open.patch, node, "duplicate"); !why.empty()) {
    s.status = why;
    return;
  }
  const Fragment f = fragment_of(s.open.patch, {std::string(node)});
  std::vector<std::string> made;
  if (!step(s, "", [&](Patch& p) {
        made = paste(p, f, kDuplicateStep).ids;
        copying::join_open(p, s, made.front());
        return Change::Structure;
      }))
    return;
  select_node(s, made.front());
  sound_follows(s);
}

void replace_with(Session& s, std::string_view node, std::string_view type) {
  if (const std::string why = refused(s.open.patch, node, "replace"); !why.empty()) {
    s.status = why;
    return;
  }
  const std::string old(node);
  const bool listened = s.listening == old;
  Replaced r;
  if (!step(s, "", [&](Patch& p) {
        r = replace_node(p, old, type);
        return Change::Structure;
      }))
    return;
  for (auto& name : s.selections)
    if (name == old) name = r.id;
  if (listened) s.listening.clear();
  (void)forget_what_went(s);
  sound_follows(s);
  std::string words = "Replaced " + old + " with " + r.id + " (" + plural(r.wiresKept, "wire") + " kept";
  words += r.dropped.empty() ? ")" : "; dropped: " + joined(r.dropped) + ")";
  if (listened) {
    listen_at(s, r.id);
    if (s.listening != r.id) words += "; listening ended: " + s.status;
  }
  s.status = words;
}

}  // namespace mforce::ui

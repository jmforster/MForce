#include "mforce/render/voice.h"
#include "mforce/patch/build_graph.h"
#include "mforce/render/compile_pitch_curve.h"
#include "mforce/source/modulator/perform_node.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <string_view>

namespace mforce {

namespace {

// The labels and the adapters' fields name the six in one order, which is
// NoteSignals' member order: the index a PerformNode's field holds is the
// PerformOut::Field the voice made the adapter for.
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Frequency)]) == "frequency");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Velocity)]) == "velocity");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Wheel)]) == "wheel");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Pressure)]) == "pressure");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Duration)]) == "duration");
static_assert(std::string_view(PerformNode::kFieldLabels[int(PerformOut::Field::Onset)]) == "onset");

// A MultiplexSource's template root: the one feed of its `source` that is not
// a tap; empty when it has none, and then the node clones nothing.
std::string template_root(const Node& n) {
  if (n.type != "MultiplexSource") return {};
  auto w = n.wires.find("source");
  if (w == n.wires.end() || w->second.size() != 1 || w->second.front().tap) return {};
  return w->second.front().source;
}

// Every node `roots` reach over wires, taps and dynamic pins; a Multiplex
// template is entered only when `intoTemplates`.
std::set<std::string> reach(const Graph& graph, std::vector<std::string> pending, bool intoTemplates) {
  std::set<std::string> reached;
  while (!pending.empty()) {
    const std::string id = pending.back();
    pending.pop_back();
    const Node* n = nullptr;
    for (const auto& c : graph.nodes) if (c.id == id) { n = &c; break; }
    if (!n || !reached.insert(id).second) continue;
    const std::string skip = intoTemplates ? std::string() : template_root(*n);
    for (const auto& [pin, feeds] : n->wires)
      for (const auto& w : feeds)
        if (skip.empty() || pin != "source") pending.push_back(w.source);
    for (const auto& [key, source] : n->dynamicPins) pending.push_back(source);
  }
  return reached;
}

// The ids of the nodes only a Multiplex template holds: reached from a
// template's root, and not from the graph's output or its tap-only tails
// without entering a template.
std::set<std::string> template_only(const Graph& graph) {
  std::vector<std::string> roots;
  for (const auto& n : graph.nodes)
    if (std::string r = template_root(n); !r.empty()) roots.push_back(r);
  if (roots.empty()) return {};
  std::set<std::string> inside = reach(graph, roots, true);
  std::vector<std::string> outsideRoots = advance_ids(graph);
  outsideRoots.push_back(graph.output);
  for (const auto& id : reach(graph, outsideRoots, false)) inside.erase(id);
  return inside;
}

// The id the build gave `node`; empty for a node it did not make.
std::string id_of(const BuiltGraph& g, const Component* node) {
  for (const auto& [id, n] : g.nodes)
    if (n.get() == node) return id;
  return {};
}

} // namespace

Voice::Voice(const Graph& graph, const InstrumentBlock& block, std::shared_ptr<InstrumentState> state,
             std::vector<std::string>* warnings) {
  performSource_ = std::make_shared<PerformSource>();
  performSource_->set_instrument_state(std::move(state));
  note_.frequency = std::make_shared<PerformOut>(performSource_, PerformOut::Field::Frequency);
  note_.velocity  = std::make_shared<PerformOut>(performSource_, PerformOut::Field::Velocity);
  note_.wheel     = std::make_shared<PerformOut>(performSource_, PerformOut::Field::Wheel);
  note_.pressure  = std::make_shared<PerformOut>(performSource_, PerformOut::Field::Pressure);
  note_.duration  = std::make_shared<PerformOut>(performSource_, PerformOut::Field::Duration);
  note_.onset     = std::make_shared<PerformOut>(performSource_, PerformOut::Field::Onset);
  BuiltGraph g = build_graph(graph, &note_, &block);
  if (warnings) warnings->insert(warnings->end(), g.warnings.begin(), g.warnings.end());

  // The output as a signal; the Envelopes in the graph's node order, each on
  // the trigger list when its `trigger` is wired or holds a nonzero constant,
  // and on the held line's list unless only a Multiplex template holds it;
  // the dynamic pins as push bindings; the tails; the nodes and signals.
  source_ = std::dynamic_pointer_cast<ValueSource>(g.output);
  if (!source_ && g.output)
    throw std::runtime_error("'" + graph.output + "' is a " + g.output->type_name() + ", a part, not a signal");
  const std::set<std::string> templated = template_only(graph);
  std::set<std::string> envelopes;
  for (const auto& n : graph.nodes) {
    auto it = g.nodes.find(n.id);
    auto* env = it == g.nodes.end() ? nullptr : dynamic_cast<Envelope*>(it->second.get());
    if (!env) continue;
    envelopes.insert(n.id);
    if (!templated.count(n.id)) {
      envelopes_.push_back(env);
      description_.heldEnvelopes.push_back(n.id);
    }
    if (env->trigger.source || env->trigger.constant != 0.0f) {
      triggers_.push_back(env);
      description_.triggerEnvelopes.push_back(n.id);
    }
  }
  for (auto& pin : g.dynamicPins) {
    pushBindings_.push_back({pin.node, pin.setting, pin.source});
    description_.dynamicPins.push_back({id_of(g, pin.node.get()), pin.setting, id_of(g, pin.source.get())});
  }
  for (const auto& a : g.advance) description_.tails.push_back(id_of(g, a.get()));
  description_.output = id_of(g, source_.get());
  tails_ = std::move(g.advance);
  for (const auto& s : g.signals) signals_.push_back({s.id, s.node});
  // The voice decided its trigger list from each Envelope's `trigger` number
  // just above, so a later write of it would not be a rebuild's equal: those
  // constants are left out, and a player cannot follow them.
  constants_ = std::move(g.constants);
  std::erase_if(constants_, [&](const BuiltGraph::Constant& k) {
    return k.pin == "trigger" && envelopes.count(k.node) != 0;
  });
  nodes_ = std::move(g.nodes);
  // The glide's two stages laid out once, so laying them out again for a
  // carried note reuses their storage.
  glide_ = std::make_shared<Envelope>();
  glide_->add_stage({});
  glide_->add_stage({});
}

void Voice::prepare(const RenderContext& c) {
  ctx_ = c.with_clock(clock_.get());
  if (source_) source_->prepare(ctx_);
  for (auto& a : tails_) a->prepare(ctx_);
  if (performSource_) performSource_->prepare(ctx_);
  // The push-binding chains are nodes of this voice too: stepped at a note's
  // Setup, never pulled by the root, so nothing else prepares them.
  for (auto& b : pushBindings_)
    if (b.chain) b.chain->prepare(ctx_);
}

// The note into the PerformSource, the bend riding it so every consumer of
// the frequency sees it; then the dynamic pins, each chain evaluated once at
// Setup so a bend never reaches a setting.
void Voice::deliver(const Note& n) {
  if (performSource_) {
    bend_ = nullptr;
    if (n.curve) {
      bend_ = compile_pitch_curve(*n.curve);
      bend_->prepare(ctx_);
      bend_->start(n.samples);
    }
    performSource_->set_note(n.freq, n.velocity, n.samples, n.seconds, bend_, float(n.onset));
  }
  for (auto& b : pushBindings_) {
    b.chain->step();                 // a Setup-time evaluation, between ticks
    b.consumer->set_setting(b.paramName, b.chain->current());
  }
}

void Voice::start_graph(int samples) {
  source_->start(samples);
  for (auto& a : tails_) a->start(samples);
}

// An Envelope of the trigger list restarts from its current value when its
// `trigger` reads nonzero at this note's Setup. Read after the note and the
// pins are delivered, so the new onset is visible.
void Voice::fire_triggers() {
  for (auto* env : triggers_)
    if (env->trigger.current() != 0.0f)
      env->retrigger();
}

void Voice::start(const Note& n, bool gated) {
  deliver(n);
  // Gated before the start: the envelopes lay out a holdable expand stage and
  // sit on it when this note's end arrives.
  if (gated) gate();
  start_graph(n.samples);
  fire_triggers();
}

void Voice::gate() {
  for (auto* e : envelopes_) e->set_gated(true);
}

void Voice::ungate() {
  for (auto* e : envelopes_) e->set_gated(false);
}

// The legato glide as an ordinary bend: semitones from the old pitch down to
// 0 over `glideSec`, then 0. The PerformSource's frequency is the new base
// times 2^(bend/12), so the ramp starts on the old pitch and lands on the new.
// The glide is laid out as a new Envelope is: both stages removed, then
// timeMode, the two stages, prepare and start, which between them set every
// field the walk reads. Removing and adding two stages reuses their storage.
void Voice::aim_glide(float fromFreq, float toFreq, float glideSec, int samples) {
  while (glide_->stage_count() > 0) glide_->remove_stage(glide_->stage_count() - 1);
  glide_->timeMode.value = 1;                    // seconds
  const float semis = 12.0f * std::log2(fromFreq / toFreq);
  glide_->add_stage({{semis, 0.0f, RampType::Linear, 0.0f}, glideSec, 0.0f, 0.0f});
  glide_->add_stage({{0.0f, 0.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f});
  glide_->prepare(ctx_);
  glide_->start(samples);
}

// The draws re-anchor so an in-line note is pinned as a fresh note is;
// physical state (bore, filters, envelope positions) carries. A pull chain
// reads the new note; a dynamic pin holds its line-start value.
void Voice::carry_on(const Note& n, float fromFreq, float glideSec) {
  for (auto& s : signals_) s.node->reseed();
  const bool glides = glideSec > 0.0f && fromFreq > 0.0f && n.freq != fromFreq;
  if (glides) aim_glide(fromFreq, n.freq, glideSec, n.samples);
  if (performSource_)
    performSource_->set_note(n.freq, n.velocity, n.samples, n.seconds, glides ? glide_ : nullptr,
                             float(n.onset));
  fire_triggers();   // the onset gesture restarts from its current value
}

int Voice::release(int lastNoteSamples) {
  int rem = 0;
  for (auto* e : envelopes_) rem = std::max(rem, e->gate_release(lastNoteSamples));
  return rem;
}

} // namespace mforce

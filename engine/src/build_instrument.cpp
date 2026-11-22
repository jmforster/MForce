#include "mforce/render/build_instrument.h"
#include "mforce/patch/build_graph.h"
#include "mforce/render/perform_source.h"
#include "mforce/source/envelope/envelope.h"
#include "mforce/source/modulator/perform_node.h"
#include <algorithm>
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

// The voice's PerformSource and its six adapters, made before the graph is
// built: every PerformNode of the graph resolves to one of these.
NoteSignals make_note_signals(PitchedInstrument::VoiceGraph& vg, std::shared_ptr<InstrumentState> state) {
  vg.performSource = std::make_shared<PerformSource>();
  vg.performSource->set_instrument_state(std::move(state));
  vg.freqOut  = std::make_shared<PerformOut>(vg.performSource, PerformOut::Field::Frequency);
  vg.velOut   = std::make_shared<PerformOut>(vg.performSource, PerformOut::Field::Velocity);
  vg.wheelOut = std::make_shared<PerformOut>(vg.performSource, PerformOut::Field::Wheel);
  vg.pressOut = std::make_shared<PerformOut>(vg.performSource, PerformOut::Field::Pressure);
  vg.durOut   = std::make_shared<PerformOut>(vg.performSource, PerformOut::Field::Duration);
  vg.onsetOut = std::make_shared<PerformOut>(vg.performSource, PerformOut::Field::Onset);
  return NoteSignals{vg.freqOut, vg.velOut, vg.wheelOut, vg.pressOut, vg.durOut, vg.onsetOut};
}

// The voice takes the build: its output as a signal; its Envelopes, in the
// graph's node order, each on the trigger list when its `trigger` is wired or
// holds a nonzero constant; its dynamic pins as push bindings; its tails; its
// nodes and signals.
void adopt_build(PitchedInstrument::VoiceGraph& vg, BuiltGraph& g, const Graph& graph) {
  vg.source = std::dynamic_pointer_cast<ValueSource>(g.output);
  if (!vg.source && g.output)
    throw std::runtime_error("'" + graph.output + "' is a " + g.output->type_name() + ", a part, not a signal");
  for (const auto& n : graph.nodes) {
    auto it = g.nodes.find(n.id);
    auto* env = it == g.nodes.end() ? nullptr : dynamic_cast<Envelope*>(it->second.get());
    if (!env) continue;
    vg.allEnvelopes.push_back(env);
    if (env->trigger.source || env->trigger.constant != 0.0f) vg.triggerBindings.push_back(env);
  }
  for (auto& pin : g.dynamicPins) vg.pushBindings.push_back({pin.node, pin.setting, pin.source});
  vg.advanceList = std::move(g.advance);
  for (const auto& s : g.signals) vg.signals.push_back({s.id, s.node});
  vg.nodesById = std::move(g.nodes);
}

} // namespace

std::unique_ptr<PitchedInstrument> build_instrument(const Patch& patch, int minPolyphony,
                                                    std::vector<std::string>* warnings) {
  if (!patch.instrument) throw std::runtime_error("Patch has no 'instrument' section");
  const InstrumentBlock& block = *patch.instrument;
  auto inst = std::make_unique<PitchedInstrument>();
  inst->volume = block.volume;
  inst->instrumentClass.onsets = block.onsets;
  inst->glideSec = block.glideMs / 1000.0f;
  inst->instrumentClass.sustaining = block.sustaining;
  const int polyphony = std::max(block.polyphony, minPolyphony);
  for (int v = 0; v < polyphony; ++v) {
    PitchedInstrument::VoiceGraph vg;
    const NoteSignals note = make_note_signals(vg, inst->instrumentState);
    BuiltGraph g = build_graph(patch.graph, &note, &block);
    if (v == 0 && warnings) warnings->insert(warnings->end(), g.warnings.begin(), g.warnings.end());
    adopt_build(vg, g, patch.graph);
    inst->voicePool.push_back(std::move(vg));
  }
  inst->prepare(RenderContext{patch.sampleRate});
  return inst;
}

} // namespace mforce

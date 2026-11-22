#include "mforce/render/build_sound.h"
#include "mforce/patch/build_graph.h"
#include "mforce/render/build_instrument.h"
#include "mforce/source/output/sound_channel.h"
#include "mforce/source/output/stereo_mixer.h"
#include <cmath>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

namespace {

// A signal as a channel's mono producer. It is its own pull loop: it owns the
// clock its graph computes on and completes the host's facts with it.
struct MonoSignal final : MonoSource {
  std::shared_ptr<ValueSource> src;
  SampleClock clock;
  RenderContext ctx;
  explicit MonoSignal(std::shared_ptr<ValueSource> s) : src(std::move(s)) {}
  void prepare(const RenderContext& c) override {
    ctx = c.with_clock(&clock);
    src->prepare(ctx);
  }
  void render(float* out, int frames) override {
    src->start(frames);
    for (int i = 0; i < frames; ++i) { clock.tick(); out[i] = src->next(); }
  }
};

// A mixer pin's signal: the source wired into it, else a constant of the
// pin's own value.
std::shared_ptr<ValueSource> pin_signal(const Param& pin) {
  return pin.source ? pin.source : std::make_shared<ConstantSource>(pin.constant);
}

// One unity channel at centre pan around `source`.
Channel unity_channel(std::unique_ptr<MonoSource> source) {
  Channel ch;
  ch.source = std::move(source);
  ch.volume = std::make_shared<ConstantSource>(1.0f);
  ch.pan = std::make_shared<ConstantSource>(0.0f);
  return ch;
}

// A plain sound's pull loop: its name in a refusal and the ids it pulls first.
struct PullLoop {
  std::string name;
  std::vector<std::string> roots;
};

// No node may be reached from two pull loops. A loop reaches every node its
// roots reach over the record's wires that are not taps; at a
// MultiplexSource the feed of `source` is not followed, since the clones it
// builds are what play. The first such node in the graph's order is refused,
// naming the first two loops that reach it.
void refuse_shared_nodes(const Graph& graph, const std::vector<PullLoop>& loops) {
  std::map<std::string, const Node*> byId;
  for (const auto& n : graph.nodes) byId.try_emplace(n.id, &n);
  std::vector<std::set<std::string>> reached(loops.size());
  for (size_t k = 0; k < loops.size(); ++k) {
    std::vector<std::string> pending = loops[k].roots;
    while (!pending.empty()) {
      const std::string id = pending.back();
      pending.pop_back();
      if (!reached[k].insert(id).second) continue;
      auto it = byId.find(id);
      if (it == byId.end()) continue;
      for (const auto& [pin, feeds] : it->second->wires) {
        if (it->second->type == "MultiplexSource" && pin == "source") continue;
        for (const auto& w : feeds)
          if (!w.tap) pending.push_back(w.source);
      }
    }
  }
  for (const auto& n : graph.nodes) {
    std::vector<size_t> in;
    for (size_t k = 0; k < loops.size(); ++k)
      if (reached[k].count(n.id)) in.push_back(k);
    if (in.size() > 1)
      throw std::runtime_error("'" + n.id + "' is reached from " + loops[in[0]].name + " and from " +
                               loops[in[1]].name + ": each pulls its nodes on its own clock, so a node belongs to one");
  }
}

// A built node's id, by pointer; empty for a node the build did not make.
std::string id_of(const BuiltGraph& g, const Component* node) {
  for (const auto& [id, n] : g.nodes)
    if (n.get() == node) return id;
  return {};
}

// The mixer form: each listed channel needs its source, no node is shared by
// two pull loops, and the mixer reads the parts' pins.
void assemble_mixer(Mixer& mixer, const StereoMixer& part, const BuiltGraph& g, const Graph& graph) {
  PullLoop mixerLoop{"the mixer's gains, volumes and pans", {}};
  const auto root_of = [&](const Param& pin) {
    const std::string id = pin.source ? id_of(g, pin.source.get()) : std::string();
    if (!id.empty()) mixerLoop.roots.push_back(id);
  };
  root_of(part.gainL);
  root_of(part.gainR);
  std::vector<PullLoop> loops;
  for (const SoundChannel* view : part.channels.views) {
    const std::string chId = id_of(g, view);
    if (!view->source.source)
      throw std::runtime_error("node '" + chId + "' (SoundChannel): 'source' is not wired; "
                               "a channel the mixer lists needs a source");
    PullLoop loop{"channel '" + chId + "'", {}};
    if (std::string id = id_of(g, view->source.source.get()); !id.empty()) loop.roots.push_back(id);
    loops.push_back(std::move(loop));
    root_of(view->volume);
    root_of(view->pan);
  }
  loops.push_back(std::move(mixerLoop));
  refuse_shared_nodes(graph, loops);
  mixer.gainL = pin_signal(part.gainL);
  mixer.gainR = pin_signal(part.gainR);
  for (const SoundChannel* view : part.channels.views) {
    Channel ch;
    ch.volume = pin_signal(view->volume);
    ch.pan = pin_signal(view->pan);
    ch.source = std::make_unique<MonoSignal>(view->source.get());
    mixer.channels.push_back(std::move(ch));
  }
}

// A plain sound: its graph built with no note and no block, its tap-only
// tails named (nothing ticks them), then the mixer form or the bare form, and
// the graph's signals listed on the mixer by id.
void build_plain(BuiltSound& built, const Patch& patch) {
  BuiltGraph g = build_graph(patch.graph, nullptr, nullptr);
  built.warnings.insert(built.warnings.end(), g.warnings.begin(), g.warnings.end());
  for (const auto& id : advance_ids(patch.graph))
    built.warnings.push_back("node '" + id + "' is read only through a tap and will not advance: a plain sound has "
                             "no voice to tick it; give the patch an instrument block, or take the output from "
                             "the loop's tail");
  auto mixer = std::make_unique<Mixer>();
  if (const auto part = std::dynamic_pointer_cast<StereoMixer>(g.output))
    assemble_mixer(*mixer, *part, g, patch.graph);
  else {
    auto signal = std::dynamic_pointer_cast<ValueSource>(g.output);
    if (!signal)
      throw std::runtime_error("'" + patch.graph.output + "' is a " + g.output->type_name() + ", a part, not a signal");
    mixer->channels.push_back(unity_channel(std::make_unique<MonoSignal>(std::move(signal))));
  }
  for (const auto& s : g.signals)
    mixer->signals.push_back({s.id, std::dynamic_pointer_cast<ValueSource>(g.nodes.at(s.id))});
  mixer->prepare(RenderContext{patch.sampleRate});
  built.mixer = std::move(mixer);
}

} // namespace

BuiltSound build_sound(const Patch& patch) {
  BuiltSound built;
  built.sampleRate = patch.sampleRate;
  built.frames = sound_frames(patch);
  if (!patch.instrument) {
    build_plain(built, patch);
    return built;
  }
  built.mixer = instrument_mixer(build_instrument(patch, 0, &built.warnings), patch.sampleRate);
  return built;
}

int sound_frames(const Patch& patch) {
  return patch.seconds ? int(std::lround(*patch.seconds * patch.sampleRate)) : patch.sampleRate * 5;
}

std::unique_ptr<Mixer> instrument_mixer(std::unique_ptr<Instrument> instrument, int sampleRate) {
  auto mixer = std::make_unique<Mixer>();
  mixer->channels.push_back(unity_channel(std::move(instrument)));
  mixer->prepare(RenderContext{sampleRate});
  return mixer;
}

} // namespace mforce

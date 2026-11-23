// LivePlayer::set_value and the build's list of constants: a value set
// before the first note plays as a player built with it, a sounding note
// hears it within one buffer, what the player cannot follow is false and
// changes nothing, no value is lost to a full queue, nothing is allocated,
// and the build lists both forms of a constant and nothing a template holds.
#include "mforce/testing/check.h"
#include "test_support.h"
#include "mforce/patch/build_graph.h"
#include "mforce/patch/edit.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/live_player.h"
#include "mforce/source/register_all_sources.h"
#include "mforce/testing/alloc_probe.h"
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

using namespace mforce;

namespace {

constexpr int kBuffer = 256;

// `buffers` buffers of the player's output, one after another.
std::vector<float> buffers_of(LivePlayer& player, int buffers) {
  std::vector<float> out, buf(kBuffer);
  for (int k = 0; k < buffers; ++k) {
    player.fill(buf.data(), kBuffer);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

// A note, then its buffers until no voice sounds (at most `most`).
std::vector<float> note_until_quiet(LivePlayer& player, const PerformedNote& note, int most) {
  CHECK(player.note_on(note) != 0);
  std::vector<float> out, buf(kBuffer);
  for (int k = 0; k < most && (k == 0 || player.sounding() > 0); ++k) {
    player.fill(buf.data(), kBuffer);
    out.insert(out.end(), buf.begin(), buf.end());
  }
  return out;
}

bool all_zero(const std::vector<float>& v, size_t from = 0) {
  return std::all_of(v.begin() + std::ptrdiff_t(from), v.end(), [](float s) { return s == 0.0f; });
}

// A held sine at a constant amplitude: the amplitude is read every sample.
const char* kSine = R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
  {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
  {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":0.5,"phase":0.0}}]}})";

// A MultiplexSource over a template of a saw: 'S' is reached by the template.
const char* kMultiplex = R"J({"graph":{"nodes":[{"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
  {"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.02,"decay":0.1,"release":0.1,"sustainLevel":0.7}},
  {"id":"S","type":"SawSource","params":{"amplitude":0.3,"frequency":{"ref":"pf"},"phase":0.0}},
  {"id":"mux","type":"MultiplexSource","params":{"count":3,"source":{"ref":"S"}}},
  {"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply","gainAdj":0.0}}],
  "output":"out"},"instrument":{"polyphony":1,"volume":0.5},"sampleRate":48000})J";

Patch edited(Patch p, const char* node, const char* pin, float v) {
  (void)set_value(p, node, pin, v);
  return p;
}

}  // namespace

// Test 1: player A built from the edited patch, player B from the patch as
// it was and told before any note. Every note equals, sample for sample,
// over three notes, so both voices of each pool have started at least once.
void run_live_set_value_first_note_tests() {
  std::printf("the live player: a value set before the notes plays as a player built with it\n");
  register_all_sources();
  const Patch base = load_patch("patches/library/winds/oboe1.json");
  REQUIRE(base.graph.nodes.size() > 0);
  LivePlayer a(edited(base, "Formant1", "cutoffFreq", 1800.0f), 1);
  LivePlayer b(base, 1);
  LivePlayer untold(base, 1);
  CHECK(b.set_value("Formant1", "cutoffFreq", 1800.0f));
  bool differsFromUntold = false;
  for (int n = 0; n < 3; ++n) {
    const PerformedNote note = note_at(0.0f, 0.15f, 60.0f + float(n) * 3.0f, 0.8f);
    const auto fromA = note_until_quiet(a, note, 400);
    const auto fromB = note_until_quiet(b, note, 400);
    const auto fromUntold = note_until_quiet(untold, note, 400);
    if (!patch_door::same_samples(fromA, fromB))
      std::printf("  note %d: %s\n", n, patch_door::difference(fromA, fromB).c_str());
    CHECK(patch_door::same_samples(fromA, fromB));
    differsFromUntold = differsFromUntold || !patch_door::same_samples(fromB, fromUntold);
  }
  CHECK(differsFromUntold);   // the value is heard
}

// Test 3: a held note sounds; the next fill after set_value differs from the
// same buffer of a twin that was not told, and the buffers before were equal.
void run_live_set_value_sounding_tests() {
  std::printf("the live player: a sounding note hears a value within one buffer\n");
  const Patch patch = patch_door::read_text(kSine);
  LivePlayer told(patch, 1), twin(patch, 1);
  const PerformedNote note = note_at(0.0f, 2.0f, 60.0f, 0.8f);
  CHECK(told.note_on(note) != 0);
  CHECK(twin.note_on(note) != 0);
  CHECK(patch_door::same_samples(buffers_of(told, 4), buffers_of(twin, 4)));
  CHECK(told.set_value("osc", "amplitude", 0.1f));
  const auto after = buffers_of(told, 1), twinAfter = buffers_of(twin, 1);
  CHECK(!patch_door::same_samples(after, twinAfter));
}

// Test 4: false for a wired pin, an unknown node, an unknown pin and a node a
// Multiplex template reaches; after each the sound is unchanged, and a
// player built from the edited patch does differ.
void run_live_set_value_false_tests() {
  std::printf("the live player: what it cannot follow is false and changes nothing\n");
  const Patch patch = patch_door::read_text(kMultiplex);
  LivePlayer told(patch, 1), twin(patch, 1);
  CHECK(!told.set_value("S", "frequency", 100.0f));     // a wired pin
  CHECK(!told.set_value("nope", "amplitude", 0.1f));    // no such node
  CHECK(!told.set_value("out", "nope", 0.1f));          // no such pin
  CHECK(!told.set_value("S", "amplitude", 0.9f));       // inside the template
  const PerformedNote note = note_at(0.0f, 0.2f, 57.0f, 0.8f);
  const auto heard = note_until_quiet(told, note, 200);
  CHECK(patch_door::same_samples(heard, note_until_quiet(twin, note, 200)));
  LivePlayer rebuilt(edited(patch, "S", "amplitude", 0.9f), 1);
  CHECK(!patch_door::same_samples(heard, note_until_quiet(rebuilt, note, 200)));
}

// Test 5: Sets past the queue's capacity, none applied until the audio side
// runs: the sounding voice ends with the last value. Then a voice that heard
// the value while sounding, was given back and starts again still has it.
void run_live_set_value_never_lost_tests() {
  std::printf("the live player: a value set past the queue's room is not lost\n");
  const Patch patch = patch_door::read_text(kSine);
  LivePlayer player(patch, 1);
  CHECK(player.note_on(note_at(0.0f, 0.5f, 60.0f, 0.8f)) != 0);
  for (int k = 0; k < 400; ++k) CHECK(player.set_value("osc", "amplitude", 0.2f + 0.001f * float(k)));
  CHECK(player.set_value("osc", "amplitude", 0.0f));    // the last value: silence
  (void)buffers_of(player, 1);                           // the queue drains: the earlier Sets apply
  (void)player.sounding();                               // a control call sends what waited
  CHECK(all_zero(buffers_of(player, 4)));
  // The note ends and its voice goes back; three more notes, so both voices
  // of the pool, the one that heard the Set among them, start again.
  for (int k = 0; k < 400 && player.sounding() > 0; ++k) (void)buffers_of(player, 1);
  for (int n = 0; n < 3; ++n) CHECK(all_zero(note_until_quiet(player, note_at(0.0f, 0.1f, 64.0f, 0.8f), 200)));
}

// Test 6: set_value after construction, and the fill that applies it,
// allocate nothing.
void run_live_set_value_allocation_tests() {
  std::printf("the live player: set_value and the fill that applies it allocate nothing\n");
  const Patch patch = patch_door::read_text(kSine);
  LivePlayer player(patch, 1);
  std::vector<float> buf(kBuffer);
  CHECK(player.note_on(note_at(0.0f, 0.5f, 60.0f, 0.8f)) != 0);
  player.fill(buf.data(), kBuffer);
  alloc_probe::reset();
  alloc_probe::arm();
  for (int k = 0; k < 300; ++k) (void)player.set_value("osc", "amplitude", 0.3f);
  (void)player.set_value("nope", "amplitude", 0.3f);
  for (int k = 0; k < 4; ++k) player.fill(buf.data(), kBuffer);
  (void)player.sounding();
  player.fill(buf.data(), kBuffer);
  alloc_probe::disarm();
  if (alloc_probe::count()) std::printf("  %zu allocation(s)\n", alloc_probe::count());
  CHECK(alloc_probe::count() == 0);
}

// Test 7: the build lists a pin's constant in both forms, in the graph's
// order and a node's by pin name; a wired pin has no entry, nor has a node
// a Multiplex template reaches.
void run_built_constants_tests() {
  std::printf("the build: each pin's constant, where it lives\n");
  const Patch plain = patch_door::read_text(R"({"graph":{"output":"mix","nodes":[
    {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":0.5,"phase":0.0}},
    {"id":"f","type":"SVFSource","params":{"source":{"ref":"osc"},"cutoffFreq":900.0,"resonance":0.7}},
    {"id":"comb","type":"CombinedSource","params":{"source1":{"ref":"f"},"source2":0.5,"operation":"Multiply"}},
    {"id":"ch","type":"SoundChannel","params":{"source":{"ref":"comb"},"pan":0.0}},
    {"id":"mix","type":"StereoMixer","params":{"channels":[{"ref":"ch"}]}}]},"seconds":0.1})");
  const BuiltGraph g = build_graph(plain.graph, nullptr, nullptr);
  bool aParam = false, aSource = false, oneForm = true;
  for (const auto& k : g.constants) {
    aParam = aParam || k.param;
    aSource = aSource || k.source;
    oneForm = oneForm && ((k.param != nullptr) != (k.source != nullptr));
    CHECK(!(k.node == "f" && k.pin == "source"));        // wired
    CHECK(!(k.node == "comb" && k.pin == "source1"));    // wired
  }
  std::printf("  %zu constants; a Param's own number: %s; a ConstantSource: %s\n", g.constants.size(),
              aParam ? "yes" : "no", aSource ? "yes" : "no");
  CHECK(aParam);
  CHECK(aSource);
  CHECK(oneForm);
  REQUIRE(g.constants.size() >= 3);
  CHECK(g.constants[0].node == "osc" && g.constants[0].pin == "amplitude");
  CHECK(g.constants[1].node == "osc" && g.constants[1].pin == "frequency");
  CHECK(g.constants[2].node == "osc" && g.constants[2].pin == "phase");
  const Patch mux = patch_door::read_text(kMultiplex);
  Voice voice(mux.graph, *mux.instrument, std::make_shared<InstrumentState>(), nullptr);
  const auto listed = voice.constants();
  CHECK(std::none_of(listed.begin(), listed.end(), [](const BuiltGraph::Constant& k) { return k.node == "S"; }));
  CHECK(std::any_of(listed.begin(), listed.end(), [](const BuiltGraph::Constant& k) { return k.node == "amp"; }));
}

// An Envelope's `trigger` number decides the voice's trigger list when it is
// built, so the player cannot follow it: false on an Envelope built with 0
// and on one built with 1, the sound unchanged after it; a player rebuilt
// from the edited patch differs (0 to nonzero retriggers audibly here).
void run_live_set_value_trigger_tests() {
  std::printf("the live player: an Envelope's trigger is not followed\n");
  const Patch zero = load_patch("patches/library/bells/bell_fast_decay.json");
  const Patch one = edited(zero, "ampEnv", "trigger", 1.0f);
  const PerformedNote note = note_at(0.0f, 0.1f, 60.0f, 0.8f);
  for (const Patch* p : {&zero, &one}) {
    LivePlayer told(*p, 1), twin(*p, 1);
    CHECK(!told.set_value("ampEnv", "trigger", p == &zero ? 0.1f : 0.0f));
    CHECK(patch_door::same_samples(note_until_quiet(told, note, 94), note_until_quiet(twin, note, 94)));
  }
  LivePlayer before(zero, 1), rebuilt(edited(zero, "ampEnv", "trigger", 0.1f), 1);
  CHECK(!patch_door::same_samples(note_until_quiet(before, note, 94), note_until_quiet(rebuilt, note, 94)));
}

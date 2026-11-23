#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "render/tests/fixtures.h"
#include "mforce/contract/pitch_curve.h"
#include "mforce/core/pins.h"
#include "mforce/patch/envelope_preset.h"
#include "mforce/render/perform_source.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/source/modulator/name_gate.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace mforce;


// Renders notes 60 and 72 of an instrument patch whose Note node drives `osc`'s
// frequency: the load must succeed, the output be finite and audible, and the
// octave double the zero crossings, so the note's pitch reached the oscillator.
static void check_note_pitch_reaches_osc(const std::string& patch) {
    const int n = 12000;
    auto render = [&](float midi) {
        auto ip = load_inst(patch);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        play_notes(*pi, {{.start = 0.0f, .duration = 0.4f, .noteNumber = midi, .velocity = 0.8f}});
        std::vector<float> b(size_t(n), 0.0f);
        ip.instrument->render(b.data(), n);
        return b;
    };
    std::vector<float> lo, hi;
    bool loaded = true;
    try { lo = render(60.0f); hi = render(72.0f); }
    catch (const std::exception&) { loaded = false; }
    CHECK(loaded);
    if (!loaded) return;

    bool finite = true;
    float peak = 0.0f;
    for (float v : lo) { finite = finite && std::isfinite(v); peak = std::max(peak, std::fabs(v)); }
    CHECK(finite);
    CHECK(peak > 0.1f);
    auto crossings = [](const std::vector<float>& b) {
        int c = 0;
        for (size_t i = 1; i < b.size(); ++i) c += (b[i] >= 0.0f) != (b[i - 1] >= 0.0f);
        return c;
    };
    const float ratio = float(crossings(hi)) / float(std::max(1, crossings(lo)));
    CHECK(ratio > 1.8f && ratio < 2.2f);       // an octave up doubles the frequency
}

// A Note node wired to an oscillator's frequency carries the note's pitch to
// it; and the same wire inside a MultiplexSource's template, where each of
// the three clones builds its own copy of the Note node, which reads the
// voice's pitch, so the pitch reaches every clone.
void run_note_wire_tests() {
    check_note_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2 },
      "graph": { "output": "osc", "nodes": [
        { "id": "pf", "type": "PerformNode", "params": { "field": "frequency" } },
        { "id": "osc", "type": "SineSource", "params": { "frequency": { "ref": "pf" }, "phase": 0.0 } } ] }
    })");
    check_note_pitch_reaches_osc(R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2 },
      "graph": { "output": "mux", "nodes": [
        { "id": "pf", "type": "PerformNode", "params": { "field": "frequency" } },
        { "id": "osc", "type": "SineSource", "params": { "frequency": { "ref": "pf" }, "phase": 0.0 } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })");
}

// A Multiplex voice follows a bend: the same three-clone template as above, one
// note rendered plain and once carrying a PitchCurve on its PerformedNote (the
// field the Conductor fills for a bend). The bend holds +7 semitones for the
// whole note, so every clone's Note node reads the bent frequency and the zero
// crossings rise by 2^(7/12) = 1.498.
void run_multiplex_bend_tests() {
    std::printf("a Multiplex voice follows a bend\n");
    const char* patch = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 2 },
      "graph": { "output": "mux", "nodes": [
        { "id": "pf", "type": "PerformNode", "params": { "field": "frequency" } },
        { "id": "osc", "type": "SineSource", "params": { "frequency": { "ref": "pf" }, "phase": 0.0 } },
        { "id": "mux", "type": "MultiplexSource",
          "params": { "count": 3, "source": { "ref": "osc" } } } ] }
    })";
    PitchCurve up7;
    up7.semi = {7.0f};
    up7.dur = {0.0f};                  // one hold that fills the note
    const int n = 12000;               // inside the 0.4 s note
    auto render = [&](std::optional<PitchCurve> curve) {
        auto ip = load_inst(patch);
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        play_notes(*pi, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = curve}});
        std::vector<float> b(size_t(n), 0.0f);
        ip.instrument->render(b.data(), n);
        return b;
    };
    std::vector<float> plain, bent;
    bool loaded = true;
    try { plain = render(std::nullopt); bent = render(up7); }
    catch (const std::exception& e) { loaded = false; std::printf("  load failed: %s\n", e.what()); }
    CHECK(loaded);
    if (!loaded) return;

    CHECK(plain != bent);
    float peak = 0.0f;
    for (float v : bent) peak = std::max(peak, std::fabs(v));
    CHECK(peak > 0.1f);
    auto crossings = [](const std::vector<float>& b) {
        int c = 0;
        for (size_t i = 1; i < b.size(); ++i) c += (b[i] >= 0.0f) != (b[i - 1] >= 0.0f);
        return c;
    };
    const float ratio = float(crossings(bent)) / float(std::max(1, crossings(plain)));
    std::printf("  zero crossings plain %d, bent %d, ratio %.3f\n", crossings(plain), crossings(bent), ratio);
    CHECK(ratio > 1.40f && ratio < 1.60f);    // 2^(7/12) = 1.498, upward
}

void run_name_gate_tests() {
    auto ps = std::make_shared<PerformSource>();
    auto in = std::make_shared<PerformOut>(ps, PerformOut::Field::Onset);
    auto ng = std::make_shared<NameGate>();
    ng->set_param("in", in);
    ng->targetId = 2.0f;
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 2.0f);
    CHECK(ng->current() == 1.0f);
    CHECK(ng->next() == 1.0f);
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 1.0f);
    CHECK(ng->current() == 0.0f);
    ng->targetId = -1.0f;                    // unresolved never matches
    ps->set_note(440.0f, 0.8f, 100, 0.0f, nullptr, 0.0f);
    CHECK(ng->current() == 0.0f);
}

void run_onset_trigger_tests() {
    // Minimal taught patch: sine * gesture envelope; gesture's trigger
    // wired Note.onset -> NameGate("tongue") -> Envelope.trigger.
    // Spec §6 end to end: the dip fires on the tongued note only.
    const char* kJson = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1, "onsets": ["tongue"] },
      "graph": {
        "output": "sine1",
        "nodes": [
          { "id": "perf1", "type": "PerformNode",
            "params": { "field": "onset" } },
          { "id": "ng1", "type": "NameGate",
            "params": { "name": "tongue", "in": { "ref": "perf1" } } },
          { "id": "gest1", "type": "Envelope",
            "params": { "timeMode": "seconds",
                        "trigger": { "ref": "ng1" },
              "startVal": [1.0, 0.1, 1.0], "endVal": [0.1, 1.0, 1.0],
              "power": [0.0, 0.0, 0.0], "type": ["Linear", "Linear", "Linear"],
              "percent": [0.01, 0.02, 0.0], "minSec": [0.0, 0.0, 0.0],
              "maxSec": [0.0, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0] } },
          { "id": "sine1", "type": "SineSource",
            "params": { "frequency": 220.0, "amplitude": { "ref": "gest1" },
                        "phase": 0.0 } }
        ]
      }
    })";
    auto ip = load_inst(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    play_notes(*pi, {{.start = 0.0f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                     {.start = 0.5f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = pi->instrument_class().onset_id("tongue"), .hold = false}});
    const int N = int(1.2f * 48000);
    std::vector<float> buf(size_t(N), 0.0f);
    RenderContext ctx{48000};
    ip.instrument->render(buf.data(), N);

    auto rms = [&](float fromSec, float toSec) {
        int a = int(fromSec * 48000), b = int(toSec * 48000);
        double s = 0.0;
        for (int i = a; i < b; ++i) s += double(buf[size_t(i)]) * buf[size_t(i)];
        return float(std::sqrt(s / double(b - a)));
    };
    float before = rms(0.45f, 0.49f);          // settled, pre-boundary
    float dip    = rms(0.507f, 0.513f);        // around the dip minimum
                                               // (10ms fall + start of rise)
    float after  = rms(0.60f, 0.90f);          // recovered sustain
    CHECK(dip < 0.5f * before);                // the consonant fired
    CHECK(after > 0.9f * before);              // and got out of the way
    // Control: same line, no onset name — no dip.
    auto ip2 = load_inst(kJson);
    auto* pi2 = dynamic_cast<PitchedInstrument*>(ip2.instrument.get());
    play_notes(*pi2, {{.start = 0.0f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                      {.start = 0.5f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
    std::vector<float> buf2(size_t(N), 0.0f);
    ip2.instrument->render(buf2.data(), N);
    double s = 0.0;
    int a = int(0.500f * 48000), b = int(0.508f * 48000);
    for (int i = a; i < b; ++i) s += double(buf2[size_t(i)]) * buf2[size_t(i)];
    float dip2 = float(std::sqrt(s / double(b - a)));
    CHECK(dip2 > 0.9f * before);
}

void run_note_reseed_tests() {
    // Per-note determinism (onsets-v2 addendum): an in-line note's Setup
    // re-anchors stochastic draws, so a held-line note's noise realization
    // equals a fresh note's. Patch: white noise * gated sustain envelope.
    // Property: with reseed, note 2's samples in the flat-sustain window
    // REPLAY note 1's (same draw index from anchor, same sustain level);
    // free-running draws would diverge at every sample.
    const char* kJson = R"({
      "sampleRate": 48000,
      "instrument": { "polyphony": 1, "sustaining": true },
      "graph": {
        "output": "wn1",
        "nodes": [
          { "id": "env1", "type": "Envelope",
            "params": {
              "startVal": [0.0, 1.0, 0.7, 0.7], "endVal": [1.0, 0.7, 0.7, 0.0],
              "power": [0.0, 0.0, 0.0, 0.0], "type": ["Linear", "Linear", "Linear", "Linear"],
              "percent": [0.1, 0.1, 0.0, 0.15], "minSec": [0.0, 0.0, 0.0, 0.0],
              "maxSec": [0.0, 0.0, 0.0, 0.0], "nominal": [0.0, 0.0, 0.0, 0.0] } },
          { "id": "wn1", "type": "WhiteNoiseSource",
            "params": { "amplitude": { "ref": "env1" } } }
        ]
      }
    })";
    auto ip = load_inst(kJson);
    auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
    CHECK(pi != nullptr);
    play_notes(*pi, {{.start = 0.0f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = true, .curve = std::nullopt},
                     {.start = 0.5f, .duration = 0.5f, .noteNumber = 57.0f, .velocity = 0.8f, .onset = 0, .hold = false, .curve = std::nullopt}});
    const int N = int(0.5f * 48000);
    std::vector<float> buf(size_t(int(1.6f * 48000)), 0.0f);
    RenderContext ctx{48000};
    ip.instrument->render(buf.data(), int(buf.size()));
    // Flat-sustain overlap: note 1 is at sustain from 25% on; note 2 is
    // HELD at sustain throughout (no re-attack). Skip the last 10% for
    // note 2's release re-layout territory.
    int from = int(0.25f * N), to = int(0.85f * N);
    int same = 0, total = 0;
    for (int k = from; k < to; ++k) {
        ++total;
        if (buf[size_t(k)] == buf[size_t(N + k)]) ++same;
    }
    CHECK(same == total);   // exact replay — draws re-anchored
}

// The voice count of the one instrument channel build_sound builds from a
// patch file; -1, with the reason printed, when the load throws or the
// mixer is not that shape.
static int file_voice_count(const char* path, const std::string& text) {
    ScratchPatch file(path, text);
    int voices = -1;
    try {
        auto built = load_sound(file.path);
        if (built.mixer && built.mixer->channels.size() == 1)
            if (auto* pi = dynamic_cast<PitchedInstrument*>(built.mixer->channels[0].source.get()))
                voices = int(pi->pool().size());
    }
    catch (const std::exception& e) { std::printf("  load failed: %s\n", e.what()); }
    return voices;
}

// The voice pool's size: the file's "polyphony" (4 when the block has none),
// floored by the caller's minimum; build_sound's floor is 0. The
// rate is the file's "sampleRate", 48000 when it has none.
void run_voice_pool_tests() {
    std::printf("the voice pool: the file's polyphony, the default, the floor; the default rate\n");
    const char* two = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* six = R"({"sampleRate":48000,"instrument":{"polyphony":6},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* unstated = R"({"sampleRate":48000,"instrument":{},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* noRate = R"({"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    const char* none = R"({"sampleRate":48000,"instrument":{"polyphony":0},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})";
    CHECK(load_inst(two, 5).instrument->pool().size() == 5);
    CHECK(load_inst(two, 0).instrument->pool().size() == 2);
    CHECK(load_inst(six, 3).instrument->pool().size() == 6);   // a floor below is no cut
    CHECK(load_inst(unstated, 0).instrument->pool().size() == 4);
    auto defaultRate = load_inst(noRate);
    CHECK(defaultRate.sampleRate == 48000);
    CHECK(defaultRate.instrument->render_context().sampleRate == 48000);
    CHECK(file_voice_count("renders/scratch/_voice_pool_two.json", two) == 2);
    // A file asking for no voices gets none: the standalone floor is 0, not 1.
    CHECK(file_voice_count("renders/scratch/_voice_pool_none.json", none) == 0);
}

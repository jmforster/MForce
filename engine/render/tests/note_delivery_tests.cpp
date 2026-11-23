#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/contract/performance.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/render/delivery.h"
#include "mforce/render/drum_kit.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/render/ring_out.h"
#include "mforce/render/voice.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace mforce;


// Per-note delivery with hold (spec 2026-09-20-note-onsets-v2 §4/§5/§7).
void run_hold_delivery_tests() {
    const int sr = 48000;
    auto rms_of = [](const std::vector<float>& b, float fromSec, float toSec) {
        int a = int(fromSec * 48000), z = int(toSec * 48000);
        a = std::max(0, a); z = std::min(z, int(b.size()));
        double s = 0.0;
        for (int i = a; i < z; ++i) s += double(b[size_t(i)]) * b[size_t(i)];
        return float(std::sqrt(s / double(std::max(1, z - a))));
    };
    auto render_all = [](LoadedInstrument& ip, int n) {
        std::vector<float> b(size_t(n), 0.0f);
        ip.instrument->render(b.data(), n);
        return b;
    };

    // (a) The classic path is deterministic and unchanged in shape: the
    //     same hold:false PerformedNote renders the same bytes twice.
    //     (The null gate over 79 patches is the real referee for "equals
    //     yesterday".)
    {
        const int N = int(1.6f * sr);
        auto a = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pa = dynamic_cast<PitchedInstrument*>(a.instrument.get());
        CHECK(pa != nullptr);
        play_notes(*pa, {{.start = 0.0f, .duration = 1.0f, .noteNumber = 60.0f, .velocity = 0.8f}});
        auto bufA = render_all(a, N);

        auto b = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pb = dynamic_cast<PitchedInstrument*>(b.instrument.get());
        play_notes(*pb, {{.start = 0.0f, .duration = 1.0f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        auto bufB = render_all(b, N);
        CHECK(std::memcmp(bufA.data(), bufB.data(),
                          size_t(N) * sizeof(float)) == 0);
    }

    // (b) Hold mechanics: quarter {hold:true} + quarter {hold:false},
    //     same pitch. The gate stays open across 0.4 s — no release dip —
    //     and the voice releases after 0.8 s.
    {
        const int N = int(2.0f * sr);
        auto ip = load_inst(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        CHECK(pi != nullptr);
        play_notes(*pi, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                         {.start = 0.4f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        auto b = render_all(ip, N);
        float sustain  = rms_of(b, 0.30f, 0.34f);
        float boundary = rms_of(b, 0.38f, 0.42f);
        CHECK(sustain > 0.1f);
        CHECK(std::fabs(boundary - sustain) < 0.10f * sustain);
        CHECK(rms_of(b, 0.70f, 0.78f) > 0.1f);        // still sounding
        CHECK(rms_of(b, 1.10f, 1.40f) < 0.01f * sustain);  // and released
    }

    // (c) Release re-layout end to end (§5): quarter {hold:true} + half
    //     {hold:false}. The release is 25% of the HALF note (0.2 s), not
    //     of the quarter that prepared the voice (which would be 0.1 s).
    {
        const int N = int(2.4f * sr);
        auto ip = load_inst(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        play_notes(*pi, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                         {.start = 0.4f, .duration = 0.8f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        auto b = render_all(ip, N);
        float peak = 0.0f;
        for (float v : b) peak = std::max(peak, std::fabs(v));
        const float thresh = peak * 0.01f;            // -40 dB below peak
        int last = 0;
        for (int i = 0; i < N; ++i)
            if (std::fabs(b[size_t(i)]) > thresh) last = i;
        float lastSec = float(last) / float(sr);
        CHECK(lastSec > 1.34f && lastSec < 1.46f);    // 1.2 s + ~0.2 s
    }

    // (d) Glide (§4.1): C4 {hold:true} then G4 {hold:false} with
    //     glideMs 15. The pitch ARRIVES at G4 within the glide, and the
    //     boundary's per-sample step is smaller than an instant retarget's.
    {
        const int N = int(1.6f * sr);
        auto glided = load_inst(hold_patch_json(15.0f));
        auto* pg = dynamic_cast<PitchedInstrument*>(glided.instrument.get());
        play_notes(*pg, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                         {.start = 0.4f, .duration = 0.4f, .noteNumber = 67.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        auto bg = render_all(glided, N);

        auto instant = load_inst(hold_patch_json(0.0f));
        auto* pn = dynamic_cast<PitchedInstrument*>(instant.instrument.get());
        play_notes(*pn, {{.start = 0.0f, .duration = 0.4f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                         {.start = 0.4f, .duration = 0.4f, .noteNumber = 67.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        auto bn = render_all(instant, N);

        // f0 from INTERPOLATED rising zero crossings (a raw crossing count
        // over 30 ms quantizes to ~70 cents — far too coarse for a
        // 30-cent gate).
        auto f0 = [&](const std::vector<float>& b, float fromSec, float toSec) {
            int a = int(fromSec * sr), z = int(toSec * sr);
            double first = -1.0, last = -1.0;
            int cycles = -1;
            for (int i = a + 1; i < z; ++i) {
                float p = b[size_t(i-1)], c = b[size_t(i)];
                if (p < 0.0f && c >= 0.0f) {
                    double t = double(i - 1) + double(-p) / double(c - p);
                    if (first < 0.0) { first = t; cycles = 0; }
                    else { last = t; ++cycles; }
                }
            }
            if (cycles <= 0) return 0.0f;
            return float(double(cycles) * double(sr) / (last - first));
        };
        const float g4 = 391.995f;
        float cents = 1200.0f * std::log2(f0(bg, 0.415f, 0.445f) / g4);
        CHECK(std::fabs(cents) < 30.0f);
        // Boundary smoothness over the first 5 ms of the glide.
        auto maxStep = [&](const std::vector<float>& b) {
            float m = 0.0f;
            for (int i = int(0.400f * sr) + 1; i < int(0.405f * sr); ++i)
                m = std::max(m, std::fabs(b[size_t(i)] - b[size_t(i-1)]));
            return m;
        };
        CHECK(maxStep(bg) < maxStep(bn));
    }

    // (e) A line left open when the part's notes end (§4.3) is released,
    //     not leaked — the output decays away.
    {
        const int N = int(2.0f * sr);
        auto ip = load_inst(hold_patch_json(15.0f));
        auto* pi = dynamic_cast<PitchedInstrument*>(ip.instrument.get());
        play_notes(*pi, {{.start = 0.0f, .duration = 0.5f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true}});
        auto b = render_all(ip, N);
        float peak = 0.0f;
        for (float v : b) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.1f);
        CHECK(rms_of(b, 1.20f, 1.60f) < 0.001f * peak);   // < -60 dBFS
    }

    // (f) Mid-line retune still re-drives the living voice: the second
    //     half of a C4 -> G4 line oscillates ~1.5x faster (the v1
    //     assertion, re-expressed per note).
    {
        const int N = int(1.6f * sr);
        auto c = load_scoreless("patches/baselines/BaselineSIN.json");
        auto* pc = dynamic_cast<PitchedInstrument*>(c.instrument.get());
        play_notes(*pc, {{.start = 0.0f, .duration = 0.5f, .noteNumber = 60.0f, .velocity = 0.8f, .onset = 0, .hold = true},
                         {.start = 0.5f, .duration = 0.5f, .noteNumber = 67.0f, .velocity = 0.8f, .onset = 0, .hold = false}});
        auto bufC = render_all(c, N);
        auto zc = [&](int from, int to) {
            int n = 0;
            for (int i = from + 1; i < to; ++i)
                if ((bufC[size_t(i)] >= 0) != (bufC[size_t(i-1)] >= 0)) ++n;
            return n;
        };
        int half = 24000;
        float ratio = float(zc(half, 2 * half)) / float(std::max(1, zc(0, half)));
        CHECK(ratio > 1.35f && ratio < 1.65f);   // 392/261.6 = 1.498
    }
}

// What each kind of instrument takes from a performance: a pitched one plays
// the notes and no hits, a kit the hits and no notes; and play on one held
// note ends its line, so the note reaches the timeline at a finite length.
void run_instrument_play_tests() {
    std::printf("Instrument::play: a pitched instrument ignores hits, a kit notes; a lone held note ends\n");
    const int sr = 48000;
    const PerformedNote held{.start = 0.0f, .duration = 0.5f, .noteNumber = 60.0f, .velocity = 0.8f, .hold = true};
    const PerformedHit hit{.start = 0.0f, .duration = 0.1f, .drumNumber = 0, .velocity = 1.0f};
    {
        auto ip = load_inst(hold_patch_json(15.0f));
        ip.instrument->play(Performance{{}, {hit}});
        CHECK(ip.instrument->rendered().empty());
        ip.instrument->play(Performance{{held}, {}});
        const auto& notes = ip.instrument->rendered();
        CHECK(notes.size() == 1);
        CHECK(notes.size() == 1 && notes[0].samples.size() > size_t(0.5f * sr)
              && notes[0].samples.size() <= size_t((0.5f + RingOut::kMaxSeconds) * sr));
    }
    {
        auto ip = load_inst(hold_patch_json(15.0f));
        DrumKit kit;
        kit.add(output_of(ip.instrument->pool().voice(0)));
        kit.prepare(RenderContext{sr});
        kit.play(Performance{{held}, {}});
        CHECK(kit.rendered().empty());
        kit.play(Performance{{}, {hit}});
        CHECK(kit.rendered().size() == 1 && kit.rendered()[0].samples.size() == size_t(0.1f * sr));
    }
}

// A kit through play: each hit on its drum at its start, a hit on a drum the
// kit lacks ignored, the notes of a performance ignored.
void run_drum_kit_play_tests() {
    std::printf("DrumKit::play: each hit on its drum, a missing drum ignored, notes ignored\n");
    const char* drum = R"({"sampleRate":48000,"instrument":{"polyphony":2},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":180.0}}]}})";
    auto ip = load_inst(drum);
    DrumKit kit;
    kit.add(output_of(ip.instrument->pool().voice(0)));
    kit.add(output_of(ip.instrument->pool().voice(1)));
    kit.prepare(RenderContext{48000});
    Performance part;
    part.notes.push_back({.start = 0.0f, .duration = 0.1f, .noteNumber = 60.0f, .velocity = 0.8f});
    part.hits = {{.start = 0.0f, .duration = 0.05f, .drumNumber = 1, .velocity = 1.0f},
                 {.start = 0.5f, .duration = 0.1f, .drumNumber = 5, .velocity = 1.0f},
                 {.start = 0.25f, .duration = 0.1f, .drumNumber = 0, .velocity = 0.5f}};
    kit.play(part);
    const auto& r = kit.rendered();
    CHECK(r.size() == 2);
    CHECK(r.size() == 2 && r[0].startSample == 0 && r[0].samples.size() == size_t(0.05f * 48000.0f)
          && r[1].startSample == int(0.25f * 48000.0f) && r[1].samples.size() == size_t(0.1f * 48000.0f));
    CHECK(kit.instrument_class().onsets.empty() && !kit.instrument_class().sustaining);
}

// RingOut alone: a silent voice stops at its body plus the fixed tail
// exactly; a loud one runs to the cap, its last 80 ms fading to zero; a
// follower given its body late, or primed over what the voice played, steps
// as one given it at the start.
void run_ring_out_tests() {
    std::printf("RingOut: silence stops at body plus tail, a loud voice runs to the cap and fades, a late body\n");
    const int sr = 48000, body = 4800;
    const int tail = int(RingOut::kTailSeconds * float(sr));
    const int cap = body + int(RingOut::kMaxSeconds * float(sr));
    const int fade = int(0.08f * float(sr));
    {
        RingOut ring(sr, body);
        int n = 0;
        while (ring.more()) { (void)ring.shape(0.0f); ++n; }
        CHECK(n == body + tail);
    }
    {
        RingOut ring(sr, body);
        std::vector<float> out;
        while (ring.more()) out.push_back(ring.shape(0.5f));
        CHECK(int(out.size()) == cap && ring.limit() == cap);
        CHECK(int(out.size()) == cap && out[size_t(cap - fade - 1)] == 0.5f && out[size_t(cap - fade)] == 0.5f);
        bool falling = true;
        for (int i = cap - fade + 1; falling && i < cap; ++i) falling = out[size_t(i)] < out[size_t(i - 1)];
        CHECK(falling && out.back() == 0.5f * (1.0f / float(fade)));
    }
    {
        const auto x = [](int i) { return 0.5f * std::exp(-float(i) / 3000.0f); };
        RingOut early(sr, body), late(sr), primed(sr, body);
        std::vector<float> a, b, played;
        bool lateGoesOn = true;
        for (int i = 0; i < 2000; ++i) {
            a.push_back(early.shape(x(i)));
            lateGoesOn = lateGoesOn && late.more();
            b.push_back(late.shape(x(i)));
            played.push_back(x(i));
        }
        late.set_body(body);
        primed.prime(played);
        std::vector<float> c(a);
        for (int i = 2000; early.more(); ++i) {
            a.push_back(early.shape(x(i)));
            lateGoesOn = lateGoesOn && late.more() && primed.more();
            b.push_back(late.shape(x(i)));
            c.push_back(primed.shape(x(i)));
        }
        CHECK(lateGoesOn && !late.more() && !primed.more());
        CHECK(a == b && a == c && int(a.size()) == body + tail);   // the decay is under the floor by the tail's end
    }
}

// The three-way choice, every row: an open line takes the note; a fresh held
// note opens a line only on a voice that can hold one; the rest are classic.
void run_delivery_choice_tests() {
    std::printf("the delivery choice: continue, open a line, classic\n");
    struct Row { bool open, hold, canHold; Delivery want; };
    const Row rows[] = {
        {true, true, true, Delivery::Continue},    {true, true, false, Delivery::Continue},
        {true, false, true, Delivery::Continue},   {true, false, false, Delivery::Continue},
        {false, true, true, Delivery::OpenLine},   {false, true, false, Delivery::Classic},
        {false, false, true, Delivery::Classic},   {false, false, false, Delivery::Classic},
    };
    for (const Row& r : rows) CHECK(choose_delivery(r.open, r.hold, r.canHold) == r.want);
}

// A held line on a voice whose only envelope is in a Multiplex template: the
// template's envelope is not the voice's to gate, so the line is delivered as
// ordinary notes and the second note sounds. An envelope outside the template
// is the voice's.
void run_multiplex_held_line_tests() {
    std::printf("a held line on a Multiplex template's envelope: ordinary notes, the second sounds\n");
    const std::string head = R"({"sampleRate":48000,"instrument":{"polyphony":2,"sustaining":true},"graph":{"output":")";
    const std::string nodes = R"(","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.7,"release":0.05}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"env"}}},
      {"id":"mux","type":"MultiplexSource","params":{"count":2,"source":{"ref":"osc"}}})";
    auto inside = load_inst(head + "mux" + nodes + "]}}");
    PitchedInstrument& pi = *inside.instrument;
    CHECK(!pi.pool().voice(0).can_hold() && pi.pool().voice(0).description().heldEnvelopes.empty());
    play_notes(pi, {note_at(0.0f, 0.4f, 60.0f, 0.8f, true), note_at(0.4f, 0.4f, 64.0f)});
    CHECK(pi.rendered().size() == 2);
    float second = 0.0f;
    if (pi.rendered().size() == 2)
        for (float s : pi.rendered()[1].samples) second = std::max(second, std::fabs(s));
    CHECK(second > 0.01f);
    auto outside = load_inst(head + "out" + nodes + R"(,
      {"id":"amp","type":"Envelope","params":{"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.7,"release":0.05}},
      {"id":"out","type":"CombinedSource","params":{"source1":{"ref":"mux"},"source2":{"ref":"amp"},"operation":"Multiply"}}]}})");
    const Voice& v = outside.instrument->pool().voice(0);
    CHECK(v.can_hold() && v.description().heldEnvelopes == std::vector<std::string>{"amp"});
}

// The capture: one node id over a one-note play is zero outside the note and
// the node's raw output inside it, the note's samples before the velocity.
void run_node_capture_tests() {
    std::printf("the capture: one node over a one-note play, zero outside, the raw output inside\n");
    auto ip = load_inst(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.5,"release":0.05}},
      {"id":"osc","type":"SineSource","params":{"frequency":220.0,"amplitude":{"ref":"env"}}}]}})");
    PitchedInstrument& pi = *ip.instrument;
    const int frames = 48000, start = 12000;
    pi.capture().begin(pi.pool(), {"osc"}, frames);
    CHECK(pi.capture().active());
    play_notes(pi, {note_at(0.25f, 0.1f, 60.0f, 0.8f)});
    pi.capture().end();
    CHECK(!pi.capture().active() && pi.capture().buffers().size() == 1);
    if (pi.capture().buffers().size() != 1 || pi.rendered().size() != 1) { CHECK(false); return; }
    const auto& strip = pi.capture().buffers()[0];
    const auto& note = pi.rendered()[0].samples;
    const int len = int(note.size());
    CHECK(int(strip.size()) == frames && start + len < frames);
    bool outsideZero = true, insideRaw = true, audible = false;
    for (int f = 0; f < frames; ++f) {
        const int k = f - start;
        if (k < 0 || k >= len) { outsideZero = outsideZero && strip[size_t(f)] == 0.0f; continue; }
        insideRaw = insideRaw && strip[size_t(f)] * 0.8f == note[size_t(k)];
        audible = audible || strip[size_t(f)] != 0.0f;
    }
    CHECK(outsideZero && insideRaw && audible);
}

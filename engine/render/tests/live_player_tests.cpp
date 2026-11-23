#include "mforce/testing/check.h"
#include "test_support.h"
#include "patch/tests/fixtures.h"
#include "mforce/contract/performance.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/patch/patch.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/live_player.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace mforce;


// --- The live player -------------------------------------------------------

// The player's output in 256-frame buffers: `buffers` of them, or until no
// voice sounds when `buffers` is negative (at most a minute), then `after`
// buffers more.
static std::vector<float> live_buffers(LivePlayer& player, int buffers, int after = 0) {
    std::vector<float> out, buf(256);
    const int most = 60 * player.sample_rate() / 256;
    for (int k = 0; buffers < 0 ? (k == 0 || (player.sounding() > 0 && k < most)) : k < buffers; ++k) {
        player.fill(buf.data(), 256);
        out.insert(out.end(), buf.begin(), buf.end());
    }
    for (int k = 0; k < after; ++k) {
        player.fill(buf.data(), 256);
        out.insert(out.end(), buf.begin(), buf.end());
    }
    return out;
}
// `notes` played offline on a fresh instrument of `patch` as one part,
// rendered for `frames`.
static std::vector<float> offline_render(const Patch& patch, std::vector<PerformedNote> notes, size_t frames) {
    auto inst = build_instrument(patch);
    play_notes(*inst, std::move(notes));
    std::vector<float> out(frames, 0.0f);
    inst->render(out.data(), int(frames));
    return out;
}
// A note of exactly `samples` samples at 48 kHz.
static float seconds_of(int samples) { return (float(samples) + 0.5f) / 48000.0f; }
// A quiet sustaining patch whose pitch reaches the oscillator: the envelope
// holds, so a line can form, and two notes sum under the peak guard's knee.
static std::string live_patch_json() {
    return hold_patch_json(15.0f);
}

// A one-voice enveloped sine whose pitch reaches the oscillator.
static std::string one_voice_patch_json() {
    return R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"pf","type":"PerformNode","params":{"field":"frequency"}},
      {"id":"env","type":"Envelope","params":{"preset":"adsr","attack":0.01,"decay":0.05,"sustainLevel":0.5,"release":0.1}},
      {"id":"osc","type":"SineSource","params":{"frequency":{"ref":"pf"},"amplitude":{"ref":"env"}}}]}})";
}

// One classic note through fill equals the same note played offline, sample
// for sample to the end of the ring-out, and fill writes zeros after it: on
// two inline patches and a library patch with noise.
void run_live_classic_tests() {
    std::printf("the live player: a classic note through fill equals the offline note\n");
    const std::string sine = one_voice_patch_json();
    const Patch hat = load_patch("patches/library/percussion/hi_hat1.json");
    const Patch patches[] = {patch_door::read_text(sine), patch_door::read_text(live_patch_json()), hat};
    for (const Patch& patch : patches) {
        LivePlayer player(patch, 1);
        const PerformedNote note = note_at(0.0f, 0.2f, 62.0f, 0.7f);
        CHECK(player.note_on(note) != 0);
        const auto live = live_buffers(player, -1, 4);
        const auto offline = offline_render(patch, {note}, live.size());
        if (!patch_door::same_samples(live, offline)) std::printf("  %s\n", patch_door::difference(live, offline).c_str());
        CHECK(patch_door::same_samples(live, offline));
        CHECK(std::all_of(live.end() - 4 * 256, live.end(), [](float s) { return s == 0.0f; }));
        CHECK(std::any_of(live.begin(), live.end(), [](float s) { return s != 0.0f; }));
    }
}

// A held note released by note_off after k buffers equals the offline line
// of one held note of that length; a held note, a second carried after k
// buffers and a note_off after m more equal the offline two-note line.
void run_live_line_tests() {
    std::printf("the live player: a held note and a legato pair equal the offline lines\n");
    const Patch patch = patch_door::read_text(live_patch_json());
    const int k = 30, m = 25;
    {
        LivePlayer player(patch, 1);
        const LivePlayer::NoteId id = player.note_on(note_at(0.0f, seconds_of(k * 256), 60.0f, 0.7f, true));
        auto live = live_buffers(player, k);
        player.note_off(id);
        const auto tail = live_buffers(player, -1, 2);
        live.insert(live.end(), tail.begin(), tail.end());
        const auto offline = offline_render(patch, {note_at(0.0f, seconds_of(k * 256), 60.0f, 0.7f, true)}, live.size());
        if (!patch_door::same_samples(live, offline)) std::printf("  held: %s\n", patch_door::difference(live, offline).c_str());
        CHECK(patch_door::same_samples(live, offline));
    }
    {
        LivePlayer player(patch, 1);
        (void)player.note_on(note_at(0.0f, seconds_of(k * 256), 60.0f, 0.7f, true));
        auto live = live_buffers(player, k);
        const LivePlayer::NoteId second = player.note_on(note_at(0.0f, seconds_of(m * 256), 67.0f, 0.5f, true));
        CHECK(second != 0);
        const auto mid = live_buffers(player, m);
        player.note_off(second);
        const auto tail = live_buffers(player, -1, 2);
        live.insert(live.end(), mid.begin(), mid.end());
        live.insert(live.end(), tail.begin(), tail.end());
        const auto offline = offline_render(patch, {note_at(0.0f, seconds_of(k * 256), 60.0f, 0.7f, true),
                                                    note_at(0.0f, seconds_of(m * 256), 67.0f, 0.5f, true)}, live.size());
        if (!patch_door::same_samples(live, offline)) std::printf("  legato: %s\n", patch_door::difference(live, offline).c_str());
        CHECK(patch_door::same_samples(live, offline));
    }
}

// A steal: with one voice to sound, a long note and then a second. The first
// fades to zero over int(0.005 * rate) samples from the next buffer's top and
// the second is whole; a third note before any fill finds no idle voice.
void run_live_steal_tests() {
    std::printf("the live player: a steal fades the first note in 5 ms, the second whole, a third dropped\n");
    const Patch patch = patch_door::read_text(one_voice_patch_json());
    const PerformedNote first = note_at(0.0f, 2.0f, 60.0f, 0.4f), second = note_at(0.0f, 0.3f, 67.0f, 0.4f);
    const int k = 4, at = k * 256, fade = int(0.005f * 48000.0f);
    LivePlayer player(patch, 1);
    CHECK(player.note_on(first) != 0);
    auto live = live_buffers(player, k);
    CHECK(player.note_on(second) != 0);
    CHECK(player.note_on(note_at(0.0f, 0.3f, 72.0f, 0.4f)) == 0);
    const auto rest = live_buffers(player, -1, 2);
    live.insert(live.end(), rest.begin(), rest.end());
    const auto a = offline_render(patch, {first}, live.size());
    const auto b = offline_render(patch, {second}, live.size() - size_t(at));
    std::vector<float> want(live.size(), 0.0f);
    for (int i = 0; i < at + fade; ++i)
        want[size_t(i)] = i < at ? a[size_t(i)] : a[size_t(i)] * (float(fade - (i - at)) / float(fade));
    for (size_t i = size_t(at); i < want.size(); ++i) want[i] += b[i - size_t(at)];
    if (!patch_door::same_samples(live, want)) std::printf("  steal: %s\n", patch_door::difference(live, want).c_str());
    CHECK(patch_door::same_samples(live, want));
}

// note_off on a classic note, on an id never given and on an earlier note of
// a line changes nothing.
void run_live_note_off_tests() {
    std::printf("the live player: note_off on a classic note, an unknown id, an earlier note of a line\n");
    const Patch patch = patch_door::read_text(live_patch_json());
    const auto run = [&](bool extraOffs) {
        LivePlayer player(patch, 2);
        const LivePlayer::NoteId plain = player.note_on(note_at(0.0f, 0.2f, 55.0f, 0.3f));
        auto out = live_buffers(player, 4);
        if (extraOffs) { player.note_off(plain); player.note_off(9999); }
        const LivePlayer::NoteId a = player.note_on(note_at(0.0f, seconds_of(10 * 256), 60.0f, 0.3f, true));
        const auto mid = live_buffers(player, 10);
        const LivePlayer::NoteId b = player.note_on(note_at(0.0f, seconds_of(10 * 256), 64.0f, 0.3f, true));
        if (extraOffs) player.note_off(a);
        const auto late = live_buffers(player, 10);
        player.note_off(b);
        const auto tail = live_buffers(player, -1, 1);
        for (const auto* part : {&mid, &late, &tail}) out.insert(out.end(), part->begin(), part->end());
        return out;
    };
    const auto quiet = run(false), noisy = run(true);
    CHECK(!quiet.empty() && patch_door::same_samples(quiet, noisy));
}

// fill's output follows set_volume, and the peak guard bounds it.
void run_live_volume_tests() {
    std::printf("the live player: fill follows set_volume; the peak guard bounds it\n");
    const Patch patch = patch_door::read_text(R"({"sampleRate":48000,"instrument":{"polyphony":1},"graph":{"output":"osc","nodes":[
      {"id":"osc","type":"SineSource","params":{"frequency":220.0}}]}})");
    const auto at = [&](float volume) {
        LivePlayer player(patch, 1);
        player.set_volume(volume);
        (void)player.note_on(note_at(0.0f, 0.1f, 60.0f, 0.8f));
        return live_buffers(player, 8);
    };
    const auto full = at(1.0f), half = at(0.5f), loud = at(10.0f);
    bool follows = full.size() == half.size();
    for (size_t i = 0; follows && i < full.size(); ++i) follows = half[i] == full[i] * 0.5f;
    CHECK(follows && std::any_of(full.begin(), full.end(), [](float s) { return s != 0.0f; }));
    CHECK(std::all_of(loud.begin(), loud.end(), [](float s) { return std::fabs(s) <= 0.999f; }));
    CHECK(std::any_of(loud.begin(), loud.end(), [](float s) { return std::fabs(s) > 0.95f; }));
}

// The steal takes the voice released longest ago. A line held two seconds
// and released (its release a quarter of that, still sounding), then a
// classic note, then a third note: the line is the victim, so a second later
// the classic note and the third still sound. (A fresh note starts only when
// no line is open, so every voice a steal weighs is released; with one line
// at a time, released-first and started-first agree.)
void run_live_steal_policy_tests() {
    std::printf("the live player: the victim is the voice released longest ago\n");
    const Patch patch = patch_door::read_text(live_patch_json());
    LivePlayer player(patch, 2);
    const int k = 375;                                               // two seconds of buffers
    const LivePlayer::NoteId line = player.note_on(note_at(0.0f, seconds_of(k * 256), 60.0f, 0.3f, true));
    (void)live_buffers(player, k);
    player.note_off(line);                                           // released first
    (void)live_buffers(player, 2);
    CHECK(player.note_on(note_at(0.0f, 3.0f, 64.0f, 0.3f)) != 0);   // released at its start, later
    (void)live_buffers(player, 2);
    CHECK(player.sounding() == 2);                                   // the line is still releasing
    CHECK(player.note_on(note_at(0.0f, 3.0f, 67.0f, 0.3f)) != 0);   // full: a steal
    (void)live_buffers(player, int(1.0f * 48000.0f) / 256);
    if (player.sounding() != 2) std::printf("  sounding: %d\n", player.sounding());
    CHECK(player.sounding() == 2);
}

// A key-up is never lost: with the queue full, note_off closes the line and
// keeps its Release, note_on returns 0 while it waits, and after a fill the
// next control call sends it, so the line rings out.
void run_live_key_up_tests() {
    std::printf("the live player: a key-up the full queue cannot take waits and is sent\n");
    const Patch patch = patch_door::read_text(live_patch_json());
    LivePlayer player(patch, 1);
    LivePlayer::NoteId last = player.note_on(note_at(0.0f, 2.0f, 60.0f, 0.3f, true));
    CHECK(last != 0);
    int carried = 0;
    for (LivePlayer::NoteId id; (id = player.note_on(note_at(0.0f, 2.0f, 62.0f, 0.3f, true))) != 0; ++carried) last = id;
    CHECK(carried > 0);                                              // the queue is full now
    player.note_off(last);
    CHECK(player.note_on(note_at(0.0f, 0.1f, 64.0f, 0.3f)) == 0);   // the Release waits
    (void)live_buffers(player, 1);
    CHECK(player.sounding() == 1);                                   // the Release is sent here
    (void)live_buffers(player, -1);
    CHECK(player.sounding() == 0);
}

// Two real threads: an audio thread filling in a loop while the control
// thread sends classic notes, held lines and bursts that steal. Afterwards,
// with fill still running, every voice comes back (none a note_on handed
// over is lost) and every sample written was finite.
void run_live_threads_tests() {
    std::printf("the live player: an audio thread and a control thread, a few thousand notes\n");
    const Patch patch = patch_door::read_text(live_patch_json());
    LivePlayer player(patch, 2);
    std::atomic<bool> stop{false}, finite{true};
    std::thread audio([&] {
        std::vector<float> buf(256);
        while (!stop.load()) {
            player.fill(buf.data(), 256);
            for (float s : buf) if (!std::isfinite(s)) finite.store(false);
        }
    });
    int sent = 0;
    LivePlayer::NoteId held = 0;
    for (int i = 0; i < 3000; ++i) {
        const float nn = 48.0f + float(i % 24);
        if (i % 10 == 3) {
            held = player.note_on(note_at(0.0f, 0.05f, nn, 0.2f, true));
        } else if (i % 10 == 6) {
            const LivePlayer::NoteId next = player.note_on(note_at(0.0f, 0.05f, nn, 0.2f, true));
            if (next) held = next;
        } else if (i % 10 == 8) {
            player.note_off(held);
        } else if (i % 50 == 0) {
            for (int k = 0; k < 5; ++k) sent += player.note_on(note_at(0.0f, 0.03f, nn + float(k), 0.2f)) != 0;
        } else {
            sent += player.note_on(note_at(0.0f, 0.02f, nn, 0.2f)) != 0;
        }
        if (i % 7 == 0) std::this_thread::yield();
    }
    player.note_off(held);
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (player.sounding() > 0 && std::chrono::steady_clock::now() < until) std::this_thread::yield();
    const int left = player.sounding();
    stop.store(true);
    audio.join();
    CHECK(sent > 0 && left == 0 && finite.load());
}

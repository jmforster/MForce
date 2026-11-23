// alloc_gate: does this patch allocate between note-on and its last sample?
//
//   alloc_gate [--list] [--verbose] <patch.json | folder>...
//
// Each patch is loaded and prepared unarmed (prepare may allocate), then the
// allocation probe is armed around what the real-time contract forbids
// allocating. An instrument patch is played through a LivePlayer, the live
// path, armed from its first note_on to its last sample: each note of its
// embedded score as a plain note (or a C4 when it has no score), filled in
// 256-frame buffers until the player is silent; then a held line (a held
// note, a second carried onto it, a note_off) when its voices can hold; then
// a steal (one note more at once than the player sounds). A mixer-mode graph
// renders each channel's source, whose adapter calls start and next. A patch
// that allocates is reported with its count and the first allocation's call
// stack; the exit code is 1 if any did, 2 if any failed to load.
#include "mforce/testing/alloc_probe.h"
#include "mforce/patch/patch_json.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/live_player.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

using namespace mforce;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

struct Options {
    bool list{false};
    bool verbose{false};
    std::vector<fs::path> paths;
};

struct Result {
    std::size_t count{0};
    std::size_t bytes{0};
    std::vector<const void*> stack;
    std::string loadError;
};

// A score event as the gate plays it. Articulation and ornament belong to
// the performer, which runs on the control side; the gate plays the written
// note.
struct Event {
    float note{60.0f};
    float velocity{0.8f};
    float duration{0.5f};
};

std::string slurp(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<fs::path> collect(const std::vector<fs::path>& given) {
    std::vector<fs::path> out;
    for (const auto& p : given) {
        if (fs::is_directory(p)) {
            for (const auto& e : fs::recursive_directory_iterator(p))
                if (e.is_regular_file() && e.path().extension() == ".json") out.push_back(e.path());
        } else {
            out.push_back(p);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<Event> score_events(const json& root) {
    std::vector<Event> events;
    if (!root.contains("score")) return events;
    for (const auto& n : root.at("score")) {
        Event e;
        e.note = n.at("note").get<float>();
        e.velocity = n.value("velocity", 0.8f);
        e.duration = n.at("duration").get<float>();
        events.push_back(e);
    }
    return events;
}

void take_result(Result& r) {
    r.count = alloc_probe::count();
    r.bytes = alloc_probe::bytes();
    int depth = 0;
    const void* const* frames = alloc_probe::first_stack(depth);
    r.stack.assign(frames, frames + depth);
}

// A note of `seconds` at `noteNumber`, held or not.
PerformedNote note_of(float noteNumber, float velocity, float seconds, bool hold = false) {
    return {.start = 0.0f, .duration = seconds, .noteNumber = noteNumber, .velocity = velocity, .hold = hold};
}

// The player filled in 256-frame buffers until no voice sounds (at most a
// minute of audio).
void fill_until_silent(LivePlayer& player, std::vector<float>& buf) {
    const int most = 60 * player.sample_rate() / int(buf.size());
    int k = 0;
    do player.fill(buf.data(), int(buf.size()));
    while (player.sounding() > 0 && ++k < most);
}

// `count` buffers filled.
void fill_for(LivePlayer& player, std::vector<float>& buf, int count) {
    for (int k = 0; k < count; ++k) player.fill(buf.data(), int(buf.size()));
}

// The live path, through a LivePlayer of one sounding voice (or the patch's
// polyphony): each score note, then a held line when the voices can hold,
// then a steal, armed throughout.
Result gate_instrument(const fs::path& path, const json& root) {
    Result r;
    const Patch patch = load_patch(path.string());
    const bool canHold = build_instrument(patch)->pool().voice(0).can_hold();
    LivePlayer player(patch, 1);
    std::vector<Event> events = score_events(root);
    if (events.empty()) events.push_back(Event{});
    const int voices = (std::max)(1, patch.instrument->polyphony);   // parenthesized: windows.h defines max
    std::vector<float> buf(256);
    alloc_probe::reset();
    alloc_probe::arm();
    for (const Event& e : events) {
        (void)player.note_on(note_of(e.note, e.velocity, e.duration));
        fill_until_silent(player, buf);
    }
    if (canHold) {
        (void)player.note_on(note_of(60.0f, 0.8f, 0.5f, true));
        fill_for(player, buf, 20);
        const LivePlayer::NoteId second = player.note_on(note_of(64.0f, 0.8f, 0.5f, true));
        fill_for(player, buf, 20);
        player.note_off(second);
        fill_until_silent(player, buf);
    }
    for (int k = 0; k <= voices; ++k) (void)player.note_on(note_of(60.0f + float(k), 0.8f, 0.5f));
    fill_until_silent(player, buf);
    alloc_probe::disarm();
    take_result(r);
    return r;
}

// A mixer-mode graph: each channel's source is a pull loop of its own whose
// render starts the graph and pulls it once per sample (the loader's
// adapters). The mix itself — Mixer::render, with its scratch buffer —
// is the offline host's and is not armed.
Result gate_mixer(BuiltSound& p) {
    Result r;
    std::vector<float> mono(size_t(p.frames), 0.0f);
    alloc_probe::reset();
    alloc_probe::arm();
    for (auto& ch : p.mixer->channels)
        if (ch.source) ch.source->render(mono.data(), p.frames);
    alloc_probe::disarm();
    take_result(r);
    return r;
}

Result gate_one(const fs::path& path) {
    try {
        const json root = json::parse(slurp(path));
        if (root.contains("instrument")) return gate_instrument(path, root);
        BuiltSound p = build_sound(load_patch(path.string()));          // unarmed: the mixer-mode graph, prepared
        return gate_mixer(p);
    } catch (const std::exception& e) {
        Result r;
        r.loadError = e.what();
        return r;
    }
}

#ifdef _WIN32
void print_frame(const void* addr) {
    HANDLE proc = GetCurrentProcess();
    char buf[sizeof(SYMBOL_INFO) + 256] = {0};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(buf);
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 255;
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD lineDisp = 0;
    DWORD64 disp = 0;
    const auto a = reinterpret_cast<DWORD64>(addr);
    const char* name = SymFromAddr(proc, a, &disp, sym) ? sym->Name : "?";
    if (SymGetLineFromAddr64(proc, a, &lineDisp, &line))
        std::printf("    %s  (%s:%lu)\n", name, line.FileName, static_cast<unsigned long>(line.LineNumber));
    else
        std::printf("    %s  (%p)\n", name, addr);
}
#else
void print_frame(const void* addr) { std::printf("    %p\n", addr); }
#endif

void report(const fs::path& path, const Result& r, const Options& o) {
    const std::string name = path.generic_string();
    if (!r.loadError.empty()) {
        std::printf("LOAD FAIL %s: %s\n", name.c_str(), r.loadError.c_str());
        return;
    }
    if (o.list) {
        std::printf("%zu\t%s\n", r.count, name.c_str());
        return;
    }
    if (r.count == 0) {
        if (o.verbose) std::printf("ok %s\n", name.c_str());
        return;
    }
    std::printf("ALLOC %s: %zu allocations, %zu bytes\n", name.c_str(), r.count, r.bytes);
    for (const void* f : r.stack) print_frame(f);
}

Options parse_args(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--list") o.list = true;
        else if (a == "--verbose") o.verbose = true;
        else o.paths.emplace_back(a);
    }
    return o;
}

}  // namespace

int main(int argc, char** argv) {
    const Options o = parse_args(argc, argv);
    if (o.paths.empty()) {
        std::fprintf(stderr, "Usage: alloc_gate [--list] [--verbose] <patch.json | folder>...\n");
        return 2;
    }
#ifdef _WIN32
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
#endif
    const std::vector<fs::path> patches = collect(o.paths);
    std::size_t allocating = 0, failed = 0;
    for (const auto& path : patches) {
        const Result r = gate_one(path);
        report(path, r, o);
        if (!r.loadError.empty()) ++failed;
        else if (r.count) ++allocating;
    }
    std::printf("%zu patches: %zu allocate, %zu failed to load\n", patches.size(), allocating, failed);
    if (failed) return 2;
    return allocating ? 1 : 0;
}

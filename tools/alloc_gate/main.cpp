// alloc_gate: does this patch allocate between note-on and its last sample?
//
//   alloc_gate [--list] [--verbose] <patch.json | folder>...
//
// Each patch is loaded and prepared unarmed (prepare may allocate), then the
// allocation probe is armed around what the real-time contract forbids
// allocating. An instrument patch is played the way the UI's keyboard plays
// it — the live path: a pool slot, the note's Setup and start in
// prepare_voice_at, one pull per sample for the note's life — once for each
// note of its embedded score (hold has no streaming form and is played as a
// plain note), or once for a C4 when it has no score. A mixer-mode graph
// renders each channel's source, whose adapter calls start and next. A patch
// that allocates is reported with its count and the first allocation's call
// stack; the exit code is 1 if any did, 2 if any failed to load.
#include "alloc_probe.h"
#include "mforce/render/patch_loader.h"
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

// The live path: for each note, a pool slot, prepare_voice_at (the note's
// Setup, then start on the voice and its tails), one pull per sample for
// the note's life (duration plus the tail), then the slot back. Scored
// timing does not bear on allocation, so the notes play one after another.
Result gate_instrument(const fs::path& path, const json& root) {
    Result r;
    InstrumentPatch ip = load_instrument_patch(path.string());
    PitchedInstrument& inst = *ip.instrument;
    std::vector<Event> events = score_events(root);
    if (events.empty()) events.push_back(Event{});
    inst.release_voice(inst.acquire_voice());   // the slot table is sized on first use: before arming
    alloc_probe::reset();
    alloc_probe::arm();
    for (const Event& e : events) {
        int slot = inst.acquire_voice();
        if (slot < 0) slot = 0;
        PitchedInstrument::StreamingVoice sv = inst.prepare_voice_at(slot, e.note, e.velocity, e.duration);
        for (int i = 0; i < sv.durSamples; ++i) {
            sv.tick();
            (void)sv.source->next();
            for (auto& a : sv.advanceList) a->next();
        }
        inst.release_voice(slot);
    }
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
        BuiltSound p = load_patch_file(path.string());          // unarmed: the mixer-mode graph, prepared
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

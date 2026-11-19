// twin_gate: does the module read a patch file as the loader reads it?
//
//   twin_gate [mode] [--expect <what.txt>] <patch.json | folder>...
//
// For each file (a folder gives every *.json below it, sorted), both readers
// in this one process from the one file. The offline path: load_patch_file
// against build_sound(load_patch(path)), each mixer rendered over its own
// frames into a float buffer, compared as bits with the sample rate, the
// frame count and the fault report. A plain sound's faults are reported by
// id through the module's mixer alone, so they are listed (FAULTS) and not
// compared; an instrument's are compared. The live path, for a file with an
// instrument block: load_instrument_patch against build_instrument, each note
// of the embedded score (a C4 where there is none) played as alloc_gate plays
// it, the probe armed over the same span on each arm; compared: every raw
// per-sample output and each note's gain as bits, and the allocation counts.
//
// Modes, one per run:
//   (none)                 the loader against the module
//   --self                 the loader against a second load of itself
//   --order reversed       the module builds the record with graph.nodes reversed
//   --order tails-kept     reversed, the tap-only tails back in their own order
//                          among the places tails hold in the reversed list
//   --resaved <dir>        the record saved under <dir>; the loader and the
//                          module each read the saved file, against the
//                          loader's render of the original
//   --sparse <dir>         the record with every Param number and scalar or
//                          string setting equal to its declared default
//                          removed; the loader and the module read it saved
//                          under <dir>, and the module builds it as it stands
//   --time <rounds>        the offline load and render of each reader, the
//                          loader first in even rounds, the module in odd;
//                          medians per file; nothing compared
//   --warnings             the module's build warnings, one line each
//
// Lines: SAME, DIFFER, OLD FAILS (; the module reads it), BOTH REFUSE,
// NEW REFUSES, UNPLAYED (a score the live path cannot play), and FAULTS; then
// one summary, ending with what the arms did: the notes compared on the live
// path, under --sparse the keys removed, under --resaved the files whose saved
// text is not the file's. A sample index on the offline path is into the
// interleaved stereo buffer; on the live path into the notes' samples one
// after another. Exit 1 on any file not SAME (with --expect: on any file
// whose line is not its row's kind), 2 on a usage error.
//
// What it cannot hear: in a comparison the loader's arm always runs first in
// the process, so a difference that depends on which reader runs first does
// not show. The live path's allocation count is taken after both offline
// renders have run in the process, so an allocation a type makes on its first
// use anywhere is in neither arm's count; the allocation gate, a process of
// its own, is where that is heard.
#include "alloc_probe.h"
#include "mforce/patch/build_graph.h"
#include "mforce/patch/patch_json.h"
#include "mforce/patch/type_declarations.h"
#include "mforce/render/build_instrument.h"
#include "mforce/render/build_sound.h"
#include "mforce/render/patch_loader.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace mforce;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

enum class Mode { Twin, Self, Reversed, TailsKept, Resaved, Sparse, Time, Warnings };

struct Options {
    Mode mode{Mode::Twin};
    fs::path scratch;
    int rounds{3};
    fs::path expect;
    std::vector<fs::path> paths;
};

// A score event as the live path plays it: the written note, as alloc_gate plays it.
struct Event {
    float note{60.0f};
    float velocity{0.8f};
    float duration{0.5f};
};

// One reader's way to the two things the gate plays from a file.
struct Arm {
    std::string name;                                                  // empty where a mode has one arm
    std::function<BuiltSound()> sound;
    std::function<std::unique_ptr<PitchedInstrument>()> instrument;
    fs::path file;                                                     // the file it reads; empty for a record
};

struct Offline {
    int rate{0};
    int frames{0};
    std::vector<float> samples;
    std::string faults;
};

struct Live {
    std::vector<float> samples;
    std::vector<float> gains;
    std::size_t allocations{0};
};

// One file's lines and the kind that names it (SAME, DIFFER, OLD_FAILS,
// BOTH_REFUSE, NEW_REFUSES or UNPLAYED); `sentence` is the module's refusal.
struct Verdict {
    std::string kind{"SAME"};
    std::vector<std::string> lines;
    std::string sentence;
    int liveNotes{0};                    // notes compared on the live path, over the arms
    std::size_t keysRemoved{0};          // --sparse: keys the sparse record lacks
    bool savedDiffers{false};            // --resaved: the saved text is not the file's
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
        if (!fs::is_directory(p)) { out.push_back(p); continue; }
        std::vector<fs::path> found;
        for (const auto& e : fs::recursive_directory_iterator(p))
            if (e.is_regular_file() && e.path().extension() == ".json") found.push_back(e.path());
        std::sort(found.begin(), found.end(),
                  [](const fs::path& a, const fs::path& b) { return a.generic_string() < b.generic_string(); });
        out.insert(out.end(), found.begin(), found.end());
    }
    return out;
}

std::vector<Event> score_events(const json& root) {
    std::vector<Event> events;
    if (root.contains("score"))
        for (const auto& n : root.at("score")) {
            Event e;
            e.note = n.at("note").get<float>();
            e.velocity = n.value("velocity", 0.8f);
            e.duration = n.at("duration").get<float>();
            events.push_back(e);
        }
    if (events.empty()) events.push_back(Event{});
    return events;
}

// A float as text that reads back to the same float.
std::string num(float v) {
    char text[32];
    std::snprintf(text, sizeof text, "%.9g", double(v));
    return text;
}

bool same_bits(float a, float b) { return std::memcmp(&a, &b, sizeof a) == 0; }

Arm loader_arm(std::string name, const fs::path& file) {
    const std::string f = file.string();
    return {std::move(name), [f] { return load_patch_file(f); },
            [f] { return std::move(load_instrument_patch(f).instrument); }, file};
}

Arm module_file_arm(std::string name, const fs::path& file) {
    const std::string f = file.string();
    return {std::move(name), [f] { return build_sound(load_patch(f)); },
            [f] { return build_instrument(load_patch(f)); }, file};
}

Arm module_record_arm(std::string name, Patch p) {
    auto shared = std::make_shared<const Patch>(std::move(p));
    return {std::move(name), [shared] { return build_sound(*shared); },
            [shared] { return build_instrument(*shared); }};
}

Offline render_offline(BuiltSound s) {
    Offline o;
    o.rate = s.sampleRate;
    o.frames = s.frames;
    o.samples.assign(2 * std::size_t(std::max(0, s.frames)), 0.0f);
    s.mixer->render(o.samples.data(), s.frames);
    std::vector<FaultReport> faults;
    s.mixer->collect_faults(faults);
    for (const auto& f : faults) o.faults += (o.faults.empty() ? "" : "; ") + fault_report_text(f);
    return o;
}

// The live path as alloc_gate plays it, the probe armed from the first note's
// slot to the last note's release. The buffer grows with the probe disarmed.
Live play_live(PitchedInstrument& inst, const std::vector<Event>& events) {
    Live out;
    inst.release_voice(inst.acquire_voice());   // the slot table is sized on first use: before arming
    alloc_probe::reset();
    alloc_probe::arm();
    for (const Event& e : events) {
        int slot = inst.acquire_voice();
        if (slot < 0) slot = 0;
        PitchedInstrument::StreamingVoice sv = inst.prepare_voice_at(slot, e.note, e.velocity, e.duration);
        alloc_probe::disarm();
        const std::size_t base = out.samples.size();
        out.samples.resize(base + std::size_t(std::max(0, sv.durSamples)));
        out.gains.push_back(sv.gain);
        alloc_probe::arm();
        for (int i = 0; i < sv.durSamples; ++i) {
            sv.tick();
            out.samples[base + std::size_t(i)] = sv.source->next();
            for (auto& a : sv.advanceList) a->next();
        }
        inst.release_voice(slot);
    }
    alloc_probe::disarm();
    out.allocations = alloc_probe::count();
    return out;
}

// "first at sample <n>: <old> <new>", or "" when the two are equal as bits.
std::string first_difference(const std::vector<float>& a, const std::vector<float>& b) {
    std::size_t i = 0;
    while (i < a.size() && i < b.size() && same_bits(a[i], b[i])) ++i;
    if (i == a.size() && i == b.size()) return "";
    const std::string x = i < a.size() ? num(a[i]) : "none";
    const std::string y = i < b.size() ? num(b[i]) : "none";
    return "first at sample " + std::to_string(i) + ": " + x + " " + y;
}

std::vector<std::string> compare_offline(const Offline& a, const Offline& b, bool faultsCompared) {
    std::vector<std::string> d;
    if (a.rate != b.rate) d.push_back("offline sampleRate: " + std::to_string(a.rate) + " " + std::to_string(b.rate));
    if (a.frames != b.frames) d.push_back("offline frames: " + std::to_string(a.frames) + " " + std::to_string(b.frames));
    if (const std::string s = first_difference(a.samples, b.samples); !s.empty()) d.push_back("offline " + s);
    if (faultsCompared && a.faults != b.faults)
        d.push_back("offline faults: \"" + a.faults + "\" \"" + b.faults + "\"");
    return d;
}

std::vector<std::string> compare_live(const Live& a, const Live& b) {
    std::vector<std::string> d;
    if (const std::string s = first_difference(a.samples, b.samples); !s.empty()) d.push_back("live " + s);
    if (const std::string s = first_difference(a.gains, b.gains); !s.empty())
        d.push_back("live gain " + s.substr(std::strlen("first at sample ")));
    if (a.allocations != b.allocations)
        d.push_back("live allocations: " + std::to_string(a.allocations) + " " + std::to_string(b.allocations));
    return d;
}

// The tap-only tails of the original, put back in their own relative order
// at the positions tails hold in `nodes`.
void keep_tails(std::vector<Node>& nodes, const Graph& original) {
    const std::vector<std::string> ids = advance_ids(original);
    const std::set<std::string> tails(ids.begin(), ids.end());
    std::vector<Node> inOrder;
    for (const Node& n : original.nodes)
        if (tails.count(n.id)) inOrder.push_back(n);
    std::size_t k = 0;
    for (Node& n : nodes)
        if (tails.count(n.id)) n = inOrder[k++];
}

// The record with every Param number, scalar setting and string setting
// equal to its declared default removed.
Patch sparse_of(Patch p) {
    for (Node& n : p.graph.nodes) {
        const TypeDeclarations* d = type_declarations(n.type);
        if (!d) continue;
        std::erase_if(n.values, [d](const auto& kv) {
            const auto* decl = d->param(kv.first);
            return decl && same_bits(kv.second, decl->defaultValue);
        });
        std::erase_if(n.settings, [d](const auto& kv) {
            const auto* decl = d->setting(kv.first);
            return decl && same_bits(kv.second, decl->defaultValue);
        });
        std::erase_if(n.strings, [d](const auto& kv) {
            const auto* decl = d->setting(kv.first);
            return decl && kv.second == decl->defaultText;
        });
    }
    return p;
}

// The Param numbers, scalar settings and string settings a record holds.
std::size_t keys_of(const Patch& p) {
    std::size_t n = 0;
    for (const Node& node : p.graph.nodes) n += node.values.size() + node.settings.size() + node.strings.size();
    return n;
}

fs::path scratch_file(const fs::path& dir, const fs::path& path) {
    std::string name = path.generic_string();
    std::replace(name.begin(), name.end(), '/', '_');
    std::replace(name.begin(), name.end(), ':', '_');
    fs::create_directories(dir);
    return dir / name;
}

// The arms a mode compares with the loader's render of the original, and
// under --sparse the keys removed into `v`. Throws where the module refuses
// the original file.
std::vector<Arm> candidates(const Options& o, const fs::path& path, Verdict& v) {
    switch (o.mode) {
    case Mode::Self: return {loader_arm("", path)};
    case Mode::Reversed:
    case Mode::TailsKept: {
        Patch p = load_patch(path.string());
        const Graph original = p.graph;
        std::reverse(p.graph.nodes.begin(), p.graph.nodes.end());
        if (o.mode == Mode::TailsKept) keep_tails(p.graph.nodes, original);
        return {module_record_arm("", std::move(p))};
    }
    case Mode::Resaved: {
        const fs::path file = scratch_file(o.scratch, path);
        save_patch(file.string(), load_patch(path.string()));
        return {loader_arm("the loader, resaved", file), module_file_arm("the module, resaved", file)};
    }
    case Mode::Sparse: {
        const fs::path file = scratch_file(o.scratch, path);
        const Patch original = load_patch(path.string());
        Patch sparse = sparse_of(original);
        v.keysRemoved = keys_of(original) - keys_of(sparse);
        save_patch(file.string(), sparse);
        return {loader_arm("the loader, sparse file", file), module_file_arm("the module, sparse file", file),
                module_record_arm("the module, sparse record", std::move(sparse))};
    }
    default: return {module_file_arm("", path)};
    }
}

std::string where(const Arm& arm) { return arm.name.empty() ? "" : arm.name + ": "; }

void differ(Verdict& v, const std::string& name, const Arm& arm, const std::vector<std::string>& diffs) {
    for (const auto& d : diffs) v.lines.push_back("DIFFER " + name + ": " + where(arm) + d);
    if (!diffs.empty() && v.kind == "SAME") v.kind = "DIFFER";
}

void refuse(Verdict& v, const std::string& name, const Arm& arm, const std::string& sentence) {
    v.lines.push_back("NEW REFUSES " + name + ": " + where(arm) + sentence);
    v.kind = "NEW_REFUSES";
    if (v.sentence.empty()) v.sentence = sentence;
}

// The loader refused the file: does the module read it?
Verdict old_fails(const std::string& name, const std::string& oldErr, std::vector<Arm>& arms,
                  const std::string& prepErr) {
    Verdict v;
    std::string newErr = prepErr;
    if (newErr.empty() && !arms.empty()) {
        try { (void)arms.front().sound(); }
        catch (const std::exception& e) { newErr = e.what(); }
    }
    if (newErr.empty()) {
        v.kind = "OLD_FAILS";
        v.lines.push_back("OLD FAILS " + name + ": " + oldErr + "; the module reads it");
    } else {
        v.kind = "BOTH_REFUSE";
        v.lines.push_back("BOTH REFUSE " + name + ": " + oldErr + " | " + newErr);
    }
    return v;
}

// Each arm's live path against the loader's, for a file with an instrument block.
void judge_live(Verdict& v, const std::string& name, const fs::path& path, const std::vector<Event>& events,
                const std::vector<Arm>& arms, const std::vector<bool>& refused) {
    Live ref;
    try { ref = play_live(*load_instrument_patch(path.string()).instrument, events); }
    catch (const std::exception& e) {
        v.lines.push_back("OLD FAILS " + name + ": live: " + e.what());
        v.kind = "OLD_FAILS";
        return;
    }
    for (std::size_t k = 0; k < arms.size(); ++k) {
        if (refused[k]) continue;
        try {
            auto inst = arms[k].instrument();
            differ(v, name, arms[k], compare_live(ref, play_live(*inst, events)));
            v.liveNotes += int(events.size());
        } catch (const std::exception& e) { refuse(v, name, arms[k], std::string("live: ") + e.what()); }
    }
}

Verdict judge_file(const Options& o, const fs::path& path) {
    const std::string name = path.generic_string();
    Offline ref;
    json root;
    std::string oldErr, prepErr;
    try {
        root = json::parse(slurp(path));
        ref = render_offline(load_patch_file(path.string()));
    } catch (const std::exception& e) { oldErr = e.what(); }
    Verdict v;
    std::vector<Arm> arms;
    try { arms = candidates(o, path, v); }
    catch (const std::exception& e) { prepErr = e.what(); }
    if (!oldErr.empty()) return old_fails(name, oldErr, arms, prepErr);
    if (o.mode == Mode::Resaved && !arms.empty()) v.savedDiffers = slurp(arms.front().file) != slurp(path);
    if (!prepErr.empty()) {
        refuse(v, name, Arm{}, prepErr);
        return v;
    }
    const bool instrument = root.is_object() && root.contains("instrument");
    std::vector<bool> refused(arms.size(), false);
    for (std::size_t k = 0; k < arms.size(); ++k) {
        Offline out;
        try { out = render_offline(arms[k].sound()); }
        catch (const std::exception& e) { refuse(v, name, arms[k], e.what()); refused[k] = true; continue; }
        differ(v, name, arms[k], compare_offline(ref, out, instrument));
        if (!instrument && !out.faults.empty())
            v.lines.push_back("FAULTS " + name + ": " + where(arms[k]) + out.faults);
    }
    try { if (instrument) (void)score_events(root); }
    catch (const std::exception& e) {
        v.kind = "UNPLAYED";
        v.lines.push_back("UNPLAYED " + name + ": " + e.what());
        return v;
    }
    if (instrument) judge_live(v, name, path, score_events(root), arms, refused);
    if (v.kind == "SAME") v.lines.insert(v.lines.begin(), "SAME " + name);
    return v;
}

// what.txt: a file name, the base's verdict, the expected kind, a needle, a description.
struct Row {
    std::string kind;
    std::string needle;
};

std::map<std::string, Row> read_rows(const fs::path& file) {
    std::map<std::string, Row> rows;
    std::istringstream in(slurp(file));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> cols;
        std::size_t at = 0;
        for (std::size_t tab; (tab = line.find('\t', at)) != std::string::npos; at = tab + 1)
            cols.push_back(line.substr(at, tab - at));
        cols.push_back(line.substr(at));
        if (cols.size() >= 4) rows[cols[0]] = {cols[2], cols[3]};
    }
    return rows;
}

// Whether a file's verdict is the one its row names; "" when it is.
std::string departure(const std::map<std::string, Row>& rows, const fs::path& path, const Verdict& v) {
    const auto it = rows.find(path.filename().string());
    if (it == rows.end()) return "no row names the file";
    const Row& row = it->second;
    if (v.kind != row.kind) return "its row says " + row.kind;
    if (row.kind == "NEW_REFUSES" && v.sentence.find(row.needle) == std::string::npos)
        return "its row's sentence holds '" + row.needle + "'";
    return "";
}

int run_gate(const Options& o, const std::vector<fs::path>& files) {
    std::map<std::string, Row> rows;
    if (!o.expect.empty()) rows = read_rows(o.expect);
    std::map<std::string, int> kinds;
    int departs = 0, faultLines = 0, liveNotes = 0, savedDiffer = 0;
    std::size_t keysRemoved = 0;
    for (const auto& path : files) {
        const Verdict v = judge_file(o, path);
        for (const auto& line : v.lines) {
            std::printf("%s\n", line.c_str());
            if (line.rfind("FAULTS ", 0) == 0) ++faultLines;
        }
        ++kinds[v.kind];
        liveNotes += v.liveNotes;
        keysRemoved += v.keysRemoved;
        savedDiffer += v.savedDiffers ? 1 : 0;
        if (!o.expect.empty())
            if (const std::string d = departure(rows, path, v); !d.empty()) {
                std::printf("DEPARTS %s: the line is %s; %s\n", path.generic_string().c_str(), v.kind.c_str(), d.c_str());
                ++departs;
            }
        std::fflush(stdout);
    }
    std::printf("%zu files: %d same, %d differ, %d old fails, %d both refuse, %d new refuses, %d depart from their row, "
                "%d fault lines, %d unplayed, %d live notes",
                files.size(), kinds["SAME"], kinds["DIFFER"], kinds["OLD_FAILS"], kinds["BOTH_REFUSE"],
                kinds["NEW_REFUSES"], departs, faultLines, kinds["UNPLAYED"], liveNotes);
    if (o.mode == Mode::Sparse) std::printf(", %zu keys removed", keysRemoved);
    if (o.mode == Mode::Resaved) std::printf(", %d saved texts differ", savedDiffer);
    std::printf("\n");
    if (!o.expect.empty()) return departs ? 1 : 0;
    return kinds["SAME"] == int(files.size()) ? 0 : 1;
}

double millis_since(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

int run_time(const Options& o, const std::vector<fs::path>& files) {
    double totalOld = 0, totalNew = 0;
    int failed = 0;
    for (const auto& path : files) {
        std::vector<double> olds, news;
        try {
            const auto timed = [](std::vector<double>& into, const auto& build) {
                const auto t0 = std::chrono::steady_clock::now();
                (void)render_offline(build());
                into.push_back(millis_since(t0));
            };
            const auto oldArm = [&] { return load_patch_file(path.string()); };
            const auto newArm = [&] { return build_sound(load_patch(path.string())); };
            for (int r = 0; r < o.rounds; ++r)
                if (r % 2 == 0) { timed(olds, oldArm); timed(news, newArm); }
                else { timed(news, newArm); timed(olds, oldArm); }
        } catch (const std::exception& e) {
            std::printf("TIME FAILS %s: %s\n", path.generic_string().c_str(), e.what());
            ++failed;
            continue;
        }
        const double a = median(olds), b = median(news);
        totalOld += a;
        totalNew += b;
        std::printf("TIME %s\t%.3f\t%.3f\n", path.generic_string().c_str(), a, b);
        std::fflush(stdout);
    }
    std::printf("time: old %.3f ms, new %.3f ms, new/old %.4f\n", totalOld, totalNew,
                totalOld > 0 ? totalNew / totalOld : 0.0);
    std::printf("%zu files: %d failed\n", files.size(), failed);
    return failed ? 1 : 0;
}

int run_warnings(const std::vector<fs::path>& files) {
    int lines = 0, refused = 0;
    for (const auto& path : files) {
        try {
            const BuiltSound s = build_sound(load_patch(path.string()));
            for (const auto& w : s.warnings) {
                std::printf("%s\t%s\n", path.generic_string().c_str(), w.c_str());
                ++lines;
            }
        } catch (const std::exception& e) {
            std::printf("NEW REFUSES %s: %s\n", path.generic_string().c_str(), e.what());
            ++refused;
        }
        std::fflush(stdout);
    }
    std::printf("%zu files: %d warnings, %d refused\n", files.size(), lines, refused);
    return refused ? 1 : 0;
}

// Fills `o` from the arguments; false on a usage error.
bool parse_args(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--self") o.mode = Mode::Self;
        else if (a == "--warnings") o.mode = Mode::Warnings;
        else if (a == "--order" && more) {
            const std::string how = argv[++i];
            if (how == "reversed") o.mode = Mode::Reversed;
            else if (how == "tails-kept") o.mode = Mode::TailsKept;
            else return false;
        }
        else if (a == "--resaved" && more) { o.mode = Mode::Resaved; o.scratch = argv[++i]; }
        else if (a == "--sparse" && more) { o.mode = Mode::Sparse; o.scratch = argv[++i]; }
        else if (a == "--time" && more) { o.mode = Mode::Time; o.rounds = std::max(1, std::atoi(argv[++i])); }
        else if (a == "--expect" && more) o.expect = argv[++i];
        else if (a.rfind("--", 0) == 0) return false;
        else o.paths.emplace_back(a);
    }
    return !o.paths.empty();
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    if (!parse_args(argc, argv, o)) {
        std::fprintf(stderr, "Usage: twin_gate [--self | --order reversed|tails-kept | --resaved <dir> | "
                             "--sparse <dir> | --time <rounds> | --warnings] [--expect <what.txt>] "
                             "<patch.json | folder>...\n");
        return 2;
    }
    const std::vector<fs::path> files = collect(o.paths);
    if (o.mode == Mode::Time) return run_time(o, files);
    if (o.mode == Mode::Warnings) return run_warnings(files);
    return run_gate(o, files);
}

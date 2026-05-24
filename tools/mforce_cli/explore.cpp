// mforce_cli --explore — parameter-sweep batch rendering.
//
// Spec JSON shape:
//   {
//     "basePatch": "patches/fm_spacy_n177.json",
//     "duration":  8.0,                       // optional override; default = base patch's "seconds"
//     "axes": [
//       { "param": "fm.frequency",   "short": "f",  "min": 1000, "max": 100000, "steps": 10, "scale": "log" },
//       { "param": "fm.modRatio",    "short": "mr", "values": [0.5, 1.0, 1.4, 2.0, 2.5] },
//       { "param": "depthEnv.stages.0.endVal", "short": "d", "min": 0, "max": 10, "steps": 5 }
//     ]
//   }
//
// Param paths take the form "<nodeId>.<sub.path>". The sub.path walks into
// the node's "params" object; numeric segments index into arrays. So
// "depthEnv.stages.0.endVal" means: find node id="depthEnv", set
// params.stages[0].endVal = <value>.
//
// "scale": "log" makes the sweep geometric (each step multiplies by the
// same ratio); default "linear" makes it arithmetic. "values" overrides
// min/max/steps with an explicit list.

#include "explore.h"
#include "mforce/render/patch_loader.h"
#include "mforce/render/wav_writer.h"
#include "mforce/util/signal_stats.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace mforce {

using json = nlohmann::json;

namespace {

// -------- Axis representation --------
struct Axis {
    std::string         param;   // dot-path, e.g. "fm.frequency"
    std::string         shortName;
    std::vector<float>  values;
};

static std::string default_short(const std::string& param) {
    auto pos = param.find_last_of('.');
    return (pos == std::string::npos) ? param : param.substr(pos + 1);
}

static std::vector<float> linspace(float lo, float hi, int steps) {
    std::vector<float> out;
    if (steps <= 1) { out.push_back(lo); return out; }
    out.reserve(steps);
    float dx = (hi - lo) / float(steps - 1);
    for (int i = 0; i < steps; ++i) out.push_back(lo + dx * float(i));
    return out;
}

static std::vector<float> logspace(float lo, float hi, int steps) {
    std::vector<float> out;
    if (steps <= 1) { out.push_back(lo); return out; }
    if (lo <= 0.0f || hi <= 0.0f) {
        throw std::runtime_error("log-scale axis requires strictly-positive min/max");
    }
    out.reserve(steps);
    float logLo = std::log(lo), logHi = std::log(hi);
    float dx = (logHi - logLo) / float(steps - 1);
    for (int i = 0; i < steps; ++i) out.push_back(std::exp(logLo + dx * float(i)));
    return out;
}

static Axis parse_axis(const json& jaxis) {
    Axis a;
    if (!jaxis.contains("param")) throw std::runtime_error("axis missing 'param'");
    a.param     = jaxis.at("param").get<std::string>();
    a.shortName = jaxis.value("short", default_short(a.param));

    if (jaxis.contains("values")) {
        a.values = jaxis.at("values").get<std::vector<float>>();
        if (a.values.empty()) throw std::runtime_error("axis 'values' is empty: " + a.param);
        return a;
    }

    if (!jaxis.contains("min") || !jaxis.contains("max") || !jaxis.contains("steps")) {
        throw std::runtime_error("axis '" + a.param +
            "' needs either 'values' or min/max/steps");
    }
    float lo = jaxis.at("min").get<float>();
    float hi = jaxis.at("max").get<float>();
    int   n  = jaxis.at("steps").get<int>();
    std::string scale = jaxis.value("scale", std::string("linear"));
    a.values = (scale == "log") ? logspace(lo, hi, n) : linspace(lo, hi, n);
    return a;
}

// -------- Patch mutation by dot-path --------
//
// Walks "nodeId.subpath" into a patch JSON: finds graph.nodes[i].id == nodeId,
// then descends into nodes[i].params via the rest of the path. Numeric path
// segments index into arrays. Throws on any missing piece (typo guard).
static void set_patch_param(json& patch, const std::string& path, float value) {
    auto dot = path.find('.');
    if (dot == std::string::npos) {
        throw std::runtime_error("path '" + path +
            "' must be '<nodeId>.<subpath>' (no '.' found)");
    }
    std::string nodeId = path.substr(0, dot);
    std::string sub    = path.substr(dot + 1);

    if (!patch.contains("graph") || !patch["graph"].contains("nodes"))
        throw std::runtime_error("patch has no graph.nodes");

    json* nodeParams = nullptr;
    for (auto& n : patch["graph"]["nodes"]) {
        if (n.value("id", std::string{}) == nodeId) {
            if (!n.contains("params")) n["params"] = json::object();
            nodeParams = &n["params"];
            break;
        }
    }
    if (!nodeParams)
        throw std::runtime_error("no node with id='" + nodeId + "' in patch");

    // Walk sub-path tokens into nodeParams.
    std::vector<std::string> tokens;
    {
        std::stringstream ss(sub);
        std::string t;
        while (std::getline(ss, t, '.')) tokens.push_back(t);
    }
    if (tokens.empty())
        throw std::runtime_error("empty sub-path for node '" + nodeId + "'");

    json* cur = nodeParams;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        // Numeric token → array index
        if (!t.empty() && std::all_of(t.begin(), t.end(), [](char c){ return std::isdigit((unsigned char)c); })) {
            int idx = std::stoi(t);
            if (!cur->is_array() || idx >= (int)cur->size())
                throw std::runtime_error("bad array index '" + t + "' in path '" + path + "'");
            cur = &(*cur)[idx];
        } else {
            if (!cur->is_object() || !cur->contains(t))
                throw std::runtime_error("missing key '" + t + "' in path '" + path + "'");
            cur = &(*cur)[t];
        }
    }
    const std::string& leaf = tokens.back();
    if (!leaf.empty() && std::all_of(leaf.begin(), leaf.end(), [](char c){ return std::isdigit((unsigned char)c); })) {
        int idx = std::stoi(leaf);
        if (!cur->is_array() || idx >= (int)cur->size())
            throw std::runtime_error("bad array index '" + leaf + "' in path '" + path + "'");
        (*cur)[idx] = value;
    } else {
        (*cur)[leaf] = value;
    }
}

// -------- Variant ID encoding --------
// "f1234_mr1.4_d5" — short names + values joined by '_', filesystem-safe.
static std::string format_value(float v) {
    std::ostringstream oss;
    // Show 3 significant figures for readability; strip trailing zeros.
    if (std::fabs(v) >= 1000.0f) {
        oss << std::fixed << std::setprecision(0) << v;
    } else if (std::fabs(v) >= 1.0f) {
        oss << std::fixed << std::setprecision(2) << v;
    } else {
        oss << std::fixed << std::setprecision(3) << v;
    }
    std::string s = oss.str();
    // Trim trailing zeros after decimal point
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    // '-' and '.' are filesystem-safe on Windows but '.' looks weird in IDs.
    // Replace '.' with 'p' for readability (e.g. "1.4" → "1p4").
    for (auto& c : s) if (c == '.') c = 'p';
    return s;
}

static std::string make_variant_id(const std::vector<Axis>& axes,
                                   const std::vector<size_t>& indices) {
    std::ostringstream oss;
    for (size_t a = 0; a < axes.size(); ++a) {
        if (a > 0) oss << '_';
        oss << axes[a].shortName << format_value(axes[a].values[indices[a]]);
    }
    return oss.str();
}

// -------- Run-ID (timestamp + optional tag) --------
static std::string make_run_id(const std::string& tag) {
    auto t  = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d_%H%M%S");
    if (!tag.empty()) oss << '-' << tag;
    return oss.str();
}

} // anonymous namespace

// -------- Entry point --------
int run_explore(int argc, char** argv) {
    if (argc < 3) {
        std::cerr <<
            "Usage: mforce_cli --explore <spec.json> [--tag <name>]\n"
            "  Sweep params across a base patch, render each variant, write a manifest.\n"
            "  Outputs to renders/explore/<run-id>/\n"
            "  See tools/mforce_cli/explore.cpp for spec format.\n";
        return 1;
    }
    std::string specPath = argv[2];
    std::string tag;
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--tag") tag = argv[i + 1];
    }

    // ---- Load spec ----
    json spec;
    {
        std::ifstream f(specPath);
        if (!f) { std::cerr << "Cannot open spec: " << specPath << "\n"; return 1; }
        f >> spec;
    }
    std::string basePatchPath = spec.at("basePatch").get<std::string>();
    std::vector<Axis> axes;
    for (const auto& jaxis : spec.at("axes")) axes.push_back(parse_axis(jaxis));
    if (axes.empty()) { std::cerr << "spec has no axes\n"; return 1; }

    // ---- Load base patch ----
    json basePatch;
    {
        std::ifstream f(basePatchPath);
        if (!f) { std::cerr << "Cannot open basePatch: " << basePatchPath << "\n"; return 1; }
        f >> basePatch;
    }
    // Optional duration override (top-level "seconds")
    if (spec.contains("duration")) {
        basePatch["seconds"] = spec["duration"].get<float>();
    }

    // ---- Output dir ----
    std::string runId = make_run_id(tag);
    std::filesystem::path outDir =
        std::filesystem::path("renders") / "explore" / runId;
    std::filesystem::create_directories(outDir);

    // ---- Cross-product iteration ----
    size_t total = 1;
    for (auto& a : axes) total *= a.values.size();
    std::cerr << "Variants: " << total << "  ("
              << axes.size() << " axes, output: " << outDir.string() << ")\n";

    json manifest = json::object();
    manifest["runId"]     = runId;
    manifest["basePatch"] = basePatchPath;
    manifest["axes"]      = spec["axes"];
    manifest["variants"]  = json::array();

    std::vector<size_t> indices(axes.size(), 0);
    size_t variantCount = 0;
    int    failed       = 0;

    while (true) {
        // Build this variant's patch
        json variant = basePatch;
        json axisValues = json::object();
        for (size_t a = 0; a < axes.size(); ++a) {
            float v = axes[a].values[indices[a]];
            try {
                set_patch_param(variant, axes[a].param, v);
            } catch (const std::exception& e) {
                std::cerr << "param set failed: " << e.what() << "\n";
                ++failed;
                goto next_variant;
            }
            axisValues[axes[a].param] = v;
        }

        {
            std::string variantId = make_variant_id(axes, indices);
            std::filesystem::path patchPath = outDir / (variantId + ".patch.json");
            std::filesystem::path wavPath   = outDir / (variantId + ".wav");

            // Serialize the variant patch to a temp file so load_patch_file
            // can pick it up (load_patch_file currently takes a path, not
            // an in-memory JSON tree).
            {
                std::ofstream pf(patchPath);
                pf << variant.dump(2);
            }

            // Render
            SignalStats stats;
            int frames = 0;
            int sr     = 48000;
            try {
                Patch p = load_patch_file(patchPath.string());
                sr     = p.sampleRate;
                frames = p.frames;
                std::vector<float> out((size_t)frames * 2);
                { RenderContext ctx{sr}; p.mixer->render(ctx, out.data(), frames); }

                // Stats on left channel only (mono content)
                std::vector<float> mono(frames);
                for (int i = 0; i < frames; ++i) mono[i] = out[i * 2];
                stats = compute_all_stats(mono.data(), frames, sr);

                if (!write_wav_16le_stereo(wavPath.string(), sr, out)) {
                    std::cerr << "wav write failed: " << wavPath.string() << "\n";
                    ++failed;
                    goto next_variant;
                }
            } catch (const std::exception& e) {
                std::cerr << "render failed for " << variantId << ": " << e.what() << "\n";
                ++failed;
                goto next_variant;
            }

            // Manifest entry
            json entry = json::object();
            entry["id"]       = variantId;
            entry["wav"]      = wavPath.filename().string();
            entry["patch"]    = patchPath.filename().string();
            entry["params"]   = axisValues;
            entry["stats"]    = {
                {"peak",             stats.peak},
                {"rms",              stats.rms},
                {"zeroCrossingRate", stats.zeroCrossingRate},
                {"spectralCentroid", stats.spectralCentroid},
                {"spectralFlatness", stats.spectralFlatness},
            };
            entry["frames"]   = frames;
            entry["sampleRate"] = sr;
            manifest["variants"].push_back(entry);

            ++variantCount;
            if (variantCount % 10 == 0 || variantCount == total) {
                std::cerr << "  [" << variantCount << "/" << total << "] "
                          << variantId << "\n";
            }
        }

      next_variant:
        // Advance odometer-style across all axes
        bool done = true;
        for (size_t a = 0; a < axes.size(); ++a) {
            if (++indices[a] < axes[a].values.size()) { done = false; break; }
            indices[a] = 0;
        }
        if (done) break;
    }

    // ---- Write manifest ----
    std::filesystem::path manifestPath = outDir / "manifest.json";
    {
        std::ofstream mf(manifestPath);
        mf << manifest.dump(2);
    }
    std::cerr << "Done. Wrote " << variantCount << " variants ("
              << failed << " failed) to " << outDir.string()
              << "\nManifest: " << manifestPath.string() << "\n";
    return failed == 0 ? 0 : 2;
}

// ===========================================================================
// --explore-filter — query/rank a manifest from --explore
// ===========================================================================

namespace {

// Map UI-friendly stat name → the JSON key under "stats" in a manifest entry.
// Also used to know what flags --<name>-min / --<name>-max should parse into.
static const std::vector<std::pair<std::string, std::string>> kStatNames = {
    {"peak",     "peak"},
    {"rms",      "rms"},
    {"zcr",      "zeroCrossingRate"},
    {"centroid", "spectralCentroid"},
    {"flatness", "spectralFlatness"},
};

static const std::string* stat_json_key(const std::string& shortName) {
    for (const auto& kv : kStatNames) if (kv.first == shortName) return &kv.second;
    return nullptr;
}

struct StatBound { float min{-1e30f}, max{1e30f}; bool active{false}; };

} // anonymous namespace

int run_explore_filter(int argc, char** argv) {
    if (argc < 3) {
        std::cerr <<
            "Usage: mforce_cli --explore-filter <manifest.json> [filters] [--sort F] [--limit N] [--json]\n"
            "  Filters: --<stat>-min V / --<stat>-max V  where <stat> in {peak,rms,zcr,centroid,flatness}\n"
            "  --sort F     sort descending by stat F (same names as filters)\n"
            "  --limit N    cap output at N variants (after sort)\n"
            "  --json       emit JSON array instead of text table\n"
            "  Example: mforce_cli --explore-filter renders/explore/<run>/manifest.json \\\n"
            "             --centroid-min 5000 --flatness-min 0.1 --sort rms --limit 20\n";
        return 1;
    }
    std::string manifestPath = argv[2];

    // Parse filter / sort / limit flags
    std::unordered_map<std::string, StatBound> bounds;  // shortName → bound
    for (const auto& kv : kStatNames) bounds[kv.first] = {};
    std::string sortField;
    int  limit  = 0;
    bool asJson = false;

    auto parse_stat_flag = [&](const std::string& flag, float val) -> bool {
        // flag form: --<name>-min  or  --<name>-max
        if (flag.size() < 7 || flag.substr(0, 2) != "--") return false;
        std::string body = flag.substr(2);  // <name>-min/max
        auto dash = body.rfind('-');
        if (dash == std::string::npos) return false;
        std::string name   = body.substr(0, dash);
        std::string suffix = body.substr(dash + 1);
        auto it = bounds.find(name);
        if (it == bounds.end()) return false;
        if      (suffix == "min") { it->second.min = val; it->second.active = true; }
        else if (suffix == "max") { it->second.max = val; it->second.active = true; }
        else return false;
        return true;
    };

    for (int i = 3; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sort"  && i + 1 < argc) { sortField = argv[++i]; continue; }
        if (a == "--limit" && i + 1 < argc) { limit     = std::atoi(argv[++i]); continue; }
        if (a == "--json")                   { asJson    = true; continue; }
        if (i + 1 < argc && parse_stat_flag(a, std::stof(argv[i + 1]))) { ++i; continue; }
        std::cerr << "Unknown flag: " << a << "\n";
        return 1;
    }

    if (!sortField.empty() && !stat_json_key(sortField)) {
        std::cerr << "Unknown sort field '" << sortField << "'. "
                  << "Use one of: peak rms zcr centroid flatness\n";
        return 1;
    }

    // Load manifest
    json manifest;
    {
        std::ifstream f(manifestPath);
        if (!f) { std::cerr << "Cannot open manifest: " << manifestPath << "\n"; return 1; }
        f >> manifest;
    }
    if (!manifest.contains("variants") || !manifest["variants"].is_array()) {
        std::cerr << "Manifest has no 'variants' array\n";
        return 1;
    }

    // Resolve manifest dir for printing relative WAV paths
    std::filesystem::path runDir = std::filesystem::path(manifestPath).parent_path();

    // Filter
    std::vector<json> matches;
    for (const auto& v : manifest["variants"]) {
        if (!v.contains("stats")) continue;
        const auto& s = v["stats"];
        bool keep = true;
        for (const auto& kv : kStatNames) {
            const StatBound& b = bounds[kv.first];
            if (!b.active) continue;
            if (!s.contains(kv.second)) continue;
            float val = s[kv.second].get<float>();
            if (val < b.min || val > b.max) { keep = false; break; }
        }
        if (keep) matches.push_back(v);
    }

    // Sort (descending by chosen stat)
    if (!sortField.empty()) {
        const std::string* key = stat_json_key(sortField);
        std::sort(matches.begin(), matches.end(),
            [&](const json& a, const json& b) {
                float va = a.value("stats", json::object()).value(*key, 0.0f);
                float vb = b.value("stats", json::object()).value(*key, 0.0f);
                return va > vb;
            });
    }

    if (limit > 0 && int(matches.size()) > limit) matches.resize(limit);

    // Emit
    if (asJson) {
        json out = json::array();
        for (auto& v : matches) out.push_back(v);
        std::cout << out.dump(2) << "\n";
        return 0;
    }

    // Text table
    std::cout << "Matched " << matches.size() << " of "
              << manifest["variants"].size() << " variants in "
              << manifestPath << "\n\n";
    if (matches.empty()) return 0;

    std::cout << std::left
              << std::setw(28) << "id"
              << std::right
              << std::setw(8)  << "peak"
              << std::setw(8)  << "rms"
              << std::setw(10) << "zcr"
              << std::setw(11) << "centroid"
              << std::setw(11) << "flatness"
              << "   wav\n";
    std::cout << std::string(28 + 8 + 8 + 10 + 11 + 11 + 3, '-') << "wav\n";
    for (const auto& v : matches) {
        const auto& s = v["stats"];
        std::cout << std::left
                  << std::setw(28) << v.value("id", std::string{})
                  << std::right << std::fixed
                  << std::setw(8) << std::setprecision(3) << s.value("peak",             0.0f)
                  << std::setw(8) << std::setprecision(3) << s.value("rms",              0.0f)
                  << std::setw(10) << std::setprecision(1) << s.value("zeroCrossingRate", 0.0f)
                  << std::setw(11) << std::setprecision(1) << s.value("spectralCentroid", 0.0f)
                  << std::setw(11) << std::setprecision(4) << s.value("spectralFlatness", 0.0f)
                  << "   " << (runDir / v.value("wav", std::string{})).string()
                  << "\n";
    }
    return 0;
}

} // namespace mforce

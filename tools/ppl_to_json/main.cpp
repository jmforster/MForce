// ppl_to_json — standalone tool that parses .ppl (Passage Pattern Library)
// files and emits the equivalent JSON for the engine's runtime loader.
//
// Usage:
//   ppl_to_json <input.ppl> <output.json>     — single file
//   ppl_to_json <input_dir> <output_dir>      — batch all *.ppl in dir
//   ppl_to_json                               — scan lib/ppl, emit to lib/json
//
// Spec: docs/superpowers/specs/2026-04-27-pattern-library-design.md

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>

namespace fs = std::filesystem;
using json = nlohmann::json;

// -----------------------------------------------------------------------------
// String helpers
// -----------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        std::string t = trim(item);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

static std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string tok;
    while (ss >> tok) out.push_back(tok);
    return out;
}

// -----------------------------------------------------------------------------
// Figure-token parser
//
// Token grammar inside a phrase definition (no whitespace required between
// tokens):
//   <motifSlot> ('~')? ("''" | "'")? ('-')*
//
// motifSlot: single uppercase letter [A-Z]
// '~':      newAnchor = true
// '/'':      modifiedDepth (1 or 2)
// '-'+:     elideAfter (count)
// -----------------------------------------------------------------------------

static std::vector<json> parse_figure_tokens(const std::string& s, int lineNum) {
    std::vector<json> out;
    size_t i = 0;
    while (i < s.size()) {
        char c = s[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (!(c >= 'A' && c <= 'Z')) {
            std::ostringstream err;
            err << "line " << lineNum << ": expected uppercase motif slot, got '"
                << c << "' at position " << i << " of phrase definition";
            throw std::runtime_error(err.str());
        }
        json tok;
        tok["slot"] = std::string(1, c);
        ++i;
        // Optional ~
        if (i < s.size() && s[i] == '~') { tok["newAnchor"] = true; ++i; }
        // Optional ' or ''
        int primes = 0;
        while (i < s.size() && s[i] == '\'') { ++primes; ++i; }
        if (primes > 0) tok["modifiedDepth"] = primes;
        // Optional dashes
        int dashes = 0;
        while (i < s.size() && s[i] == '-') { ++dashes; ++i; }
        if (dashes > 0) tok["elideAfter"] = dashes;
        out.push_back(std::move(tok));
    }
    return out;
}

// -----------------------------------------------------------------------------
// Phrase definitions parser
//
// Expects the content between ":" (after pattern header) and "|" (before
// passage). Format:
//   P1:<tokens> P2:<tokens> P3:<tokens> ...
//
// Phrase definitions are space-separated. Within each "P<n>:", whitespace
// after ":" is allowed.
// -----------------------------------------------------------------------------

static std::vector<json> parse_phrase_defs(const std::string& s, int lineNum) {
    std::vector<json> out;
    // Find each "P<digits>:" marker; everything between this marker's ":" and
    // the next "P<digits>:" (or end) is the phrase's figure-token string.
    size_t i = 0;
    while (i < s.size()) {
        // Skip whitespace.
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        if (i >= s.size()) break;

        if (s[i] != 'P') {
            std::ostringstream err;
            err << "line " << lineNum << ": expected 'P' for phrase index, got '"
                << s[i] << "' at position " << i;
            throw std::runtime_error(err.str());
        }
        ++i;
        // Read digits.
        std::string digits;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            digits.push_back(s[i]);
            ++i;
        }
        if (digits.empty()) {
            std::ostringstream err;
            err << "line " << lineNum << ": expected digits after 'P' at position "
                << i;
            throw std::runtime_error(err.str());
        }
        int idx = std::stoi(digits);

        // Expect ':'.
        if (i >= s.size() || s[i] != ':') {
            std::ostringstream err;
            err << "line " << lineNum << ": expected ':' after P" << idx
                << " at position " << i;
            throw std::runtime_error(err.str());
        }
        ++i;

        // Tokens run until we hit another " P<digits>:" pattern or end of string.
        size_t end = i;
        while (end < s.size()) {
            if (std::isspace(static_cast<unsigned char>(s[end]))) {
                size_t look = end;
                while (look < s.size() && std::isspace(static_cast<unsigned char>(s[look]))) ++look;
                if (look < s.size() && s[look] == 'P' && look + 1 < s.size()
                    && std::isdigit(static_cast<unsigned char>(s[look + 1]))) {
                    break;
                }
            }
            ++end;
        }
        std::string tokenStr = trim(s.substr(i, end - i));
        json phrase;
        phrase["index"] = idx;
        phrase["figures"] = parse_figure_tokens(tokenStr, lineNum);
        out.push_back(std::move(phrase));
        i = end;
    }
    return out;
}

// -----------------------------------------------------------------------------
// Passage parser
//
// Passage line has space-separated tokens of the form:
//   P<n>            — phrase reference, no cadence
//   P<n>hc          — half cadence
//   P<n>fc          — full / authentic cadence
// -----------------------------------------------------------------------------

static std::vector<json> parse_passage(const std::string& s, int lineNum) {
    std::vector<json> out;
    auto tokens = split_ws(s);
    for (const auto& tok : tokens) {
        if (tok.size() < 2 || tok[0] != 'P') {
            std::ostringstream err;
            err << "line " << lineNum << ": passage step must start with 'P', got '"
                << tok << "'";
            throw std::runtime_error(err.str());
        }
        size_t i = 1;
        std::string digits;
        while (i < tok.size() && std::isdigit(static_cast<unsigned char>(tok[i]))) {
            digits.push_back(tok[i]);
            ++i;
        }
        if (digits.empty()) {
            std::ostringstream err;
            err << "line " << lineNum << ": passage step missing phrase index in '"
                << tok << "'";
            throw std::runtime_error(err.str());
        }
        int idx = std::stoi(digits);
        json step;
        step["phrase"] = idx;
        std::string suffix = tok.substr(i);
        if (suffix == "hc") step["cadence"] = "half";
        else if (suffix == "fc") step["cadence"] = "full";
        else if (!suffix.empty()) {
            std::ostringstream err;
            err << "line " << lineNum << ": unknown cadence suffix '" << suffix
                << "' in '" << tok << "'";
            throw std::runtime_error(err.str());
        }
        out.push_back(std::move(step));
    }
    return out;
}

// -----------------------------------------------------------------------------
// Pattern line parser
//
// PatternName(Roles/Length): <phrases> | <passage>
//
// Roles is comma-separated. Length is integer bars.
// -----------------------------------------------------------------------------

static json parse_pattern_line(const std::string& line, int lineNum) {
    json out;
    size_t lparen = line.find('(');
    size_t rparen = line.find(')');
    size_t colon  = line.find(':', rparen == std::string::npos ? 0 : rparen);
    size_t pipe   = line.find('|');
    if (lparen == std::string::npos || rparen == std::string::npos
        || colon == std::string::npos || pipe == std::string::npos) {
        std::ostringstream err;
        err << "line " << lineNum
            << ": pattern line must match Name(Roles/Bars): phrases | passage";
        throw std::runtime_error(err.str());
    }

    std::string name = trim(line.substr(0, lparen));
    std::string parens = line.substr(lparen + 1, rparen - lparen - 1);
    std::string phrasesStr = trim(line.substr(colon + 1, pipe - colon - 1));
    std::string passageStr = trim(line.substr(pipe + 1));

    if (name.empty()) {
        std::ostringstream err;
        err << "line " << lineNum << ": empty pattern name";
        throw std::runtime_error(err.str());
    }
    out["name"] = name;

    // (Roles/Length) — split on '/'. Length is required.
    size_t slash = parens.find('/');
    if (slash == std::string::npos) {
        std::ostringstream err;
        err << "line " << lineNum
            << ": pattern parens must contain '/' separating roles from bars: '("
            << parens << ")'";
        throw std::runtime_error(err.str());
    }
    std::string rolesStr = parens.substr(0, slash);
    std::string barsStr  = trim(parens.substr(slash + 1));
    out["roles"] = split_csv(rolesStr);
    if (barsStr.empty()) {
        std::ostringstream err;
        err << "line " << lineNum << ": missing bars value after '/' in pattern parens";
        throw std::runtime_error(err.str());
    }
    out["bars"] = std::stoi(barsStr);

    out["phrases"] = parse_phrase_defs(phrasesStr, lineNum);
    out["passage"] = parse_passage(passageStr, lineNum);
    return out;
}

// -----------------------------------------------------------------------------
// File parser
//
// Reads a .ppl file and returns the JSON form of one PatternLibrary.
//
// Header lines (Name, Genres, Roles) come first. The first non-header line
// (and every subsequent non-blank line) is parsed as a Pattern. A blank line
// terminates the header.
// -----------------------------------------------------------------------------

static bool starts_with_header_key(const std::string& line, const char* key) {
    size_t k = std::strlen(key);
    if (line.size() < k) return false;
    if (line.compare(0, k, key) != 0) return false;
    for (size_t i = k; i < line.size(); ++i) {
        if (std::isspace(static_cast<unsigned char>(line[i]))) continue;
        return line[i] == ':';
    }
    return false;
}

static json parse_ppl_file(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open: " + path.string());

    json lib;
    lib["name"] = path.stem().string();
    lib["genres"] = json::array();
    lib["roles"] = json::array();
    lib["patterns"] = json::array();

    std::string line;
    int lineNum = 0;
    bool inHeader = true;
    while (std::getline(in, line)) {
        ++lineNum;
        std::string trimmed = trim(line);
        if (trimmed.empty()) {
            inHeader = false;  // blank line ends header
            continue;
        }

        if (inHeader) {
            if (starts_with_header_key(trimmed, "Name")) {
                size_t c = trimmed.find(':');
                lib["name"] = trim(trimmed.substr(c + 1));
                continue;
            }
            if (starts_with_header_key(trimmed, "Genres")) {
                size_t c = trimmed.find(':');
                lib["genres"] = split_csv(trimmed.substr(c + 1));
                continue;
            }
            if (starts_with_header_key(trimmed, "Roles")) {
                size_t c = trimmed.find(':');
                lib["roles"] = split_csv(trimmed.substr(c + 1));
                continue;
            }
            // Not a recognized header key — assume header has ended.
            inHeader = false;
            // fall through to pattern parsing
        }

        // Pattern line.
        json pat = parse_pattern_line(trimmed, lineNum);
        lib["patterns"].push_back(std::move(pat));
    }
    return lib;
}

// -----------------------------------------------------------------------------
// I/O orchestration
// -----------------------------------------------------------------------------

static int convert_one(const fs::path& inPath, const fs::path& outPath) {
    try {
        json lib = parse_ppl_file(inPath);
        fs::create_directories(outPath.parent_path());
        std::ofstream out(outPath);
        if (!out) {
            std::cerr << "cannot open output: " << outPath << "\n";
            return 1;
        }
        out << lib.dump(2) << "\n";
        std::cout << "ok: " << inPath << " -> " << outPath << " ("
                  << lib["patterns"].size() << " patterns)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error parsing " << inPath << ": " << e.what() << "\n";
        return 1;
    }
}

static int convert_dir(const fs::path& inDir, const fs::path& outDir) {
    if (!fs::exists(inDir) || !fs::is_directory(inDir)) {
        std::cerr << "input dir not found: " << inDir << "\n";
        return 1;
    }
    int rc = 0, count = 0;
    for (const auto& ent : fs::directory_iterator(inDir)) {
        if (!ent.is_regular_file()) continue;
        if (ent.path().extension() != ".ppl") continue;
        fs::path outPath = outDir / (ent.path().stem().string() + ".json");
        int r = convert_one(ent.path(), outPath);
        if (r != 0) rc = r;
        ++count;
    }
    if (count == 0) {
        std::cerr << "no .ppl files in: " << inDir << "\n";
        return 1;
    }
    return rc;
}

int main(int argc, char** argv) {
    if (argc == 1) {
        // Default: scan lib/ppl -> lib/json
        return convert_dir("lib/ppl", "lib/json");
    }
    if (argc == 3) {
        fs::path in = argv[1];
        fs::path out = argv[2];
        if (fs::is_directory(in)) return convert_dir(in, out);
        return convert_one(in, out);
    }
    std::cerr << "usage:\n"
              << "  ppl_to_json                       # lib/ppl -> lib/json\n"
              << "  ppl_to_json <in.ppl> <out.json>   # one file\n"
              << "  ppl_to_json <in_dir> <out_dir>    # batch directory\n";
    return 1;
}

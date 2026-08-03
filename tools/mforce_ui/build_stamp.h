// =========================================================================
// Engine-stamp guard (dsp backlog 3c).
//
// A stale mforce_ui binary has now produced two phantom bug reports — a curve
// dropdown that was "missing" and a formantFloor that was "not there" — both
// of which were simply an exe built before the engine change. Nothing in the
// UI made that visible, so the only symptom was a feature failing to exist.
//
// This compares the running exe's own timestamp against the newest source file
// the exe actually depends on and says so in the title bar, plus a dismissable
// banner on the first frames. No build-system changes and no git binary: the
// repo root is found by walking up from the exe, the dependency set comes from
// MSBuild's own read logs, and the commit is read straight out of .git/HEAD,
// so this cannot itself go stale.
//
// Detection lives in this header rather than in main.cpp so it can be exercised
// headlessly (tools/stamp_test) while mforce_ui.exe is running and therefore
// locked against relinking. Only draw_banner() stays in main.cpp — it is the
// one piece that needs ImGui.
// =========================================================================
#pragma once

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace stamp {

static std::string s_exeTime;      // "MM-DD HH:MM" of the running binary
static std::string s_commit;       // short hash at .git/HEAD, or empty
static std::string s_newestFile;   // dependency newer than the exe, if any
static std::string s_depSource;    // "tlog" (real dep set) or "scan" (fallback)
static int         s_depCount = 0; // dependencies actually checked
static bool        s_stale = false;
static bool        s_dismissed = false;

static std::string fmt_time(const FILETIME& ft) {
    SYSTEMTIME st, lt;
    FileTimeToSystemTime(&ft, &st);
    SystemTimeToTzSpecificLocalTime(nullptr, &st, &lt);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d-%02d %02d:%02d",
             lt.wMonth, lt.wDay, lt.wHour, lt.wMinute);
    return buf;
}

static bool file_time(const std::string& path, FILETIME& out) {
    WIN32_FILE_ATTRIBUTE_DATA fad;
    if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad)) return false;
    out = fad.ftLastWriteTime;
    return true;
}

// Newest LastWriteTime under `dir`, recursively, restricted to source files.
static void newest_under(const std::string& dir, FILETIME& best, std::string& bestName) {
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.') continue;
        std::string full = dir + "\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // third_party is vendored and does not move with engine work.
            if (_stricmp(fd.cFileName, "third_party") == 0) continue;
            newest_under(full, best, bestName);
        } else {
            const char* dot = strrchr(fd.cFileName, '.');
            if (!dot) continue;
            if (_stricmp(dot, ".h") && _stricmp(dot, ".hpp") && _stricmp(dot, ".cpp"))
                continue;
            if (CompareFileTime(&fd.ftLastWriteTime, &best) > 0) {
                best = fd.ftLastWriteTime;
                bestName = fd.cFileName;
            }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// Case- and separator-normalized, for comparing paths from three different
// sources: GetModuleFileNameA (backslashes), an argv path (may be either), and
// the tlog (always uppercase backslashes). Folding case alone is not enough —
// a forward-slash root then prefix-matches nothing and the whole dependency set
// silently filters to empty.
static std::string upper(std::string s) {
    for (auto& c : s) {
        if (c == '/') c = '\\';
        else c = (char)toupper((unsigned char)c);
    }
    return s;
}

// Read an MSBuild .tlog: UTF-16LE with a BOM, one path per line. A '^' prefixes
// the source that triggered the reads below it and may carry several paths
// joined by '|'; those are dependencies too, so the marker is just stripped.
static bool read_tlog(const std::string& path, std::vector<std::string>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string raw;
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) raw.append(buf, n);
    fclose(f);
    if (raw.empty()) return false;

    std::string text;
    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {
        const wchar_t* w = reinterpret_cast<const wchar_t*>(raw.data() + 2);
        int wlen = (int)((raw.size() - 2) / sizeof(wchar_t));
        int need = WideCharToMultiByte(CP_ACP, 0, w, wlen, nullptr, 0, nullptr, nullptr);
        text.resize((size_t)need);
        if (need)
            WideCharToMultiByte(CP_ACP, 0, w, wlen, &text[0], need, nullptr, nullptr);
    } else {
        text = raw;
    }

    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        size_t end = (eol == std::string::npos) ? text.size() : eol;
        std::string line = text.substr(pos, end - pos);
        pos = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty()) continue;
        if (line[0] == '^') line.erase(0, 1);
        size_t s = 0;
        while (s <= line.size()) {
            size_t bar = line.find('|', s);
            size_t pe = (bar == std::string::npos) ? line.size() : bar;
            if (pe > s) out.push_back(line.substr(s, pe - s));
            if (bar == std::string::npos) break;
            s = bar + 1;
        }
    }
    return true;
}

// Newest repo file the exe ACTUALLY depends on, taken from MSBuild's own read
// logs for the two targets that produce it (this exe, and the engine static lib
// it links). Comparing against every file under engine/ instead — as this guard
// originally did — gives a FALSE STALE: mforce_ui pulls in 59 of the 116 engine
// sources, so a comp-lane edit to e.g. composer.h leaves the exe correctly
// un-relinked while the guard shouts to rebuild it (reported by Wolfie,
// 2026-08-02). Returns false when the logs are unavailable, so the caller can
// fall back to the coarse scan.
static bool newest_from_tlogs(const std::string& root, const std::string& exeDir,
                              FILETIME& best, std::string& bestName, int& depCount) {
    // exeDir = <build>\tools\mforce_ui\<config>
    size_t s1 = exeDir.find_last_of("\\/");
    if (s1 == std::string::npos) return false;
    std::string config = exeDir.substr(s1 + 1);
    std::string uiDir = exeDir.substr(0, s1);              // <build>\tools\mforce_ui
    size_t s2 = uiDir.find_last_of("\\/");
    if (s2 == std::string::npos) return false;
    size_t s3 = uiDir.substr(0, s2).find_last_of("\\/");
    if (s3 == std::string::npos) return false;
    std::string buildDir = uiDir.substr(0, s3);            // <build>

    std::vector<std::string> files;
    bool any = false;
    if (read_tlog(uiDir + "\\mforce_ui.dir\\" + config +
                  "\\mforce_ui.tlog\\CL.read.1.tlog", files)) any = true;
    if (read_tlog(buildDir + "\\engine\\mforce_engine.dir\\" + config +
                  "\\mforce_engine.tlog\\CL.read.1.tlog", files)) any = true;
    if (!any) return false;

    const std::string ROOT  = upper(root) + "\\";
    const std::string BUILD = upper(buildDir) + "\\";
    std::unordered_set<std::string> seen;
    depCount = 0;
    for (const auto& f : files) {
        std::string U = upper(f);
        if (U.rfind(ROOT, 0) != 0) continue;                       // toolchain/SDK
        if (U.rfind(BUILD, 0) == 0) continue;                      // generated
        if (U.find("THIRD_PARTY") != std::string::npos) continue;  // vendored
        if (!seen.insert(U).second) continue;
        FILETIME ft;
        if (!file_time(f, ft)) continue;                           // listed but gone
        ++depCount;
        if (CompareFileTime(&ft, &best) > 0) {
            best = ft;
            size_t sl = f.find_last_of("\\/");
            bestName = (sl == std::string::npos) ? f : f.substr(sl + 1);
        }
    }
    return depCount > 0;
}

// exePathOverride lets tools/stamp_test evaluate a binary other than itself;
// mforce_ui always passes nullptr and stamps the running exe. forceScan skips
// the tlog dependency set and takes the coarse whole-engine scan instead — it
// exists so stamp_test can A/B the two and show the false stale directly.
static void init(const char* exePathOverride = nullptr, bool forceScan = false) {
    char exePath[MAX_PATH] = {0};
    if (exePathOverride) {
        strncpy(exePath, exePathOverride, MAX_PATH - 1);
    } else if (!GetModuleFileNameA(nullptr, exePath, MAX_PATH)) {
        return;
    }

    FILETIME exeFt{};
    if (!file_time(exePath, exeFt)) return;
    s_exeTime = fmt_time(exeFt);

    // Walk up from the exe looking for the repo root (the dir holding engine/).
    std::string root = exePath;
    for (int up = 0; up < 8; ++up) {
        size_t slash = root.find_last_of("\\/");
        if (slash == std::string::npos) return;
        root.resize(slash);
        DWORD a = GetFileAttributesA((root + "\\engine\\include\\mforce").c_str());
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) break;
        if (up == 7) return;
    }

    // Commit at .git/HEAD — follow the ref one hop if it is symbolic.
    auto slurp = [](const std::string& p) {
        std::string out;
        FILE* f = fopen(p.c_str(), "rb");
        if (!f) return out;
        char buf[256];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        buf[n] = 0;
        out = buf;
        while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
        return out;
    };
    std::string head = slurp(root + "\\.git\\HEAD");
    if (head.rfind("ref: ", 0) == 0) {
        std::string ref = head.substr(5);
        for (auto& c : ref) if (c == '/') c = '\\';
        head = slurp(root + "\\.git\\" + ref);
    }
    if (head.size() >= 7) s_commit = head.substr(0, 7);

    std::string exeDir = exePath;
    size_t es = exeDir.find_last_of("\\/");
    if (es != std::string::npos) exeDir.resize(es);

    FILETIME newest{};
    std::string newestName;
    s_depSource = "tlog";
    if (forceScan || !newest_from_tlogs(root, exeDir, newest, newestName, s_depCount)) {
        // No MSBuild logs (fresh clone, other generator) — coarse scan, which
        // can over-report. Named in --stamp so the difference is visible.
        s_depSource = "scan(engine)";
        newest_under(root + "\\engine\\include", newest, newestName);
        newest_under(root + "\\engine\\src", newest, newestName);
    }
    if (newestName.empty()) return;

    if (CompareFileTime(&newest, &exeFt) > 0) {
        s_stale = true;
        s_newestFile = newestName + " (" + fmt_time(newest) + ")";
    }
}

// Appended to whatever the title bar already says.
static std::string title_suffix() {
    std::string s = "  [build " + s_exeTime;
    if (!s_commit.empty()) s += " @" + s_commit;
    s += "]";
    if (s_stale) s += "  *** STALE — REBUILD ***";
    return s;
}

// Shared by mforce_ui --stamp and tools/stamp_test so the two cannot drift.
static int print_report() {
    printf("exe built : %s\n", s_exeTime.c_str());
    printf("commit    : %s\n", s_commit.empty() ? "(not found)" : s_commit.c_str());
    printf("dep set   : %s (%d files)\n",
           s_depSource.empty() ? "(none)" : s_depSource.c_str(), s_depCount);
    printf("stale     : %s\n", s_stale ? "YES" : "no");
    if (s_stale) printf("newer file: %s\n", s_newestFile.c_str());
    printf("title     : MForce - Patch Graph (unsaved)%s\n", title_suffix().c_str());
    return s_stale ? 1 : 0;
}

} // namespace stamp

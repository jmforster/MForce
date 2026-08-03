// Headless exerciser for the mforce_ui engine-stamp guard (dsp backlog 3c).
//
// mforce_ui.exe cannot be relinked while Matt has the UI open, and `--stamp`
// therefore reports whatever the *old* binary believed. This tool runs the same
// detection code (tools/mforce_ui/build_stamp.h, included directly — not a
// copy) against an arbitrary exe path, so the guard stays verifiable.
//
//   stamp_test <path-to-exe>     -> same report as `mforce_ui --stamp`,
//                                   exit 1 when that exe is stale.
//   stamp_test <exe> --deps      -> also list every dependency considered,
//                                   newest first. This is how the tlog dep set
//                                   was checked against the coarse engine scan.
//   stamp_test <exe> --scan      -> force the old whole-engine scan, so the two
//                                   answers can be compared on the same tree.
//
// Build (no CMake target needed, keeps Matt's build dir untouched):
//   cl /std:c++17 /EHsc /nologo /Fe:stamp_test.exe tools/stamp_test/main.cpp

#include "../mforce_ui/build_stamp.h"

#include <algorithm>

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("usage: stamp_test <path-to-exe> [--deps]\n");
        return 2;
    }
    const char* exe = argv[1];
    bool listDeps = false, forceScan = false;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--deps") listDeps = true;
        else if (a == "--scan") forceScan = true;
    }

    stamp::init(exe, forceScan);
    int rc = stamp::print_report();

    if (listDeps) {
        // Re-walk the dependency set, this time keeping every entry so the
        // ordering can be eyeballed against `ls -t` on engine/.
        std::string root = exe, exeDir = exe;
        size_t es = exeDir.find_last_of("\\/");
        if (es != std::string::npos) exeDir.resize(es);
        for (int up = 0; up < 8; ++up) {
            size_t slash = root.find_last_of("\\/");
            if (slash == std::string::npos) return rc;
            root.resize(slash);
            DWORD a = GetFileAttributesA((root + "\\engine\\include\\mforce").c_str());
            if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) break;
        }

        size_t s1 = exeDir.find_last_of("\\/");
        std::string config = exeDir.substr(s1 + 1);
        std::string uiDir  = exeDir.substr(0, s1);
        size_t s2 = uiDir.find_last_of("\\/");
        size_t s3 = uiDir.substr(0, s2).find_last_of("\\/");
        std::string buildDir = uiDir.substr(0, s3);

        std::vector<std::string> files;
        stamp::read_tlog(uiDir + "\\mforce_ui.dir\\" + config +
                         "\\mforce_ui.tlog\\CL.read.1.tlog", files);
        stamp::read_tlog(buildDir + "\\engine\\mforce_engine.dir\\" + config +
                         "\\mforce_engine.tlog\\CL.read.1.tlog", files);

        const std::string ROOT  = stamp::upper(root) + "\\";
        const std::string BUILD = stamp::upper(buildDir) + "\\";
        std::unordered_set<std::string> seen;
        std::vector<std::pair<FILETIME, std::string>> deps;
        for (const auto& f : files) {
            std::string U = stamp::upper(f);
            if (U.rfind(ROOT, 0) != 0) continue;
            if (U.rfind(BUILD, 0) == 0) continue;
            if (U.find("THIRD_PARTY") != std::string::npos) continue;
            if (!seen.insert(U).second) continue;
            FILETIME ft;
            if (!stamp::file_time(f, ft)) continue;
            deps.push_back({ft, f.substr(ROOT.size())});
        }
        std::sort(deps.begin(), deps.end(), [](const auto& a, const auto& b) {
            return CompareFileTime(&a.first, &b.first) > 0;
        });
        printf("\ndependencies considered: %d\n", (int)deps.size());
        for (const auto& d : deps)
            printf("  %s  %s\n", stamp::fmt_time(d.first).c_str(), d.second.c_str());
    }
    return rc;
}
